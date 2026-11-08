#include "park_construction_internal.hpp"

namespace simcoaster {
namespace {
using namespace construction_detail;

void repair_segment(ConstructionWorld& c, TrackCell& row, CellLocalOffset first, CellLocalOffset second) {
    auto* a = resolve_track_cell_at_local_offset(c, &row.base, first, 2);
    auto* b = resolve_track_cell_at_local_offset(c, &row.base, second, 2);
    const auto direction = get_world_cell_direction(&row.base);
    if (direction != 1 && direction != 4 && direction != 16 && direction != 64) {
        throw std::invalid_argument("track segment direction is not cardinal");
    }
    const std::uint16_t a_expected = direction == 1 || direction == 4 ? 2 : 8, b_expected = direction == 1 || direction == 4 ? 8 : 2;
    clear_world_cell_flags(&row.base, 15);
    const auto repair = [&](TrackCell* target, std::uint16_t expected, std::uint16_t opposite) {
        if (!target || !world_cell_has_track_segment_flags(&target->base)) {
            return;
        }
        const auto flags = target->base.common.flags;
        if (!(flags & expected) && !(flags & 4u)) {
            return;
        }
        clear_world_cell_flags(&target->base, 15);
        assign_world_cell_flags(&target->base, flags & expected ? 1 : opposite);
        update_world_cell_mesh(c, &target->base);
    };
    repair(a, a_expected, b_expected);
    repair(b, b_expected, a_expected);
    update_world_cell_mesh(c, &row.base);
}

float preview_height(ConstructionWorld& c, const CellBase& row) {
    const auto type = resolve_track_cell_for_grid_entry(c, &row)->base.common.type;
    if (type != 11 && type != 12 && type != 16 && type != 17 && type != 25 && row.common.type != 7 && row.common.type != 30 && !footprint(row.common.type)) {
        return 0;
    }
    const auto p = coordinate(row.index);
    const auto value = fixed::add(fixed::truncate_integer(sample_world_map_cell_surface(c, p.x, p.y)), 9);
    return static_cast<float>(value / 10) * 10;
}
} //namespace

bool ParkConstructionRuntime::command(ConstructionWorld& c, CellBase& cell, std::uint32_t flags, std::int32_t x, std::int32_t y) {
    using namespace construction_detail;
    const auto code = flags & 255u;
    const auto request = static_cast<CellInteractionRequest>(code);
    if (flags & 512u) {
        return !rejected(evaluate(c, cell, request));
    }
    if (flags & 256u) {
        const auto p = coordinate(cell.index);
        const auto base_height = preview_height(c, cell);
        int material = 0, geometry = 1;
        if (code == 136) {
            material = 9;
        } else if (code == 137) {
            material = 1;
        } else if (code == 4 || code == 24 || ((code == 11 || code == 16) && x == 5)) {
            material = x;
            geometry = y;
        }
        CellInteractionResult result = CellInteractionResult::rejected;
        const auto cost_before_cell = state.cost;
        if (!state.skip_nonpath_preview || code == 1) {
            result = evaluate(c, cell, request);
        }
        const bool accepted = !rejected(result);
        if (accepted) {
            if (result != CellInteractionResult::accept) {
                material = std::to_underlying(result);
            }
        } else {
            const auto quoted_cost = state.cost;
            (void)evaluate(c, cell, request, true);
            //Preserve the second pass's topology/feedback effects without charging
            //this cell twice. User-authorized repair of native67B756.
            if (quoted_cost != cost_before_cell) {
                state.cost = quoted_cost;
            }
            state.status_latch = true;
            material = 1;
            geometry = 1;
        }
        auto height = base_height;
        if (accepted && material != 2 && (flags & 127u) == 21u) {
            height += 3;
        }
        if (flags == 34u) {
            height -= 3; //Native exact comparison is retained even for a preview command.
        }
        const auto face = [&](float h, std::uint32_t direction, int style, int selector, bool skip) {
            if (state.faces.size() >= 2054 || (style == 1 && !state.show_red_faces)) {
                return;
            }
            state.faces.push_back({ { static_cast<float>(p.x) * 10, h + 1.5f, static_cast<float>(p.y) * 10 }, direction, static_cast<std::uint32_t>(style), 10, 10, selector, skip });
        };
        face(height, 0, material, geometry, state.frame_rate_below_clip || state.coordinate_adjustment_disabled);
        for (const auto direction : std::array<std::uint8_t, 4> { 1, 4, 16, 64 }) {
            auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell, direction);
            if (!neighbor) {
                continue;
            }
            auto neighbor_height = preview_height(c, neighbor->base);
            if (accepted && result != CellInteractionResult::orange && (flags & 127u) == 21u) {
                neighbor_height += 3;
            }
            const auto top = fixed::truncate_integer(accepted ? height : base_height), bottom = fixed::truncate_integer(neighbor_height);
            for (int level = bottom; level < top; level = fixed::add(level, 10)) {
                const auto wall_style = accepted ? (material == 2 ? 2 : material == 1 ? 1 : 0) : 1;
                if (accepted) {
                    state.stop = false;
                    state.validation_failed = false;
                }
                const auto skip = accepted ? (state.frame_rate_below_clip ? c.construction.mode != 4 : true) : state.frame_rate_below_clip;
                face(static_cast<float>(level), direction, wall_style, 0, skip || state.coordinate_adjustment_disabled);
            }
        }
        return true;
    }
    match (code) {
        case 0 => {
            remove_map_cell_contents(c, &cell, static_cast<std::uint32_t>(x), y);
            auto& paired = root_track(c, cell);
            if (!world_cell_is_track_root(&paired.base) || !world_cell_has_track_segment_flags(&paired.base)) {
                return true;
            }
            auto& origin = map(c, paired.base.index);
            unsigned paths = 0;
            for (const auto delta : std::array<CellLocalOffset, 4> { { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } } }) {
                auto* probe = delta.x || delta.y ? resolve_world_cell_by_scaled_local_offset(c, &origin.base, delta, 1) : &origin;
                if (probe && (probe->base.common.type == 1 || probe->base.common.type == 3)) {
                    ++paths;
                }
            }
            if (paths) {
                return true;
            }
            const auto direction = get_world_cell_direction(&paired.base);
            if (direction == 1 || direction == 16) {
                repair_segment(c, paired, { 0, 1 }, { 0, -1 });
            } else if (direction == 4 || direction == 64) {
                repair_segment(c, paired, { -1, 0 }, { 1, 0 });
            }
            return true;
        }
        case 128 => {
            update_cell_connections(c, &cell, c.construction.last_successful_type, x, y, true, true);
            return true;
        }
        case 129 => {
            refresh_world_cell_render_state(c, &cell);
            c.construction.last_successful_type = 0;
            if (c.construction.line_endpoint_dispatch && c.construction.endpoint_stop_pending) {
                c.construction.endpoint_stop_pending = 0;
                return false;
            }
            return true;
        }
        case 130 => {
            if (get_world_cell_direction(&cell)) {
                return true;
            }
            if (x == -1) {
                set_world_cell_direction(&cell, 4);
            } else if (x == 1) {
                set_world_cell_direction(&cell, 64);
            }
            if (y == -1) {
                set_world_cell_direction(&cell, 16);
            } else if (y == 1) {
                set_world_cell_direction(&cell, 1);
            }
            return true;
        }
        case 131 => {
            cell.common.parent_id = c.construction.origin_cell;
            return true;
        }
        case 132 => {
            if (cell.record_kind != CellStorageKind::map) {
                throw std::invalid_argument("pylon assignment requires map cell");
            }
            map(c, cell.index).pylon_index = state.next_pylon++;
            return true;
        }
        case 133 => {
            auto& paired = root_track(c, cell);
            if (!world_cell_is_track_root(&paired.base) || paired.base.common.type == 25 || get_world_cell_corner_neighbor_mask(c, &paired.base) || get_world_cell_branch_neighbor_mask(c, &paired.base) || world_cell_has_track_segment_flags(&paired.base)) {
                return true;
            }
            clear_world_cell_flags(&paired.base, 15);
            assign_world_cell_flags(&paired.base, 1);
            return true;
        }
        case 134 => {
            refresh_track_links(c, cell);
            return true;
        }
        case 135 => {
            remove_lower_priority(c, cell);
            return true;
        }
        case _ => {
            c.construction.endpoint_stop_pending = 0;
            if (c.construction.line_endpoint_dispatch && code != 21 && (cell.common.type == 1 || cell.common.type == 3)) {
                c.construction.endpoint_stop_pending = 1;
            }
            if (code == 21 && ((cell.common.flags & 256u) || footprint(cell.common.type))) {
                c.construction.endpoint_stop_pending = 1;
            }
            if (!try_change_world_cell_type(c, &cell, static_cast<int>(flags))) {
                return false;
            }
            c.construction.last_successful_type = static_cast<int>(flags);
            return true;
        }
    }
}

