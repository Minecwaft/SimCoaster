#include "simcoaster/scene_camera.hpp"
#include "simcoaster/mesh_transform.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

void require(bool value) {
    if (!value)
        throw std::runtime_error("Invalid mesh scene geometry");
}

Point cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Point normalized(Point value) {
    const auto length = std::sqrt(dot(value, value));
    if (length > 0)
        for (auto& component : value)
            component /= length;
    else
        value = { 0, -1, 0 };
    return value;
}
} //namespace

std::array<float, 3> SceneCamera::eye() const {
    if (explicit_pose)
        return { (*explicit_pose)[12], (*explicit_pose)[13], (*explicit_pose)[14] };
    const auto radius = distance * std::cos(pitch);
    return { target[0] + radius * std::sin(yaw),
             target[1] + distance * std::sin(pitch),
             target[2] + radius * std::cos(yaw) };
}

std::array<float, 3> SceneCamera::forward() const {
    if (explicit_pose)
        return normalized({ (*explicit_pose)[8], (*explicit_pose)[9], (*explicit_pose)[10] });
    const auto position = eye();
    return normalized({ target[0] - position[0], target[1] - position[1], target[2] - position[2] });
}

void SceneCamera::fit(const MeshBounds& bounds) {
    explicit_pose.reset();
    float radius_squared = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        target[axis] = (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5f;
        const auto extent = (bounds.maximum[axis] - bounds.minimum[axis]) * 0.5f;
        radius_squared += extent * extent;
    }
    const auto radius = std::max(std::sqrt(radius_squared), 1.0f);
    distance = radius * 2.8f;
    near_plane = std::max(radius * 0.001f, 0.01f);
    far_plane = distance + radius * 20;
}

MeshMatrix SceneCamera::view() const {
    if (explicit_pose)
        return mesh_transform::inverse(*explicit_pose);
    require(distance > 0);
    const auto position = eye();
    Point forward;
    for (std::size_t axis = 0; axis < 3; ++axis)
        forward[axis] = target[axis] - position[axis];
    forward = normalized(forward);
    const auto right = normalized(cross({ 0, 1, 0 }, forward)), up = cross(forward, right);
    return { right[0],
             up[0],
             forward[0],
             0,
             right[1],
             up[1],
             forward[1],
             0,
             right[2],
             up[2],
             forward[2],
             0,
             -dot(position, right),
             -dot(position, up),
             -dot(position, forward),
             1 };
}

MeshMatrix SceneCamera::view_projection(float aspect) const {
    require(aspect > 0 && near_plane > 0 && far_plane > near_plane);
    const auto view_matrix = view();
    const auto focal = 1.0f / std::tan(vertical_fov * 0.5f), depth = far_plane / (far_plane - near_plane);
    const MeshMatrix projection{
        focal / aspect, 0, 0, 0, 0, focal, 0, 0, 0, 0, depth, 1, 0, 0, -near_plane * depth, 0
    };
    MeshMatrix result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t k = 0; k < 4; ++k)
                result[row * 4 + column] += view_matrix[row * 4 + k] * projection[k * 4 + column];
    return result;
}
} //namespace simcoaster
