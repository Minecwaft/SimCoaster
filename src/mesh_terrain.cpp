#include "simcoaster/mesh_terrain.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world_types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

[[noreturn]] void fail(std::size_t index = 0) {
    throw MeshError{ MeshFailure::configuration, index, {}, {} };
}

void require(bool valid, std::size_t index = 0) {
    if (!valid)
        fail(index);
}

void valid_grid(const DefinitionGrid& grid) {
    require(grid.width >= 0 && grid.width <= 40 && grid.height >= 0 && grid.height <= 40);
}

Point transformed(const MeshMatrix& matrix, const Point& point) {
    return mesh_transform::point(matrix, point);
}

//Native uses a full LU inverse. Checked pivoted elimination removes its
//unchecked singular-matrix behavior without assuming orthogonal transforms.
MeshMatrix inverse(const MeshMatrix& source) {
    return mesh_transform::inverse(source);
}

template <class Body>
void traverse(const MeshModel& model, Body&& body) {
    std::vector<std::size_t> pending;
    std::vector<bool> visited(model.nodes.size());
    if (model.root)
        pending.push_back(*model.root);
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        require(index < model.nodes.size() && !visited[index], index);
        visited[index] = true;
        if (!body(index))
            continue;
        std::vector<std::size_t> children;
        for (auto child = model.nodes[index].first_child; child; child = model.nodes[*child].next_sibling) {
            require(*child < model.nodes.size() && children.size() < model.nodes.size(), index);
            children.push_back(*child);
        }
        for (auto child = children.rbegin(); child != children.rend(); ++child)
            pending.push_back(*child);
    }
}

struct Coordinate {
    std::int64_t cell{}, line{};
    float fraction{};
    bool upper{}, snapped{};
};

Coordinate coordinate(float world, float origin, float scale, float epsilon) {
    require(std::isfinite(origin) && std::isfinite(scale) && scale > 0);
    const auto normalized = std::max(0.0f, static_cast<float>((static_cast<double>(world) - origin) / scale));
    require(std::isfinite(world)
            && std::isfinite(normalized)
            && static_cast<double>(normalized) < static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    double integral{};
    const auto fraction = static_cast<float>(std::modf(static_cast<double>(normalized), &integral));
    const auto cell = static_cast<std::int64_t>(integral);
    const bool upper = fraction >= 0.5f;
    return { cell,
             cell + (upper ? 1 : 0),
             fraction,
             upper,
             std::abs(fraction) < epsilon || std::abs(static_cast<double>(fraction) - 1) < epsilon };
}

float vertex(const TerrainGridView& grid, std::int64_t x, std::int64_t z) {
    require(x >= 0 && z >= 0 && x <= grid.width_cells && z <= grid.height_cells);
    const auto index =
        static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * (static_cast<std::size_t>(grid.width_cells) + 1);
    require(index < grid.height_samples.size(), index);
    const auto result = grid.height_samples[index];
    require(std::isfinite(result), index);
    return result;
}

double linear_height(const TerrainGridView& grid, const Coordinate& x, const Coordinate& z) {
    const auto blend = [](double first, auto&& second, float fraction) {
        return fraction == 0 ? first : first * (1.0 - fraction) + second() * fraction;
    };
    if (x.snapped)
        return blend(
            vertex(grid, x.line, z.cell),
            [&] {
                return vertex(grid, x.line, z.cell + 1);
            },
            z.fraction);
    return blend(
        vertex(grid, x.cell, z.line),
        [&] {
            return vertex(grid, x.cell + 1, z.line);
        },
        x.fraction);
}

double surface_height(const TerrainGridView& grid, Coordinate x, Coordinate z) {
    require(grid.width_cells && grid.height_cells);
    //At the outer edge use the limiting triangle, instead of native unchecked
    //reads into the next cell/row. Interior cell and diagonal rules are exact.
    if (x.cell == grid.width_cells && x.fraction == 0) {
        --x.cell;
        x.fraction = 1;
    }
    if (z.cell == grid.height_cells && z.fraction == 0) {
        --z.cell;
        z.fraction = 1;
    }
    require(x.cell >= 0 && z.cell >= 0 && x.cell < grid.width_cells && z.cell < grid.height_cells);
    const auto cell = static_cast<std::size_t>(x.cell) + static_cast<std::size_t>(z.cell) * grid.width_cells;
    require(cell < grid.cell_metadata.size(), cell);
    const auto h00 = vertex(grid, x.cell, z.cell), h10 = vertex(grid, x.cell + 1, z.cell),
               h01 = vertex(grid, x.cell, z.cell + 1), h11 = vertex(grid, x.cell + 1, z.cell + 1);
    const double fx = x.fraction, fz = z.fraction;
    //Unlike the ordinary terrain query, this path ignores two_triangles (800).
    if ((std::to_underlying(grid.cell_metadata[cell].flags) & 4u) != 0) {
        if (1 - fz > fx)
            return (1 - fx) * h00 + fx * h10 + (static_cast<double>(h01) - h00) * fz;
        return (1 - fx) * h01 + fx * h11 - (static_cast<double>(h11) - h10) * (1 - fz);
    }
    if (fx >= fz)
        return (1 - fx) * h00 + fx * h10 + (static_cast<double>(h11) - h10) * fz;
    return (1 - fx) * h01 + fx * h11 - (static_cast<double>(h01) - h00) * (1 - fz);
}
} //namespace

