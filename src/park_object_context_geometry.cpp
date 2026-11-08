#include "simcoaster/park_object_context_geometry.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <bitset>

namespace simcoaster {
std::optional<WorldCellCoordinates> find_park_object_track_edit_endpoint(WorldRuntime& c, const ParkObjectRuntimeView& object) {
    using namespace construction_detail;
    if (object.is_track_ride_valid) {
        return {};
    }
    WorldCellCoordinates point { object.base.x >> 8, object.base.y >> 8 };
    auto direction = object_anchor(point, std::bit_cast<int>(object.angle));
    std::bitset<map_cell_count> visited;
    for (;;) {
        if (!bounds(point)) {
            throw std::out_of_range("Context track endpoint leaves the map");
        }
        const auto id = cell_id(point);
        if (visited.test(id - 1u)) {
            throw std::length_error("Cyclic context track endpoint trace");
        }
        visited.set(id - 1u);
        auto& cell = track(c, id).base;
        if (cell.common.type == 11 || cell.common.type == 16) {
            const auto corner = get_world_cell_corner_neighbor_mask(c, &cell);
            if (corner) {
                direction = opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &cell);
                cell.common.overlap_counter = 1;
            } else {
                cell.common.overlap_counter = get_world_cell_branch_neighbor_mask(c, &cell) ? 1 : 0;
            }
            if (!corner && !(get_world_cell_neighbor_mask(c, &cell) & direction)) {
                break;
            }
        }
        point = stepped(point, direction, 2);
    }
    return point;
}

bool adjust_park_object_context_track_camera(WorldRuntime& c, const ParkObjectRuntimeView& object, CellId fresh_cursor) {
    using namespace construction_detail;
    const auto endpoint = find_park_object_track_edit_endpoint(c, object);
    if (!endpoint || !fresh_cursor) {
        return false;
    }
    const auto point = *endpoint;
    const auto cursor = coordinate(fresh_cursor);
    auto& focus = c.view.camera.focus_position;
    //Unlike queue465A10,465D10 preserves the fractional camera focus.
    focus[0] = static_cast<float>(static_cast<double>(focus[0]) + static_cast<double>(point.x - cursor.x) * 10.);
    focus[2] = static_cast<float>(static_cast<double>(focus[2]) + static_cast<double>(point.y - cursor.y) * 10.);
    return true;
}

bool adjust_park_object_context_coaster_camera(WorldRuntime& c, const ParkObjectRuntimeView& object, CellId fresh_cursor) {
    if (!c.resources.meshes || !c.resources.meshes->coasters()) {
        throw std::logic_error("Coaster context camera has no runtime owner");
    }
    auto& world = *c.resources.meshes->coasters();
    const auto* ride = world.find(object.mesh_instance_id);
    if (!ride) {
        throw std::logic_error("Coaster context camera target has no runtime");
    }
    const auto flags = ride->train_state.placement_flags;
    if (((flags & 1u) && !(flags & 2u)) || !fresh_cursor) {
        return false;
    }
    std::optional<std::size_t> current;
    if (c.construction.editor && c.construction.editor->active_map() == ride->map_id) {
        current = c.construction.editor->active_node();
    }
    if (!current && (flags & 2u)) {
        for (std::size_t i = 0; i < ride->geometry.size(); ++i) {
            if ((ride->geometry[i].geometry_flags & 0x50000u) == 0x50000u) {
                current = i;
            }
        }
    }
    if (!current && !ride->geometry_state.empty()) {
        current = ride->geometry_state.size() - 1;
    }
    if (!current || *current >= ride->geometry_state.size()) {
        throw std::logic_error("Coaster context camera has no current geometry");
    }
    const auto& placement = world.placements.at(ride->geometry_state.at(*current).placement);
    const auto cursor = construction_detail::coordinate(fresh_cursor);
    auto& focus = c.view.camera.focus_position;
    focus[0] = static_cast<float>(static_cast<std::int32_t>(static_cast<double>(focus[0]) * .1 + placement.tile_x - cursor.x)) * 10.f + 5.f;
    focus[2] = static_cast<float>(static_cast<std::int32_t>(static_cast<double>(focus[2]) * .1 + placement.tile_y - cursor.y)) * 10.f + 5.f;
    return true;
}
} //namespace simcoaster
