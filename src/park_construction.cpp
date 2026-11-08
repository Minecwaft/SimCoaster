#include "simcoaster/world_object_placement.hpp"
#include "coaster_catalog_recovery.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include <bitset>
#include <cstring>
#include <exception>
#include <map>
#include <set>

namespace simcoaster {
namespace {
using namespace construction_detail;

void record_word(ConstructionWorld& c, std::uint8_t event, std::uint16_t value) {
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized) {
        return;
    }
    if (!c.construction.action_record_stream) {
        throw std::logic_error("construction recording stream is not bound");
    }
    const std::array head { static_cast<std::byte>(event) };
    const auto wire = save::detail::encode(value);
    (void)c.construction.action_record_stream->write(head);
    (void)c.construction.action_record_stream->write(wire);
}

std::uint8_t primary_anchor(WorldCellCoordinates& p, int rotation) {
    match (rotation) {
        case 0 => {
            p.x = fixed::add(p.x, 4);
            p.y = fixed::add(p.y, 1);
            return 4;
        }
        case 90 => {
            p.x = fixed::add(p.x, 1);
            p.y = fixed::subtract(p.y, 5);
            return 1;
        }
        case 180 => {
            p.x = fixed::subtract(p.x, 5);
            p.y = fixed::subtract(p.y, 2);
            return 64;
        }
        case 270 => {
            p.x = fixed::subtract(p.x, 2);
            p.y = fixed::add(p.y, 4);
            return 16;
        }
        case _ => throw std::invalid_argument("park object rotation is not cardinal");
    }
}

void secondary_anchor(WorldCellCoordinates& p, int rotation) {
    match (rotation) {
        case 0 => {
            p.x = fixed::subtract(p.x, 2);
            p.y = fixed::add(p.y, 1);
        }
        case 90 => {
            p.x = fixed::add(p.x, 1);
            p.y = fixed::add(p.y, 1);
        }
        case 180 => {
            p.x = fixed::add(p.x, 1);
            p.y = fixed::subtract(p.y, 2);
        }
        case 270 => {
            p.x = fixed::subtract(p.x, 2);
            p.y = fixed::subtract(p.y, 2);
        }
        case _ => throw std::invalid_argument("park object rotation is not cardinal");
    }
}
} //namespace

namespace construction_detail {
void animate_track_endpoint(ConstructionWorld& c, ParkObjectRuntimeView& owner, bool opposite, bool valid) {
    auto p = WorldCellCoordinates { owner.base.x >> 8, owner.base.y >> 8 };
    auto direction = object_anchor(p, std::bit_cast<int>(owner.angle), opposite);
    std::bitset<map_cell_count> visited;
    while (bounds(p)) {
        auto& row = track(c, cell_id(p)).base;
        if (visited.test(row.index - 1u)) {
            throw std::length_error("cyclic track animation traversal");
        }
        visited.set(row.index - 1u);
        bool connected = true;
        if (world_cell_is_track_root(&row)) {
            if (get_world_cell_corner_neighbor_mask(c, &row)) {
                auto reverse = opposite_world_cell_direction(direction);
                if (!(get_world_cell_neighbor_mask(c, &row) & reverse)) {
                    reverse = direction;
                }
                direction = reverse ^ get_world_cell_neighbor_mask(c, &row);
            }
            if (row.common.mesh_instance) {
                if (!c.resources.meshes) {
                    throw std::logic_error("track animation mesh owner missing");
                }
                const auto result = c.resources.meshes->request(c, row.common.mesh_instance, 0, { { valid ? 5u : 12u, 0 }, 7, 1 });
                if (!result) {
                    throw result.error();
                }
            }
        } else {
            direction = 0;
        }
        if (!get_world_cell_corner_neighbor_mask(c, &row) && !(get_world_cell_neighbor_mask(c, &row) & direction)) {
            connected = false;
        }
        auto* next = resolve_track_cell_in_direction(c, &row, direction, 2);
        if (next && track_variant(c, next->base) && next->base.common.parent_id == row.common.parent_id && (get_world_cell_neighbor_mask(c, &next->base) & opposite_world_cell_direction(direction))) {
            connected = false;
        }
        if (!direction) {
            return;
        }
        p = stepped(p, direction, 2);
        if (!connected) {
            return;
        }
    }
}

void select_parent_origin(ConstructionWorld& c, ParkConstructionState& state, ParkObjectRuntimeView& owner) {
    c.view.secondary_cursor_record_snapshot = &owner.base.header;
    c.construction.park_object = &owner;
    c.construction.origin_cell = static_cast<CellId>((owner.base.y >> 8) * 128u + (owner.base.x >> 8) + 1u);
    state.origin = coordinate(c.construction.origin_cell);
    state.object_rotation_degrees = std::bit_cast<int>(owner.angle);
    auto primary = state.origin, secondary = state.origin;
    state.direction = object_anchor(primary, state.object_rotation_degrees);
    (void)object_anchor(secondary, state.object_rotation_degrees, true);
    c.construction.interaction_cursor_anchor = { primary.x, primary.y };
    c.construction.interaction_secondary_anchor = { secondary.x, secondary.y };
    state.track = &track(c, c.construction.origin_cell);
}
} //namespace construction_detail

