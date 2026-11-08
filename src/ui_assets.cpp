#include "simcoaster/ui_assets.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace simcoaster {
namespace {
std::string lower(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return value;
}

UiModelAssetKind kind(std::string_view stem) {
    //5082D0 recognizes these exact stems before generic loading. Slider is
    //the separate508860 path; other names beginning! are not registered.
    constexpr std::array<std::string_view, 11> frames { "!frame", "!f_plain", "!cbut", "!tablist", "!listnarrow", "!list", "!window", "!tooltip", "!window_s", "!listshort", "!optionbox" };
    if (std::ranges::find(frames, stem) != frames.end()) {
        return UiModelAssetKind::frame;
    }
    return stem == "!slider" ? UiModelAssetKind::slider : UiModelAssetKind::model;
}

TextureImagePtr white() {
    static const TextureImagePtr image = std::make_shared<TextureImage>(TextureImage { 1, 1, { { 255, 255, 255, 255 } }, TextureAlpha::opaque });
    return image;
}

std::uint32_t narrow_index(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("UI geometry exceeds its index domain");
    }
    return static_cast<std::uint32_t>(value);
}

RgbaPixel modulate(RgbaPixel first, const RgbaPixel& second) {
    for (std::size_t i = 0; i < first.size(); ++i) {
        first[i] = static_cast<std::uint8_t>((static_cast<unsigned>(first[i]) * second[i]) / 255u);
    }
    return first;
}

bool finite(const UiModelPlacement& p) {
    return std::ranges::all_of(p.bounds,
                               [](float f) {
                                   return std::isfinite(f);
                               })
        && std::ranges::all_of(p.clip,
                               [](float f) {
                                   return std::isfinite(f);
                               })
        && std::ranges::all_of(p.pivot, [](float f) {
               return std::isfinite(f);
           });
}

struct Deformation {
    std::optional<std::size_t> node;
    float middle_x {}, middle_y {}, width_delta {}, height_delta {};
    bool slider {};

    std::array<float, 3> point(std::size_t node_index, std::size_t position, std::array<float, 3> value) const {
        if (node != node_index) {
            return value;
        }
        if (slider) {
            //4E75F0 changes exactly the first eight unique positions of the root.
            if (position < 8 && value[1] < 0) {
                value[1] += height_delta;
            }
        } else {
            //4E7160 compares in local model space, preserving UVs and border size.
            if (value[0] > middle_x) {
                value[0] += width_delta;
            }
            if (value[1] < middle_y) {
                value[1] += height_delta;
            }
        }
        return value;
    }

    MeshBounds bounds(std::size_t node_index, MeshBounds value) const {
        if (node == node_index) {
            value.minimum[1] += height_delta;
            if (!slider) {
                value.maximum[0] += width_delta;
            }
        }
        return value;
    }
};

struct Layout {
    const UiModelPlacement& placement;
    std::array<float, 3> minimum {}, maximum {};
    float units_x {}, units_y {}, cosine = 1, sine {};
    bool normalize_depth {};

    std::array<float, 3> point(const std::array<float, 3>& value) const {
        //4E68A0 builds model->view matrices around(1024,768), then projects
        //with1/2048 and1/1536. Cancelling those coordinate changes produces
        //this logical-canvas form, including its half-coordinate correction.
        const auto x = placement.bounds[0] + (value[0] - minimum[0]) / units_x;
        const auto y = placement.bounds[3] - (value[1] - minimum[1]) / units_y;
        const auto dx = x - placement.pivot[0], dy = y - placement.pivot[1];
        const auto z_span = std::abs(maximum[2] - minimum[2]);
        return { placement.rotation ? placement.pivot[0] + dx * cosine - dy * (units_y / units_x) * sine : x, placement.rotation ? placement.pivot[1] + dx * (units_x / units_y) * sine + dy * cosine : y, (1.f - static_cast<float>(placement.depth) / 40.f + (normalize_depth && z_span > 0 ? value[2] / (40.f * z_span) : 0.f)) * .1f };
    }
};
} //namespace

