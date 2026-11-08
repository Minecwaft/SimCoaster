#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_save.hpp"
#include <utility>

namespace simcoaster {
namespace {
using namespace construction_detail;

void append_point(ParkConstructionState& state, WorldCellCoordinates p) {
    if (state.path_points.size() <= 1024) {
        state.path_points.push_back(p);
    }
}

WorldCellCoordinates major_axis(WorldCellCoordinates start, WorldCellCoordinates p) {
    const auto dx = std::abs(static_cast<std::int64_t>(p.x) - start.x);
    if (dx >= std::abs(static_cast<std::int64_t>(p.y) - start.y)) {
        p.y = start.y;
    }
    if (dx <= std::abs(static_cast<std::int64_t>(p.y) - start.y)) {
        p.x = start.x;
    }
    return p;
}

void record_click(ConstructionWorld& c, WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized) {
        return;
    }
    if (!c.construction.action_record_stream) {
        throw std::logic_error("construction recorder stream is not bound");
    }
    //ACTION_LMB_UP=20 and ACTION_LMB_UP_MODIFIED=45, followed by packed
    //BYTE coordinates and, for the latter, the native modifier WORD.
    const auto value = std::to_underlying(modifiers);
    const std::array bytes { static_cast<std::byte>(value ? 45 : 20), static_cast<std::byte>(p.x), static_cast<std::byte>(p.y) };
    (void)c.construction.action_record_stream->write(bytes);
    if (value) {
        const auto word = save::detail::encode(static_cast<std::uint16_t>(value));
        (void)c.construction.action_record_stream->write(word);
    }
}
} //namespace

void ParkConstructionRuntime::finish_placement(ConstructionWorld& c, const ParkPlacementResult& placed, bool alternate) {
    using namespace construction_detail;
    auto* owner = object(c, placed.created_object);
    if (!owner) {
        throw std::logic_error("new construction owner is missing");
    }
    if (!state.queue_placement_enabled || !bounds(placed.entrance)) {
        set_mode(c, 0);
        return;
    }
    auto& entrance = map(c, cell_id(placed.entrance)).base;
    const auto outward = opposite_world_cell_direction(placed.entrance_direction);
    or_world_cell_neighbor_mask_bits(c, &entrance, outward);
    const auto adjacent = stepped(placed.entrance, outward);
    if (!bounds(adjacent)) {
        throw std::out_of_range("placed entrance approach");
    }
    state.entry_neighbor = &map(c, cell_id(adjacent));
    if (can_change_world_cell_type(c, &state.entry_neighbor->base, 3)) {
        set_world_cell_direction(&state.entry_neighbor->base, placed.entrance_direction);
    }
    const auto selector = construction_track_type(c);
    if (selector == 1 || selector == 2) {
        //Initial13/18 station spans two root cells. Editing begins beyond it;
        //the opposite anchor receives a second ordinary root and reciprocal bit.
        auto anchor = placed.placement_origin;
        state.direction = object_anchor(anchor, state.rotation_degrees);
        WorldCellCoordinates station {}, station_end {}, secondary {};
        match (state.rotation_degrees) {
            case 0 => {
                station = { anchor.x - 4, anchor.y };
                station_end = { anchor.x - 2, anchor.y };
                secondary = station;
            }
            case 90 => {
                station = { anchor.x, anchor.y + 4 };
                station_end = { anchor.x, anchor.y + 2 };
                secondary = { anchor.x, anchor.y + 5 };
            }
            case 180 => {
                station = { anchor.x + 4, anchor.y };
                station_end = { anchor.x + 2, anchor.y };
                secondary = { anchor.x + 5, anchor.y };
            }
            case 270 => {
                station = { anchor.x, anchor.y - 4 };
                station_end = { anchor.x, anchor.y - 2 };
                secondary = station;
            }
            case _ => throw std::invalid_argument("construction track orientation");
        } const auto station_type = selector == 1 ? 13u : 18u;
        for (const auto operation : std::array { station_type, 128u, 130u, 131u }) {
            (void)track_command_line(c, operation, station, station_end);
        }
        c.construction.interaction_cursor_anchor = { station.x, station.y };
        if (!bounds(anchor) || !bounds(secondary)) {
            throw std::out_of_range("construction track anchors");
        }
        state.track = &track(c, cell_id(anchor));
        state.secondary_track = &track(c, cell_id(secondary));
        auto end = anchor;
        const auto mode = selector == 1 ? 11 : 16;
        set_mode(c, mode);
        c.construction.path_start_x = anchor.x;
        c.construction.path_start_y = anchor.y;
        for (const auto operation : std::array { 135u, static_cast<unsigned>(mode), 128u }) {
            (void)track_command_line(c, operation, anchor, end);
        }
        set_world_cell_direction(&track(c, cell_id(end)).base, state.direction);
        for (const auto operation : std::array { 131u, 133u, 134u, 129u }) {
            (void)track_command_line(c, operation, anchor, end);
        }
        state.path_points.clear();
        append_point(state, end);
        refresh_origin(c);
        auto opposite = WorldCellCoordinates { c.construction.interaction_secondary_anchor[0], c.construction.interaction_secondary_anchor[1] };
        auto opposite_end = opposite;
        for (const auto operation : std::array { 135u, static_cast<unsigned>(mode), 128u }) {
            (void)track_command_line(c, operation, opposite, opposite_end);
        }
        auto& opposite_cell = track(c, cell_id(opposite_end)).base;
        const auto direction = c.construction.interaction_cursor_anchor[0] > opposite_end.x ? 4u : c.construction.interaction_cursor_anchor[0] < opposite_end.x ? 64u : c.construction.interaction_cursor_anchor[1] > opposite_end.y ? 16u : 1u;
        or_world_cell_neighbor_mask_bits(c, &opposite_cell, static_cast<std::uint8_t>(direction));
        set_world_cell_direction(&opposite_cell, static_cast<std::uint8_t>(direction));
        if (auto* next = resolve_track_cell_in_direction(c, &opposite_cell, static_cast<std::uint8_t>(direction), 2)) {
            or_world_cell_neighbor_mask_bits(c, &next->base, opposite_world_cell_direction(static_cast<std::uint8_t>(direction)));
        }
        for (const auto operation : std::array { 131u, 133u, 134u }) {
            (void)track_command_line(c, operation, opposite, opposite_end);
        }
        owner->is_track_ride_valid = 0;
        return;
    }
    finish_queue_placement(c, placed, alternate);
}

