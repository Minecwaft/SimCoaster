#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/park_object_context_geometry.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world_park_admission.hpp"
#include <bit>
#include <bitset>

namespace simcoaster {
namespace {
using namespace construction_detail;
constexpr std::array<std::uint8_t, 4> cardinals { 1, 4, 16, 64 };

TrackCell& normalized(ConstructionWorld& c, TrackCell& cell) {
    return world_cell_is_track_child(&cell.base) ? track(c, cell.base.common.parent_id) : cell;
}

std::int16_t segment(ConstructionWorld& c, TrackCell& cell) {
    return std::bit_cast<std::int16_t>(normalized(c, cell).segment_number);
}

bool straight_or_corner(ConstructionWorld& c, const CellBase& cell) {
    const auto neighbors = get_world_cell_neighbor_mask(c, &cell);
    const bool straight = count_world_cell_cardinal_neighbor_bits(c, &cell) == 2 && (((neighbors & 1u) && (neighbors & 16u)) || ((neighbors & 4u) && (neighbors & 64u)));
    return straight || get_world_cell_corner_neighbor_mask(c, &cell);
}

bool cardinal(std::uint8_t direction) {
    return std::ranges::find(cardinals, direction) != cardinals.end();
}

//Native686A40. Link repair uses raw track coordinates, not a terrain-cell
//receiver or a normalized parent. Secondary-anchor repair removes only the
//connections absent from its retained direction.
void repair_neighbor_links(ConstructionWorld& c, WorldCellCoordinates p, std::uint8_t override_direction = 0) {
    if (!bounds(p)) {
        throw std::out_of_range("Track edit link repair coordinate");
    }
    auto& cell = track(c, cell_id(p)).base;
    const bool secondary = p.x == c.construction.interaction_secondary_anchor[0] && p.y == c.construction.interaction_secondary_anchor[1];
    const auto direction = get_world_cell_direction(&cell);
    const auto mask = static_cast<std::uint8_t>(secondary ? (~direction & get_world_cell_neighbor_mask(c, &cell)) : (override_direction ? override_direction : direction));
    if (mask) {
        clear_world_cell_neighbor_mask_bits(c, &cell, mask);
        update_world_cell_mesh(c, &cell);
        if (cell.common.overlap_counter) {
            cell.common.overlap_counter = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(cell.common.overlap_counter) - 1u));
        }
        if (auto* neighbor = resolve_track_cell_in_direction(c, &cell, mask, 2)) {
            clear_world_cell_neighbor_mask_bits(c, &neighbor->base, opposite_world_cell_direction(mask));
            update_world_cell_mesh(c, &neighbor->base);
        }
    }
    if (secondary) {
        return;
    }
    const WorldCellCoordinates primary { c.construction.interaction_cursor_anchor[0], c.construction.interaction_cursor_anchor[1] };
    const auto dx = std::abs(p.x - primary.x), dy = std::abs(p.y - primary.y);
    if (!bounds(primary) || !((dx == 2 && dy == 0) || (dx == 0 && dy == 2))) {
        return;
    }
    auto& anchor = track(c, cell_id(primary)).base;
    //Preserve native ordering: all kart-station candidates precede all water
    //candidates, including each resulting mesh update.
    for (const auto type : { 13, 18 }) {
        for (const auto scan : cardinals) {
            if (const auto* neighbor = resolve_track_cell_in_direction(c, &anchor, scan, 2); neighbor && neighbor->base.common.type == type) {
                or_world_cell_neighbor_mask_bits(c, &anchor, scan);
                update_world_cell_mesh(c, &anchor);
            }
        }
    }
}

