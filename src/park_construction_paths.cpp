#include "park_construction_internal.hpp"
#include <bitset>

namespace simcoaster {
namespace {
using namespace construction_detail;

void append_point(ParkConstructionState& s, WorldCellCoordinates p) {
    if (s.path_points.size() <= 1024)
        s.path_points.push_back(p);
}

bool queue_cell(const CellBase& cell) {
    return cell.common.type == 3 || cell.common.type == 9;
}

bool ordinary_track(const CellBase& cell) {
    return cell.common.type == 11 || cell.common.type == 16;
}
} //namespace

//Native0x67F340: endpoints are read-only and ties choose the Y axis.
bool ParkConstructionRuntime::command_line(ConstructionWorld& c,
                                           std::uint32_t code,
                                           WorldCellCoordinates start,
                                           WorldCellCoordinates end) {
    using namespace construction_detail;
    c.construction.cell_line_first_dispatch_pending = 1;
    c.construction.line_endpoint_dispatch = 0;
    const bool horizontal =
        std::abs(static_cast<std::int64_t>(end.x) - start.x) > std::abs(static_cast<std::int64_t>(end.y) - start.y);
    const int step = horizontal ? (start.x <= end.x ? 1 : -1) : (start.y <= end.y ? 1 : -1);
    auto p = start;
    const auto done = [&] {
        return horizontal ? p.x == end.x : p.y == end.y;
    };
    for (;;) {
        const bool endpoint = done();
        if (endpoint)
            c.construction.line_endpoint_dispatch = 1;
        if (bounds(p)) {
            const auto result = command(c, map(c, cell_id(p)).base, code, horizontal ? step : 0, horizontal ? 0 : step);
            c.construction.cell_line_first_dispatch_pending = 0;
            if (endpoint)
                c.construction.line_endpoint_dispatch = 0;
            if (!result)
                return false;
        }
        if (endpoint)
            break;
        if (horizontal)
            p.x = fixed::add(p.x, step);
        else
            p.y = fixed::add(p.y, step);
    }
    return true;
}

//trace_park_object_queue_path_from_entry stores corners, not every cell. Its
//overlap writes matter when the reverse path is removed, including shared
//corners.
std::uint8_t ParkConstructionRuntime::trace_queue(ConstructionWorld& c, ParkObjectRuntimeView& owner) {
    using namespace construction_detail;
    state.path_points.clear();
    auto p = coordinate(owner.entry_cell);
    const auto origin = p;
    auto& entry = map(c, owner.entry_cell).base;
    auto direction = get_world_cell_neighbor_mask(c, &entry);
    const bool cardinal = direction == 1 || direction == 4 || direction == 16 || direction == 64;
    if (cardinal) {
        auto* next = resolve_world_cell_from_selector_flag_byte(c, &entry, direction);
        if (next) {
            const auto adjacent = coordinate(next->base.index);
            if (queue_cell(next->base) && next->base.common.parent_id != entry.common.parent_id) {
                append_point(state, adjacent);
                c.construction.path_start_x = adjacent.x;
                c.construction.path_start_y = adjacent.y;
                return direction;
            }
            append_point(state, adjacent);
        }
    }
    if (!direction || !bounds(origin)) {
        std::uint8_t rotation = owner.angle == 90 ? 4 : owner.angle == 180 ? 16 : owner.angle == 270 ? 64 : 1;
        direction = advance_world_cell_direction(get_world_cell_direction(&entry), rotation);
        if (direction == 1 || direction == 4 || direction == 16 || direction == 64)
            p = stepped(origin, direction);
        append_point(state, p);
        c.construction.path_start_x = p.x;
        c.construction.path_start_y = p.y;
        return direction;
    }
    std::bitset<map_cell_count> visited;
    while (bounds(p)) {
        auto& current = map(c, cell_id(p)).base;
        if (visited.test(current.index - 1u))
            throw std::length_error("cyclic queue construction path");
        visited.set(current.index - 1u);
        bool stop = false;
        if (queue_cell(current)) {
            if (map_corner(c, current)) {
                direction = opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &current);
                append_point(state, p);
                current.common.overlap_counter = 1;
            } else
                current.common.overlap_counter = 0;
        } else
            stop = true;
        if (current.common.type != 9) {
            if (count_world_cell_neighbor_bits(c, &current) == 1)
                stop = true;
            if (current.common.type == 1) {
                const auto previous = stepped(p, opposite_world_cell_direction(direction));
                auto& back = map(c, cell_id(previous)).base;
                if (back.common.type != 9) {
                    clear_world_cell_neighbor_mask_bits(c, &back, direction);
                    refresh_world_cell_render_state(c, &back);
                    p = previous;
                }
                clear_world_cell_neighbor_mask_bits(c, &current, opposite_world_cell_direction(direction));
                refresh_world_cell_render_state(c, &current);
                break;
            }
        }
        if (stop)
            break;
        p = stepped(p, direction);
    }
    append_point(state, p);
    c.construction.path_start_x = p.x;
    c.construction.path_start_y = p.y;
    return direction;
}