void ParkConstructionRuntime::finish_queue_placement(ConstructionWorld& c, const ParkPlacementResult& placed, bool alternate) {
    using namespace construction_detail;
    auto* owner = object(c, placed.created_object);
    if (!owner || !bounds(placed.entrance)) {
        throw std::logic_error("Queue placement has no live entrance owner");
    }
    state.connection_placement_active = true; //Native662027, before connecting the queue.
    const auto outward = opposite_world_cell_direction(placed.entrance_direction);
    const auto adjacent = stepped(placed.entrance, outward);
    if (!bounds(adjacent)) {
        throw std::out_of_range("Queue placement approach");
    }
    if (alternate && (construction_track_type(c) == 1 || construction_track_type(c) == 2)) {
        auto& entrance = map(c, cell_id(placed.entrance)).base;
        or_world_cell_neighbor_mask_bits(c, &entrance, outward);
        auto& approach = map(c, cell_id(adjacent)).base;
        if (can_change_world_cell_type(c, &approach, 3)) {
            set_world_cell_direction(&approach, placed.entrance_direction);
        }
    }
    set_mode(c, 3);
    c.construction.path_start_x = adjacent.x;
    c.construction.path_start_y = adjacent.y;
    state.path_points.clear();
    append_point(state, adjacent);
    state.entry_neighbor = nullptr;
    for (const auto operation : std::array { 133u, 134u, 131u, 129u }) {
        (void)command_line(c, operation, adjacent, adjacent);
    }
    rebuild_park_object_queue_layout(c, owner);
    c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
    begin_park_object_connection_camera(c, owner);
    if (map(c, cell_id(adjacent)).base.common.type == 1 || (!alternate && c.mode.game == GameMode::code_3)) {
        set_mode(c, 0);
    }
}

