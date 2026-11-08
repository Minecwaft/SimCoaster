#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_surface.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
World& world(WorldRuntime& c) {
    return require_bound(c.simulation.current, "map surface World is not bound");
}
} //namespace

ParkObjectRuntimeView* find_world_park_object_at_map_cell(WorldRuntime& c, std::int32_t x, std::int32_t y) {
    if (!is_world_map_coordinate_in_bounds(x, y)) {
        return nullptr;
    }
    auto& w = world(c);
    const auto& gate = c.visitors.environment.FixedItemInfo;
    ThingId id {};
    if (gate.EntranceAPosX && gate.EntranceAPosY && x >= gate.EntranceAPosX - 1 && x <= gate.EntranceBPosX + 1 && y == gate.EntranceAPosY) {
        id = w.saved.park_gates;
    } else {
        const auto& cell = w.map_cells.at(static_cast<std::size_t>(x + y * 128));
        const auto type = cell.base.common.type;
        if (type != 4 && type != 9 && type != 10 && !cell.is_obstacle) {
            return nullptr;
        }
        const auto parent = cell.base.common.parent_id;
        if (!parent || parent > map_cell_count) {
            return nullptr;
        }
        id = find_first_park_object_id_in_map_cell(c, &w.map_cells.at(parent - 1));
    }
    if (!id) {
        return nullptr;
    }
    auto* payload = c.entities.at(id).payload;
    return payload && payload->kind == ThingKind::park_object ? reinterpret_cast<ParkObjectRuntimeView*>(payload) : nullptr;
}

namespace {
std::uint32_t track_at(WorldRuntime& c, int x, int y) {
    if (!is_world_map_coordinate_in_bounds(x, y)) {
        return 0;
    }
    auto& w = world(c);
    auto* cell = &w.track_cells.at(static_cast<std::size_t>(x + y * 128));
    if (world_cell_is_track_child(&cell->base)) {
        const auto parent = cell->base.common.parent_id;
        if (!parent || parent > map_cell_count) {
            return 0;
        }
        cell = &w.track_cells.at(parent - 1);
    }
    return cell->base.common.mesh_instance;
}
} //namespace

std::uint32_t world_mesh_slot_at_map_cell(WorldRuntime& c, std::int32_t x, std::int32_t y) {
    if (const auto slot = track_at(c, x, y)) {
        return slot;
    }
    if (const auto* object = find_world_park_object_at_map_cell(c, x, y)) {
        return object->mesh_instance_id;
    }
    return 0;
}

