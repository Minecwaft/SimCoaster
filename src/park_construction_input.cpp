#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"

namespace simcoaster {
namespace {
using namespace construction_detail;
constexpr auto backtrack_mode = std::to_underlying(WorldInteractionMode::backtrack_preview);

bool same(WorldCellCoordinates a, WorldCellCoordinates b) {
    return a.x == b.x && a.y == b.y;
}

int sign(int value) {
    return (value > 0) - (value < 0);
}

bool live_map_input(const ConstructionQuery& c) {
    const auto* editor = c.construction.editor;
    return !editor || !editor->active_map() || editor->input_route() == CoasterEditorInputRoute::map_action;
}

WorldCellCoordinates snap_track(WorldCellCoordinates p, WorldCellCoordinates anchor) {
    const auto snap = [](int value, int origin) {
        auto delta = value - origin;
        if (delta < 0 && delta % 2)
            --delta;
        return origin + 2 * (delta / 2);
    };
    return { snap(p.x, anchor.x), snap(p.y, anchor.y) };
}

WorldCellCoordinates primary_anchor(ParkConstructionState& state) {
    auto anchor = state.origin;
    state.direction = object_anchor(anchor, state.object_rotation_degrees);
    return anchor;
}

bool track_backtrack(ConstructionWorld& c, const ParkConstructionState& state, WorldCellCoordinates p) {
    if ((state.backtrack_saved_mode != 11 && state.backtrack_saved_mode != 16)
        || !c.construction.park_object
        || !bounds(p))
        return false;
    const auto selector = definition(c, c.construction.park_object->definition_id).which_track_type;
    return selector >= 1
        && selector <= 3
        && (world_cell_is_track_child(&track(c, cell_id(p)).base)
            || world_track_reference_matches_type_set(c, cell_id(p)));
}

//Native674CA0, including its single-seed branch pruning. The ordinary
//endpoint editor uses only the non-pruning variant of this operation.
void reconcile_anchor(ConstructionWorld& c, bool prune_branches) {
    const WorldCellCoordinates p{ c.construction.interaction_cursor_anchor[0],
                                  c.construction.interaction_cursor_anchor[1] };
    if (!bounds(p))
        return;
    auto& anchor = track(c, cell_id(p));
    if (!world_cell_is_track_root(&anchor.base))
        return;
    constexpr std::array<std::uint8_t, 4> directions{ 1, 4, 16, 64 };
    for (const auto direction : directions) {
        auto* neighbor = resolve_track_cell_in_direction(c, &anchor.base, direction, 2);
        if (get_world_cell_neighbor_mask(c, &anchor.base) & direction) {
            if (!neighbor
                || (!world_cell_is_track_root(&neighbor->base) && !track_variant(c, neighbor->base))
                || (world_cell_is_track_root(&neighbor->base)
                    && !(get_world_cell_neighbor_mask(c, &neighbor->base)
                         & opposite_world_cell_direction(direction)))) {
                clear_world_cell_neighbor_mask_bits(c, &anchor.base, direction);
                update_world_cell_mesh(c, &anchor.base);
            }
        } else if (neighbor
                   && track_variant(c, neighbor->base)
                   && neighbor->base.common.parent_id == anchor.base.common.parent_id) {
            set_world_cell_direction(&anchor.base, opposite_world_cell_direction(direction));
            or_world_cell_neighbor_mask_bits(c, &anchor.base, direction);
            update_world_cell_mesh(c, &anchor.base);
        }
    }
    if (!prune_branches || !get_world_cell_corner_neighbor_mask(c, &anchor.base))
        return;
    for (const auto direction : directions)
        if (get_world_cell_neighbor_mask(c, &anchor.base) & direction)
            if (auto* neighbor = resolve_track_cell_in_direction(c, &anchor.base, direction, 2)) {
                if (track_variant(c, neighbor->base)) {
                    set_world_cell_direction(&anchor.base, 0);
                    set_world_cell_direction(&anchor.base, opposite_world_cell_direction(direction));
                } else {
                    clear_world_cell_neighbor_mask_bits(c, &anchor.base, direction);
                    update_world_cell_mesh(c, &anchor.base);
                    clear_world_cell_neighbor_mask_bits(c, &neighbor->base, opposite_world_cell_direction(direction));
                    update_world_cell_mesh(c, &neighbor->base);
                }
            }
}
} //namespace

bool
ParkConstructionRuntime::pointer_hover(ConstructionWorld& c, WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    if (!live_map_input(c)) {
        state.faces.clear();
        invalidate_interaction_cost_quote();
        return false;
    }
    const auto mode = c.construction.mode;
    if (mode != backtrack_mode)
        state.backtrack_saved_mode = mode;
    const auto saved = state.backtrack_saved_mode;
    const bool eligible_mode = saved == 1 || saved == 3 || saved == 20 || saved == 11 || saved == 16 || saved == 19;
    const WorldCellCoordinates start{ c.construction.path_start_x, c.construction.path_start_y };
    if (!eligible_mode)
        return preview(c, p, modifiers);
    bool candidate = false;
    if (bounds(start) && bounds(p)) {
        const auto& cell = map(c, cell_id(p)).base;
        candidate =
            (saved == 1 && cell.common.type == 1)
            || ((saved == 3 || saved == 20) && cell.common.type == 3)
            || ((saved == 11 || saved == 16 || saved == 19) && world_track_reference_matches_type_set(c, cell_id(p)));
    }
    bool reverse = false;
    if (candidate && !state.path_points.empty()) {
        auto current = start, target = p;
        if (saved == 11 || saved == 16) {
            const auto anchor = primary_anchor(state);
            current = snap_track(current, anchor);
            target = snap_track(target, anchor);
            auto& cell = root_track(c, map(c, cell_id(p)).base).base;
            candidate = get_world_cell_corner_neighbor_mask(c, &cell) != 0;
        }
        if (candidate && !same(current, target)) {
            const auto original = current;
            std::size_t horizontal{}, vertical{};
            bool first_point = false, diagonal = false;
            //Native snapshot674AF0 is a value owner; hover cannot pop live points.
            const auto snapshot = state.path_points;
            for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
                first_point = it + 1 == snapshot.rend();
                const auto dx = sign(it->x - current.x), dy = sign(it->y - current.y);
                horizontal += dx != 0;
                vertical += dy != 0;
                if (dx && dy) {
                    diagonal = true;
                    break;
                }
                std::size_t steps{};
                while (!same(current, *it) && !same(current, target)) {
                    current.x += dx;
                    current.y += dy;
                    if (++steps > map_cell_count)
                        throw std::length_error("Invalid reverse pointer path");
                }
                if (same(current, target))
                    break;
            }
            //4D32F0's exceptional complete first-leg path loop remains normal
            //mode1. Every other target on the saved polyline selects139.
            reverse = !diagonal
                   && same(current, target)
                   && (!first_point
                       || (current.x != original.x && current.y != original.y)
                       || !horizontal
                       || !vertical
                       || saved != 1);
        }
    }
    //These are native raw global writes, not SET_MODE actions: no recorder,
    //line reset, Advisor cancellation, or new handler ownership is involved.
    c.construction.mode = reverse ? backtrack_mode : saved;
    const auto skip = state.skip_nonpath_preview;
    if (reverse)
        state.skip_nonpath_preview = false;
    const auto result = preview(c, p, WorldInputModifierMask::none);
    state.skip_nonpath_preview = skip;
    return result;
}

