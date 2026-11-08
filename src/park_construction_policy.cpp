#include "park_construction_internal.hpp"

namespace simcoaster {
CellInteractionResult ParkConstructionRuntime::evaluate(ConstructionWorld& c, const CellBase& cell, CellInteractionRequest request, bool bypass_failure) {
    using namespace construction_detail;
    using R = CellInteractionRequest;
    using Result = CellInteractionResult;
    auto effective = request;
    bool skip_priority = false;
    int pending_cost = 0;
    if (request == R::special_object || request == R::special_object_27) {
        skip_priority = true;
        effective = R::object;
    } else if (request == R::orange) {
        return Result::orange;
    } else if (request == R::light_blue) {
        return Result::light_blue;
    } else if (request == R::erase58 || request == R::erase61 || request == R::erase62) {
        return Result::erase;
    }
    if (cell.common.flags & 32u) {
        if (effective == R::object || effective == R::entry || effective == R::exit) {
            return Result::rejected;
        }
        if (effective == R::path_unless_locked && cell.common.type == 1) {
            return Result::accept;
        }
    }
    if (effective == R::path_unless_locked) {
        effective = R::path;
    }
    if (effective == R::accept || effective == R::patrol_preview || effective == R::red_preview) {
        return Result::accept;
    }
    if (footprint(cell.common.type)) {
        const auto* placed = object_in(c, cell);
        if (!placed) {
            throw std::logic_error("construction footprint has no park object");
        }
        state.occupant_overwrite_priority = definition(c, placed->definition_id).balance->Info.OverwritePriority;
    }
    if (!state.validation_failed && effective == R::support && cell.common.type == 21 && (cell.common.flags & 256u) && cell.common.parent_id == c.construction.origin_cell) {
        state.existing_endpoint = true;
        return Result::end_marker;
    }
    if (cell.common.flags & 64u) {
        return Result::rejected;
    }
    if (footprint(cell.common.type)) {
        if (!skip_priority && state.candidate_overwrite_priority > state.occupant_overwrite_priority && effective != R::support && effective != R::move_support && !state.stop && !state.validation_failed) {
            return Result::accept;
        }
        state.validation_failed = true;
        return Result::rejected;
    }
    if (effective == R::path) {
        pending_cost = c.construction.path_cell_base_cost;
    } else if (effective == R::queue) {
        pending_cost = c.construction.queue_cell_base_cost;
    }
    auto& paired = root_track(c, cell);
    if ((effective == R::kart || effective == R::water) && world_cell_is_track_root(&paired.base) && !c.construction.selected_track_type) {
        state.validation_failed = true;
        return Result::rejected;
    }
    if (state.stop) {
        state.validation_failed = true;
    }
    if (!bypass_failure && state.validation_failed) {
        return Result::rejected;
    }
    if (paired.base.common.type == 25) {
        state.validation_failed = true;
        return Result::rejected;
    }
    const auto code = std::to_underlying(effective);
    if (can_change_world_cell_type(c, &cell, code) && cell.common.type != code && !c.construction.cell_costs_suppressed && (effective != R::queue || cell.common.type != 1)) {
        state.cost = fixed::add(state.cost, pending_cost);
    }
    if (!c.construction.action_recording_disabled && state.cost && state.cost > bank(c).balance) {
        state.validation_failed = true;
        state.status_latch = true;
        state.insufficient_funds = true;
        return Result::rejected;
    }
    if (c.construction.selected_track_type && (effective == R::kart || effective == R::water)) {
        return static_cast<Result>((cell.common.flags & 16u) || !world_cell_is_track_root(&paired.base) || get_world_cell_corner_neighbor_mask(c, &paired.base) || get_world_cell_branch_neighbor_mask(c, &paired.base) || paired.base.common.parent_id != c.construction.origin_cell || cell.common.type == 3);
    }
    if (effective == R::support || effective == R::move_support) {
        if (world_cell_is_track_root(&paired.base)) {
            return Result::rejected;
        }
    } else if (effective == R::queue_owner) {
        return static_cast<Result>(cell.common.type != 3 || cell.common.parent_id != c.construction.origin_cell);
    } else if (effective == R::queue && cell.common.type == 3) {
        if ((count_world_cell_neighbor_bits(c, &cell) == 2 && !state.queue_join_override) || cell.common.parent_id != c.construction.origin_cell) {
            state.validation_failed = true;
            return Result::rejected;
        }
        if (c.construction.line_endpoint_dispatch) {
            state.existing_endpoint = true;
            return Result::end_marker;
        }
        return Result::accept;
    }
    if (code == cell.common.type && ((c.construction.cell_line_first_dispatch_pending && state.reuse_line_start) || c.construction.line_endpoint_dispatch)) {
        if (cell.common.type == 3 && count_world_cell_neighbor_bits(c, &cell) == 2) {
            return Result::rejected;
        }
        state.existing_endpoint = true;
        return Result::end_marker;
    }
    if ((effective == R::support || effective == R::move_support) && !(cell.common.flags & 32u) && cell.common.type == 1) {
        return Result::accept;
    }
    if ((effective == R::support || effective == R::move_support) && cell.common.type == 21 && cell.common.parent_id == c.construction.origin_cell) {
        return Result::accept;
    }
    if (cell.common.type == 21) {
        state.stop = true;
        return Result::rejected;
    }
    if (effective == R::object && world_cell_is_track_root(&paired.base)) {
        return static_cast<Result>(!c.construction.selected_track_type);
    }
    if (get_world_cell_corner_neighbor_mask(c, &paired.base) || get_world_cell_branch_neighbor_mask(c, &paired.base)) {
        state.stop = true;
        return Result::rejected;
    }
    if (effective == R::path && cell.common.type == 1) {
        return Result::accept;
    }
    if (effective == R::object && cell.common.type == 4) {
        return Result::rejected;
    }
    if (code == cell.common.type) {
        if (!state.entry_neighbor) {
            throw std::logic_error("construction entry neighbor is missing");
        }
        if (cell.common.parent_id == state.entry_neighbor->base.index) {
            return Result::accept;
        }
    }
    if (!cell.common.type) {
        return Result::accept;
    }
    if (effective == R::queue) {
        if (cell.common.type == 1) {
            if (c.construction.line_endpoint_dispatch && !state.validation_failed) {
                state.queue_joined = true;
                state.stop = true;
                return Result::accept;
            }
            if (!(cell.common.flags & 32u)) {
                return Result::accept;
            }
            state.stop = true;
            return Result::rejected;
        }
        if (cell.common.type == 4) {
            state.stop = true;
        }
        return Result::rejected;
    }
    if (effective == R::path) {
        if (cell.common.type == 4 || cell.common.type == 9) {
            state.stop = true;
            return Result::rejected;
        }
        if (cell.common.type == 3) {
            if (count_world_cell_neighbor_bits(c, &cell) == 1) {
                const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell, get_world_cell_neighbor_mask(c, &cell));
                if (neighbor && neighbor->base.common.type == 3) {
                    state.queue_joined = true;
                    return Result::accept;
                }
            }
            state.validation_failed = true;
        }
        return Result::rejected;
    }
    if (effective == R::object) {
        return static_cast<Result>(cell.common.type != 1);
    }
    if (effective == R::kart || effective == R::water) {
        return static_cast<Result>(cell.common.type != 1);
    }
    return Result::rejected;
}
} //namespace simcoaster
