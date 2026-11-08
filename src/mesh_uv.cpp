#include "simcoaster/mesh_model.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
void require(bool valid) {
    if (!valid)
        throw MeshError{ MeshFailure::configuration, 0, {}, {} };
}
} //namespace

void
normalize_mesh_uvs(MeshModel& model, std::optional<std::size_t> selected_node, const MeshUvBaseCoverage* coverage) {
    if (selected_node)
        require(*selected_node < model.nodes.size());
    for (std::size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        if (selected_node && node_index != *selected_node)
            continue;
        auto& node = model.nodes[node_index];
        if (!node.drawable || (node.flags & 0x200u) || node.drawable->texture_coordinates.empty())
            continue;
        auto& drawable = *node.drawable;
        for (auto& point : drawable.texture_coordinates)
            for (auto& value : point) {
                require(std::isfinite(value));
                double integral{};
                auto fraction = std::modf(static_cast<double>(value), &integral);
                const auto test = static_cast<float>(fraction);
                if (test < -0.98f)
                    fraction = -1;
                else if (test > -0.02f && test < 0.02f)
                    fraction = 0;
                else if (test > 0.98f)
                    fraction = 1;
                value = static_cast<float>(static_cast<float>(integral) + fraction);
            }
        std::vector<std::array<float, 2>> minima;
        minima.reserve(drawable.triangles.size());
        for (const auto& triangle : drawable.triangles) {
            std::array<float, 2> minimum{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            for (const auto corner : triangle.corners) {
                require(corner < drawable.texture_coordinates.size());
                for (std::size_t axis = 0; axis < 2; ++axis)
                    minimum[axis] = std::min(minimum[axis], drawable.texture_coordinates[corner][axis]);
            }
            minima.push_back(minimum);
        }
        std::vector<bool> visited(drawable.texture_coordinates.size());
        for (std::size_t i = 0; i < drawable.triangles.size(); ++i)
            for (const auto corner : drawable.triangles[i].corners)
                if (!visited[corner]) {
                    for (std::size_t axis = 0; axis < 2; ++axis)
                        drawable.texture_coordinates[corner][axis] -= std::floor(minima[i][axis]);
                    visited[corner] = true;
                }
        for (const auto& group : drawable.groups)
            if (group.texture_slot) {
                bool covered = true;
                if (coverage) {
                    require(coverage->stride && coverage->positions.size() == drawable.positions.size());
                    for (std::size_t i = group.first_triangle;
                         i < std::size_t{ group.first_triangle } + group.triangle_count;
                         ++i) {
                        require(i < drawable.triangles.size());
                        std::array<std::array<float, 3>, 3> p;
                        for (std::size_t corner = 0; corner < 3; ++corner)
                            p[corner] =
                                coverage
                                    ->positions[drawable.position_indices.at(drawable.triangles[i].corners[corner])];
                        const auto x =
                            static_cast<std::int64_t>((double{ p[0][0] } + p[2][0] + p[1][0]) * 0.33333334 * 0.1);
                        const auto y = (double{ p[2][1] } + p[1][1] + p[0][1]) * 0.33333334;
                        const auto z =
                            static_cast<std::int64_t>((double{ p[2][2] } + p[1][2] + p[0][2]) * 0.33333334 * 0.1);
                        if (y <= -0.1
                            || y >= 0.1
                            || x < 0
                            || z < 0
                            || std::cmp_greater_equal(x, coverage->stride)
                            || std::cmp_greater_equal(z, coverage->stride)
                            || static_cast<std::size_t>(x) * coverage->stride + static_cast<std::size_t>(z)
                                   >= coverage->mask.size()
                            || !coverage->mask[static_cast<std::size_t>(x) * coverage->stride
                                               + static_cast<std::size_t>(z)])
                            covered = false;
                    }
                }
                auto& flags = model.textures.at(*group.texture_slot).flags;
                if (covered && !(flags & 0x8000u))
                    flags &= static_cast<std::uint16_t>(~0x30u);
            }
    }
}
} //namespace simcoaster
