#include "terrain_scene_cache.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/world_scene.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/construction_preview.hpp"
#include "simcoaster/terrain_catalog.hpp"
#include "simcoaster/terrain_texture.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace simcoaster {
bool world_mesh_intersects_view(const MeshModel& model, const MeshMatrix& view_projection) {
    if (model.terrain)
        return true; //Terrain has its own grid producer and visibility contract.
    const auto poses = evaluate_mesh_pose(model);
    if (!poses)
        throw std::runtime_error(describe_mesh_error(poses.error()));
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        if (node.drawable
            && !(node.flags & 0x8210u)
            && render_bounds_intersect(node.drawable->bounds, (*poses)[i], view_projection))
            return true;
    }
    return false;
}

struct WorldMeshScene::Sources {
    struct Source {
        MeshModelLibrary library;
        MeshTextureLibrary textures;

        static MeshTextureSources policy(const MeshModelLibrary& model_library,
                                         const std::filesystem::path& root,
                                         std::string_view theme,
                                         MeshTextureSet texture_set,
                                         TextureUse use) {
            const bool small = texture_set == MeshTextureSet::small;
            MeshTextureSources sources;
            sources.primary = { &model_library, {}, small ? "stexture" : "textures" };
            match (use) {
                case TextureUse::model => {
                    //4C2E20: even Generic Dynamic car meshes use ordinary SharedTex.
                    sources.secondary =
                        MeshTextureSource{ nullptr,
                                           root / "Data/levels" / theme / (small ? "ssharete" : "sharetex"),
                                           {} };
                    sources.fallback_to_library_root = true;
                }
                case TextureUse::dynamic_overlay => {
                    //688DE0: generated placement faces have their own source pair.
                    sources.secondary = MeshTextureSource{ nullptr,
                                                           root / "Data/levels" / theme / "dynamic",
                                                           small ? "stexture" : "textures" };
                    sources.use_quickload = false;
                }
                case TextureUse::coaster_track => {
                    //46DB70/46DF10: selected-size ride tracks, full Generic fallback.
                    sources.primary.relative_namespace = small ? "sgtextur" : "gtexture";
                    sources.secondary = MeshTextureSource{ nullptr, root / "Data/generic/dynamic", "textures" };
                    sources.use_quickload = false;
                }
                case _ => throw std::invalid_argument("Unknown native texture source policy");
            } return sources;
        }

        Source(MeshModelLibrary value,
               const std::filesystem::path& root,
               std::string_view theme,
               TextureUse use,
               MeshTextureSet texture_set,
               bool animated_textures)
            : library(std::move(value)),
              textures(policy(library, root, theme, texture_set, use), root, texture_set, animated_textures) {}
    };

    std::map<std::pair<std::string, TextureUse>, std::unique_ptr<Source>> entries;
    std::map<std::pair<std::string, TextureUse>, MeshTextureLibrary*> requests;
    std::optional<TerrainTextureAtlas> terrain_textures;
    TerrainTextureCatalog terrain_catalog;
    std::vector<TerrainCellMetadataRecord> initial_terrain_cells;
    bool special_view_terrain{};
    bool procedural_enabled = true;
    TerrainTileBounds procedural_bounds;
    TerrainSceneCache terrain_scene_cache;
    std::set<std::tuple<std::string, std::string, std::size_t, MeshLightingTransformFailure>> reported_lighting;
    std::vector<std::string> lighting_warnings;
};

WorldMeshScene::WorldMeshScene(std::filesystem::path root, std::string theme)
    : sources_(std::make_unique<Sources>()),
      root_(std::move(root)),
      theme_(std::move(theme)) {}

WorldMeshScene::~WorldMeshScene() = default;

namespace {
void lighting_owner(std::ostream& out, std::string_view asset, const WorldMeshInstance* instance) {
    out << "asset=" << asset;
    if (instance) {
        out << " instance=" << instance->metadata.slot << " generation=" << instance->generation;
        for (std::size_t i = 0; i < instance->playback.controls.size(); ++i) {
            const auto& control = instance->playback.controls[i];
            if (control.clip.active())
                out
                    << " control="
                    << i
                    << " animation="
                    << control.clip.category
                    << ':'
                    << control.clip.variant
                    << " frame="
                    << control.frame;
        }
    }
}
} //namespace

