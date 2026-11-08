#pragma once
#include "coaster_mesh.hpp"
#include "mesh_terrain.hpp"

namespace simcoaster {
//Render values captured from a real, temporary graph edit. A zero source_slot
//means this model was created for the proposal and has no live runtime handle.
//The temporary graph, models and allocator changes are rolled back before
//these values leave the editor. They cannot enter save projections or picking.
struct NativeCoasterPreviewModel {
    std::uint32_t source_slot{}, template_id{};
    bool registered{};
    MeshModel model;
    std::optional<MeshModel> hoarding;
    std::vector<MeshBaseTilePlacement> terrain_texture_cells;
};

struct NativeCoasterPreview {
    std::uint32_t map_id{};
    bool valid{};
    std::int32_t piece_cost{}, interaction_cost{};
    std::optional<CoasterTrackMesh> track;
    std::vector<NativeCoasterPreviewModel> models;
};
} //namespace simcoaster