//Native674CA0 with prune_branches=false, the variant used by endpoint edits.
void reconcile_anchor(ConstructionWorld& c) {
    const WorldCellCoordinates p { c.construction.interaction_cursor_anchor[0], c.construction.interaction_cursor_anchor[1] };
    if (!bounds(p)) {
        return;
    }
    auto& anchor = track(c, cell_id(p)).base;
    if (!world_cell_is_track_root(&anchor)) {
        return;
    }
    for (const auto direction : cardinals) {
        auto* neighbor = resolve_track_cell_in_direction(c, &anchor, direction, 2);
        if (get_world_cell_neighbor_mask(c, &anchor) & direction) {
            if (!neighbor || (!world_cell_is_track_root(&neighbor->base) && !track_variant(c, neighbor->base)) || (world_cell_is_track_root(&neighbor->base) && !(get_world_cell_neighbor_mask(c, &neighbor->base) & opposite_world_cell_direction(direction)))) {
                clear_world_cell_neighbor_mask_bits(c, &anchor, direction);
                update_world_cell_mesh(c, &anchor);
            }
        } else if (neighbor && track_variant(c, neighbor->base) && neighbor->base.common.parent_id == anchor.common.parent_id) {
            set_world_cell_direction(&anchor, opposite_world_cell_direction(direction));
            or_world_cell_neighbor_mask_bits(c, &anchor, direction);
            update_world_cell_mesh(c, &anchor);
        }
    }
}

//Native5E1BF0 deliberately does not submit the ordinary park-object closing
//event. A track made incomplete must disable admission even when already shut.
void disable_admission(ConstructionWorld& c, ParkObjectRuntimeView& owner) {
    owner.can_load = 0;
    owner.person_being_loaded = 0;
    world_operations::set_script_slot(c, world_operations::find_script(c, owner.script_id), 6, 1);
    world_operations::select_park_object_mesh_animation_variant(c, owner.mesh_instance_id, ParkObjectMeshVariant::closed);
}
} //namespace

namespace construction_detail {
//Native65DBB0: a two-cell attached object can contribute a neighboring root
//endpoint. Preserve the original root while scanning all four directions.
void adjust_track_endpoint(ConstructionWorld& c, WorldCellCoordinates& p) {
    auto& root = normalized(c, track(c, cell_id(p))).base;
    if (!(root.common.flags & 16u)) {
        return;
    }
    const auto parent = map(c, root.index).base.common.parent_id;
    for (const auto direction : cardinals) {
        if (const auto* neighbor = resolve_track_cell_in_direction(c, &root, direction, 2)) {
            if ((neighbor->base.common.flags & 16u) && map(c, neighbor->base.index).base.common.parent_id == parent && world_cell_is_track_root(&neighbor->base)) {
                p = stepped(p, direction, 2);
            }
        }
    }
}
} //namespace construction_detail

void ParkConstructionRuntime::begin_context_queue_edit(ConstructionWorld& c, ParkObjectRuntimeView& owner) {
    select_parent_origin(c, state, owner);
    set_mode(c, 20, true);
    initialize_context_mode(c);
}

void ParkConstructionRuntime::begin_context_track_edit(ConstructionWorld& c, ParkObjectRuntimeView& owner, CellId fresh_cursor) {
    const auto type = definition(c, owner.definition_id).which_track_type;
    if (type == 3) {
        if (!c.construction.editor) {
            throw std::logic_error("Coaster editor owner is not bound");
        }
        (void)adjust_park_object_context_coaster_camera(c, owner, fresh_cursor);
        c.construction.editor->activate_object(c, owner.base.header.id, true);
        return;
    }
    if (type != 1 && type != 2) {
        throw std::invalid_argument("Object has no context-editable track");
    }
    (void)adjust_park_object_context_track_camera(c, owner, fresh_cursor);
    select_parent_origin(c, state, owner);
    set_construction_track_type(c, static_cast<std::uint32_t>(type), true);
    set_mode(c, type == 1 ? 14 : 19, true);
    initialize_context_mode(c);
}

void ParkConstructionRuntime::initialize_context_mode(ConstructionWorld& c) {
    auto* owner = c.construction.park_object;
    if (!owner || owner->base.header.retirement_pending) {
        throw std::logic_error("Context construction owner is missing or retired");
    }
    const auto mode = c.construction.mode;
    if (mode != 20 && mode != 14 && mode != 19) {
        throw std::invalid_argument("Not a context construction entry mode");
    }
    state.status_latch = state.validation_failed = state.stop = false;
    if (mode == 20) {
        trace_queue(c, *owner);
        rebuild_park_object_queue_layout(c, owner);
        set_mode_preserving_path(c, 3);
        return;
    }
    state.track_preview = {};
    if (!owner->is_track_ride_valid) {
        trace_track(c, *owner, false);
        set_mode_preserving_path(c, mode == 14 ? 11 : 16);
    }
    //Native660D80's zero-coordinate entry request reaches these link updates,
    //but is not user selection of map cell0,0 in this typed API.
    (void)reconcile_track_links(c, *owner, false, true, false, false);
    (void)reconcile_track_links(c, *owner, true, true, false, false);
}

