#include "simcoaster/frame_profile.hpp"
#include "simcoaster/terrain_texture.hpp"
#include "simcoaster/world_scene.hpp"
#include "simcoaster/text_format.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
void require(bool valid, std::string resource = {}) {
    if (!valid) {
        throw MeshError { MeshFailure::configuration, 0, std::move(resource), {} };
    }
}

bool same(const MeshBaseTilePlacement& a, const MeshBaseTilePlacement& b) {
    return a.column == b.column && a.row == b.row && a.cell.flags == b.cell.flags && a.cell.resource_slot_index == b.cell.resource_slot_index;
}
} //namespace

bool WorldMeshScene::prepare_mesh_base(WorldMeshInstance& instance, const WorldMeshTemplate& source, bool force) {
    FrameTimer timer { FrameStage::mesh_bases };
    const auto* atlas = procedural_textures();
    require(terrain_ && terrain_->terrain && atlas, "terrain texture owner");
    //Native p-prefixed construction alternates have no primary tile records.
    const bool alternate = instance.playback.assets == source.alternate;
    if (!procedural_terrain_enabled() || !(instance.metadata.creation_flags & 8u) || alternate || !source.terrain_texture_plan) {
        for (const auto& [slot, image] : instance.terrain_texture_originals) {
            auto& target = instance.playback.model.textures.at(slot);
            target.image_override = image;
            target.flags = static_cast<std::uint16_t>((target.flags & ~0x800u) | (source.assets->model.textures.at(slot).flags & 0x800u));
        }
        instance.terrain_texture_originals.clear();
        instance.terrain_texture_cells.clear();
        instance.terrain_texture_error.reset();
        return true;
    }
    const auto grid = terrain_->terrain->view();
    if (active_frame_profile) {
        ++active_frame_profile->counters.mesh_base_evaluations;
    }
    auto placed = place_mesh_base_tiles(*source.terrain_texture_plan, instance.playback.model, grid, instance.metadata.orientation_degrees);
    if (!placed) {
        throw placed.error();
    }
    if (!force && std::ranges::equal(*placed, instance.terrain_texture_cells, same)) {
        return !instance.terrain_texture_error;
    }
    auto slots = instance.playback.model.textures;
    for (const auto& [slot, image] : instance.terrain_texture_originals) {
        slots.at(slot).image_override = image;
        slots[slot].flags = static_cast<std::uint16_t>((slots[slot].flags & ~0x800u) | (source.assets->model.textures.at(slot).flags & 0x800u));
    }
    std::vector<std::pair<std::size_t, TextureImagePtr>> originals;
    auto& library = textures(source.source_directory);
    //6E41F0 scans Y-major, then X, including repeated material slots. A repeated
    //slot sees the previous generated image; this is deliberately not deduped.
    try {
        for (const auto& cell : *placed) {
            const auto slot = std::size_t { cell.cell.resource_slot_index };
            require(slot < slots.size(), source.source_directory);
            auto& target = slots[slot];
            TextureImagePtr original = target.image_override;
            if (std::ranges::none_of(originals, [&](const auto& entry) {
                    return entry.first == slot;
                })) {
                originals.emplace_back(slot, original);
            }
            if (!original) {
                //Native get_mesh_texture_source_record_slot requests source0,
                //independent of the currently selected animation variant.
                auto first_source = target;
                first_source.source_variant = 0;
                auto loaded = library.load(first_source);
                if (!loaded) {
                    loaded = library.missing_texture();
                }
                if (loaded) {
                    original = *loaded;
                }
            }
            require(original != nullptr, source.source_directory);
            if (active_frame_profile) {
                ++active_frame_profile->counters.mesh_base_compositions;
            }
            auto composed = compose_terrain_texture_cell(atlas->definition(), atlas->layer_index(), cell.column, cell.row, original.get(), cell.cell.flags);
            if (!composed) {
                throw MeshError { MeshFailure::configuration, composed.error().offset, describe_texture_error(composed.error()), {} };
            }
            target.image_override = std::make_shared<const TextureImage>(std::move(*composed));
            //The native override holds an already-derived resource state. Source
            //bit800 suppresses initial file binding, not this newly resolved private
            //source.
            target.flags &= static_cast<std::uint16_t>(~0x800u);
        }
    } catch (const MeshError& error) {
        //4C3C54 logs a failed blend and still returns the live mesh. Keep the
        //previous images intact on refresh; no partial resource-state chain can
        //escape. Retain covered cells, as the native factory marks them first.
        instance.terrain_texture_error = error;
        instance.terrain_texture_cells = std::move(*placed);
        print_text(stderr, "Could not blend base for ride {}: {}\n", source.resource_name, describe_mesh_error(error));
        return false;
    }
    instance.playback.model.textures = std::move(slots);
    instance.terrain_texture_originals = std::move(originals);
    instance.terrain_texture_cells = std::move(*placed);
    instance.terrain_texture_error.reset();
    return true;
}
} //namespace simcoaster