void ParkConstructionRuntime::pointer_down(ConstructionWorld& c,
                                           WorldCellCoordinates p,
                                           WorldInputModifierMask modifiers,
                                           bool use_preview_coordinates) {
    if (!live_map_input(c))
        return;
    mouse_down(c, p, modifiers, use_preview_coordinates);
}

bool ParkConstructionRuntime::backtrack_path_step(ConstructionWorld& c,
                                                  ParkObjectRuntimeView* queue_owner,
                                                  bool refresh_links) {
    const auto mode = c.construction.mode;
    const auto saved = state.backtrack_saved_mode;
    const auto refresh_track = [&](bool single) {
        if (mode != 11 && mode != 16 && mode != 53)
            return;
        reconcile_anchor(c, single);
        if (refresh_links
            && c.construction.park_object
            && !reconcile_track_links(c, *c.construction.park_object, false, true, false, false))
            (void)reconcile_track_links(c, *c.construction.park_object, true, false, false, false);
    };
    if (state.path_points.size() < 2) {
        refresh_track(false);
        set_mode_preserving_path(c, mode, true);
        state.backtrack_saved_mode = saved;
        return false;
    }
    const auto current = state.path_points.back();
    state.path_points.pop_back(); //Retain the first seed exactly as674EF0.
    const auto next = state.path_points.back();
    state.status_latch = false;
    bool rebuild_queue = false;
    if (mode == 1 || mode == 3 || mode == 52 || mode == backtrack_mode) {
        if (refresh_links && mode == backtrack_mode)
            set_mode(c, 53, true);
        else {
            //Native intentionally uses X for both positioned sound axes.
            (void)submit_positioned_template_request(c,
                                                     0,
                                                     c.resources.ui_catalog_slot,
                                                     95,
                                                     current.x * 10,
                                                     0,
                                                     current.x * 10);
            set_mode(c, 52, true);
            rebuild_queue = true;
        }
    } else if (mode == 11 || mode == 16 || mode == 25 || mode == 53)
        set_mode(c, 53, true);
    (void)dispatch(c, current, WorldInputModifierMask::none, false);
    if (mode != 3 || map(c, cell_id(next)).base.common.type != 1)
        (void)dispatch(c, next, WorldInputModifierMask::none, false);
    c.construction.path_start_x = next.x;
    c.construction.path_start_y = next.y;
    set_mode_preserving_path(c, mode, true);
    state.backtrack_saved_mode = saved;
    if (rebuild_queue) {
        auto* owner = queue_owner ? queue_owner : c.construction.park_object;
        if (owner && !owner->base.header.retirement_pending)
            rebuild_park_object_queue_layout(c, owner);
    }
    refresh_track(state.path_points.size() == 1);
    return true;
}