bool ParkConstructionRuntime::preview_context_track_edit(ConstructionWorld& c, WorldCellCoordinates cursor) {
    auto* owner = c.construction.park_object;
    if (!owner || !c.construction.origin_cell) {
        return true;
    }
    const auto& d = definition(c, owner->definition_id);
    state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    set_construction_track_type(c, static_cast<std::uint32_t>(d.which_track_type), true);
    const auto mode = d.which_track_type == 1 ? 14 : 19;
    if (c.construction.mode != mode) {
        set_mode_preserving_path(c, mode, true);
    }
    state.object_rotation_degrees = std::bit_cast<int>(owner->angle);
    auto offset = state.origin, secondary = state.origin;
    c.construction.interaction_cursor_anchor = { state.origin.x, state.origin.y };
    state.direction = object_anchor(offset, state.object_rotation_degrees);
    (void)object_anchor(secondary, state.object_rotation_degrees, true);
    c.construction.interaction_secondary_anchor = { secondary.x, secondary.y };
    const auto snap = [](int value, int origin) {
        auto delta = value - origin;
        if (delta < 0 && delta % 2) {
            --delta;
        }
        return origin + 2 * (delta / 2);
    };
    auto p = WorldCellCoordinates { snap(cursor.x, offset.x), snap(cursor.y, offset.y) };
    state.preview_coordinates = p;
    if (!bounds(p)) {
        state.validation_failed = state.status_latch = true;
        return false;
    }
    if (state.track_preview.start_segment == -1) {
        auto end = p;
        adjust_track_endpoint(c, end);
        (void)track_command_line(c, 256u | static_cast<unsigned>(mode), p, end);
    }
    if (!owner->is_track_ride_valid) {
        state.status_latch = state.validation_failed = state.stop = false;
        trace_track(c, *owner, false);
        state.preview_reference_reset = WorldCellCoordinates { c.construction.path_start_x, c.construction.path_start_y };
        return true;
    }
    if (state.track_preview.start_segment != -1) {
        auto& raw = track(c, cell_id(p));
        auto end = p;
        adjust_track_endpoint(c, end);
        if (bounds(end) && segment(c, track(c, cell_id(end))) > segment(c, raw)) {
            p = end;
        }
        state.track_preview.end = cell_id(p);
        state.track_preview.end_segment = segment(c, track(c, cell_id(p)));
        if (evaluate_track(c, track(c, cell_id(p)).base, mode) != TrackInteractionResult::track_endpoint) {
            state.track_preview.end = state.track_preview.start;
            state.track_preview.end_segment = state.track_preview.start_segment;
            state.status_latch = state.validation_failed = state.stop = false;
        }
        auto& start_cell = track(c, state.track_preview.start);
        auto& end_cell = track(c, state.track_preview.end);
        if (state.track_preview.start_segment > state.track_preview.end_segment) {
            (void)process_context_track_chain(c, end_cell, start_cell, false);
        } else {
            (void)process_context_track_chain(c, start_cell, end_cell, false);
        }
    }
    return !state.status_latch && !state.validation_failed;
}