WorldMapCellSurface query_world_map_cell_surface(WorldRuntime& c, std::int32_t x, std::int32_t y, std::uint32_t selected) {
    const ParkObjectRuntimeView* object = nullptr;
    if (!selected) {
        selected = track_at(c, x, y);
        if (!selected) {
            object = find_world_park_object_at_map_cell(c, x, y);
            if (object) {
                selected = object->mesh_instance_id;
            }
        }
    }
    if (!selected || !c.resources.meshes) {
        return {};
    }
    const auto* instance = c.resources.meshes->instance(selected);
    const auto* definition = instance ? c.resources.meshes->definition(instance->template_id) : nullptr;
    if (!instance || !definition || !definition->height_map) {
        return {};
    }
    const auto& model = instance->playback.model;
    if (!model.root) {
        throw std::logic_error("surface mesh has no root");
    }
    const auto& root = model.nodes.at(*model.root).local_transform;
    float local_x = static_cast<float>(x) * 10 - root[12], local_z = static_cast<float>(y) * 10 - root[14];
    if (object) {
        const auto* catalog = find_object_definition_by_id(&c.resources.object_catalog, object->definition_id);
        if (!catalog || !catalog->balance) {
            throw std::logic_error("surface object definition");
        }
        const auto& info = catalog->balance->Info;
        if (info.IsObstacle == 1) {
            local_x = static_cast<float>(x - static_cast<int>(object->base.x >> 8) + info.MapOffsetX) * 10;
            local_z = static_cast<float>(y - static_cast<int>(object->base.y >> 8) + info.MapOffsetY) * 10;
        }
    }
    const auto original_x = local_x;
    match (instance->metadata.orientation_degrees) {
        case 90 => {
            local_x = -local_z - 10;
            local_z = original_x;
        }
        case 180 => {
            local_x = -local_x - 10;
            local_z = -local_z - 10;
        }
        case 270 => {
            local_x = local_z;
            local_z = -original_x - 10;
        }
        case _ => {
        }
    } const auto& grid = *definition->height_map;
    if (grid.cell_heights.size() != std::size_t { grid.width } * grid.height) {
        throw std::logic_error("surface mesh height grid storage");
    }
    //The original performs an unchecked byte read here. A ray's discovered
    //mesh (especially a linked coaster piece) need not cover this query cell.
    //Missing cells cannot be sampled, clamped to an edge, or allowed to alias
    //the preceding/following row. Preserve the mesh for exact triangle picking.
    const auto column = std::trunc(static_cast<double>(local_x) * .1), row = -std::trunc(static_cast<double>(local_z) * -.1);
    if (!std::isfinite(column) || !std::isfinite(row)) {
        throw std::domain_error("surface mesh height coordinates are not finite");
    }
    if (column < 0 || row < 0 || column >= grid.width || row >= grid.height) {
        return { selected, std::nullopt };
    }
    const auto index = static_cast<std::size_t>(column) + std::size_t { grid.width } * static_cast<std::size_t>(row);
    return { selected, grid.cell_heights[index] * static_cast<double>(.39215687f) + root[13] };
}

double sample_world_map_cell_surface(WorldRuntime& c, std::int32_t x, std::int32_t y, std::uint32_t* slot) {
    const auto sample = query_world_map_cell_surface(c, x, y, slot ? *slot : 0);
    if (slot) {
        *slot = sample.height ? sample.mesh_slot : 0;
    }
    if (sample.height) {
        return *sample.height;
    }
    return slot ? 0 : world_operations::sample_terrain_surface_height(c, static_cast<float>(x) * 10, static_cast<float>(y) * 10);
}

void set_mesh_root_pose_zxy(WorldRuntime& c, std::uint32_t slot, const std::array<float, 3>& p, const std::array<float, 3>& angles) {
    auto* instance = c.resources.meshes ? c.resources.meshes->instance(slot) : nullptr;
    if (!instance || !instance->playback.model.root) {
        throw std::logic_error("pose mesh has no root");
    }
    auto& t = instance->playback.model.nodes.at(*instance->playback.model.root).local_transform;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            t[row * 4 + column] = row == column ? 1.f : 0.f;
        }
    }
    t[12] = p[0];
    t[13] = p[1];
    t[14] = p[2];
    const auto sc = [](float a) {
        return std::pair { simulation_sine_at_rounded_index(a * simulation_sine_samples_per_radian), simulation_sine_at_rounded_index((a + 1.5707964f) * simulation_sine_samples_per_radian) };
    };
    const auto [sz, cz] = sc(angles[0]);
    const auto [sx, cx] = sc(angles[1]);
    const auto [sy, cy] = sc(angles[2]);
    for (std::size_t row = 0; row < 3; ++row) {
        auto& xx = t[row * 4];
        auto& yy = t[row * 4 + 1];
        auto& zz = t[row * 4 + 2];
        const auto old_x = xx, old_y = yy;
        xx = old_x * cz - old_y * sz;
        yy = old_y * cz + old_x * sz;
        const auto rotated_y = yy;
        yy = rotated_y * cx - zz * sx;
        zz = zz * cx + rotated_y * sx;
        const auto rotated_x = xx;
        xx = zz * sy + rotated_x * cy;
        zz = zz * cy - rotated_x * sy;
    }
}
} //namespace simcoaster
