#pragma once
#include "mesh_types.hpp"
#include <optional>

namespace simcoaster {
struct SceneCamera {
    std::array<float, 3> target {};
    float yaw { 0.75f }, pitch { 0.55f }, distance { 100 }, vertical_fov { 0.7853982f }, near_plane { 0.1f }, far_plane { 1000 };
    //Owned camera-to-world pose for native World, POI and embedded ride cameras.
    //Preserve their roll/scale/blended basis; ordinary diagnostic cameras
    //retain the existing target/yaw/pitch/distance orbit contract.
    std::optional<MeshMatrix> explicit_pose;
    std::array<float, 3> eye() const;
    std::array<float, 3> forward() const;
    MeshMatrix view() const;
    MeshMatrix view_projection(float aspect) const;
    void fit(const MeshBounds&);
};
} //namespace simcoaster