std::span<const std::string> WorldMeshScene::lighting_warnings() const {
    return sources_->lighting_warnings;
}

void WorldMeshScene::report_lighting(const MeshScene& scene,
                                     std::size_t first,
                                     std::string_view asset,
                                     const WorldMeshInstance* instance) {
    for (std::size_t i = first; i < scene.lighting_diagnostics.size(); ++i) {
        const auto& diagnostic = scene.lighting_diagnostics[i];
        if (!sources_->reported_lighting
                 .emplace(std::string{ asset }, diagnostic.model, diagnostic.node_index, diagnostic.reason)
                 .second)
            continue;
        std::ostringstream message;
        message << "Ambient lighting fallback: ";
        lighting_owner(message, asset, instance);
        message
            << " model="
            << diagnostic.model
            << " node="
            << diagnostic.node_index
            << " ("
            << diagnostic.node
            << ") matrix=";
        for (const auto value : diagnostic.transform)
            message << value << ',';
        sources_->lighting_warnings.push_back(message.str());
        std::ofstream log(root_ / "simcoaster-render.log", std::ios::app);
        if (log)
            log << message.str() << '\n';
    }
}

void WorldMeshScene::append_model(MeshScene& scene,
                                  const MeshModel& model,
                                  MeshTextureLibrary& source,
                                  const MeshLighting& lighting,
                                  std::string_view asset,
                                  const WorldMeshInstance* instance,
                                  bool whiten) {
    const auto first = scene.lighting_diagnostics.size();
    try {
        append_mesh_scene(scene, model, source, lighting, nullptr, {}, whiten);
    } catch (const std::exception& error) {
        std::ostringstream message;
        lighting_owner(message, asset, instance);
        message << ": " << error.what();
        throw std::runtime_error(message.str());
    }
    report_lighting(scene, first, asset, instance);
}

MeshTextureLibrary& WorldMeshScene::textures(const std::filesystem::path& directory, TextureUse use) {
    auto text = directory.native_encoded_string();
    while (!text.empty() && (text.back() == '/' || text.back() == '\\'))
        text.pop_back();
    auto path = text.empty() ? root_ : std::filesystem::path{ text };
    //A scene owns fixed mounted providers. Resolve filesystem aliases once;
    //checking/canonicalizing every live mesh's path on every frame dominated
    //CPU submission on Windows, in both hardware and software render modes.
    const auto request = std::pair{ path.lexically_normal().native_encoded_string(), use };
    if (const auto found = sources_->requests.find(request); found != sources_->requests.end())
        return *found->second;
    if (!std::filesystem::is_directory(path) && !std::filesystem::is_regular_file(path))
        path += ".wad";
    const auto key = std::pair{ std::filesystem::weakly_canonical(path).native_encoded_string(), use };
    if (const auto found = sources_->entries.find(key); found != sources_->entries.end()) {
        sources_->requests.emplace(request, &found->second->textures);
        return found->second->textures;
    }
    auto library = open_mesh_model_library(path);
    if (!library)
        throw std::runtime_error(describe_mesh_error(library.error()));
    auto created =
        std::make_unique<Sources::Source>(std::move(*library), root_, theme_, use, texture_set_, animated_textures_);
    auto& result = created->textures;
    sources_->entries.emplace(key, std::move(created));
    sources_->requests.emplace(request, &result);
    return result;
}

void WorldMeshScene::configure_texture_set(MeshTextureSet selected) {
    if (selected == texture_set_)
        return;
    if (terrain_ || !sources_->entries.empty())
        throw std::logic_error("Changing original texture sets requires World recreation");
    texture_set_ = selected;
}

void WorldMeshScene::configure_animated_textures(bool enabled) {
    animated_textures_ = enabled;
    for (auto& [key, source] : sources_->entries) {
        (void)key;
        source->textures.set_animated_textures(enabled);
    }
}