void ParkConstructionRuntime::remove_lower_priority(ConstructionWorld& c, CellBase& cell) {
    if (state.candidate_overwrite_priority <= state.occupant_overwrite_priority) {
        return;
    }
    const auto start_x = c.construction.path_start_x, start_y = c.construction.path_start_y, mode = c.construction.mode;
    if (cell.common.type != 1 || mode == 1 || mode == 3) {
        (void)demolish(c, construction_detail::coordinate(cell.index));
    } else {
        remove_map_cell_contents(c, &cell, 0, 0);
    }
    c.construction.path_start_x = start_x;
    c.construction.path_start_y = start_y;
    c.construction.mode = mode;
}

void ParkConstructionRuntime::refresh_connected_paths(ConstructionWorld& c, MapCell& cell, bool last_option) {
    update_cell_connections(c, &cell.base, 1, 0, 0, true, last_option);
    (void)command(c, cell.base, 129);
    for (const auto direction : std::array<std::uint8_t, 4> { 1, 4, 16, 64 }) {
        if (get_world_cell_neighbor_mask(c, &cell.base) & direction) {
            auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell.base, direction);
            if (neighbor && neighbor->base.common.type == 1) {
                update_cell_connections(c, &neighbor->base, 1, 0, 0, true, last_option);
                (void)command(c, neighbor->base, 129);
            }
        }
    }
}
} //namespace simcoaster