std::uint8_t ParkConstructionRuntime::dispatch(ConstructionWorld& c, WorldCellCoordinates p, WorldInputModifierMask modifiers, bool use_preview) {
    using namespace construction_detail;
    if (state.relocation) {
        state.relocation->cursor_event_seen = true;
    }
    if (c.construction.mode == 21 || c.construction.mode == 22) {
        if (!c.construction.editor) {
            throw std::logic_error("coaster editor owner is not bound");
        }
        return c.construction.editor->mouse_up(c, *this, p, modifiers, use_preview);
    }
    if (use_preview) {
        p = state.preview_coordinates;
    }
    if (!bounds(p)) {
        return 0;
    }
    //Native660D80 LABEL17 skips synthetic LMB_UP during source hoarding
    //construction, while retaining the genuine SET_MODE/selection operations.
    if ((!state.status_latch && !c.construction.world_hoarding_mode) || c.construction.mode == 22) {
        record_click(c, p, modifiers);
    }
    const bool mouse_down_pending = std::exchange(state.mouse_down_pending, false);
    if (state.status_latch && c.construction.mode != 56) {
        if (!c.construction.action_recording_disabled && mouse_down_pending) {
            (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 175, p.x * 10, 0, p.y * 10);
        }
        if (c.construction.mode == 1 || c.construction.mode == 3 || c.construction.mode == 11 || c.construction.mode == 14 || c.construction.mode == 16 || c.construction.mode == 19) {
            set_mode(c, 0);
        }
        return 0;
    }
    const auto previous_owner = c.construction.park_object;
    refresh_origin(c);
    c.construction.park_object = previous_owner;
    const auto mode = c.construction.mode;
    WorldCellCoordinates start { c.construction.path_start_x, c.construction.path_start_y };
    if (mode == 0) {
        return 0;
    }
    if (mode == 14 || mode == 19) {
        dispatch_context_track_edit(c, p);
        return 0;
    }
    if (mode == 26) {
        dispatch_context_hoarding_edit(c, p);
        return 0;
    }
    if (mode == std::to_underlying(WorldInteractionMode::backtrack_preview)) {
        return 0; //Native660D80 has no mouse-up mutation for temporary mode139.
    }
    if (mode == std::to_underlying(WorldInteractionMode::demolish_rectangle)) {
        if (start.x == -1 && start.y == -1) {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
        } else {
            (void)demolish_rectangle(c, start, p);
            set_mode(c, 0);
        }
        return 0;
    }
    if (mode == 25) {
        if (start.x == -1 && start.y == -1) {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
            state.path_points.clear();
            append_point(state, p);
            return 0;
        }
        p = major_axis(start, p);
        if (p.x != start.x || p.y != start.y || (p.x == c.construction.hoarding_chain_start_x && p.y == c.construction.hoarding_chain_start_y)) {
            for (const auto operation : std::array { 25u, 128u, 130u, 129u }) {
                (void)track_command_line(c, operation, start, p);
            }
            append_point(state, p);
        }
        if (p.x == start.x && p.y == start.y) {
            c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
            set_mode(c, 0);
        } else {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
        }
        return 0;
    }
    if (mode == 57) {
        bind_land_boundary_anchors(c);
        p = snap_land_boundary_corner(c, p);
        if (start.x == -1 && start.y == -1) {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
        } else {
            (void)expand_land_rectangle(c, start, p);
            set_mode(c, 0);
            c.staff.interaction.cursor_presentation = 0;
        }
        return 0;
    }
    if (mode == 51) {
        state.entry_neighbor = nullptr;
        state.track = state.secondary_track = nullptr;
        state.rotation_degrees = 0;
        (void)demolish(c, p);
        return 0;
    }
    if (mode == 50) {
        if (map(c, cell_id(p)).base.common.type != 4) {
            (void)command_line(c, 0, p, p);
            (void)command_line(c, 134, p, p);
        }
        return 0;
    }
    if (mode == 52) {
        if (start.x != -1 || start.y != -1) {
            (void)command_line(c, 0, start, p);
            (void)command_line(c, 134, start, p);
        }
        c.construction.path_start_x = p.x;
        c.construction.path_start_y = p.y;
        return 0;
    }
    if (mode == 53) {
        if (start.x != -1 || start.y != -1) {
            (void)track_command_line(c, 50, start, p);
        }
        c.construction.path_start_x = p.x;
        c.construction.path_start_y = p.y;
        return 0;
    }
    if (mode == 59) {
        dispatch_object_relocation(c, p, modifiers);
        return 0;
    }
    if (mode == 4) {
        const auto result = place(c, c.construction.selected_object_definition_id, p, state.rotation_degrees);
        if (result && std::to_underlying(modifiers) != 12) {
            state.rotation_degrees = 0;
        }
        return 0;
    }
    if (mode == 20) {
        if (!c.construction.park_object) {
            throw std::logic_error("queue edit owner missing");
        }
        trace_queue(c, *c.construction.park_object);
        rebuild_park_object_queue_layout(c, c.construction.park_object);
        set_mode_preserving_path(c, 3);
        return 0;
    }
    if (mode == 1 || mode == 3) {
        //Native664734 plays for both the free seed and subsequent line clicks.
        if (!c.construction.action_recording_disabled) {
            (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 101, p.x * 10, 0, p.y * 10);
        }
        bool resume_connection = false;
        if ((state.queue_joined || (start.x == p.x && start.y == p.y)) && c.construction.park_object && state.connection_placement_active) {
            auto* owner = c.construction.park_object;
            const auto& exit = map(c, owner->exit_cell).base;
            const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &exit, static_cast<std::uint8_t>(get_world_cell_neighbor_mask(c, &exit)));
            if (!neighbor) {
                throw std::logic_error("Connection placement exit has no neighbor");
            }
            if (!has_clear_entry_exit_path(c, *owner) && cell_id(p) != neighbor->base.index) {
                if (static_cast<double>(c.balance.standard_supplement.auto_rotate.zoom_level) > c.view.mobile.camera_zoom) {
                    focus_world_view_camera_on_park_object_exit(c, owner, true);
                }
                resume_connection = true;
            }
        }
        const auto finish_connection = [&] {
            if (resume_connection) {
                resume_object_connection_placement(c, p);
            }
        };
        state.candidate_overwrite_priority = mode == 1 ? 2 : 3;
        if (start.x == -1 && start.y == -1) {
            //Validate a fresh quote; the hover accumulator is not another cell.
            state.cost = 0;
            if (evaluate(c, map(c, cell_id(p)).base, static_cast<CellInteractionRequest>(mode)) != CellInteractionResult::rejected) {
                c.construction.path_start_x = p.x;
                c.construction.path_start_y = p.y;
                state.path_points.clear();
                append_point(state, p);
            }
            finish_connection();
            return 0;
        }
        p = major_axis(start, p);
        const bool same = start.x == p.x && start.y == p.y;
        bool stopped = false;
        if (!same || state.path_points.size() == 1) {
            state.cost = 0;
            state.validation_failed = state.stop = state.existing_endpoint = false;
            if (!command_line(c, 512u | static_cast<unsigned>(mode), start, p)) {
                finish_connection();
                return 0;
            }
            append_point(state, p);
            for (const auto operation : std::array { 135u, static_cast<unsigned>(mode), 128u, 130u }) {
                (void)command_line(c, operation, start, p);
            }
            state.entry_neighbor = nullptr;
            for (const auto operation : std::array { 133u, 134u, 131u }) {
                (void)command_line(c, operation, start, p);
            }
            stopped = !command_line(c, 129, start, p);
        }
        if (mode == 3 && c.construction.park_object) {
            rebuild_park_object_queue_layout(c, c.construction.park_object);
            if (!c.construction.action_recording_disabled && state.queue_joined) {
                (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 139, p.x * 10, 0, p.y * 10);
            }
        }
        if (!same && !stopped) {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
        } else {
            state.queue_join_override = false;
            c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
            set_mode(c, 0);
        }
        finish_connection();
        return 0;
    }
    if (mode == 11 || mode == 16) {
        if (start.x == -1 && start.y == -1) {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
            state.path_points.clear();
            append_point(state, p);
            world_operations::queue_scored_advisor_action(c, 165);
            return 0;
        }
        p = major_axis(start, p);
        bool same = start.x == p.x && start.y == p.y, stopped = false;
        if (!same || (p.x == c.construction.interaction_cursor_anchor[0] && p.y == c.construction.interaction_cursor_anchor[1])) {
            state.cost = 0;
            state.validation_failed = state.stop = state.existing_endpoint = false;
            auto checked = p;
            if (!track_command_line(c, 512u | static_cast<unsigned>(mode), start, checked)) {
                return 0;
            }
            p = checked;
            for (const auto operation : std::array { 135u, static_cast<unsigned>(mode), 128u, 130u, 131u, 133u, 134u }) {
                (void)track_command_line(c, operation, start, p);
            }
            stopped = !track_command_line(c, 129, start, p);
            append_point(state, p);
            same = start.x == p.x && start.y == p.y;
        }
        auto* owner = c.construction.park_object;
        if (!owner) {
            throw std::logic_error("track edit owner missing");
        }
        if (same || stopped) {
            owner->is_track_ride_valid = refresh_track_path(c);
            if (owner->is_track_ride_valid && is_park_object_open_eligible(c, *owner)) {
                open_park_object(c, *owner);
            }
            (void)reconcile_track_links(c, *owner, false, true, false, false);
            (void)reconcile_track_links(c, *owner, true, false, false, false);
            c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
            if (state.entry_neighbor) {
                const auto queue_start = coordinate(state.entry_neighbor->base.index);
                set_mode(c, 3);
                c.construction.path_start_x = queue_start.x;
                c.construction.path_start_y = queue_start.y;
                state.path_points.clear();
                append_point(state, queue_start);
                state.entry_neighbor = nullptr;
                rebuild_park_object_queue_layout(c, owner);
            } else if (std::to_underlying(modifiers) != 12) {
                set_mode(c, 0);
            }
        } else {
            c.construction.path_start_x = p.x;
            c.construction.path_start_y = p.y;
            (void)reconcile_track_links(c, *owner, false, true, false, false);
            (void)reconcile_track_links(c, *owner, true, true, false, false);
        }
        return 0;
    }
    throw std::logic_error("interaction mode is outside the compiled park-object " "construction workflow");
}
} //namespace simcoaster