void WorldMeshScene::load_terrain() {
    const auto path = root_ / "Data/levels" / theme_ / "terrain.wad";
    auto library = open_mesh_model_library(path);
    if (!library)
        throw std::runtime_error(describe_mesh_error(library.error()));
    const auto id = library->load(1, "base");
    if (!id)
        throw std::runtime_error(describe_mesh_error(id.error()));
    auto loaded = *library->model(*id);
    if (!loaded.terrain)
        throw std::runtime_error("Base terrain model has no height grid");
    //The nested LND load can re-enable feature4 after Intro's pre-load clear.
    //Use the native fixed16 Intro resolution,32 for the default park profile.
    auto folded = theme_;
    for (auto& ch : folded)
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
    auto catalog_bytes = library->read_resource(theme_ + ".tct");
    if (!catalog_bytes || !*catalog_bytes)
        throw std::runtime_error("Terrain texture catalog is missing");
    const auto& bytes = **catalog_bytes;
    auto catalog = parse_terrain_texture_catalog({ reinterpret_cast<const char*>(bytes.data()), bytes.size() },
                                                 texture_set_ == MeshTextureSet::small);
    if (!catalog)
        throw std::runtime_error(describe_texture_error(catalog.error()));
    bind_terrain_texture_catalog(loaded, *catalog);
    auto procedural = load_terrain_texture_atlas(*library, folded == "wintro" ? 16u : 32u);
    if (!procedural)
        throw std::runtime_error(describe_texture_error(procedural.error()));
    initialize_terrain_slope_flags(*loaded.terrain);
    sources_->initial_terrain_cells = loaded.terrain->cell_metadata;
    initialize_procedural_terrain_cell_flags(*loaded.terrain);
    procedural->configure_bounds(sources_->procedural_bounds);
    procedural->update(loaded, textures(path));
    terrain_ = std::move(loaded);
    sources_->terrain_textures = std::move(*procedural);
    sources_->terrain_catalog = std::move(*catalog);
}

void WorldMeshScene::update_cell_texture(std::int32_t x,
                                         std::int32_t y,
                                         std::uint32_t group,
                                         std::uint32_t member,
                                         std::uint32_t rotation) {
    if (terrain_)
        update_terrain_cell_texture(*terrain_,
                                    sources_->terrain_catalog,
                                    sources_->initial_terrain_cells,
                                    x,
                                    y,
                                    group,
                                    member,
                                    rotation);
}

void WorldMeshScene::configure_procedural_terrain(bool enabled, std::uint32_t dimension) {
    if (!terrain_ || !terrain_->terrain || !sources_->terrain_textures)
        throw std::logic_error("Profile terrain owner is not initialized");
    if (enabled && sources_->terrain_textures->dimension() != dimension) {
        auto library = open_mesh_model_library(root_ / "Data/levels" / theme_ / "terrain.wad");
        if (!library)
            throw std::runtime_error(describe_mesh_error(library.error()));
        auto atlas = load_terrain_texture_atlas(*library, dimension);
        if (!atlas)
            throw std::runtime_error(describe_texture_error(atlas.error()));
        atlas->configure_bounds(sources_->procedural_bounds);
        sources_->terrain_textures = std::move(*atlas);
    }
    sources_->procedural_enabled = enabled;
    auto& grid = *terrain_->terrain;
    if (enabled && !sources_->special_view_terrain)
        initialize_procedural_terrain_cell_flags(grid);
    else
        for (std::size_t i = 0; i < grid.cell_metadata.size(); ++i) {
            const auto current = static_cast<std::uint16_t>(grid.cell_metadata[i].flags);
            if (!(current & 0x81u))
                grid.cell_metadata[i].flags = static_cast<TerrainCellFlags>(
                    (current & 0xFF07u)
                    | (static_cast<std::uint16_t>(sources_->initial_terrain_cells[i].flags) & 0xF8u));
        }
    sources_->terrain_textures->invalidate();
}

void WorldMeshScene::configure_procedural_bounds(TerrainTileBounds bounds) {
    sources_->procedural_bounds = bounds;
    if (sources_->terrain_textures)
        sources_->terrain_textures->configure_bounds(bounds);
}

bool WorldMeshScene::procedural_terrain_enabled() const {
    return sources_->procedural_enabled;
}

const TerrainTextureAtlas* WorldMeshScene::procedural_textures() const {
    return sources_->terrain_textures ? &*sources_->terrain_textures : nullptr;
}

std::span<const TerrainCellMetadataRecord> WorldMeshScene::original_terrain_cells() const {
    return sources_->initial_terrain_cells;
}