void ParkConstructionRuntime::dispatch_context_track_edit(ConstructionWorld& c, WorldCellCoordinates p) {
    auto* owner = c.construction.park_object;
    if (!owner) {
        throw std::logic_error("Context track edit owner is missing");
    }
    const auto mode = c.construction.mode;
    if (!owner->is_track_ride_valid) {
        trace_track(c, *owner, false);
        set_mode_preserving_path(c, mode == 14 ? 11 : 16);
        state.preview_coordinates = p;
    } else {
        auto& raw = track(c, cell_id(p));
        auto& selected = normalized(c, raw);
        if (!track_variant(c, selected.base)) {
            auto adjusted = p;
            adjust_track_endpoint(c, adjusted);
            if (state.track_preview.start_segment == -1) {
                if (bounds(adjusted) && segment(c, track(c, cell_id(adjusted))) < segment(c, selected)) {
                    state.preview_coordinates = adjusted;
                }
                state.track_preview.start = cell_id(p);
                state.track_preview.start_segment = segment(c, selected);
                if (evaluate_track(c, selected.base, 14) == TrackInteractionResult::rejected) {
                    state.track_preview.start = 0;
                    state.track_preview.start_segment = -1;
                }
                owner->is_track_ride_valid = refresh_track_path(c, true);
                if (!owner->is_track_ride_valid) {
                    set_mode(c, 0);
                } else if (is_park_object_open_eligible(c, *owner)) {
                    open_park_object(c, *owner);
                }
                return;
            }
            if (bounds(adjusted) && segment(c, track(c, cell_id(adjusted))) > segment(c, selected)) {
                state.preview_coordinates = adjusted;
            }
            state.track_preview.end = cell_id(p);
            state.track_preview.end_segment = segment(c, raw);
            if (evaluate_track(c, selected.base, 14) == TrackInteractionResult::track_endpoint) {
                auto& start = track(c, state.track_preview.start);
                if (state.track_preview.start_segment > state.track_preview.end_segment) {
                    (void)process_context_track_chain(c, raw, start, true);
                } else {
                    (void)process_context_track_chain(c, start, raw, true);
                }
                reconcile_anchor(c);
                if (!reconcile_track_links(c, *owner, false, true, false, false)) {
                    (void)reconcile_track_links(c, *owner, true, false, false, false);
                }
                state.track_preview = {};
                const auto end = coordinate(selected.base.index);
                c.construction.path_start_x = end.x;
                c.construction.path_start_y = end.y;
                owner->is_track_ride_valid = refresh_track_path(c);
                if (owner->is_track_ride_valid) {
                    animate_track_endpoint(c, *owner, false, true);
                    if (is_park_object_open_eligible(c, *owner)) {
                        open_park_object(c, *owner);
                    }
                    set_mode_preserving_path(c, 0);
                } else {
                    animate_track_endpoint(c, *owner, false, false);
                    animate_track_endpoint(c, *owner, true, false);
                    disable_admission(c, *owner);
                    set_mode(c, 0);
                }
            } else {
                set_mode(c, 0);
            }
        }
    }
    (void)reconcile_track_links(c, *owner, false, true, false, false);
    (void)reconcile_track_links(c, *owner, true, true, false, false);
}

//Native676300: this is the editable polyline trace, distinct from the
//entrance-to-entrance ownership route in trace_land_boundary.
void ParkConstructionRuntime::trace_hoarding_edit_path(ConstructionWorld& c) {
    auto p = WorldCellCoordinates { c.construction.hoarding_chain_start_x, c.construction.hoarding_chain_start_y };
    std::uint8_t direction = 4;
    state.direction = direction;
    state.path_points.clear();
    //The original inclusive1024 guard has an owned1025th endpoint slot.
    const auto append = [&](WorldCellCoordinates point) {
        if (state.path_points.size() <= 1024) {
            state.path_points.push_back(point);
        }
    };
    append(p);
    std::bitset<map_cell_count> visited;
    for (std::size_t steps = 0;; ++steps) {
        if (!bounds(p) || visited.test(cell_id(p) - 1u)) {
            throw std::length_error("Invalid editable hoarding route");
        }
        visited.set(cell_id(p) - 1u);
        const auto& cell = track(c, cell_id(p)).base;
        if (cell.common.type == 25 && get_world_cell_corner_neighbor_mask(c, &cell)) {
            direction = opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &cell);
            append(p);
        }
        const auto* next = resolve_track_cell_in_direction(c, &cell, direction, 2);
        if ((next && next->base.common.type != 25) || (steps && count_world_cell_neighbor_bits(c, &cell) == 1)) {
            break;
        }
        if (!cardinal(direction)) {
            throw std::logic_error("Noncardinal editable hoarding route");
        }
        p = stepped(p, direction, 2);
    }
    append(p);
    c.construction.path_start_x = p.x;
    c.construction.path_start_y = p.y;
}

namespace {
WorldCellCoordinates snap_hoarding_edit(const ParkConstructionState& state, WorldCellCoordinates p) {
    const auto snap = [](int value, int origin) {
        auto delta = value - origin;
        if (delta < 0 && delta % 2) {
            --delta;
        }
        return origin + 2 * (delta / 2);
    };
    return { snap(p.x, state.origin.x), snap(p.y, state.origin.y) };
}
} //namespace