void ParkConstructionRuntime::reset_after_restore() {
    state = {};
}

void ParkConstructionRuntime::refresh_origin(ConstructionWorld& c, bool record_action) {
    using namespace construction_detail;
    state.previous_origin = c.construction.origin_cell;
    c.construction.origin_cell = 0;
    state.origin = { -1, -1 };
    state.object_rotation_degrees = -1;
    if ((c.construction.mode == 25 || c.construction.mode == 26) && state.hoarding_origin.x != -1) {
        state.origin = state.hoarding_origin;
        c.construction.origin_cell = cell_id(state.origin);
        c.construction.interaction_cursor_anchor = { state.origin.x, state.origin.y };
        state.object_rotation_degrees = 0;
        c.construction.interaction_secondary_anchor = c.construction.interaction_cursor_anchor;
        state.direction = 4;
        state.track = &track(c, c.construction.origin_cell);
        auto p = state.origin;
        secondary_anchor(p, 0);
        c.construction.interaction_secondary_anchor = { p.x, p.y };
    } else {
        auto* header = c.view.secondary_cursor_record_snapshot;
        c.construction.park_object = header && header->kind == ThingKind::park_object ? reinterpret_cast<ParkObjectRuntimeView*>(header) : nullptr;
        auto* selected = c.construction.park_object;
        if (!selected) {
            return;
        }
        if (selected->upgrade_parent && definition(c, selected->definition_id).balance->AddOn.UpgradeType == 1) {
            selected = object(c, selected->upgrade_parent);
            if (!selected) {
                throw std::logic_error("construction upgrade parent is missing");
            }
            c.construction.park_object = selected;
            c.view.secondary_cursor_record_snapshot = &selected->base.header;
        }
        c.construction.origin_cell = static_cast<CellId>((selected->base.y >> 8) * 128u + (selected->base.x >> 8) + 1u);
        state.track = &track(c, c.construction.origin_cell);
        state.origin = coordinate(c.construction.origin_cell);
        state.object_rotation_degrees = std::bit_cast<int>(selected->angle);
        auto a = state.origin, b = state.origin;
        state.direction = primary_anchor(a, state.object_rotation_degrees);
        secondary_anchor(b, state.object_rotation_degrees);
        c.construction.interaction_cursor_anchor = { a.x, a.y };
        c.construction.interaction_secondary_anchor = { b.x, b.y };
    }
    if (c.construction.park_object && c.construction.origin_cell != state.previous_origin && record_action) {
        record_word(c, 50, c.construction.origin_cell);
    }
}

