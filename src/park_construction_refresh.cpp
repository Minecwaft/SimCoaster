#include "simcoaster/require_bound.hpp"
#include "simcoaster/park_construction_refresh.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace simcoaster {
namespace {
void validate(const TerrainGridView& grid) {
    if (!grid.width_cells
        || !grid.height_cells
        || grid.width_cells > 128
        || grid.height_cells > 128
        || grid.cell_metadata.size() != static_cast<std::size_t>(grid.width_cells) * grid.height_cells
        || grid.height_samples.size()
               != (static_cast<std::size_t>(grid.width_cells) + 1) * (static_cast<std::size_t>(grid.height_cells) + 1))
        throw std::logic_error("invalid construction terrain grid");
}

const TerrainGridView& terrain(ConstructionWorld& c) {
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid)
        throw std::logic_error("construction terrain owner is not bound");
    return *c.construction.active_terrain_model_data->terrain_grid;
}

WorldMeshRuntime& meshes(ConstructionWorld& c) {
    return require_bound(c.resources.meshes, "construction mesh owner is not bound");
}

MeshModel& model(ConstructionWorld& c, std::uint32_t slot) {
    auto* instance = meshes(c).instance(slot);
    if (!instance)
        throw std::out_of_range("construction mesh instance");
    return instance->playback.model;
}

std::int32_t tile(float value) {
    if (!std::isfinite(value))
        throw std::domain_error("nonfinite construction mesh coordinate");
    return fixed::truncate_integer(static_cast<double>(value) * 0.1);
}

float snap_height(const TerrainGridView& grid, float x, float z) {
    //4A8120 bypasses the excluded-cell sentinel and honors grid origin/scale.
    if (x < 0
        || z < 0
        || x >= static_cast<double>(grid.width_cells) * 10
        || z >= static_cast<double>(grid.height_cells) * 10)
        return 0;
    const auto nx = static_cast<float>((static_cast<double>(x) - grid.origin_x) / grid.cell_scale_x),
               nz = static_cast<float>((static_cast<double>(z) - grid.origin_y) / grid.cell_scale_y);
    if (nx < 0 || nz < 0 || static_cast<double>(nx) >= grid.width_cells || static_cast<double>(nz) >= grid.height_cells)
        return 0;
    double ix, iz;
    const auto fx = static_cast<float>(std::modf(nx, &ix));
    const auto fz = std::modf(nz, &iz);
    const auto column = static_cast<std::size_t>(ix), row = static_cast<std::size_t>(iz),
               stride = static_cast<std::size_t>(grid.width_cells) + 1, base = row * stride + column;
    const double a = grid.height_samples[base], b = grid.height_samples[base + 1],
                 d = grid.height_samples[base + stride], e = grid.height_samples[base + stride + 1];
    const bool sum = (std::to_underlying(grid.cell_metadata[row * grid.width_cells + column].flags) & 4u) != 0;
    const auto result =
        sum ? (1 - fz > fx ? (1 - fx) * a + fx * b + (d - a) * fz : (1 - fx) * d + fx * e - (e - b) * (1 - fz))
            : (fx < fz ? (1 - fx) * d + fx * e - (d - a) * (1 - fz) : (1 - fx) * a + fx * b + (e - b) * fz);
    return static_cast<float>(result);
}
} //namespace

std::array<float, 3> construction_normal_intensity(std::array<float, 3> normal, float target) {
    const auto length = std::sqrt(static_cast<double>(normal[0]) * normal[0]
                                  + static_cast<double>(normal[1]) * normal[1]
                                  + static_cast<double>(normal[2]) * normal[2]);
    //Degenerate exported normals use the renderer's existing downward fallback;
    //valid vectors retain the original magnitude-target operation.
    if (!(length > 0) || !std::isfinite(length))
        return { 0, -target, 0 };
    const auto scale = target / length;
    for (auto& value : normal)
        value = static_cast<float>(scale * value);
    return normal;
}