//The initial/paired track tracers retain an asymmetric endpoint rule.
void ParkConstructionRuntime::trace_track(ConstructionWorld& c, ParkObjectRuntimeView& owner, bool opposite) {
    using namespace construction_detail;
    state.path_points.clear();
    WorldCellCoordinates p{ owner.base.x >> 8, owner.base.y >> 8 };
    auto direction = object_anchor(p, std::bit_cast<int>(owner.angle), opposite);
    state.direction = direction;
    const auto exclusion =
        bounds({ c.construction.interaction_cursor_anchor[0], c.construction.interaction_cursor_anchor[1] })
            ? cell_id({ c.construction.interaction_cursor_anchor[0], c.construction.interaction_cursor_anchor[1] })
            : 0;
    bool stop = !bounds(p) || (opposite && !world_cell_is_track_root(&track(c, cell_id(p)).base));
    if (bounds(p) && ordinary_track(track(c, cell_id(p)).base))
        append_point(state, p);
    std::bitset<map_cell_count> visited;
    while (bounds(p)) {
        auto& row = track(c, cell_id(p)).base;
        if (visited.test(row.index - 1u))
            throw std::length_error("cyclic editable track path");
        visited.set(row.index - 1u);
        if (ordinary_track(row)) {
            if (get_world_cell_corner_neighbor_mask(c, &row)) {
                direction = opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &row);
                append_point(state, p);
                row.common.overlap_counter = 1;
            } else
                row.common.overlap_counter = get_world_cell_branch_neighbor_mask(c, &row) ? 1 : 0;
            if (opposite && row.index == exclusion)
                row.common.overlap_counter = 0;
            if (!get_world_cell_corner_neighbor_mask(c, &row) && !(get_world_cell_neighbor_mask(c, &row) & direction))
                stop = true;
            auto* next = resolve_track_cell_in_direction(c, &row, direction, 2);
            if (next
                && track_variant(c, next->base)
                && next->base.common.parent_id == row.common.parent_id
                && (get_world_cell_neighbor_mask(c, &next->base) & opposite_world_cell_direction(direction))) {
                if (!opposite)
                    p = stepped(p, direction, 2);
                break;
            }
        }
        if (stop)
            break;
        p = stepped(p, direction, 2);
    }
    append_point(state, p);
    c.construction.path_start_x = p.x;
    c.construction.path_start_y = p.y;
}

void ParkConstructionRuntime::remove_traced_path(ConstructionWorld& c, bool track_path) {
    using namespace construction_detail;
    //process_world_interaction_path_step pops the endpoint while retaining one
    //seed point. Each leg is dispatched twice; overlap counters prevent erasing
    //shared corners too early and queue PATH endpoints are preserved.
    while (state.path_points.size() > 1) {
        const auto current = state.path_points.back();
        state.path_points.pop_back();
        const auto next = state.path_points.back();
        if (track_path) {
            if (bounds(current))
                remove_track_cell_contents(c, &track(c, cell_id(current)).base, 0, 0, 1);
            auto p = current;
            const bool horizontal =
                std::abs(static_cast<std::int64_t>(next.x) - p.x) > std::abs(static_cast<std::int64_t>(next.y) - p.y);
            auto target = next;
            int delta = (horizontal ? p.x - target.x : p.y - target.y);
            if (delta > 0 && delta % 2)
                ++delta;
            const int aligned = (horizontal ? p.x : p.y) - 2 * (delta / 2);
            if (horizontal)
                target.x = aligned;
            else
                target.y = aligned;
            const int step = (horizontal ? p.x : p.y) < aligned ? 2 : (horizontal ? p.x : p.y) > aligned ? -2 : 0;
            for (;;) {
                const bool endpoint = horizontal ? p.x == target.x : p.y == target.y;
                if (bounds(p))
                    remove_track_cell_contents(c,
                                               &track(c, cell_id(p)).base,
                                               horizontal ? step : 0,
                                               horizontal ? 0 : step,
                                               1);
                if (endpoint)
                    break;
                if (horizontal)
                    p.x += step;
                else
                    p.y += step;
            }
        } else {
            (void)command_line(c, 0, current, current);
            (void)command_line(c, 134, current, current);
            if (!bounds(next) || map(c, cell_id(next)).base.common.type != 1) {
                (void)command_line(c, 0, current, next);
                (void)command_line(c, 134, current, next);
            }
        }
        c.construction.path_start_x = next.x;
        c.construction.path_start_y = next.y;
    }
}
} //namespace simcoaster