std::uint32_t ui_model_asset_hash(std::string_view name) {
    std::uint32_t hash {};
    for (const auto c : name) {
        if (!c) {
            break;
        }
        const auto byte = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(c));
        hash = 47u * (hash ^ static_cast<std::uint32_t>(static_cast<std::int32_t>(byte)));
    }
    return hash;
}

struct UiAssetLibrary::State {
    struct Asset {
        std::uint32_t model_id {};
        std::vector<std::size_t> parts;
        std::vector<MeshMatrix> pose;
        //An engaged null pointer records the native missing-source state; keep it
        //distinct from a slot that has not yet attempted texture acquisition.
        std::vector<std::optional<TextureImagePtr>> textures;
        bool normalize_depth {};
    };

    MeshModelLibrary models;
    MeshTextureLibrary textures;
    std::vector<UiModelAssetInfo> info;
    std::vector<Asset> data;
    std::map<std::uint32_t, std::size_t> registry;
    std::vector<std::string> diagnostics;

    static MeshModelLibrary open(const std::filesystem::path& root) {
        auto path = root / "Data/ui";
        auto archive = path;
        archive += ".wad";
        //Shipped UI models/textures live in ui.wad alongside a loose ui/cursors
        //directory. That cursor-only directory must not shadow the model archive.
        if (std::filesystem::is_regular_file(archive)) {
            path = std::move(archive);
        }
        auto library = open_mesh_model_library(path);
        if (!library) {
            throw std::runtime_error(describe_mesh_error(library.error()));
        }
        return std::move(*library);
    }

