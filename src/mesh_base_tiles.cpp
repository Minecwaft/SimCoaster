#include "simcoaster/mesh_terrain.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world_types.hpp"
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;
using Uv = std::array<float, 2>;

void require(bool value, std::size_t index = 0) {
    if (!value)
        throw MeshError{ MeshFailure::configuration, index, "procedural mesh base", {} };
}

bool special(std::int32_t code) {
    return code == 4 || code == 9 || code == 23 || code == 10;
}

std::vector<std::uint8_t> perimeter(const DefinitionGrid& shape) {
    require(shape.width >= 0 && shape.width <= 40 && shape.height >= 0 && shape.height <= 40);
    std::vector<std::uint8_t> mask(40 * 40);
    const auto selected = [&](int x, int y) {
        //66D0C0 includes equality at the logical upper bounds. Retain its reads
        //within the physical definition grid, without the old 16x16 output
        //overrun.
        return x >= 0
            && y >= 0
            && x <= shape.width
            && y <= shape.height
            && x < 40
            && y < 40
            && special(shape.cells[static_cast<std::size_t>(y * 40 + x)].code);
    };
    for (int y = 0; y < shape.height; ++y)
        for (int x = 0; x < shape.width; ++x)
            if (selected(x, y)
                && !(selected(x - 1, y) && selected(x + 1, y) && selected(x, y - 1) && selected(x, y + 1)))
                mask[static_cast<std::size_t>(x * 40 + y)] = 1;
    return mask;
}

struct Candidate {
    float alignment_x{}, alignment_z{};
    std::optional<std::size_t> group, triangle;
    Point direction_x{}, direction_z{};
    unsigned edge_x{}, edge_z{};
};

TerrainCellFlags orientation(const Candidate& candidate, const MeshDrawable& drawable) {
    const auto& triangle = drawable.triangles.at(*candidate.triangle);
    const auto x0 = candidate.direction_x[0] > 0 ? candidate.edge_x : (candidate.edge_x + 1) % 3;
    const auto x1 = candidate.direction_x[0] > 0 ? (candidate.edge_x + 1) % 3 : candidate.edge_x;
    const auto z0 = candidate.direction_z[2] > 0 ? candidate.edge_z : (candidate.edge_z + 1) % 3;
    const auto z1 = candidate.direction_z[2] > 0 ? (candidate.edge_z + 1) % 3 : candidate.edge_z;
    std::array<unsigned, 4> corners{};
    unsigned missing{};
    if (x0 == z0) {
        missing = 2;
        corners = { x0, z1, 0, x1 };
    } else if (z1 == x0) {
        missing = 3;
        corners = { z0, z1, x1, 0 };
    } else if (x1 == z1) {
        missing = 0;
        corners = { 0, x0, x1, z0 };
    } else {
        missing = 1;
        corners = { x0, 0, z1, x1 };
    }
    std::array<Uv, 4> uv{};
    for (unsigned i = 0; i < 4; ++i)
        if (i != missing)
            uv[i] = drawable.texture_coordinates.at(triangle.corners[corners[i]]);
    for (unsigned axis = 0; axis < 2; ++axis)
        uv[missing][axis] = uv[(missing + 1) % 4][axis] + uv[(missing + 3) % 4][axis] - uv[(missing + 2) % 4][axis];
    Uv minimum = uv[0];
    for (const auto& p : uv)
        for (unsigned axis = 0; axis < 2; ++axis)
            minimum[axis] = std::min(minimum[axis], p[axis]);
    unsigned closest = 0;
    double distance = std::numeric_limits<float>::max();
    for (unsigned i = 0; i < 4; ++i) {
        const double dx = double{ uv[i][0] } - minimum[0], dy = double{ uv[i][1] } - minimum[1], d = dx * dx + dy * dy;
        if (d < distance) {
            distance = d;
            closest = i;
        }
    }
    std::uint16_t flags = std::array<std::uint16_t, 4>{ 0x80, 0x88, 0x90, 0xA0 }[closest];
    if (std::abs(double{ uv[closest][0] } - uv[(closest + 1) % 4][0])
        > std::abs(double{ uv[closest][1] } - uv[(closest + 1) % 4][1]))
        flags |= 0x40;
    return static_cast<TerrainCellFlags>(flags);
}
} //namespace

