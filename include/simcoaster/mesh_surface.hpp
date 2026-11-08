#pragma once
#include "mesh_model.hpp"

namespace simcoaster {
struct MeshSurfaceTriangle {
    std::array<std::array<float, 3>, 3> vertices;
    std::array<float, 3> normal;
};

struct MeshSurfaceBatch {
    std::vector<MeshSurfaceTriangle> triangles;
    bool first_hit {}, return_on_improvement {};
};

//Snapshot of the native workspace: accumulated translation and cardinal
//placement, independent of the animated clone matrices.
struct MeshSurfaceQuery {
    bool terrain {};
    float offset {}, fallback { -100 };
    std::vector<MeshSurfaceBatch> batches;
    float sample(float x, float z) const;
};

std::expected<MeshSurfaceQuery, MeshError> prepare_mesh_surface_query(const MeshModel&, std::optional<std::size_t> base_node, std::int32_t orientation, bool auxiliary_surfaces);
} //namespace simcoaster
