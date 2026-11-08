#include "simcoaster/object_catalog.hpp"
#include "simcoaster/sign_font.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include <map>
#include <tuple>

namespace simcoaster {
struct WorldSignRuntime {
    SignFontLibrary fonts;
    std::string language;
    std::map<std::pair<std::string, std::string>, std::shared_ptr<const SignFontConfiguration>> configurations;
    std::map<std::tuple<const SignFontConfiguration*, std::u16string, std::u16string>, SignTexturePair> rendered;

    WorldSignRuntime(std::filesystem::path path, std::string selected) : fonts(std::move(path)), language(std::move(selected)) {}
};

namespace {
std::string lower(std::string name) {
    for (auto& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return name;
}

std::u16string component(const std::array<char16_t, 33>& source, bool first, std::size_t limit) {
    std::u16string result { source.begin(), std::ranges::find(source, char16_t {}) };
    if (first && result.ends_with(u"--")) {
        result.pop_back();
    }
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

std::array<std::optional<std::size_t>, 2> sign_slots(const MeshModel& model) {
    std::array<std::optional<std::size_t>, 2> result;
    unsigned count = 0;
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const auto& slot = model.textures[i];
        if (slot.metadata_flags & 8u) {
            result[0] = i;
            ++count;
        } else if (slot.metadata_flags & 16u) {
            result[1] = i;
            ++count;
        } else if (!(slot.metadata_flags & 2u) && slot.filenames.size() <= 1) {
            const auto name = lower(slot.filename);
            if (name == "sign1.tga" && !result[0]) {
                result[0] = i;
                ++count;
            } else if (name == "sign2.tga" && !result[1]) {
                result[1] = i;
                ++count;
            }
        }
    }
    if (count != 2) {
        return {};
    }
    return result;
}

std::expected<MeshModelLibrary, MeshError> open_sign_library(std::string directory) {
    while (!directory.empty() && (directory.back() == '\\' || directory.back() == '/')) {
        directory.pop_back();
    }
    auto path = std::filesystem::path { directory };
    if (!std::filesystem::is_directory(path) && !std::filesystem::is_regular_file(path)) {
        path += ".wad";
    }
    return open_mesh_model_library(path);
}
} //namespace

WorldMeshRuntime::WorldMeshRuntime() = default;
WorldMeshRuntime::~WorldMeshRuntime() = default;

void WorldMeshRuntime::configure_sign_fonts(std::filesystem::path archive, std::string language) {
    signs_ = std::make_unique<WorldSignRuntime>(std::move(archive), std::move(language));
    sign_texture_generation = true;
}

std::expected<bool, MeshError> WorldMeshRuntime::render_model_signs(MeshModel& model, std::string_view directory, std::string_view basename, std::u16string_view first, std::optional<std::u16string_view> second) {
    try {
        if (!sign_texture_generation) {
            return false;
        }
        if (!signs_) {
            throw MeshError { MeshFailure::configuration, 0, "sign font owner is not configured", {} };
        }
        const auto slots = sign_slots(model);
        if (!slots[0] || !slots[1]) {
            return false;
        }
        std::string resource { basename };
        if (resource.empty()) {
            std::string path { directory };
            while (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
                path.pop_back();
            }
            resource = std::filesystem::path { path }.filename().native_encoded_string();
            if (lower(std::filesystem::path { resource }.extension().native_encoded_string()) == ".wad") {
                resource = std::filesystem::path { resource }.stem().native_encoded_string();
            }
        }
        const auto config_key = std::pair { std::string { directory }, resource };
        auto configuration = signs_->configurations.find(config_key);
        if (configuration == signs_->configurations.end()) {
            auto library = open_sign_library(std::string { directory });
            if (!library) {
                throw library.error();
            }
            auto bytes = library->read_resource(signs_->language + "_" + resource + ".sgn");
            if (!bytes) {
                throw bytes.error();
            }
            if (!*bytes) {
                bytes = library->read_resource(resource + ".sgn");
                if (!bytes) {
                    throw bytes.error();
                }
            }
            //4CAC10 ignores a missing SGN's load failure. Constructor defaults have
            //no surface artwork and zero effect opacity, producing transparent
            //sign halves; installed NS2WWgate actually exercises this branch.
            auto parsed = *bytes ? parse_sign_font_configuration(**bytes) : std::expected<SignFontConfiguration, MeshError> { SignFontConfiguration {} };
            if (!parsed) {
                throw parsed.error();
            }
            configuration = signs_->configurations.emplace(config_key, std::make_shared<const SignFontConfiguration>(std::move(*parsed))).first;
        }
        const auto names = second ? std::array<std::u16string, 2> { std::u16string { first.substr(0, first.find(char16_t {})) }, std::u16string { second->substr(0, second->find(char16_t {})) } } : split_sign_caption(first);
        const auto key = std::tuple { configuration->second.get(), names[0], names[1] };
        auto cached = signs_->rendered.find(key);
        if (cached == signs_->rendered.end()) {
            auto rendered = signs_->fonts.render(*configuration->second, names[0], names[1]);
            if (!rendered) {
                throw rendered.error();
            }
            //Generated textures are immutable and can be shared by identical live
            //captions. Keep a finite cache; live slots retain their own shared
            //owner.
            if (signs_->rendered.size() >= 256) {
                signs_->rendered.clear();
            }
            cached = signs_->rendered.emplace(key, std::move(*rendered)).first;
        }
        std::array<std::optional<std::size_t>, 2> nodes;
        for (std::size_t half = 0; half < 2; ++half) {
            for (std::size_t index = 0; index < model.nodes.size(); ++index) {
                if (const auto& drawable = model.nodes[index].drawable) {
                    if (std::ranges::any_of(drawable->groups, [&](const auto& group) {
                            return group.texture_slot == slots[half];
                        })) {
                        nodes[half] = index;
                        break;
                    }
                }
            }
        }
        normalize_mesh_uvs(model, nodes[0]);
        if (nodes[1] != nodes[0]) {
            normalize_mesh_uvs(model, nodes[1]);
        }
        for (std::size_t half = 0; half < 2; ++half) {
            auto& slot = model.textures[*slots[half]];
            slot.image_override = cached->second.halves[half];
            slot.metadata_flags |= half ? 0x12u : 0xAu;
            slot.flags |= 0x480u;
        }
        model.flags |= 0x8000u;
        return true;
    } catch (MeshError error) {
        if (error.resource.empty()) {
            error.resource = std::string { directory } + "/" + std::string { basename };
        }
        last_error = error;
        return std::unexpected(error);
    }
}

bool WorldMeshRuntime::set_object_sign_caption(WorldRuntime& c, ParkObjectRuntimeView& object, std::u16string_view first, std::u16string_view second) {
    if (!get_park_object_definition(c, &object) || !object.mesh_instance_id) {
        return false;
    }
    auto* mesh = instance(object.mesh_instance_id);
    if (!mesh) {
        return false;
    }
    const auto* source = definition(mesh->template_id);
    if (!source) {
        throw std::logic_error("sign mesh template is missing");
    }
    mesh->sign_text_override = std::u16string { first };
    mesh->sign_source_override = std::u16string { second };
    bool rendered = true;
    if (sign_texture_generation) {
        const auto result = render_model_signs(mesh->playback.model, source->source_directory, {}, first, second);
        if (!result) {
            throw std::runtime_error(describe_mesh_error(result.error()));
        }
        rendered = *result;
    }
    //The writer packs this LIVE flag into saved bit100. The saved-header
    //staging field is overwritten during serialization and is not runtime state.
    if (!(mesh->playback.runtime_flags & std::to_underlying(MeshInstanceFlag::template_definition))) {
        mesh->playback.runtime_flags |= std::to_underlying(MeshInstanceFlag::custom_sign_caption);
    }
    invalidate_mesh_renderer_cache(c);
    return rendered;
}

void WorldMeshRuntime::rebuild_world_sign_textures(WorldRuntime& c) {
    std::size_t visited = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (++visited >= c.entities.size()) {
            throw std::length_error("cyclic sign object list");
        }
        auto* payload = row->payload;
        if (!payload || payload->kind != ThingKind::park_object) {
            continue;
        }
        auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(payload);
        auto* mesh = instance(object.mesh_instance_id);
        if (!mesh || !(mesh->playback.runtime_flags & std::to_underlying(MeshInstanceFlag::custom_sign_caption))) {
            continue;
        }
        const auto* d = get_park_object_definition(c, &object);
        if (!d || !d->balance) {
            continue;
        }
        const auto* source = &object;
        if (d->balance->UsageInfo.IsInfoDesk && object.promoted_attraction) {
            const auto* promoted = c.entities.at(object.promoted_attraction).payload;
            if (!promoted || promoted->kind != ThingKind::park_object) {
                throw std::logic_error("promoted sign object is missing");
            }
            source = reinterpret_cast<const ParkObjectRuntimeView*>(promoted);
        }
        (void)set_object_sign_caption(c, object, component(source->name_a, true, 256), component(source->name_b, false, 256));
    }
}
} //namespace simcoaster