std::expected<MeshBaseTilePlan, MeshError> build_mesh_base_tile_plan(MeshModel& model,
                                                                     const MeshProceduralBase& base,
                                                                     const DefinitionGrid* shape,
                                                                     const TerrainGridView& terrain,
                                                                     std::uint32_t width,
                                                                     std::uint32_t height) {
    try {
        MeshBaseTilePlan plan;
        if (!shape || !base.allowed || !(model.flags & 8u) || (!base.best_node && !(model.flags & 0x80u)))
            return plan;
        require(width <= 40 && height <= 40 && terrain.cell_scale_x > 0 && terrain.cell_scale_y > 0);
        const auto index = base.best_node ? base.best_node : model.root;
        require(index && *index < model.nodes.size());
        auto& node = model.nodes[*index];
        require(node.drawable.has_value());
        auto& drawable = *node.drawable;
        const auto pose = evaluate_mesh_pose(model);
        if (!pose)
            throw pose.error();
        std::vector<Point> positions;
        positions.reserve(drawable.positions.size());
        for (const auto& p : drawable.positions)
            positions.push_back(mesh_transform::point((*pose)[*index], p));
        const auto mask = perimeter(*shape);
        const bool force = (model.flags & 0x80u) != 0;
        std::vector<Candidate> candidates(40 * 40);
        for (std::size_t group_index = 0; group_index < drawable.groups.size(); ++group_index) {
            const auto& group = drawable.groups[group_index];
            require(group.texture_slot && *group.texture_slot < model.textures.size());
            for (std::size_t t = group.first_triangle; t < std::size_t{ group.first_triangle } + group.triangle_count;
                 ++t) {
                const auto& triangle = drawable.triangles.at(t);
                std::array<Point, 3> p;
                for (unsigned i = 0; i < 3; ++i)
                    p[i] = positions.at(drawable.position_indices.at(triangle.corners[i]));
                const auto x = static_cast<std::int64_t>(
                    (double{ p[0][0] } + p[1][0] + p[2][0]) * 0.33333334 / terrain.cell_scale_x);
                const auto z = static_cast<std::int64_t>(
                    (double{ p[0][2] } + p[1][2] + p[2][2]) * 0.33333334 / terrain.cell_scale_y);
                if (x < 0 || z < 0 || x >= 40 || z >= 40)
                    continue;
                auto& material = model.textures[*group.texture_slot];
                if (force || mask[static_cast<std::size_t>(x * 40 + z)])
                    material.flags |= 0x800;
                else
                    material.flags &= static_cast<std::uint16_t>(~0x800u);
                Candidate found;
                for (unsigned i = 0; i < 3; ++i) {
                    Point direction;
                    double length_squared = 0;
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        direction[axis] = p[(i + 1) % 3][axis] - p[i][axis];
                        length_squared += double{ direction[axis] } * direction[axis];
                    }
                    const float length = static_cast<float>(std::sqrt(length_squared));
                    if (!(length > 0))
                        continue;
                    for (auto& value : direction)
                        value /= length;
                    const auto ax = std::abs(direction[0]), az = std::abs(direction[2]);
                    if (ax > found.alignment_x) {
                        found.alignment_x = ax;
                        found.direction_x = direction;
                        found.edge_x = i;
                    }
                    if (az > found.alignment_z) {
                        found.alignment_z = az;
                        found.direction_z = direction;
                        found.edge_z = i;
                    }
                }
                auto& current = candidates[static_cast<std::size_t>(z * 40 + x)];
                if (found.alignment_x > 0.89999998
                    && found.alignment_z > 0.89999998
                    && (found.alignment_x > current.alignment_x || found.alignment_z > current.alignment_z)) {
                    found.group = group_index;
                    found.triangle = t;
                    current = found;
                }
            }
        }
        const MeshUvBaseCoverage coverage{ mask, positions, 40 };
        normalize_mesh_uvs(model, index, &coverage);
        plan.width = width;
        plan.height = height;
        plan.cells.resize(std::size_t{ width } * height, { TerrainCellFlags::source_grid_excluded, 10000 });
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x) {
                if (!force && (!mask[x * 40 + y] || !shape->cells[y * 40 + x].code))
                    continue;
                const auto& selected = candidates[y * 40 + x];
                if (!selected.group)
                    continue;
                const auto slot = *drawable.groups[*selected.group].texture_slot;
                require(slot <= std::numeric_limits<std::uint16_t>::max());
                plan.cells[y * width + x] = { orientation(selected, drawable), static_cast<std::uint16_t>(slot) };
            }
        return plan;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<std::vector<MeshBaseTilePlacement>, MeshError> place_mesh_base_tiles(const MeshBaseTilePlan& plan,
                                                                                   const MeshModel& model,
                                                                                   const TerrainGridView& terrain,
                                                                                   std::int32_t angle) {
    try {
        std::vector<MeshBaseTilePlacement> placed;
        if (plan.cells.empty())
            return placed;
        require(plan.cells.size() == std::size_t{ plan.width } * plan.height && model.root.has_value());
        require(angle == 0 || angle == 90 || angle == 180 || angle == 270);
        const auto& root = model.nodes.at(*model.root).local_transform;
        require(
            std::isfinite(root[12]) && std::isfinite(root[14]) && terrain.cell_scale_x > 0 && terrain.cell_scale_y > 0);
        auto rx = static_cast<std::int64_t>(double{ root[12] } / terrain.cell_scale_x),
             ry = static_cast<std::int64_t>(double{ root[14] } / terrain.cell_scale_y);
        if (angle == 90 || angle == 180)
            --ry;
        if (angle == 180 || angle == 270)
            --rx;
        constexpr std::array<std::uint16_t, 4> turns{ 0, 8, 0x10, 0x20 };
        for (std::size_t y = 0; y < plan.height; ++y)
            for (std::size_t x = 0; x < plan.width; ++x) {
                const auto cell = plan.cells[y * plan.width + x];
                const auto source = std::to_underlying(cell.flags);
                if (!(source & 0x80u))
                    continue;
                auto tx = rx, ty = ry;
                const auto sx = static_cast<std::int64_t>(x), sy = static_cast<std::int64_t>(y);
                match (angle) {
                    case 0 => {
                        tx += sx;
                        ty += sy;
                    }
                    case 90 => {
                        tx += sy;
                        ty -= sx;
                    }
                    case 180 => {
                        tx -= sx;
                        ty -= sy;
                    }
                    case 270 => {
                        tx -= sy;
                        ty += sx;
                    }
                    case _ => {}
                }
                require(tx >= 0
                        && ty >= 0
                        && std::cmp_less(tx, terrain.width_cells)
                        && std::cmp_less(ty, terrain.height_cells));
                const auto prior = terrain.cell_metadata.at(static_cast<std::size_t>(ty) * terrain.width_cells
                                                            + static_cast<std::size_t>(tx));
                const auto found = std::ranges::find(turns, source & 0x38u);
                require(found != turns.end());
                const auto rotation =
                    (static_cast<std::size_t>(found - turns.begin()) + static_cast<unsigned>(angle / 90)) % 4;
                const auto flags = static_cast<TerrainCellFlags>(
                    (source & 0x40u) | turns[rotation] | (std::to_underlying(prior.flags) & 6u) | 0x281u);
                placed.push_back({ static_cast<std::uint32_t>(tx),
                                   static_cast<std::uint32_t>(ty),
                                   { flags, cell.resource_slot_index } });
            }
        std::ranges::stable_sort(placed, [](const auto& a, const auto& b) {
            return std::pair{ a.row, a.column } < std::pair{ b.row, b.column };
        });
        return placed;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
