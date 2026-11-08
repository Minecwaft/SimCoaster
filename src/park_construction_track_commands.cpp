#include "park_construction_internal.hpp"

namespace simcoaster {
namespace {
using namespace construction_detail;
constexpr std::array<CellLocalOffset, 4> block_offsets{ { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } } };

void connect_track(ConstructionWorld& c, CellBase& cell, int type, int x, int y, bool first) {
    int ex = x, ey = y;
    if (first) {
        if (c.construction.previous_path_step_x == 128 || c.construction.previous_path_step_y == 128)
            ex = ey = 0;
        else {
            ex = c.construction.previous_path_step_x;
            ey = c.construction.previous_path_step_y;
        }
    }
    if (type == 11 || type == 13 || type == 16 || type == 18 || type == 25) {
        const auto connect = [&](std::uint8_t direction) {
            auto* adjacent = resolve_track_cell_in_direction(c, &cell, direction, 2);
            if (!adjacent)
                return;
            auto& other = adjacent->base;
            const auto t = other.common.type;
            if ((type == 25 ? t == 25 : t == 11 || t == 13 || t == 16 || t == 18 || t == 25)
                && count_world_cell_neighbor_bits(c, &cell) < 4
                && count_world_cell_neighbor_bits(c, &other) < 4
                && (!other.common.parent_id || other.common.parent_id == c.construction.origin_cell)) {
                or_world_cell_neighbor_mask_bits(c, &cell, direction);
                or_world_cell_neighbor_mask_bits(c, &other, opposite_world_cell_direction(direction));
            }
        };
        if (ey == 2)
            connect(1);
        if (ex == -2)
            connect(4);
        if (ey == -2)
            connect(16);
        if (ex == 2)
            connect(64);
    }
    c.construction.previous_path_step_x = x;
    c.construction.previous_path_step_y = y;
}
} //namespace

