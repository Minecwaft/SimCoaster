#pragma once
#include "world_domains_fwd.hpp"
#include "mesh_model.hpp"

namespace simcoaster {
struct World;
enum class TerrainShadowSampling {
    object_clamped,
    terrain_centered
};
//A missing sample preserves the existing normal. Terrain rendering uses the
//native half-cell selector; object meshes clamp their containing map cell.
std::optional<float> construction_terrain_shadow_gain(const TerrainGridView&,
                                                      float world_x,
                                                      float world_z,
                                                      TerrainShadowSampling,
                                                      float shadowed_gain = 0.25f);
std::array<float, 3> construction_normal_intensity(std::array<float, 3>, float target);
void rebuild_terrain_shading_flags(TerrainGridData&, const World&, std::uint32_t map_width, std::uint32_t map_height);
void apply_construction_mesh_darkening(MeshModel&,
                                       const TerrainGridView&,
                                       TerrainShadowSampling = TerrainShadowSampling::object_clamped,
                                       bool packed_grayscale = false);
void snap_construction_mesh_to_terrain(MeshModel&, const TerrainGridView&);
void rebuild_construction_terrain_shading(ConstructionWorld&, TerrainGridData&);
void refresh_construction_object_meshes(ConstructionWorld&);
std::uint32_t normalize_construction_special_ride_meshes(ConstructionWorld&);
} //namespace simcoaster
