#include "simcoaster/mesh_surface.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

Point rotated(Point value, std::int32_t angle) {
    const auto x = value[0], z = value[2];
    match (angle) {
        case 90 => {
            value[0] = z;
            value[2] = -x;
        }
        case 180 => {
            value[0] = -x;
            value[2] = -z;
        }
        case 270 => {
            value[0] = -z;
            value[2] = x;
        }
        case _ => {
        }
    } return value;
}

std::optional<float> triangle_height(const MeshSurfaceTriangle& triangle, float x, float z) {
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& a = triangle.vertices[i];
        const auto& b = triangle.vertices[(i + 1) % 3];
        if ((static_cast<double>(b[2]) - a[2]) * (static_cast<double>(x) - a[0]) - (static_cast<double>(b[0]) - a[0]) * (static_cast<double>(z) - a[2]) < 0) {
            return std::nullopt;
        }
    }
    const auto& a = triangle.vertices[0];
    const auto& n = triangle.normal;
    return static_cast<float>(-((static_cast<double>(n[0]) * x - (static_cast<double>(a[2]) * n[2] + static_cast<double>(a[0]) * n[0] + static_cast<double>(a[1]) * n[1]) + static_cast<double>(n[2]) * z) / n[1]));
}
} //namespace

float MeshSurfaceQuery::sample(float x, float z) const {
    auto best = -100.0f;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        auto candidate = -100.0f;
        for (const auto& triangle : batch.triangles) {
            if (const auto height = triangle_height(triangle, x, z)) {
                candidate = std::max(candidate, *height);
                if (batch.first_hit) {
                    break;
                }
            }
        }
        if (i == 0 && candidate == -100.0f) {
            candidate = fallback;
        }
        if (candidate > best) {
            best = candidate;
            if (batch.return_on_improvement) {
                return best + offset;
            }
        }
    }
    return best + offset;
}

std::expected<MeshSurfaceQuery, MeshError> prepare_mesh_surface_query(const MeshModel& model, std::optional<std::size_t> base, std::int32_t orientation, bool auxiliary) {
    try {
        const auto require = [](bool valid, std::size_t i) {
            if (!valid) {
                throw MeshError { MeshFailure::index, i, {}, {} };
            }
        };
        MeshSurfaceQuery query;
        query.offset = std::isnan(model.surface_height_offset) ? 0 : model.surface_height_offset;
        query.terrain = !auxiliary && ((model.flags & 0x80u) || model.surface_height_offset != 0);
        if (query.terrain) {
            return query;
        }
        const auto root = model.root ? model.nodes.at(*model.root).local_transform : mesh_identity_matrix;
        const auto append = [&](std::size_t index, bool first, bool early) {
            require(index < model.nodes.size(), index);
            const auto& node = model.nodes[index];
            if (!node.drawable) {
                return;
            }
            Point translation {};
            std::size_t depth = 0;
            for (auto current = std::optional { index }; current; current = model.nodes[*current].parent) {
                require(*current < model.nodes.size() && depth++ < model.nodes.size(), index);
                if (model.nodes[*current].parent) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        translation[axis] += model.nodes[*current].local_transform[12 + axis];
                    }
                }
            }
            const auto& drawable = *node.drawable;
            MeshSurfaceBatch batch;
            batch.first_hit = first;
            batch.return_on_improvement = early;
            for (const auto& group : drawable.groups) {
                for (std::size_t i = group.first_triangle; i < static_cast<std::size_t>(group.first_triangle) + group.triangle_count; ++i) {
                    require(i < drawable.triangles.size(), i);
                    const auto& source = drawable.triangles[i];
                    if (!(source.normal_index_and_flags & 0x8000u)) {
                        continue;
                    }
                    MeshSurfaceTriangle triangle;
                    const auto normal = source.normal_index_and_flags & 0x7FFFu;
                    require(normal < drawable.normals.size(), normal);
                    triangle.normal = rotated(drawable.normals[normal], orientation);
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto position = drawable.position_indices.at(source.corners[corner]);
                        auto point = drawable.positions.at(position);
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            point[axis] += translation[axis];
                        }
                        point = rotated(point, orientation);
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            point[axis] += root[12 + axis];
                        }
                        triangle.vertices[corner] = point;
                    }
                    batch.triangles.push_back(triangle);
                }
            }
            query.batches.push_back(std::move(batch));
        };
        if (auxiliary) {
            for (const auto& resource : model.resources) {
                if (resource.flags & 2u) {
                    append(resource.node_index, (resource.flags & 4u) != 0, (resource.flags & 4u) != 0);
                }
            }
        } else if (base) {
            query.fallback = root[13];
            append(*base, true, false);
        }
        return query;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