    State(const std::filesystem::path& root, const std::string& language, MeshTextureSet texture_set, bool animated_textures) : models(open(root)), textures(models, root, "", {}, texture_set, animated_textures) {
        std::vector<std::string> names;
        if (models.archive) {
            for (const auto& entry : models.archive->entries()) {
                if (lower(std::filesystem::path { entry.path }.extension().native_encoded_string()) == ".md2") {
                    names.push_back(entry.path);
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(models.source)) {
                if (entry.is_regular_file() && lower(entry.path().extension().native_encoded_string()) == ".md2") {
                    names.push_back(entry.path().filename().native_encoded_string());
                }
            }
        }
        models.templates.reserve(names.size());
        info.reserve(names.size());
        data.reserve(names.size());
        for (const auto& name : names) {
            const auto filename = std::filesystem::path { name }.filename().native_encoded_string();
            const auto stem = lower(std::filesystem::path { filename }.stem().native_encoded_string());
            const auto type = kind(stem);
            if ((stem.starts_with('!') && type == UiModelAssetKind::model) || filename == "w_small_shadow.md2") {
                continue;
            }
            const auto id = models.load(narrow_index(data.size() + 1), name);
            if (!id) {
                throw std::runtime_error(describe_mesh_error(id.error()));
            }
            auto& model = models.templates[*id - 1].model;
            if (!model.root || *model.root >= model.nodes.size()) {
                throw std::runtime_error("UI asset has no model root: " + name);
            }
            const auto registry_name = type == UiModelAssetKind::slider ? std::string { "!slider" } : model.nodes[*model.root].name;
            if (registry_name.empty()) {
                throw std::runtime_error("UI asset has no root debug name: " + name);
            }
            const auto hash = ui_model_asset_hash(registry_name);
            if (registry.contains(hash)) {
                //Native851FBF/852038 return the first hash match. The tail-node
                //duplicate insertion bug cannot change that observable lookup.
                diagnostics.push_back("UI registry retains first hash match: " + registry_name);
                continue;
            }
            const bool localized_delete = stem == "tb_delete" && lower(language) != "american";
            if (localized_delete) {
                //508A70 registers the remapped clone before trying ordinary loading.
                //First-hash lookup therefore keeps the locale's bomb-delete art.
                constexpr std::array<std::string_view, 4> originals { "tb_delete.tga", "tb_deleted.tga", "tb_deleteh.tga", "tb_deletehd.tga" };
                constexpr std::array<std::string_view, 4> replacements { "tb_bombdelete.tga", "tb_bombdeleted.tga", "tb_bombdeleteh.tga", "tb_bombdeletehd.tga" };
                const auto remap = [&](std::string& value) {
                    const auto found = std::ranges::find(originals, lower(value));
                    if (found != originals.end()) {
                        value = replacements[static_cast<std::size_t>(found - originals.begin())];
                    }
                };
                for (auto& slot : model.textures) {
                    remap(slot.filename);
                    for (auto& value : slot.filenames) {
                        remap(value);
                    }
                }
            }
            Asset asset;
            asset.model_id = *id;
            asset.normalize_depth = localized_delete;
            auto pose = evaluate_mesh_pose(model);
            if (!pose) {
                throw std::runtime_error(describe_mesh_error(pose.error()));
            }
            asset.pose = std::move(*pose);
            for (auto part = model.root; part; part = model.nodes[*part].first_child) {
                if (asset.parts.size() >= model.nodes.size() || *part >= model.nodes.size()) {
                    throw std::runtime_error("Invalid UI part chain: " + registry_name);
                }
                if (!model.nodes[*part].drawable) {
                    throw std::runtime_error("UI part has no bounded drawable: " + registry_name);
                }
                asset.parts.push_back(*part);
            }
            asset.textures.resize(model.textures.size());
            registry.emplace(hash, info.size());
            info.push_back({ registry_name, hash, type, asset.parts.size() });
            data.push_back(std::move(asset));
        }
    }

    TextureImagePtr texture(Asset& asset, const MeshModel& model, std::size_t index) {
        auto& cached = asset.textures.at(index);
        if (cached) {
            return *cached;
        }
        const auto& slot = model.textures.at(index);
        if (slot.image_override) {
            cached = slot.image_override;
            return *cached;
        }
        const auto& name = textures.filename(slot);
        auto image = textures.load(slot);
        if (image) {
            cached = *image;
        } else {
            //6DA260 returns the real shared NotFound source on acquisition failure.
            //Only a null fallback/private source takes the solid material path.
            image = textures.missing_texture();
            cached = image ? *image : TextureImagePtr {};
            const auto message = "Missing original UI texture: " + name + " (native shared fallback)";
            if (std::ranges::find(diagnostics, message) == diagnostics.end()) {
                diagnostics.push_back(message);
            }
        }
        return *cached;
    }
};

UiAssetLibrary::UiAssetLibrary(const std::filesystem::path& root, std::string language, MeshTextureSet texture_set, bool animated_textures) : state_(std::make_unique<State>(root, language, texture_set, animated_textures)) {}

UiAssetLibrary::~UiAssetLibrary() = default;

void UiAssetLibrary::animated_textures(bool enabled) {
    if (state_->textures.animated_textures() == enabled) {
        return;
    }
    state_->textures.set_animated_textures(enabled);
    for (auto& asset : state_->data) {
        std::fill(asset.textures.begin(), asset.textures.end(), std::nullopt);
    }
}

std::span<const UiModelAssetInfo> UiAssetLibrary::assets() const {
    return state_->info;
}

const UiModelAssetInfo* UiAssetLibrary::find(std::uint32_t hash) const {
    const auto found = state_->registry.find(hash);
    return found == state_->registry.end() ? nullptr : &state_->info[found->second];
}

bool UiAssetLibrary::is_original_null_binding(std::uint32_t hash) const {
    //Details7012 and List30008 retain the staffstat hash from their original
    //templates, but no shipped UI model registers that root name. Native
    //lookup_ui_model_asset_by_hash (852038) returns null, and the primary
    //attachment setter (84B186) explicitly accepts it. Keep this exact absence
    //distinct from unexpected missing resources, without substituting artwork.
    return hash == ui_model_asset_hash("staffstat") && !state_->registry.contains(hash);
}

std::expected<TextureImagePtr, TextureError> UiAssetLibrary::texture(std::string_view name) {
    const auto path = std::filesystem::path { name };
    //Procedural HUD attachments use the native data/ui/textures directory.
    return state_->textures.load(path.has_parent_path() ? name : "textures/" + std::string { name });
}

const std::vector<std::string>& UiAssetLibrary::diagnostics() const {
    return state_->diagnostics;
}

bool UiAssetLibrary::append(UiDrawList& list, std::uint32_t hash, const UiModelPlacement& placement) {
    if (!finite(placement)) {
        throw std::invalid_argument("Invalid UI model placement");
    }
    const auto registered = state_->registry.find(hash);
    if (registered == state_->registry.end()) {
        return false;
    }
    const auto& info = state_->info[registered->second];
    //858B85 submits signed selection heights to generic hilight/lolight
    //models. 4E6C10 preserves those edges and 4E68A0 uses the signed dimension
    //plus0.5, including zero. Frame/Slider deformation has a separate contract.
    if (info.kind != UiModelAssetKind::model && (placement.bounds[2] < placement.bounds[0] || placement.bounds[3] < placement.bounds[1])) {
        throw std::invalid_argument("Invalid UI model placement");
    }
    auto& asset = state_->data[registered->second];
    const auto part = static_cast<std::uint16_t>(placement.part_base + placement.part_value);
    if (part >= asset.parts.size()) {
        return false; //Native4E6B50 likewise reaches a null part, without
    }
    //fallback.
    const auto& model = *state_->models.model(asset.model_id);
    const auto selected = asset.parts[part];
    const auto& selected_node = model.nodes[selected];
    auto selected_bounds = selected_node.drawable->bounds;
    Deformation deform;
    if (info.kind == UiModelAssetKind::frame || info.kind == UiModelAssetKind::slider) {
        deform.node = info.kind == UiModelAssetKind::slider ? model.root : selected;
        deform.slider = info.kind == UiModelAssetKind::slider;
        const auto& bounds = model.nodes[*deform.node].drawable->bounds;
        if (deform.slider && model.nodes[*deform.node].drawable->positions.size() < 8) {
            throw std::runtime_error("UI slider requires eight root positions: " + info.name);
        }
        const auto width = bounds.maximum[0] - bounds.minimum[0];
        const auto height = bounds.maximum[1] - bounds.minimum[1];
        deform.middle_x = bounds.minimum[0] + width * .5f;
        deform.middle_y = bounds.minimum[1] + height * .5f;
        deform.width_delta = placement.bounds[2] - placement.bounds[0] - width;
        deform.height_delta = -(placement.bounds[3] - placement.bounds[1] - height);
        selected_bounds = deform.bounds(selected, selected_bounds);
    }
    auto minimum = mesh_transform::point(asset.pose[selected], selected_bounds.minimum);
    auto maximum = mesh_transform::point(asset.pose[selected], selected_bounds.maximum);
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (maximum[axis] < minimum[axis]) {
            std::swap(maximum[axis], minimum[axis]);
        }
    }
    const auto width = maximum[0] - minimum[0], height = maximum[1] - minimum[1];
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
        throw std::runtime_error("Degenerate UI model layout: " + info.name);
    }
    Layout layout { placement, minimum, maximum, width / (placement.bounds[2] - placement.bounds[0] + .5f), height / (placement.bounds[3] - placement.bounds[1] + .5f) };
    layout.normalize_depth = asset.normalize_depth;
    if (placement.rotation) {
        const auto angle = static_cast<double>(placement.rotation) * 3.1415927 * .00048828125;
        const auto sine_index = static_cast<std::int32_t>(static_cast<float>(angle * simulation_sine_samples_per_radian));
        const auto cosine_index = static_cast<std::int32_t>(static_cast<float>((static_cast<float>(angle) + 1.5707964) * simulation_sine_samples_per_radian));
        layout.sine = simulation_sine_at_index(sine_index);
        layout.cosine = simulation_sine_at_index(cosine_index);
    }
    auto clip = placement.clip;
    clip[0] = std::max(clip[0], 0.f);
    clip[1] = std::max(clip[1], 0.f);
    clip[2] = std::min(clip[2], list.canvas_width);
    clip[3] = std::min(clip[3], list.canvas_height);
    if (clip[0] >= clip[2] || clip[1] >= clip[3]) {
        return true;
    }
    //Constructor4E6550 hides only the first-child part chain. Traverse the
    //complete model using normal mesh hierarchy rules; non-part siblings keep
    //their own flags. The selected part clears only the native8010 mask.
    std::vector<std::size_t> pending;
    for (auto root = model.root; root; root = model.nodes[*root].next_sibling) {
        pending.push_back(*root);
    }
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const auto node_index = pending.back();
        pending.pop_back();
        const auto& node = model.nodes[node_index];
        auto flags = node.flags;
        if (std::ranges::find(asset.parts, node_index) != asset.parts.end()) {
            flags |= 0xC8010u;
            if (node_index == selected) {
                flags &= ~0x8010u;
            }
        }
        if (node.drawable && !(flags & 0x8210u)) {
            const auto& drawable = *node.drawable;
            const auto base = list.vertices.size();
            (void)narrow_index(base + drawable.position_indices.size());
            for (std::size_t corner = 0; corner < drawable.position_indices.size(); ++corner) {
                const auto position = drawable.position_indices[corner];
                if (position >= drawable.positions.size()) {
                    throw std::runtime_error("Invalid UI model corner: " + info.name);
                }
                UiDrawVertex vertex;
                vertex.position = layout.point(mesh_transform::point(asset.pose[node_index], deform.point(node_index, position, drawable.positions[position])));
                if (!std::ranges::all_of(vertex.position, [](float value) {
                        return std::isfinite(value);
                    })) {
                    throw std::runtime_error("Nonfinite UI model geometry: " + info.name);
                }
                if (corner < drawable.texture_coordinates.size()) {
                    vertex.uv = drawable.texture_coordinates[corner];
                }
                //Shared asset decoding includes the native source/upload row flips.
                //Imported UI UVs therefore remain exactly as authored, like World
                //and Advisor meshes. Generated glyph atlases own their separate UVs.
                vertex.color = corner < drawable.packed_vertex_colors.size() ? modulate(mesh_argb_color(drawable.packed_vertex_colors[corner]), placement.color) : placement.color;
                list.vertices.push_back(vertex);
            }
            for (const auto& group : drawable.groups) {
                if (!group.triangle_count) {
                    continue;
                }
                const auto* slot = group.texture_slot ? &model.textures.at(*group.texture_slot) : nullptr;
                const auto image = group.texture_slot ? state_->texture(asset, model, *group.texture_slot) : white();
                UiDrawCommand command;
                command.depth_buffered = true;
                command.texture = image;
                command.first_index = narrow_index(list.indices.size());
                command.clip = clip;
                command.material = derive_mesh_material(flags, group.flags, slot, image.get());
                //4E6550 marks UI texture sources0x100000, which6EFC10 maps to
                //renderer0x40000: sample the original top mip without minification.
                command.material.flags |= 0x40000u;
                const auto end = static_cast<std::size_t>(group.first_triangle) + group.triangle_count;
                if (end > drawable.triangles.size()) {
                    throw std::runtime_error("Invalid UI triangle group: " + info.name);
                }
                for (std::size_t triangle = group.first_triangle; triangle < end; ++triangle) {
                    for (const auto corner : drawable.triangles[triangle].corners) {
                        if (corner >= drawable.position_indices.size()) {
                            throw std::runtime_error("Invalid UI triangle corner: " + info.name);
                        }
                        list.indices.push_back(narrow_index(base + corner));
                    }
                }
                command.index_count = narrow_index(list.indices.size()) - command.first_index;
                command.native_model_filter = true;
                list.commands.push_back(std::move(command));
            }
        }
        if (!(flags & 0x20u)) {
            std::vector<std::size_t> children;
            for (auto child = node.first_child; child; child = model.nodes[*child].next_sibling) {
                children.push_back(*child);
            }
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                pending.push_back(*child);
            }
        }
    }
    return true;
}
} //namespace simcoaster