void WorldMeshScene::set_special_view_terrain(bool special) {
    if (special == sources_->special_view_terrain)
        return;
    if (!terrain_ || !terrain_->terrain || !sources_->terrain_textures)
        throw std::logic_error("Special camera terrain owner is not initialized");
    auto& grid = *terrain_->terrain;
    if (grid.cell_metadata.size() != sources_->initial_terrain_cells.size())
        throw std::logic_error("Special camera terrain snapshot extent changed");
    if (special || !sources_->procedural_enabled) {
        //4D7C50(0): retain current low state bits and protected/overlay cells,
        //restoring the original static UV rotation/mirror/overlay source bits.
        for (std::size_t i = 0; i < grid.cell_metadata.size(); ++i) {
            const auto current = static_cast<std::uint16_t>(grid.cell_metadata[i].flags);
            if (!(current & 0x81u))
                grid.cell_metadata[i].flags = static_cast<TerrainCellFlags>(
                    (current & 0xFF07u)
                    | (static_cast<std::uint16_t>(sources_->initial_terrain_cells[i].flags) & 0xF8u));
        }
    } else {
        //4D7C50(1), reusing the same native metadata operation as terrain load.
        initialize_procedural_terrain_cell_flags(grid);
    }
    sources_->special_view_terrain = special;
    sources_->terrain_textures->invalidate();
}

bool WorldMeshScene::special_view_terrain() const {
    return sources_->special_view_terrain;
}

void WorldMeshScene::append_construction_preview(WorldRuntime& c,
                                                 const ParkConstructionState& state,
                                                 MeshScene& output,
                                                 const MeshLighting& lighting,
                                                 float phase,
                                                 float heading,
                                                 std::array<float, 2> offset) {
    if (state.faces.empty())
        return;
    //Keep generated Dynamic sources distinct from ordinary car model sources
    //that happen to use the same Generic archive.
    if (!state.show_red_faces) {
        auto visible = state.faces;
        std::erase_if(visible, [](const auto& face) {
            return face.material == 1;
        });
        simcoaster::append_construction_preview(c,
                                                output,
                                                visible,
                                                textures(root_ / "Data/generic/dynamic", TextureUse::dynamic_overlay),
                                                lighting,
                                                phase,
                                                heading,
                                                offset);
        return;
    }
    simcoaster::append_construction_preview(c,
                                            output,
                                            state.faces,
                                            textures(root_ / "Data/generic/dynamic", TextureUse::dynamic_overlay),
                                            lighting,
                                            phase,
                                            heading,
                                            offset);
}