bool ParkConstructionRuntime::preview_context_hoarding_edit(ConstructionWorld& c, WorldCellCoordinates p) {
    if (!c.construction.origin_cell) {
        return true;
    }
    p = snap_hoarding_edit(state, p);
    state.preview_coordinates = p;
    if (!bounds(p)) {
        state.validation_failed = state.status_latch = true;
        return false;
    }
    if (state.track_preview.start_segment == -1) {
        auto end = p;
        (void)track_command_line(c, 256u | 26u, p, end);
    }
    if (!c.construction.world_hoarding_route_valid) {
        trace_hoarding_edit_path(c);
        (void)dispatch(c, { c.construction.path_start_x, c.construction.path_start_y }, WorldInputModifierMask::none);
        if (c.construction.mode == 26) {
            set_mode_preserving_path(c, 25, true);
        }
    } else if (state.track_preview.start_segment != -1) {
        state.track_preview.end = cell_id(p);
        state.track_preview.end_segment = segment(c, track(c, cell_id(p)));
        auto& end = track(c, state.track_preview.end);
        if (evaluate_track(c, end.base, 26) != TrackInteractionResult::track_endpoint) {
            state.track_preview.end = state.track_preview.start;
            state.track_preview.end_segment = state.track_preview.start_segment;
        }
        auto& first = track(c, state.track_preview.start);
        auto& last = track(c, state.track_preview.end);
        if (state.track_preview.start_segment > state.track_preview.end_segment) {
            (void)process_context_track_chain(c, last, first, false);
        } else {
            (void)process_context_track_chain(c, first, last, false);
        }
    }
    return !state.validation_failed && !state.status_latch;
}

void ParkConstructionRuntime::dispatch_context_hoarding_edit(ConstructionWorld& c, WorldCellCoordinates p) {
    //Retained legacy mode26 branch:503FD0 suppresses hoarding-child hover
    //targets, and683900 has no positive type25/op26 endpoint case. Preserve
    //that original rejection rather than inventing an editable World target.
    p = snap_hoarding_edit(state, p);
    if (!bounds(p)) {
        return;
    }
    state.preview_coordinates = p;
    auto& raw = track(c, cell_id(p));
    if (state.track_preview.start_segment == -1) {
        state.track_preview.start = cell_id(p);
        state.track_preview.start_segment = segment(c, raw);
        if (evaluate_track(c, raw.base, 26) == TrackInteractionResult::rejected) {
            state.track_preview = {};
        }
        return;
    }
    state.track_preview.end = cell_id(p);
    state.track_preview.end_segment = segment(c, raw);
    auto& selected = normalized(c, raw);
    if (evaluate_track(c, selected.base, 26) != TrackInteractionResult::track_endpoint) {
        state.track_preview.end = state.track_preview.start;
        state.track_preview.end_segment = state.track_preview.start_segment;
        return;
    }
    auto& first = track(c, state.track_preview.start);
    auto& last = track(c, state.track_preview.end);
    const auto count = state.track_preview.start_segment > state.track_preview.end_segment ? process_context_track_chain(c, last, first, true) : process_context_track_chain(c, first, last, true);
    state.track_preview = {};
    const auto end = coordinate(selected.base.index);
    c.construction.path_start_x = end.x;
    c.construction.path_start_y = end.y;
    c.construction.world_hoarding_route_valid = false;
    if (count != -1) {
        trace_hoarding_edit_path(c);
        set_mode_preserving_path(c, 25);
    }
}