namespace {
constexpr double terrain_coordinate_scale = static_cast<double>(0.1f);

std::int32_t nearest_even_integer(float value) {
    const auto lower = std::floor(static_cast<double>(value));
    const auto fraction = static_cast<double>(value) - lower;
    return fixed::truncate_integer(lower
                                   + ((fraction > .5 || (fraction == .5 && std::fmod(lower, 2.) != 0)) ? 1. : 0.));
}

std::optional<std::array<int, 2>>
shadow_cell(const TerrainGridView& grid, float x, float z, TerrainShadowSampling sampling, bool precheck = false) {
    const bool clamped = sampling == TerrainShadowSampling::object_clamped;
    int column, row;
    if (clamped) {
        column = std::clamp(fixed::truncate_integer(x * terrain_coordinate_scale),
                            0,
                            static_cast<int>(grid.width_cells) - 1);
        row = std::clamp(fixed::truncate_integer(z * terrain_coordinate_scale),
                         0,
                         static_cast<int>(grid.height_cells) - 1);
    } else {
        const auto sx = x * terrain_coordinate_scale - .5;
        const auto sz = z * terrain_coordinate_scale - .5;
        //4A5755/5D call truncating __ftol only in the node precheck. The actual
        //attribute loops store float then FISTP with default nearest-even mode.
        //Express both conversions explicitly; no process FP-mode dependency.
        column = precheck ? fixed::truncate_integer(sx) : nearest_even_integer(static_cast<float>(sx));
        row = precheck ? fixed::truncate_integer(sz) : nearest_even_integer(static_cast<float>(sz));
        if (column < 0
            || row < 0
            || static_cast<unsigned>(column) >= grid.width_cells
            || static_cast<unsigned>(row) >= grid.height_cells)
            return std::nullopt;
    }
    return std::array{ column, row };
}

std::optional<double> terrain_shadow_gain(const TerrainGridView& grid,
                                          float x,
                                          float z,
                                          TerrainShadowSampling sampling,
                                          float shadowed_gain) {
    const auto cell = shadow_cell(grid, x, z, sampling);
    if (!cell)
        return std::nullopt;
    const auto [column, row] = *cell;
    const auto flags = std::to_underlying(
        grid.cell_metadata[static_cast<std::size_t>(row) * grid.width_cells + static_cast<unsigned>(column)].flags);
    const auto fx = (static_cast<double>(x) - 10 * column) * terrain_coordinate_scale,
               fz = (static_cast<double>(z) - 10 * row) * terrain_coordinate_scale;
    const auto gain = [&](std::uint16_t flag) {
        return flags & flag ? static_cast<double>(shadowed_gain) : 1.;
    };
    const auto left = (gain(0x1000u) - gain(0x4000u)) * fz + gain(0x4000u),
               right = (gain(0x2000u) - gain(0x8000u)) * fz + gain(0x8000u);
    return (right - left) * fx + left;
}
} //namespace

std::optional<float> construction_terrain_shadow_gain(const TerrainGridView& grid,
                                                      float x,
                                                      float z,
                                                      TerrainShadowSampling sampling,
                                                      float shadowed_gain) {
    validate(grid);
    const auto gain = terrain_shadow_gain(grid, x, z, sampling, shadowed_gain);
    return gain ? std::optional{ static_cast<float>(*gain) } : std::nullopt;
}

void rebuild_terrain_shading_flags(TerrainGridData& grid,
                                   const World& world,
                                   std::uint32_t map_width,
                                   std::uint32_t map_height) {
    validate(grid.view());
    if (!map_width || !map_height || map_width > 128 || map_height > 128)
        throw std::logic_error("invalid shading World extent");
    constexpr std::array<std::tuple<int, int, std::uint16_t>, 9> samples{ { { -1, 1, 0x1000 },
                                                                            { 1, 1, 0x2000 },
                                                                            { -1, -1, 0x4000 },
                                                                            { 1, -1, 0x8000 },
                                                                            { 0, -1, 0xC000 },
                                                                            { 0, 1, 0x3000 },
                                                                            { 1, 0, 0xA000 },
                                                                            { -1, 0, 0x5000 },
                                                                            { 0, 0, 0xF400 } } };
    for (std::uint32_t y = 0; y < grid.height_cells; ++y)
        for (std::uint32_t x = 0; x < grid.width_cells; ++x) {
            auto& metadata = grid.cell_metadata[static_cast<std::size_t>(y) * grid.width_cells + x];
            auto flags = static_cast<std::uint16_t>(std::to_underlying(metadata.flags) & 0x0BFFu);
            for (const auto& [dx, dy, mask] : samples) {
                const auto sx = static_cast<unsigned>(
                               std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(map_width) - 1)),
                           sy = static_cast<unsigned>(
                               std::clamp(static_cast<int>(y) + dy, 0, static_cast<int>(map_height) - 1));
                const auto source = world.map_cells.at(static_cast<std::size_t>(sy) * 128 + sx).base.common.flags;
                if ((source & 64u) && !(source & 128u))
                    flags = static_cast<std::uint16_t>(flags | mask);
            }
            metadata.flags = static_cast<TerrainCellFlags>(flags);
        }
}

