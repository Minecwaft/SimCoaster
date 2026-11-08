#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace simcoaster {
struct WorldRuntime;
struct ParkObjectRuntimeView;
//Native666D20/666C00 selection: entrance-sign span, then footprint/entry/
//exit/obstacle parent ownership. Callers apply their own UI/retirement gates.
ParkObjectRuntimeView* find_world_park_object_at_map_cell(WorldRuntime&, std::int32_t x, std::int32_t y);
std::uint32_t world_mesh_slot_at_map_cell(WorldRuntime&, std::int32_t x, std::int32_t y);

struct WorldMapCellSurface {
    std::uint32_t mesh_slot{};
    std::optional<double> height;
};

//Keep an identified mesh when its coarse grid has no sample for this cell.
//Cursor picking must then test its triangles without the height prefilter.
WorldMapCellSurface
query_world_map_cell_surface(WorldRuntime&, std::int32_t x, std::int32_t y, std::uint32_t selected_mesh_slot = 0);
//Native 0x4A8760. A supplied nonzero slot selects that mesh directly. Without
//a usable height grid, an in/out slot is cleared and returns zero; a null slot
//pointer instead samples the terrain. This is distinct from triangle queries.
//A cell outside that grid follows the same missing-height contract.
double sample_world_map_cell_surface(WorldRuntime&, std::int32_t x, std::int32_t y, std::uint32_t* slot = nullptr);
//Reset the root basis, preserve its projective components, set translation,
//and rotate Z, X, Y using the native rounded simulation sine lookup.
void set_mesh_root_pose_zxy(WorldRuntime&,
                            std::uint32_t slot,
                            const std::array<float, 3>& position,
                            const std::array<float, 3>& angles);
} //namespace simcoaster