std::expected<MeshProceduralBase, MeshError> select_mesh_procedural_base(const MeshModel& model,
                                                                         const DefinitionGrid* shape,
                                                                         const TerrainGridView& terrain,
                                                                         std::int32_t width,
                                                                         std::int32_t height,
                                                                         bool allowed) {
    try {
        MeshProceduralBase result;
        result.allowed = allowed;
        result.vertex_indices.resize(model.nodes.size());
        result.secondary_vertex_indices.resize(model.nodes.size());
        if (!allowed)
            return result;
        require(width >= 0 && width <= 40 && height >= 0 && height <= 40);
        if (shape)
            valid_grid(*shape);
        const auto poses = evaluate_mesh_pose(model);
        if (!poses)
            throw poses.error();
        //Native initializes auxiliary clone metadata before building the base.
        //The decoded source stays immutable, so project those effective flags.
        std::vector<std::uint32_t> node_flags;
        for (const auto& node : model.nodes)
            node_flags.push_back(node.flags);
        for (const auto& resource : model.resources) {
            require(resource.node_index < node_flags.size(), resource.node_index);
            if (resource.flags & 8u)
                node_flags[resource.node_index] |= 0x40u;
            if (resource.flags & 0x10000u)
                node_flags[resource.node_index] |= 0x20000u;
        }
        std::array<std::uint8_t, 1600> edges{};
        const auto occupied = [&](std::int32_t x, std::int32_t z) {
            return shape
                && x >= 0
                && z >= 0
                && x < width
                && z < height
                && (shape->cells[static_cast<std::size_t>(z * 40 + x)].code & 15) != 0;
        };
        if (shape && !(model.flags & 0x80u))
            for (std::int32_t z = 0; z < height; ++z)
                for (std::int32_t x = 0; x < width; ++x) {
                    auto& mask = edges[static_cast<std::size_t>(z * 40 + x)];
                    const bool inside = occupied(x, z);
                    if (inside != occupied(x, z - 1))
                        mask |= 1;
                    if (inside != occupied(x - 1, z))
                        mask |= 2;
                    if (inside != occupied(x, z + 1))
                        mask |= 4;
                    if (inside != occupied(x + 1, z))
                        mask |= 8;
                }
        const auto select = [&](std::optional<std::size_t> filter, bool secondary) {
            traverse(model, [&](std::size_t index) {
                const auto& node = model.nodes[index];
                if (!node.drawable || (node_flags[index] & (0x200u | 0x20000u)) || (filter && *filter != index))
                    return true;
                auto& selected = secondary ? result.secondary_vertex_indices[index] : result.vertex_indices[index];
                const auto epsilon = secondary ? 0.1f : 0.01f;
                for (std::size_t i = 0; i < node.drawable->positions.size(); ++i) {
                    const auto point = transformed((*poses)[index], node.drawable->positions[i]);
                    auto x = coordinate(point[0], terrain.origin_x, terrain.cell_scale_x, epsilon),
                         z = coordinate(point[2], terrain.origin_y, terrain.cell_scale_y, epsilon);
                    if (x.line > terrain.width_cells || z.line > terrain.height_cells)
                        return false;
                    if (!(secondary ? (x.snapped || z.snapped) : (x.snapped && z.snapped))
                        || std::abs(point[1]) >= (secondary ? 100.0f : 0.01f))
                        continue;
                    if (!(model.flags & 0x100u)) {
                        if (x.cell >= width) {
                            x.cell = static_cast<std::int64_t>(width) - 1;
                            x.upper = true;
                        }
                        if (z.cell >= height) {
                            z.cell = static_cast<std::int64_t>(height) - 1;
                            z.upper = true;
                        }
                        if (x.cell < 0 || z.cell < 0)
                            continue;
                        const auto mask = edges[static_cast<std::size_t>(z.cell * 40 + x.cell)];
                        if (!((x.snapped && (mask & (x.upper ? 8 : 2))) || (z.snapped && (mask & (z.upper ? 4 : 1)))))
                            continue;
                    }
                    require(i <= std::numeric_limits<std::uint16_t>::max(), i);
                    selected.push_back(static_cast<std::uint16_t>(i));
                }
                return true;
            });
        };
        select(std::nullopt, false);
        std::size_t best_count = 0;
        //First traversal winner wins ties, even when model storage is not DFS
        //order.
        traverse(model, [&](std::size_t index) {
            const auto count = result.vertex_indices[index].size();
            result.selected_vertex_count += count;
            if (count > best_count) {
                best_count = count;
                result.best_node = index;
            }
            return true;
        });
        for (std::size_t index = 0; index < model.nodes.size(); ++index)
            if ((node_flags[index] & 0x40u) && result.best_node != index)
                select(index, true);
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

void apply_mesh_procedural_base_flags(MeshModel& model, const MeshProceduralBase& base) {
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        if ((index < base.vertex_indices.size() && !base.vertex_indices[index].empty())
            || (index < base.secondary_vertex_indices.size() && !base.secondary_vertex_indices[index].empty()))
            model.nodes[index].flags |= 0x1000u;
    }
    if (base.best_node && *base.best_node < model.nodes.size())
        model.nodes[*base.best_node].flags |= 0x4000u;
}

std::expected<void, MeshError> fit_mesh_to_terrain(const MeshModel& source,
                                                   MeshModel& runtime,
                                                   const MeshProceduralBase& base,
                                                   const TerrainGridView& terrain,
                                                   float reference,
                                                   MeshTerrainProcessingFlag processing) {
    try {
        require(&source != &runtime
                && source.nodes.size() == runtime.nodes.size()
                && base.vertex_indices.size() == runtime.nodes.size()
                && base.secondary_vertex_indices.size() == runtime.nodes.size());
        require(std::isfinite(reference));
        const auto flags = std::to_underlying(processing);
        if (base.allowed) {
            const auto poses = evaluate_mesh_pose(runtime);
            if (!poses)
                throw poses.error();
            const auto fit = [&](bool secondary, std::optional<std::size_t> filter) {
                traverse(runtime, [&](std::size_t index) {
                    auto& node = runtime.nodes[index];
                    const bool surface = (node.flags & 0x20000u) != 0;
                    if (!node.drawable
                        || (node.flags & 0x200u)
                        || (filter && *filter != index)
                        || (surface ? (flags & 4u) : ((flags & 2u) || !(node.flags & 0x1000u))))
                        return true;
                    const auto& original = source.nodes[index].drawable;
                    require(original && original->positions.size() == node.drawable->positions.size(), index);
                    const auto& indices = secondary ? base.secondary_vertex_indices[index] : base.vertex_indices[index];
                    if (!surface && indices.empty())
                        return true;
                    const auto undo = inverse((*poses)[index]);
                    std::size_t cursor = 0;
                    for (std::size_t i = 0; i < original->positions.size(); ++i) {
                        if (!surface && (cursor == indices.size() || indices[cursor] != i))
                            continue;
                        auto world = transformed((*poses)[index], original->positions[i]);
                        const auto epsilon = secondary ? 0.1f : 0.01f;
                        const auto x = coordinate(world[0], terrain.origin_x, terrain.cell_scale_x, epsilon),
                                   z = coordinate(world[2], terrain.origin_y, terrain.cell_scale_y, epsilon);
                        if (!surface) {
                            if (x.line > terrain.width_cells || z.line > terrain.height_cells)
                                return false;
                            //The original FNSTSW follows FLD/FABS, whose C0 is undefined.
                            //Use the explicit nearest-line test instead of x87 status debt.
                            if (x.snapped)
                                world[0] = static_cast<float>(static_cast<double>(x.line) * terrain.cell_scale_x
                                                              + terrain.origin_x);
                            if (z.snapped)
                                world[2] = static_cast<float>(static_cast<double>(z.line) * terrain.cell_scale_y
                                                              + terrain.origin_y);
                            world[1] = secondary
                                         ? static_cast<float>(linear_height(terrain, x, z) - reference + world[1])
                                         : vertex(terrain, x.line, z.line);
                        } else
                            world[1] = static_cast<float>(surface_height(terrain, x, z) - reference + world[1]);
                        const auto point = transformed(undo, world);
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            require(std::isfinite(point[axis]), i);
                            node.drawable->bounds.minimum[axis] =
                                std::min(node.drawable->bounds.minimum[axis], point[axis]);
                            node.drawable->bounds.maximum[axis] =
                                std::max(node.drawable->bounds.maximum[axis], point[axis]);
                        }
                        node.drawable->positions[i] = point;
                        if (!surface && ++cursor == indices.size())
                            break;
                    }
                    return true;
                });
            };
            fit(false, std::nullopt);
            for (std::size_t index = 0; index < base.secondary_vertex_indices.size(); ++index)
                if (!base.secondary_vertex_indices[index].empty())
                    fit(true, index);
        }
        if (runtime.root)
            runtime.nodes[*runtime.root].flags |= 0x10000u;
        runtime.flags |= 0x40000u;
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<std::array<std::int64_t, 2>, MeshError> mesh_footprint_sample(const std::array<std::int64_t, 2>& minimum,
                                                                            const std::array<std::int64_t, 2>& maximum,
                                                                            std::int32_t orientation,
                                                                            float x,
                                                                            float y) {
    try {
        const double x0 = static_cast<float>(minimum[0]), x1 = static_cast<float>(maximum[0]),
                     y0 = static_cast<float>(minimum[1]), y1 = static_cast<float>(maximum[1]);
        double grid_x{}, grid_y{};
        match (orientation) {
            case 0 => {
                grid_x = (1.0 - x) * x0 + x * x1;
                grid_y = (1.0 - y) * y0 + y * y1;
            }
            case 90 => {
                grid_x = (1.0 - y) * x0 + y * x1;
                grid_y = (1.0 - x) * y1 + x * y0;
            }
            case 180 => {
                grid_x = (1.0 - x) * x1 + x * x0;
                grid_y = (1.0 - y) * y1 + x * y0;
            }
            case 270 => {
                grid_x = (1.0 - y) * x1 + y * x0;
                grid_y = (1.0 - x) * y0 + x * y1;
            }
            case _ => fail();
        } std::array<std::int64_t, 2>
            result;
        const std::array<double, 2> coordinates{ grid_x + 0.1, grid_y + 0.1 };
        for (std::size_t axis = 0; axis < 2; ++axis) {
            const auto value = coordinates[axis];
            require(std::isfinite(value)
                    && value >= static_cast<double>(std::numeric_limits<std::int64_t>::min())
                    && value < static_cast<double>(std::numeric_limits<std::int64_t>::max()));
            result[axis] = static_cast<std::int64_t>(value);
        }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<float, MeshError> mesh_placement_reference_height(const TerrainGridView& terrain,
                                                                const std::array<std::int64_t, 2>& origin,
                                                                const DefinitionGrid* shape,
                                                                std::int32_t width,
                                                                std::int32_t height,
                                                                std::uint32_t flags,
                                                                std::int32_t orientation) {
    try {
        if (shape) {
            valid_grid(*shape);
            require(width <= 40 && height <= 40);
        }
        float minimum = 10000, maximum = -10000;
        if (width <= 0 || height <= 0)
            return 0;
        const bool straight = orientation == 0 || orientation == 180;
        const std::array<std::int64_t, 2> end{ origin[0] + (straight ? width : height),
                                               origin[1] + (straight ? height : width) };
        const auto step_x = static_cast<float>(1.0 / width), step_y = static_cast<float>(1.0 / height);
        float fx = 0;
        for (std::int32_t x = 0; x < width; ++x, fx += step_x) {
            float fy = 0;
            for (std::int32_t y = 0; y < height; ++y, fy += step_y) {
                if (shape && !(shape->cells[static_cast<std::size_t>(y * 40 + x)].code & 15) && !(flags & 0x10u))
                    continue;
                for (unsigned corner = 0; corner < 4; ++corner) {
                    const auto sample = mesh_footprint_sample(origin,
                                                              end,
                                                              orientation,
                                                              fx + ((corner & 1u) ? step_x : 0),
                                                              fy + ((corner & 2u) ? step_y : 0));
                    if (!sample)
                        throw sample.error();
                    const auto value = vertex(terrain, (*sample)[0], (*sample)[1]);
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            }
        }
        return static_cast<float>((static_cast<double>(minimum) + maximum) * 0.5);
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