void apply_construction_mesh_darkening(MeshModel& model,
                                       const TerrainGridView& grid,
                                       TerrainShadowSampling sampling,
                                       bool packed_grayscale) {
    validate(grid);
    const auto pose = evaluate_mesh_pose(model);
    if (!pose)
        throw pose.error();
    if (model.drawable_node_count > model.nodes.size())
        throw std::logic_error("construction drawable node count");
    for (std::size_t index = 0; index < model.drawable_node_count; ++index)
        if (auto& shape = model.nodes[index].drawable) {
            //Native4A5680 skips this node's own attributes, still visiting its
            //children. The separate4CCA50 object pass has no such flag gate.
            if (sampling == TerrainShadowSampling::terrain_centered && (model.nodes[index].flags & 0x200u))
                continue;
            auto& geometry = *shape;
            const auto count = geometry.position_indices.size();
            const bool colored = packed_grayscale || !geometry.packed_vertex_colors.empty();
            if (colored)
                geometry.world_shadow_colors.assign(count, 0xFFFFFFFFu);
            else {
                geometry.world_shadow_colors.clear();
                if (geometry.normals.size() < count)
                    throw std::logic_error("construction mesh lacks corner normals");
            }
            std::vector<std::array<float, 3>> positions;
            positions.reserve(geometry.positions.size());
            for (const auto& point : geometry.positions)
                positions.push_back(mesh_transform::point(pose->at(index), point));
            if (colored) {
                bool any_dark = false;
                for (const auto vertex : geometry.position_indices) {
                    const auto& p = positions.at(vertex);
                    const auto cell = shadow_cell(grid, p[0], p[2], sampling, true);
                    if (cell
                        && (std::to_underlying(
                                grid.cell_metadata[static_cast<std::size_t>((*cell)[1]) * grid.width_cells + (*cell)[0]]
                                    .flags)
                            & 0xF000u)) {
                        any_dark = true;
                        break;
                    }
                }
                if (!any_dark)
                    continue; //Native no-dark branch overwrites every packed value white.
            }
            for (std::size_t corner = 0; corner < count; ++corner) {
                const auto& p = positions.at(geometry.position_indices[corner]);
                const auto gain = terrain_shadow_gain(grid, p[0], p[2], sampling, colored ? 0.5f : 0.25f);
                if (!gain)
                    continue;
                if (colored) {
                    //Native4A5680/4CCA50 overwrite packed attributes with grayscale,
                    //using half intensity and nearest-even; authored colors are not a
                    //multiplier. Keep the packed operation, including extrapolation.
                    const auto level =
                        std::bit_cast<std::uint32_t>(nearest_even_integer(static_cast<float>(*gain * 255.0)));
                    geometry.world_shadow_colors[corner] = level | ((level | ((level | 0xFFFFFF00u) << 8)) << 8);
                } else
                    geometry.normals[corner] =
                        construction_normal_intensity(geometry.normals[corner], static_cast<float>(*gain));
            }
        }
}

