#include "park_construction_internal.hpp"

namespace simcoaster {
TrackInteractionResult ParkConstructionRuntime::evaluate_track(ConstructionWorld& c, const CellBase& cell, int operation, bool followup, std::uint8_t direction) {
    using namespace construction_detail;
    using R = TrackInteractionResult;
    if (operation == 57) {
        return state.validation_failed ? R::rejected : R::accept;
    }
    auto& origin = map(c, cell.index);
    std::array<MapCell*, 4> block { &origin, resolve_world_cell_by_scaled_local_offset(c, &origin.base, { 1, 0 }, 1), resolve_world_cell_by_scaled_local_offset(c, &origin.base, { 1, 1 }, 1), resolve_world_cell_by_scaled_local_offset(c, &origin.base, { 0, 1 }, 1) };
    if (std::ranges::any_of(block, [](const auto* p) {
            return !p;
        })) {
        return R::rejected;
    }
    bool conflict = false;
    for (const auto* p : block) {
        const auto type = p->base.common.type;
        conflict |= footprint(type) || type == 7 || type == 30 || type == 21 || type == 5;
        if ((p->base.common.flags & 64u) || p->is_obstacle) {
            return R::rejected;
        }
    }
    //The fourth native priority probe repeats the diagonal alias. Preserve
    //that instruction-level behavior while validating all four clearances.
    for (const auto* p : std::array { block[0], block[1], block[2], block[2] }) {
        if (footprint(p->base.common.type)) {
            if (auto* owner = object_in(c, p->base)) {
                state.occupant_overwrite_priority = definition(c, owner->definition_id).balance->Info.OverwritePriority;
            }
            if (state.candidate_overwrite_priority > state.occupant_overwrite_priority) {
                return R::accept;
            }
        }
    }
    int cost = 0;
    if (operation == 11 || operation == 16) {
        cost = operation == 11 ? c.construction.kart_track_cell_base_cost : c.construction.water_track_cell_base_cost;
        for (std::size_t i = 1; i < block.size(); ++i) {
            auto& row = root_track(c, block[i]->base).base;
            if (world_cell_is_track_root(&row) && row.common.parent_id != c.construction.origin_cell) {
                conflict = true;
                state.validation_failed = true;
            }
        }
    }
    if (state.stop) {
        state.validation_failed = true;
    }
    if (!followup && state.validation_failed) {
        return R::rejected;
    }
    const auto type = cell.common.type;
    const auto neighbors = get_world_cell_neighbor_mask(c, &cell), forward = get_world_cell_direction(&cell), reverse = opposite_world_cell_direction(forward);
    const auto count = count_world_cell_neighbor_bits(c, &cell);
    const bool same_owner = cell.common.parent_id == c.construction.origin_cell;
    if (type == 25 && operation == 25) {
        const auto p = coordinate(cell.index);
        return (p.x == c.construction.interaction_cursor_anchor[0] && p.y == c.construction.interaction_cursor_anchor[1]) || (p.x == c.construction.interaction_secondary_anchor[0] && p.y == c.construction.interaction_secondary_anchor[1]) ? R::rejected : R::accept;
    }
    if (type != 25 && operation == 25) {
        if (type && type != 7) {
            state.validation_failed = true;
            return R::rejected;
        }
        return R::accept;
    }
    if (operation == type && same_owner && (!state.track_reuse || count == 1 || count == 3)) {
        state.track_reuse = true;
    }
    const auto stop = [&] {
        if (!followup) {
            state.stop = true;
        }
        return R::rejected;
    };
    if (operation == type && !state.track_first && !state.track_endpoint) {
        const bool straight = count_world_cell_cardinal_neighbor_bits(c, &cell) == 2 && (((neighbors & 1u) && (neighbors & 16u)) || ((neighbors & 4u) && (neighbors & 64u))) && neighbors;
        if (!straight && count != 1) {
            return stop();
        }
    }
    if ((type == 25 || type == 7 || (cell.common.flags & 64u) || !operation || !type || operation == type) && type != operation && !c.construction.cell_costs_suppressed) {
        state.cost = fixed::add(state.cost, cost);
    }
    if (!c.construction.action_recording_disabled && operation != 14 && operation != 19 && state.cost > bank(c).balance) {
        if (!followup) {
            state.validation_failed = true;
            state.status_latch = true;
        }
        state.insufficient_funds = true;
        return R::rejected;
    }
    if (state.track_endpoint && type != 11 && type != 16 && (operation == 11 || operation == 16)) {
        for (const auto* p : block) {
            if (p->base.common.type == 3 || p->base.common.type == 9 || (p->base.common.flags & 32u)) {
                return stop();
            }
        }
    }
    if (!type && operation != 14 && operation != 19 && !conflict) {
        if (!followup) {
            state.track_reuse = false;
        }
        return R::accept;
    }
    const bool matching = operation == type || (operation == 11 && type == 13) || (operation == 16 && type == 18);
    if (matching) {
        if ((state.track_first && state.reuse_line_start) || state.track_endpoint) {
            if (type == 11 || type == 16) {
                if (direction != forward && count == 2) {
                    return stop();
                }
                if (map_corner(c, cell)) {
                    return stop();
                }
            }
            if ((type == 13 || type == 18) && direction != forward) {
                return stop();
            }
            if ((!(cell.common.flags & 15u) || !direction || direction == forward || (!state.track_endpoint && !state.track_first)) && (direction == reverse || !same_owner || !(cell.common.flags & 16u) || direction == forward)) {
                state.existing_endpoint = true;
                return R::end_marker;
            }
            return stop();
        }
        if ((cell.common.flags & 15u) && direction && direction != forward && state.track_first) {
            return stop();
        }
    }
    if (((type == 11 && operation == 11) || (type == 16 && operation == 16)) && direction != reverse && same_owner) {
        if ((cell.common.flags & 16u) && direction != forward) {
            return stop();
        }
        if ((cell.common.flags & 15u) && direction != forward) {
            return stop();
        }
        return R::accept;
    }
    if (((type == 18 && operation == 16) || (type == 13 && operation == 11)) && direction == forward) {
        if (!followup) {
            state.stop = true;
        }
        return R::accept;
    }
    if (conflict && !(cell.common.flags & 16u)) {
        return stop();
    }
    if (operation == 15) {
        if ((type == 11 || type == 16) && same_owner) {
            return R::skip_coordinate_adjustment;
        }
        return type == 25 ? R::skip_coordinate_adjustment : R::rejected;
    }
    if ((type == 11 || type == 16) && (operation == 14 || operation == 19) && same_owner && (count_world_cell_cardinal_neighbor_bits(c, &cell) <= 2 || !neighbors)) {
        return R::track_endpoint;
    }
    const auto fail = [&] {
        if (!followup) {
            state.validation_failed = true;
        }
        return R::rejected;
    };
    if (map_corner(c, cell) && !state.queue_join_override) {
        return fail();
    }
    if (cell.common.flags & 15u) {
        if (!state.track_first || (direction != forward && direction != reverse)) {
            return fail();
        }
    }
    if (count == 2 && direction != forward && direction != reverse) {
        state.track_reuse = false;
    }
    return operation != type || !same_owner || count != 3 ? R::rejected : R::accept;
}
} //namespace simcoaster