void ParkConstructionRuntime::set_mode(ConstructionWorld& c, int mode, bool record_action) {
    state.shortcut_rearm_mode.reset();
    //The UI pointer-handler lifetime is owned separately by the interaction
    //frontend. These are the mode, recorder, cursor and Advisor effects shared
    //by the native construction/retirement operations.
    if (mode == 1 && c.construction.mode == 21) {
        return;
    }
    if (mode != c.construction.mode) {
        invalidate_interaction_cost_quote();
    }
    if (record_action) {
        record_word(c, 0, static_cast<std::uint16_t>(mode));
    }
    if (mode != c.construction.mode) {
        auto* h = world(c).saved.advisor ? c.entities.at(world(c).saved.advisor).payload : nullptr;
        if (h && h->kind == ThingKind::advisor) {
            for (int action : std::array { 147, 164, 166, 163, 155, 272, 266, 510, 167 }) {
                world_operations::cancel_advisor_action_by_id(c, reinterpret_cast<AdvisorRecord*>(h), action);
            }
        }
    }
    state.previous_mode = c.construction.mode;
    if (mode == std::to_underlying(WorldInteractionMode::backtrack_preview) && c.construction.mode != mode) {
        state.backtrack_saved_mode = c.construction.mode;
    }
    c.construction.mode = mode;
    c.construction.path_start_x = c.construction.path_start_y = -1;
    if (!mode) {
        state.cursor_help.clear();
        state.track_preview = {};
        state.skip_nonpath_preview = false;
        c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
        state.status_latch = false;
        cancel_world_staff_interaction(c);
        //Native673230 reinstalls the selected toolbar handler;4D6160 arms it.
        //The live owner activates it with the cached/next admitted pointer cell.
        //UI takeover or selection of another tool clears the pending activation.
        const auto pointer_mode = c.staff.interaction.pointer_mode_code;
        if (pointer_mode == 2 || pointer_mode == 3) {
            state.shortcut_rearm_mode = pointer_mode == 2 ? 1 : 58;
        }
    } else if (mode == 1) {
        c.staff.interaction.cursor_presentation = 3;
    } else if (mode == 3) {
        world_operations::queue_scored_advisor_action(c, 164);
        c.staff.interaction.cursor_presentation = 4;
    } else if (mode == 53) {
        c.construction.previous_path_step_x = c.construction.previous_path_step_y = 128;
    }
}

void ParkConstructionRuntime::set_mode_preserving_path(ConstructionWorld& c, int mode, bool record_action) {
    using namespace construction_detail;
    if (mode == 1 && c.construction.mode == 21) {
        return;
    }
    if (record_action) {
        record_word(c, 25, static_cast<std::uint16_t>(mode));
    }
    if (mode != c.construction.mode) {
        invalidate_interaction_cost_quote();
        auto* header = world(c).saved.advisor ? c.entities.at(world(c).saved.advisor).payload : nullptr;
        if (header && header->kind == ThingKind::advisor) {
            for (const auto action : std::array { 147, 164, 166, 163, 167, 155, 272, 266, 510 }) {
                world_operations::cancel_advisor_action_by_id(c, reinterpret_cast<AdvisorRecord*>(header), action);
            }
        }
    }
    state.previous_mode = c.construction.mode;
    if (mode == std::to_underlying(WorldInteractionMode::backtrack_preview) && c.construction.mode != mode) {
        state.backtrack_saved_mode = c.construction.mode;
    }
    c.construction.mode = mode;
    //Native673B20 preserves line coordinates, status and path-step caches,
    //including mode0. Handler ownership is provided by the modern frontend.
    if (mode == 1) {
        c.staff.interaction.cursor_presentation = 3;
    } else if (mode == 3) {
        world_operations::queue_scored_advisor_action(c, 164);
        c.staff.interaction.cursor_presentation = 4;
    }
}

void ParkConstructionRuntime::begin_placement(ConstructionWorld& c, std::uint16_t id, int rotation) {
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        throw std::invalid_argument("placement rotation is not cardinal");
    }
    auto& d = construction_detail::definition(c, id);
    if (is_coaster_catalog_definition(d)) {
        if (!c.construction.coaster_catalog) {
            throw std::logic_error("coaster catalog owner is not bound");
        }
        c.construction.coaster_catalog->begin(c, id, rotation);
        return;
    }
    if (c.construction.coaster_catalog) {
        c.construction.coaster_catalog->clear(c);
    }
    validate_placement_parent(c, d);
    const bool track_attachment = d.balance->AddOn.UpgradeType == 1;
    apply_set_ride_action(c, &d.id, track_attachment ? 1u : 0u);
    set_construction_track_type(c, d.which_track_type, true);
    select_object_definition_and_placement_grid(c, id);
    state.rotation_degrees = rotation;
    state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
    state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    record_word(c, 10, static_cast<std::uint16_t>(rotation));
    set_mode(c, 4, true);
}

