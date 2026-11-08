#pragma once
#include "mesh_scene.hpp"

namespace simcoaster {
class TerrainVisibilityCache;

//6D2C00 retains a batch and updates only its UVs until the view/grid/source
//changes. Owned world vertices let D3D11 do homogeneous clipping and winding
//rejection, without the original fixed 128-sample scratch rows or WORD limits.
class TerrainSkyShadowCache {
    TerrainGridData grid_;
    MeshMatrix view_projection_{};
    TextureImagePtr source_;
    MeshPoint sky_offset_{};
    float height_bias_{};
    std::vector<SceneVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    bool valid_{};
    std::shared_ptr<TerrainVisibilityCache> visibility_;

public:
    void clear();
    void append(MeshScene&,
                const TerrainGridView*,
                const WeatherFrame&,
                const MeshMatrix& view_projection,
                bool cull_to_view = false);
};
} //namespace simcoaster