bool ParkConstructionRuntime::track_command(ConstructionWorld& c, CellBase& cell, std::uint32_t flags, int x, int y) {
    using namespace construction_detail;
    if (cell.record_kind != CellStorageKind::track)
        throw std::invalid_argument("track command requires track storage");
    const auto code = flags & 255u;
    if (flags & 256u)
        return preview_track_cell(c, cell, code, x, y);
    if (flags & 512u) {
        const auto direction = x == -2 ? 64u : x == 2 ? 4u : y == -2 ? 1u : y == 2 ? 16u : 0u;
        return evaluate_track(c, cell, static_cast<int>(code), false, static_cast<std::uint8_t>(direction))
            != TrackInteractionResult::rejected;
    }
    match (code) {
        case 50 => {
            remove_track_cell_contents(c, &cell, x, y, 1);
            return true;
        }
        case 128 => {
            connect_track(c, cell, c.construction.last_successful_type, x, y, state.track_first);
            return true;
        }
        case 129 => {
            if (c.construction.world_hoarding_mode
                && (cell.common.type == 11
                    || cell.common.type == 12
                    || cell.common.type == 16
                    || cell.common.type == 17
                    || map(c, cell.index).base.common.type == 21))
                return true;
            if (cell.common.type == 25)
                refresh_hoarding_cell(c, cell);
            else if (map_corner(c, cell) || get_world_cell_branch_neighbor_mask(c, &cell))
                clear_world_cell_flags(&cell, 15);
            c.construction.last_successful_type = 0;
            if (cell.common.type != 13 && cell.common.type != 18)
                update_world_cell_mesh(c, &cell);
            if (state.track_endpoint && state.track_endpoint_stop) {
                state.track_endpoint_stop = false;
                return false;
            }
            return true;
        }
        case 130 => {
            const auto original =
                count_world_cell_cardinal_neighbor_bits(c, &cell) > 2 ? get_world_cell_direction(&cell) : 0;
            if (x == -2)
                set_world_cell_direction(&cell, static_cast<std::uint8_t>(original | 64u));
            else if (x == 2)
                set_world_cell_direction(&cell, static_cast<std::uint8_t>(original | 4u));
            if (y == -2)
                set_world_cell_direction(&cell, static_cast<std::uint8_t>(original | 1u));
            else if (y == 2)
                set_world_cell_direction(&cell, static_cast<std::uint8_t>(original | 16u));
            return true;
        }
        case 131 => {
            if (c.construction.world_hoarding_mode
                && (cell.common.type == 11
                    || cell.common.type == 12
                    || cell.common.type == 16
                    || cell.common.type == 17
                    || map(c, cell.index).base.common.type == 21))
                return true;
            if (!state.track)
                return true;
            cell.common.parent_id = c.construction.origin_cell;
            for (const auto offset : std::span{ block_offsets }.subspan(1))
                if (auto* child = resolve_track_cell_at_local_offset(c, &cell, offset, 1)) {
                    child->base.common.parent_id = cell.index;
                    (void)try_assign_world_cell_type(c, &child->base, 12);
                }
            return true;
        }
        case 133 => {
            if (map_corner(c, cell) || get_world_cell_branch_neighbor_mask(c, &cell))
                return true;
            int empty = 0;
            for (const auto offset : block_offsets) {
                auto* p = offset.x || offset.y
                            ? resolve_world_cell_by_scaled_local_offset(c, &map(c, cell.index).base, offset, 1)
                            : &map(c, cell.index);
                if (!p)
                    continue;
                if (cell.common.type == 25
                        ? p->base.common.type == 2 || p->base.common.type == 7 || p->base.common.type == 30
                        : p->base.common.type == 1 || p->base.common.type == 3)
                    cell.common.flags = 1;
                if (!p->base.common.type)
                    ++empty;
            }
            if (empty == 4 && cell.common.type != 25)
                clear_world_cell_flags(&cell, 15);
            return true;
        }
        case 134 => {
            if (!world_cell_is_track_root(&cell) || cell.common.type == 25 || !cell.common.parent_id)
                return true;
            auto* owner = object(c, find_first_park_object_id_in_map_cell(c, &map(c, cell.common.parent_id)));
            if (owner && !reconcile_track_links(c, *owner, false, true, false, false))
                (void)reconcile_track_links(c, *owner, true, false, false, false);
            return true;
        }
        case 135 => {
            if (state.candidate_overwrite_priority <= state.occupant_overwrite_priority)
                return true;
            const auto p = coordinate(cell.index);
            RestoreValue sx{ c.construction.path_start_x };
            RestoreValue sy{ c.construction.path_start_y };
            RestoreValue mode{ c.construction.mode };
            if (!world_cell_is_track_root(&cell)) {
                for (const auto offset : std::array<CellLocalOffset, 4>{ { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } } }) {
                    const WorldCellCoordinates target{ p.x + offset.x, p.y + offset.y };
                    if (bounds(target))
                        (void)demolish(c, target);
                }
                if (state.track_endpoint)
                    for (const auto offset : block_offsets)
                        if (auto* m =
                                offset.x || offset.y
                                    ? resolve_world_cell_by_scaled_local_offset(c, &map(c, cell.index).base, offset, 1)
                                    : &map(c, cell.index))
                            remove_map_cell_contents(c, &m->base, 0, 0);
            }
            return true;
        }
        case _ => {
            state.track_endpoint_stop = (state.track_endpoint
                                         && (cell.common.type == 11
                                             || cell.common.type == 13
                                             || cell.common.type == 16
                                             || cell.common.type == 18))
                                     || cell.common.type == 25;
            if (!try_assign_world_cell_type(c, &cell, static_cast<int>(flags)))
                return code == 25;
            c.construction.last_successful_type = static_cast<int>(flags);
            return true;
        }
    }
}

bool ParkConstructionRuntime::track_command_line(ConstructionWorld& c,
                                                 std::uint32_t code,
                                                 WorldCellCoordinates start,
                                                 WorldCellCoordinates& end) {
    using namespace construction_detail;
    state.track_first = true;
    state.track_endpoint = false;
    auto p = start;
    const bool horizontal =
        std::abs(static_cast<std::int64_t>(end.x) - start.x) > std::abs(static_cast<std::int64_t>(end.y) - start.y);
    int delta = horizontal ? start.x - end.x : start.y - end.y;
    if (delta > 0 && delta % 2)
        ++delta;
    const int aligned = (horizontal ? start.x : start.y) - 2 * (delta / 2);
    if (horizontal)
        end.x = aligned;
    else
        end.y = aligned;
    const int step = (horizontal ? start.x : start.y) < aligned ? 2
                   : (horizontal ? start.x : start.y) > aligned ? -2
                                                                : 0;
    for (;;) {
        const bool endpoint = horizontal ? p.x == end.x : p.y == end.y;
        if (endpoint)
            state.track_endpoint = true;
        c.construction.track_endpoint_phase = state.track_endpoint;
        if (bounds(p)) {
            const auto result =
                track_command(c, track(c, cell_id(p)).base, code, horizontal ? step : 0, horizontal ? 0 : step);
            state.track_first = false;
            if (endpoint) {
                state.track_endpoint = false;
                c.construction.track_endpoint_phase = 0;
            }
            if (!result)
                return false;
        }
        if (endpoint)
            break;
        if (horizontal)
            p.x += step;
        else
            p.y += step;
    }
    return true;
}
} //namespace simcoaster
