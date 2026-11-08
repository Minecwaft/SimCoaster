#pragma once
#include "mesh_model.hpp"
#include "terrain_grid.hpp"

namespace simcoaster {
struct DefinitionGrid;

struct MeshBaseTilePlan {
    std::uint32_t width {}, height {};
    std::vector<TerrainCellMetadataRecord> cells;
};

struct MeshProceduralBase {
    std::optional<std::size_t> best_node;
    std::size_t selected_vertex_count {}; //Primary list only, excluding subnodes.
    std::vector<std::vector<std::uint16_t>> vertex_indices, secondary_vertex_indices;
    bool allowed { true };
};

//Selection half of 0x4CE600: primary vertices need BOTH grid axes within
//.01; flag40 subnodes need EITHER axis within .1 and abs(world Y)<100.
std::expected<MeshProceduralBase, MeshError> select_mesh_procedural_base(const MeshModel&, const DefinitionGrid* shape, const TerrainGridView&, std::int32_t width, std::int32_t height, bool allow_skirts = true);
void apply_mesh_procedural_base_flags(MeshModel&, const MeshProceduralBase&);
//4CFA40: recover each base tile's material and UV orientation from the most
//axis-aligned eligible triangle. Also performs the native coverage-aware UV
//preparation on the selected node. No global scratch buffers or 16-tile limit.
std::expected<MeshBaseTilePlan, MeshError> build_mesh_base_tile_plan(MeshModel&, const MeshProceduralBase&, const DefinitionGrid*, const TerrainGridView&, std::uint32_t width, std::uint32_t height);

struct MeshBaseTilePlacement {
    std::uint32_t column {}, row {};
    TerrainCellMetadataRecord cell;
};

//4C3A15..4C3C54: cardinal placement and composition of the exported UV turn.
std::expected<std::vector<MeshBaseTilePlacement>, MeshError> place_mesh_base_tiles(const MeshBaseTilePlan&, const MeshModel&, const TerrainGridView&, std::int32_t orientation);
enum class MeshTerrainProcessingFlag : std::uint32_t {
    none = 0,
    suppress_gridlines = 2,
    suppress_surface = 4
};
//Source positions stay immutable. Runtime transforms supply the world pose;
//the inverse returns fitted points to local space. Bounds only expand.
//Processing bits2/4 suppress gridline/surface geometry, respectively.
std::expected<void, MeshError> fit_mesh_to_terrain(const MeshModel& source, MeshModel& runtime, const MeshProceduralBase&, const TerrainGridView&, float reference_height, MeshTerrainProcessingFlag processing_flags = MeshTerrainProcessingFlag::none);
//Shared cardinal mapper (0x4AA2C0) used by footprint and hoarding samples.
std::expected<std::array<std::int64_t, 2>, MeshError> mesh_footprint_sample(const std::array<std::int64_t, 2>& minimum, const std::array<std::int64_t, 2>& maximum, std::int32_t orientation, float x_fraction, float y_fraction);
std::expected<float, MeshError> mesh_placement_reference_height(const TerrainGridView&, const std::array<std::int64_t, 2>& origin, const DefinitionGrid* shape, std::int32_t width, std::int32_t height, std::uint32_t creation_flags, std::int32_t orientation);
} //namespace simcoaster