void snap_construction_mesh_to_terrain(MeshModel& model, const TerrainGridView& grid) {
    validate(grid);
    const auto pose = evaluate_mesh_pose(model);
    if (!pose)
        throw pose.error();
    if (model.drawable_node_count > model.nodes.size())
        throw std::logic_error("construction drawable node count");
    for (std::size_t index = 0; index < model.drawable_node_count; ++index)
        if (auto& shape = model.nodes[index].drawable) {
            auto& geometry = *shape;
            const auto& transform = pose->at(index);
            const auto inverse = mesh_transform::inverse(transform);
            for (auto& point : geometry.positions) {
                const auto world = mesh_transform::point(transform, point);
                const auto x = std::clamp(tile(world[0]), 0, static_cast<int>(grid.width_cells) - 1),
                           z = std::clamp(tile(world[2]), 0, static_cast<int>(grid.height_cells) - 1);
                for (const auto offset :
                     std::array<std::array<int, 2>, 4>{ { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } }) {
                    const std::array<float, 3> corner{ static_cast<float>((x + offset[0]) * 10),
                                                       snap_height(grid,
                                                                   static_cast<float>((x + offset[0]) * 10),
                                                                   static_cast<float>((z + offset[1]) * 10)),
                                                       static_cast<float>((z + offset[1]) * 10) };
                    const auto current = mesh_transform::point(transform, point);
                    if (std::abs(corner[0] - current[0]) < 0.3f
                        && std::abs(corner[1] - current[1]) < 0.3f
                        && std::abs(corner[2] - current[2]) < 0.3f) {
                        point = mesh_transform::point(inverse, corner);
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            geometry.bounds.minimum[axis] = std::min(geometry.bounds.minimum[axis], point[axis]);
                            geometry.bounds.maximum[axis] = std::max(geometry.bounds.maximum[axis], point[axis]);
                        }
                    }
                }
            }
        }
    if (model.root)
        model.nodes.at(*model.root).flags |= 0x10000u;
    model.flags |= 0x40000u;
}

void rebuild_construction_terrain_shading(ConstructionWorld& c, TerrainGridData& grid) {
    auto* world = c.referenced_world();
    if (!world)
        throw std::logic_error("shading primary World is not bound");
    rebuild_terrain_shading_flags(grid, *world, c.weather.map_heightfield_width, c.weather.map_heightfield_height);
    meshes(c).invalidate_mesh_renderer_cache(c);
}

std::uint32_t normalize_construction_special_ride_meshes(ConstructionWorld& c) {
    if (!c.simulation.current)
        throw std::logic_error("special owner World is not bound");
    const auto count = std::bit_cast<std::int32_t>(c.attractions.special_ride_group_count);
    for (std::int32_t index = 0; index < count; ++index) {
        const auto id = c.attractions.special_ride_groups.at(static_cast<std::size_t>(index)).thing_id;
        if (!id)
            continue;
        auto* header = c.entities.at(id).payload;
        if (!header || header->kind != ThingKind::park_object)
            throw std::logic_error("special registry owner is missing");
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
        const auto cell = static_cast<std::size_t>(object.base.y >> 8) * 128u + (object.base.x >> 8);
        if (!(c.simulation.current->map_cells.at(cell).base.common.flags & 64u))
            world_operations::set_park_object_mesh_normal_intensity(c, object.mesh_instance_id, 1, 1, 0, 0);
    }
    return c.attractions.special_ride_group_count;
}

void refresh_construction_object_meshes(ConstructionWorld& c) {
    const auto& grid = terrain(c);
    std::size_t visited = 0;
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next) {
        if (++visited > c.entities.size())
            throw std::length_error("cyclic active construction objects");
        auto* header = entry->payload;
        if (!header)
            throw std::logic_error("active construction payload missing");
        if (header->kind != ThingKind::park_object)
            continue;
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
        if (!object.mesh_instance_id)
            continue;
        const auto& info = get_park_object_balance(c, object).Info;
        if (info.IsSpecial)
            apply_construction_mesh_darkening(model(c, object.mesh_instance_id), grid);
        else if (info.IsObstacle || info.IsSupportingObstacle) {
            auto& mesh = model(c, object.mesh_instance_id);
            snap_construction_mesh_to_terrain(mesh, grid);
            apply_construction_mesh_darkening(mesh, grid);
        }
    }
    (void)normalize_construction_special_ride_meshes(c);
    meshes(c).invalidate_mesh_renderer_cache(c);
}

void world_operations::builtin::refresh_active_park_object_meshes(WorldRuntime& c) {
    refresh_construction_object_meshes(c);
}

std::uint32_t world_operations::builtin::normalize_special_ride_owner_vectors(WorldRuntime& c) {
    return normalize_construction_special_ride_meshes(c);
}
} //namespace simcoaster