bool ParkConstructionRuntime::prepare_placement(ConstructionWorld& c, std::uint16_t id, WorldCellCoordinates& p, int& rotation, int cost_override, bool select_definition) {
    const auto& d = construction_detail::definition(c, id);
    //Mission specials append counted registry rows. Native624280 clears an
    //activated/retired header without reclaiming its requirement row; a full
    //mission registry therefore has no capacity for another construction.
    //Reject before the overwrite pass, allocation, history or bank mutation.
    if (d.balance->Info.IsSpecial == 1 && c.attractions.special_ride_group_count >= c.attractions.special_ride_groups.size()) {
        state.validation_failed = state.status_latch = true;
        state.result = {};
        return false;
    }
    if (select_definition) {
        begin_placement(c, id, rotation);
    } else {
        validate_placement_parent(c, d);
        state.rotation_degrees = rotation;
        state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
        state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    }
    state.validation_failed = state.stop = state.existing_endpoint = state.queue_joined = state.insufficient_funds = state.status_latch = false;
    state.occupant_overwrite_priority = 0;
    state.faces.clear();
    const bool dependent = d.balance->AddOn.UpgradeType || d.balance->Info.IsPhotoBooth;
    const bool track_attachment = d.balance->AddOn.UpgradeType == 1;
    auto* parent = dependent ? c.construction.park_object : nullptr;
    if (track_attachment) {
        using namespace construction_detail;
        if (!bounds(p)) {
            return false;
        }
        const auto aligned = [](int delta) {
            if (delta < 0 && delta % 2) {
                --delta;
            }
            return 2 * (delta / 2);
        };
        p = { state.origin.x + aligned(p.x - state.origin.x), state.origin.y + aligned(p.y - state.origin.y) };
        if (!bounds(p)) {
            return false;
        }
        const auto& root = root_track(c, map(c, cell_id(p)).base).base;
        if (!world_cell_is_track_root(&root) || root.common.type == 25 || root.common.parent_id != c.construction.origin_cell) {
            return false;
        }
        match (get_world_cell_direction(&root)) {
            case 1  => rotation = 0;
            case 4  => rotation = 270;
            case 16 => rotation = 180;
            case 64 => rotation = 90;
            case _  => {
            }
        } const auto& b = d.balance->Bumper;
        match (rotation) {
            case 0 => {
                p.x += b.NorthXAdjust;
                p.y += b.NorthYAdjust;
            }
            case 90 => {
                p.x += b.WestXAdjust;
                p.y += b.WestYAdjust;
            }
            case 180 => {
                p.x += b.SouthXAdjust;
                p.y += b.SouthYAdjust;
            }
            case 270 => {
                p.x += b.EastXAdjust;
                p.y += b.EastYAdjust;
            }
            case _ => {
            }
        } match (parent->angle) {
            case 90  => --p.x;
            case 180 => {
                --p.x;
                ++p.y;
            }
            case 270 => ++p.y;
            case _   => {
            }
        } state.rotation_degrees = rotation;
    } else if (dependent && d.balance->AddOn.UpgradeType == 2 && d.balance->AddOn.UpgradeDistance) {
        const auto origin = WorldCellCoordinates { parent->base.x >> 8, parent->base.y >> 8 };
        const auto& footprint = *get_park_object_balance(c, *parent).Info.Shape;
        const auto distance = d.balance->AddOn.UpgradeDistance;
        if (p.x < origin.x - distance || p.x > origin.x + footprint.width + distance || p.y < origin.y - distance || p.y > origin.y + footprint.height + distance) {
            return false;
        }
    }
    state.cost = cost_override ? cost_override : d.balance->Upgrades[0].CostOfUpgrade;
    return true;
}