std::uint8_t ParkConstructionRuntime::pointer_up(ConstructionWorld& c,
                                                 WorldCellCoordinates p,
                                                 WorldInputModifierMask modifiers,
                                                 bool use_preview_coordinates) {
    if (!live_map_input(c))
        return 0;
    if (c.construction.mode != backtrack_mode) {
        const auto result = dispatch(c, p, modifiers, use_preview_coordinates);
        if (!state.validation_failed && bounds(p))
            state.last_pointer_cell = cell_id(p);
        return result;
    }
    if (state.relocation)
        state.relocation->cursor_event_seen = true;
    state.mouse_down_pending = false;
    const auto saved = state.backtrack_saved_mode;
    auto* queue_owner = c.construction.park_object;
    const auto refresh_links = track_backtrack(c, state, p);
    auto current = WorldCellCoordinates{ c.construction.path_start_x, c.construction.path_start_y };
    auto target = p;
    if (refresh_links) {
        const auto anchor = primary_anchor(state);
        current = snap_track(current, anchor);
        target = snap_track(target, anchor);
    }
    if (!bounds(current) || !bounds(target) || state.path_points.empty())
        throw std::logic_error("Reverse pointer owner has no valid saved path");
    auto endpoint = state.path_points.back();
    const int scale = refresh_links ? 2 : 1;
    std::size_t removed{};
    while (!same(current, target)) {
        endpoint = state.path_points.back();
        //Native4D303A..4D306C has a distinct equality branch for EACH axis.
        //Hex-Rays incorrectly collapses the X equality branch in this worker.
        const auto dx = sign(endpoint.x - current.x) * scale;
        const auto dy = sign(endpoint.y - current.y) * scale;
        std::size_t steps{};
        while (!same(current, endpoint) && !same(current, target)) {
            current.x += dx;
            current.y += dy;
            if ((!dx && !dy) || ++steps > map_cell_count)
                throw std::length_error("Invalid reverse pointer segment");
        }
        if (!same(current, target)) {
            if (!backtrack_path_step(c, queue_owner, refresh_links) || ++removed > map_cell_count)
                throw std::logic_error("Reverse pointer target is outside its saved path");
        }
    }
    c.construction.path_start_x = endpoint.x;
    c.construction.path_start_y = endpoint.y;
    std::uint8_t result{};
    if (!same(current, endpoint)) {
        c.construction.mode = saved;
        result = dispatch(c, target, modifiers, false);
    }
    if (!state.validation_failed && bounds(p))
        state.last_pointer_cell = cell_id(p);
    return result;
}

bool ParkConstructionRuntime::backtrack_current_path(ConstructionWorld& c) {
    return backtrack_path_step(c, nullptr, true);
}

bool ParkConstructionRuntime::resume_shortcut_tool(ConstructionWorld& c,
                                                   std::optional<WorldCellCoordinates> pointer_cell) {
    const auto requested = std::exchange(state.shortcut_rearm_mode, std::nullopt);
    if (!requested
        || c.construction.mode
        || !std::holds_alternative<DefaultWorldInteraction>(c.staff.interaction.handler.value))
        return false;
    const auto intent = c.staff.interaction.pointer_mode_code;
    if ((*requested == 1 && intent != 2) || (*requested == 58 && intent != 3))
        return false; //A newer explicit selection supersedes delayed activation.
    //4D6160 previews the actual cached cursor before recording activation.
    const auto cursor = c.staff.interaction.cursor.cell;
    if (!pointer_cell && cursor && cursor <= map_cell_count) {
        const auto zero = static_cast<std::uint32_t>(cursor) - 1u;
        pointer_cell =
            WorldCellCoordinates{ static_cast<std::int32_t>(zero & 127u), static_cast<std::int32_t>(zero >> 7u) };
    }
    if (!c.simulation.current
        || !pointer_cell
        || !is_world_map_coordinate_in_bounds(pointer_cell->x, pointer_cell->y)) {
        state.shortcut_rearm_mode = requested;
        return false;
    }
    (void)preview(c, *pointer_cell, WorldInputModifierMask::none);
    set_mode(c, *requested, true);
    return true;
}
} //namespace simcoaster
