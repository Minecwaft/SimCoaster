#pragma once
#include <array>

namespace simcoaster {
using MeshPoint = std::array<float, 3>;
using MeshMatrix = std::array<float, 16>;
inline constexpr MeshMatrix mesh_identity_matrix { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

struct MeshBounds {
    std::array<float, 3> minimum {}, maximum {};
};
} //namespace simcoaster