//Native66B6E0 shared hover/removal kernel. Raw coordinates can alias the same
//effective parent more than once; bounds/cycle protection therefore tracks
//the raw visited coordinate, never just the normalized record's index.
std::int32_t ParkConstructionRuntime::process_context_track_chain(ConstructionWorld& c, TrackCell& input_start, TrackCell& input_end, bool reconcile) {
    auto &start = normalized(c, input_start), &end = normalized(c, input_end);
    const auto start_segment = segment(c, start), end_segment = segment(c, end);
    TrackCell *start_boundary = nullptr, *end_boundary = nullptr;
    bool start_branch = false, end_branch = false;
    std::int32_t count = 0;
    if (!reconcile || end_segment >= start_segment || c.construction.mode == 139) {
        auto p = coordinate(start.base.index), span = p;
        WorldCellCoordinates previous { -1, -1 };
        auto direction = get_world_cell_direction(&start.base);
        std::bitset<map_cell_count> visited;
        for (;;) {
            if (!bounds(p) || visited.test(cell_id(p) - 1u)) {
                return -1;
            }
            visited.set(cell_id(p) - 1u);
            auto& cell = normalized(c, track(c, cell_id(p)));
            if (!world_cell_is_track_root(&cell.base)) {
                return -1;
            }
            const bool branch = get_world_cell_branch_neighbor_mask(c, &cell.base) != 0;
            if (reconcile) {
                if (!start_boundary) {
                    if (segment(c, cell) == start_segment) {
                        start_boundary = &cell;
                    } else if (branch) {
                        start_boundary = &cell;
                        start_branch = true;
                    }
                }
                if (segment(c, cell) == end_segment) {
                    end_boundary = &cell;
                    end_branch = false;
                } else if (!end_boundary && branch) {
                    end_boundary = &cell;
                    end_branch = true;
                }
            }
            std::optional<WorldCellCoordinates> span_end;
            if (get_world_cell_corner_neighbor_mask(c, &cell.base)) {
                if (count) {
                    auto incoming = opposite_world_cell_direction(direction);
                    const auto neighbors = get_world_cell_neighbor_mask(c, &cell.base);
                    if (!(neighbors & incoming)) {
                        incoming = direction;
                    }
                    direction = incoming ^ neighbors;
                }
                span_end = previous;
            }
            const bool reached = &cell == &end;
            if (reached) {
                span_end = p;
            }
            if (reconcile) {
                set_world_cell_direction(&cell.base, direction | (branch ? get_world_cell_direction(&cell.base) : 0));
            }
            ++count;
            if (span_end && span_end->x != -1) {
                auto output = *span_end;
                (void)track_command_line(c, 271u, span, output);
                span = p;
            }
            previous = p;
            if (reached) {
                break;
            }
            if (!cardinal(direction)) {
                return -1;
            }
            p = stepped(p, direction, 2);
        }
    }
    if (!reconcile) {
        return count;
    }
    if (start_boundary && end_boundary && start_boundary != end_boundary) {
        auto p = coordinate(start_boundary->base.index);
        auto direction = get_world_cell_direction(get_world_cell_branch_neighbor_mask(c, &start_boundary->base) ? &start.base : &start_boundary->base);
        std::bitset<map_cell_count> visited;
        for (;;) {
            if (!bounds(p) || visited.test(cell_id(p) - 1u)) {
                throw std::length_error("Invalid track edit cleanup chain");
            }
            visited.set(cell_id(p) - 1u);
            auto& cell = normalized(c, track(c, cell_id(p)));
            bool done = !world_cell_is_track_root(&cell.base);
            if (!done) {
                const auto retained = get_world_cell_direction(&cell.base);
                //Native counts cardinal direction bits, preserving diagonals.
                const auto cardinal_count = std::popcount(static_cast<unsigned>(retained & 85u));
                if (cardinal_count == 1) {
                    direction = retained;
                } else {
                    set_world_cell_direction(&cell.base, retained & ~direction);
                }
            }
            done |= &cell == end_boundary;
            if (c.construction.mode != 139 || &cell != start_boundary) {
                const bool branch = get_world_cell_branch_neighbor_mask(c, &cell.base) != 0;
                remove_track_cell_contents(c, &cell.base, branch ? -1 : 0, branch ? -1 : 0, 1);
            }
            if (done) {
                break;
            }
            if (!cardinal(direction)) {
                throw std::logic_error("Noncardinal track edit cleanup direction");
            }
            p = stepped(p, direction, 2);
        }
    }
    const bool connected = straight_or_corner(c, start.base);
    if (c.construction.park_object) {
        prune_track_ride_dynamic_path(c, c.construction.park_object->track_ride_handle);
    }
    if (end_segment - start_segment <= 1 && connected) {
        repair_neighbor_links(c, coordinate(start.base.index));
    }
    if (start_branch && connected) {
        repair_neighbor_links(c, coordinate(start.base.index));
    }
    if (end_branch && connected) {
        repair_neighbor_links(c, coordinate(end.base.index), opposite_world_cell_direction(get_world_cell_direction(&start.base)));
    }
    if (start_boundary && start_boundary == end_boundary && connected) {
        remove_track_cell_contents(c, &start_boundary->base, 0, 0, 1);
    }
    return count;
}
} //namespace simcoaster