std::optional<ParkPlacementResult> ParkConstructionRuntime::place(ConstructionWorld& c, std::uint16_t id, WorldCellCoordinates p, int rotation, int cost_override) {
    if (c.construction.coaster_catalog && c.construction.coaster_catalog->handles(c, id)) {
        return c.construction.coaster_catalog->place(c, id, p, rotation);
    }
    if (!prepare_placement(c, id, p, rotation, cost_override)) {
        return std::nullopt;
    }
    const auto& d = construction_detail::definition(c, id);
    const bool dependent = d.balance->AddOn.UpgradeType || d.balance->Info.IsPhotoBooth;
    const bool track_attachment = d.balance->AddOn.UpgradeType == 1;
    auto* parent = dependent ? c.construction.park_object : nullptr;
    if (d.balance->UsageInfo.SingleInstance == 1) {
        const auto* control = find_world_object_control(construction_detail::world(c).object_controls, id);
        if (control && control->live_instance_count) {
            return std::nullopt;
        }
    }
    if (!c.construction.action_recording_disabled && state.cost > construction_detail::bank(c).balance) {
        state.validation_failed = state.insufficient_funds = state.status_latch = true;
        return std::nullopt;
    }
    std::optional<ParkPlacementResult> checked;
    {
        construction_detail::RestoreValue restore { c.construction.cell_costs_suppressed };
        c.construction.cell_costs_suppressed = 1;
        checked = process_grid(c, p, rotation, cost_override, false, true);
    }
    if (!checked || state.validation_failed) {
        return std::nullopt;
    }
    construction_detail::RestoreValue suppression { c.construction.cell_costs_suppressed };
    c.construction.cell_costs_suppressed = 1;
    if (!dependent) {
        const auto result = process_grid(c, p, rotation, cost_override, true, false);
        if (result) {
            finish_placement(c, *result);
        }
        return result;
    }
    //Keep owned map/control/economic state for exception recovery. Native
    //pruning completes vehicle riders before rebuilding; recovery retains that
    //completion queue rather than resurrecting destroyed vehicle resources.
    using namespace construction_detail;
    const PlacementRecovery recovery { c };
    const auto old_photo = parent->photo_booth;
    const auto old_valid = parent->is_track_ride_valid;
    const bool was_open = parent->can_load != 0;
    const auto restore_parent = [&] {
        select_parent_origin(c, state, *parent);
    };
    const auto reconcile_span = [&] {
        auto& root = root_track(c, map(c, cell_id(p)).base).base;
        auto start = coordinate(root.index);
        int dx = 0, dy = 0;
        if (root.common.flags & 16u) {
            const auto owner = map(c, root.index).base.common.parent_id;
            for (auto direction : std::array<std::uint8_t, 4> { 1, 4, 16, 64 }) {
                if (auto* next = resolve_track_cell_in_direction(c, &root, direction, 2)) {
                    if ((next->base.common.flags & 16u) && map(c, next->base.index).base.common.parent_id == owner && world_cell_is_track_root(&next->base)) {
                        if (direction == 1) {
                            dy = -2;
                        } else if (direction == 4) {
                            dx = 2;
                        } else if (direction == 16) {
                            dy = 2;
                        } else {
                            dx = -2;
                        }
                    }
                }
            }
        }
        WorldCellCoordinates end { start.x + dx, start.y + dy };
        if (dx) {
            const auto step = dx < 0 ? -2 : 2;
            start.x -= step;
            end.x += step;
        }
        if (dy) {
            const auto step = dy < 0 ? -2 : 2;
            start.y -= step;
            end.y += step;
        }
        for (const auto operation : std::array { 133u, 134u, 129u }) {
            (void)track_command_line(c, operation, start, end);
        }
    };
    try {
        if (track_attachment) {
            prune_track_ride_dynamic_path(c, parent->track_ride_handle);
        }
        const auto result = process_grid(c, p, rotation, cost_override, true, false);
        if (!result) {
            throw std::logic_error("validated attachment grid failed during commit");
        }
        if (track_attachment) {
            reconcile_span();
            restore_parent();
            parent->is_track_ride_valid = refresh_track_path(c);
            animate_track_endpoint(c, *parent, false, parent->is_track_ride_valid != 0);
            if (parent->is_track_ride_valid) {
                if (is_park_object_open_eligible(c, *parent)) {
                    open_park_object(c, *parent);
                }
            } else {
                animate_track_endpoint(c, *parent, true, false);
            }
        } else {
            finish_placement(c, *result);
        }
        c.construction.selected_track_type = 0;
        set_mode(c, 0);
        return result;
    } catch (...) {
        const auto failure = std::current_exception();
        const auto provisional = state.result.created_object;
        if (auto* created = object(c, provisional)) {
            if (created->track_ride_handle) {
                destroy_track_ride(c, created->track_ride_handle);
            }
            (void)retire_world_used_thing(c, &created->base.header);
            if (c.entities.removals.tail && c.entities.removals.tail->id == provisional) {
                ThingId removed {};
                pop_pending_world_used_thing_removal(c, &removed);
                unlink_and_free_world_used_thing(c, &c.entities.at(removed));
            }
        }
        recovery.restore(c);
        parent->photo_booth = old_photo;
        parent->is_track_ride_valid = old_valid;
        c.view.secondary_cursor_record_snapshot = &parent->base.header;
        restore_parent();
        if (track_attachment) {
            parent->is_track_ride_valid = refresh_track_path(c);
            animate_track_endpoint(c, *parent, false, parent->is_track_ride_valid != 0);
            if (!parent->is_track_ride_valid) {
                animate_track_endpoint(c, *parent, true, false);
            } else if (was_open && is_park_object_open_eligible(c, *parent)) {
                open_park_object(c, *parent);
            }
        }
        c.construction.selected_track_type = 0;
        set_mode(c, 0);
        std::rethrow_exception(failure);
    }
}
} //namespace simcoaster