WorldSceneStatistics WorldMeshScene::build(WorldRuntime& context,
                                           WorldMeshRuntime& runtime,
                                           MeshScene& scene,
                                           const MeshLighting& lighting,
                                           const MeshMatrix* view_projection) {
    if (context.construction.editor)
        context.construction.editor->refresh_preview(context);
    const auto* preview = context.construction.editor ? context.construction.editor->preview() : nullptr;
    scene.clear();
    WorldSceneStatistics statistics;
    std::vector<std::uint8_t> base_coverage;
    if (runtime.terrain_texture_blending
        && sources_->procedural_enabled
        && !sources_->special_view_terrain
        && terrain_
        && terrain_->terrain) {
        base_coverage.resize(terrain_->terrain->cell_metadata.size());
        for (std::size_t index = 1; index < runtime.slot_limit(); ++index)
            if (auto* instance = runtime.instance(static_cast<std::uint32_t>(index));
                instance && instance->playback.registered) {
                const NativeCoasterPreviewModel* replacement = nullptr;
                if (preview)
                    for (const auto& model : preview->models)
                        if (model.source_slot == index) {
                            replacement = &model;
                            break;
                        }
                const auto* definition = runtime.definition(instance->template_id);
                if (!definition)
                    throw std::logic_error("mesh base template is missing");
                if (!replacement)
                    prepare_mesh_base(*instance, *definition);
                const auto& cells = replacement ? replacement->terrain_texture_cells : instance->terrain_texture_cells;
                if (!replacement || replacement->registered)
                    for (const auto& cell : cells)
                        base_coverage.at(std::size_t{ cell.row } * terrain_->terrain->width_cells + cell.column) = 1;
            }
        if (preview)
            for (const auto& model : preview->models)
                if (!model.source_slot && model.registered)
                    for (const auto& cell : model.terrain_texture_cells)
                        base_coverage.at(std::size_t{ cell.row } * terrain_->terrain->width_cells + cell.column) = 1;
    }
    if (terrain_) {
        auto& source = textures(root_ / "Data/levels" / theme_ / "terrain.wad");
        if (sources_->terrain_textures && sources_->procedural_enabled && !sources_->special_view_terrain)
            sources_->terrain_textures->update(*terrain_, source);
        append_cached_terrain_scene(
            scene,
            *terrain_,
            source,
            lighting,
            false,
            sources_->special_view_terrain || !sources_->procedural_enabled ? nullptr : procedural_textures(),
            base_coverage,
            sources_->terrain_scene_cache,
            view_projection);
        const auto first = scene.lighting_diagnostics.size();
        append_mesh_drawables(scene, *terrain_, source, lighting, false, view_projection);
        report_lighting(scene, first, "terrain");
    }
    FrameTimer meshes_timer{ FrameStage::world_meshes };
    for (std::size_t index = 1; index < runtime.slot_limit(); ++index) {
        auto* instance = runtime.instance(static_cast<std::uint32_t>(index));
        if (!instance)
            continue;
        ++statistics.live_slots;
        if (!instance->playback.registered && !(instance->hoarding && instance->hoarding->registered))
            continue;
        //0x4C3660 repairs bindings deferred by the background updater, evaluates
        //the current pose and marks this slot handled before geometry submission.
        const auto activated = runtime.mark_visible(context, static_cast<std::uint32_t>(index));
        if (!activated)
            throw std::runtime_error(describe_mesh_error(activated.error()));
        const auto* definition = runtime.definition(instance->template_id);
        if (!definition)
            throw std::runtime_error("Live mesh template is missing");
        auto& source = textures(definition->source_directory);
        const NativeCoasterPreviewModel* replacement = nullptr;
        if (preview)
            for (const auto& model : preview->models)
                if (model.source_slot == index) {
                    replacement = &model;
                    break;
                }
        if (instance->playback.registered) {
            const auto& model = replacement ? replacement->model : instance->playback.model;
            if (!view_projection || world_mesh_intersects_view(model, *view_projection)) {
                append_model(scene,
                             model,
                             source,
                             lighting,
                             definition->source_directory,
                             instance,
                             runtime.hovered_slot == index && runtime.hovered_generation == instance->generation);
                ++statistics.main_meshes;
            }
            if (const auto* coasters = runtime.coasters())
                if (const auto* ride = coasters->find(static_cast<std::uint32_t>(index));
                    ride && ((preview && preview->map_id == ride->map_id && preview->track) || ride->generated_mesh)) {
                    const auto& track = preview && preview->map_id == ride->map_id && preview->track
                                          ? *preview->track
                                          : *ride->generated_mesh;
                    append_coaster_track_scene(scene,
                                               track,
                                               *ride->definition->balance.mesh_definition,
                                               textures(definition->source_directory, TextureUse::coaster_track),
                                               lighting,
                                               &textures(root_ / "Data/generic/dynamic", TextureUse::dynamic_overlay));
                    ++statistics.coaster_tracks;
                }
        }
        if (instance->hoarding && instance->hoarding->registered) {
            const auto& model =
                replacement && replacement->hoarding ? *replacement->hoarding : instance->hoarding->model;
            if (!view_projection || world_mesh_intersects_view(model, *view_projection)) {
                append_model(scene, model, source, lighting, definition->source_directory, instance);
                ++statistics.hoardings;
            }
        }
    }
    if (preview)
        for (const auto& model : preview->models)
            if (!model.source_slot) {
                const auto* definition = runtime.definition(model.template_id);
                if (!definition)
                    throw std::runtime_error("Coaster preview template is missing");
                auto& source = textures(definition->source_directory);
                if (model.registered) {
                    append_model(scene, model.model, source, lighting, definition->source_directory);
                    ++statistics.main_meshes;
                }
                if (model.hoarding) {
                    append_model(scene, *model.hoarding, source, lighting, definition->source_directory);
                    ++statistics.hoardings;
                }
            }
    return statistics;
}
} //namespace simcoaster
