#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/park_special_ride_construction.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"

namespace simcoaster {
namespace {
using namespace construction_detail;

SpecialRideRegistryGroup* registered_group(WorldRuntime& c, ThingId id) {
    for (std::uint32_t index = 0; index < c.attractions.special_ride_group_count; ++index) {
        if (c.attractions.special_ride_groups.at(index).thing_id == id) {
            return &c.attractions.special_ride_groups[index];
        }
    }
    return nullptr;
}

std::uint8_t marker_direction(std::uint8_t direction, int angle) {
    //Native5D8180 shifts once by angle/90 and wraps once, rather than using
    //the placement grid's separate one-hot cardinal rotation operation.
    auto value = static_cast<std::uint32_t>(direction) << (static_cast<std::uint32_t>(angle / 90) & 31u);
    if (std::bit_cast<std::int32_t>(value) > 128) {
        value >>= 8;
    }
    return static_cast<std::uint8_t>(value);
}

WorldCellCoordinates neighbor(WorldRuntime& c, CellId anchor, std::uint8_t direction) {
    auto* next = resolve_world_cell_from_selector_flag_byte(c, &map(c, anchor).base, direction);
    if (!next) {
        throw std::out_of_range("special ride approach outside World");
    }
    return coordinate(next->base.index);
}

std::uint32_t clear_neighbor_kinds(WorldRuntime& c, const ParkObjectRuntimeView& owner) {
    //Native66EEE0's caller uses this separate neighborhood operation first.
    //The footprint scan is axis aligned after its rotated origin adjustment;
    //matching the actual linked owner is the eligibility gate inside it.
    const auto& d = definition(c, owner.definition_id);
    const auto offset = rotate_park_object_local_cell_offset({ d.cached_map_offset[0], d.cached_map_offset[1] }, std::bit_cast<int>(owner.angle));
    const auto anchor = static_cast<CellId>(static_cast<std::uint32_t>(owner.top_left) + static_cast<std::uint32_t>(offset.x) + static_cast<std::uint32_t>(offset.y) * 128u);
    const auto start = coordinate(anchor);
    std::uint32_t changed = 0;
    for (int row = 0; row < d.balance->Info.Shape->height; ++row) {
        for (int column = 0; column < d.balance->Info.Shape->width; ++column) {
            const WorldCellCoordinates p { start.x + column, start.y + row };
            if (!bounds(p)) {
                continue;
            }
            const auto parent = map(c, cell_id(p)).base.common.parent_id;
            if (!parent || find_first_park_object_id_in_map_cell(c, &map(c, parent)) != owner.base.header.id) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const WorldCellCoordinates at { p.x + dx, p.y + dy };
                    if (!bounds(at)) {
                        continue;
                    }
                    auto& cell = map(c, cell_id(at));
                    if (find_first_park_object_id_in_map_cell(c, &cell)) {
                        continue;
                    }
                    const auto type = cell.base.common.type;
                    if (type != 0 && type != 5) {
                        continue;
                    }
                    if (try_change_world_cell_type(c, &cell.base, 0) && type != cell.base.common.type) {
                        ++changed;
                    }
                }
            }
        }
    }
    return changed;
}

void configure_approaches(WorldRuntime& c, ParkConstructionRuntime& editor, ParkObjectRuntimeView& owner, SpecialRideConstructionResult& result, std::uint8_t entry_direction) {
    auto& entry = map(c, owner.entry_cell).base;
    const auto outward = opposite_world_cell_direction(entry_direction);
    auto& approach = map(c, cell_id(result.entry_approach)).base;
    auto& state = editor.state;
    state.entry_neighbor = nullptr;
    state.track = state.secondary_track = nullptr;
    if (state.queue_placement_enabled) {
        or_world_cell_neighbor_mask_bits(c, &entry, outward);
        set_world_cell_direction(&entry, outward);
        (void)editor.command(c, approach, 135);
        (void)editor.command(c, approach, 3);
        const auto toward = opposite_world_cell_direction(outward);
        or_world_cell_neighbor_mask_bits(c, &approach, toward);
        set_world_cell_direction(&approach, toward);
        set_world_cell_parent_id_unchecked(&approach, entry.common.parent_id);
        assign_world_cell_flags(&approach, 32);
        (void)editor.command(c, approach, 129);
        rebuild_park_object_queue_layout(c, &owner);
    } else {
        if (approach.common.type != 1) {
            (void)editor.command(c, approach, 135);
        }
        (void)editor.command(c, approach, 1);
        update_cell_connections(c, &approach, 1, 0, 0, true, true);
        (void)editor.command(c, approach, 129);
        assign_world_cell_flags(&approach, 32);
        for (const auto direction : std::array<std::uint8_t, 4> { 1, 4, 16, 64 }) {
            if (get_world_cell_neighbor_mask(c, &approach) & direction) {
                if (auto* adjacent = resolve_world_cell_from_selector_flag_byte(c, &approach, direction)) {
                    if (adjacent->base.common.type == 1) {
                        update_cell_connections(c, &adjacent->base, 1, 0, 0, true, true);
                        (void)editor.command(c, adjacent->base, 129);
                    }
                }
            }
        }
    }
    if (state.queue_placement_enabled) {
        auto& exit = map(c, cell_id(result.exit_approach)).base;
        if (exit.common.type != 1) {
            (void)editor.command(c, exit, 135);
        }
        (void)editor.command(c, exit, 1);
        update_cell_connections(c, &exit, 1, 0, 0, true, true);
        (void)editor.command(c, exit, 129);
        assign_world_cell_flags(&exit, 32);
        for (const auto direction : std::array<std::uint8_t, 4> { 1, 4, 16, 64 }) {
            if (get_world_cell_neighbor_mask(c, &exit) & direction) {
                if (auto* adjacent = resolve_world_cell_from_selector_flag_byte(c, &exit, direction)) {
                    if (adjacent->base.common.type == 1) {
                        update_cell_connections(c, &adjacent->base, 1, 0, 0, true, true);
                        (void)editor.command(c, adjacent->base, 129);
                    }
                }
            }
        }
    }
}

void initialize_track(WorldRuntime& c, ParkConstructionRuntime& editor, ParkObjectRuntimeView& owner, SpecialRideConstructionResult& result) {
    const auto& d = definition(c, owner.definition_id);
    if (!d.balance->Bumper.BumperType || (d.which_track_type != 1 && d.which_track_type != 2)) {
        return;
    }
    auto& state = editor.state;
    state.rotation_degrees = std::bit_cast<int>(owner.angle);
    state.placement_origin = coordinate(owner.top_left);
    auto primary = state.placement_origin;
    state.direction = object_anchor(primary, state.rotation_degrees);
    WorldCellCoordinates start {}, end {}, secondary {};
    match (state.rotation_degrees) {
        case 0 => {
            start = { primary.x - 4, primary.y };
            end = { primary.x - 2, primary.y };
            secondary = start;
        }
        case 90 => {
            start = { primary.x, primary.y + 4 };
            end = { primary.x, primary.y + 2 };
            secondary = { primary.x, primary.y + 5 };
        }
        case 180 => {
            start = { primary.x + 4, primary.y };
            end = { primary.x + 2, primary.y };
            secondary = { primary.x + 5, primary.y };
        }
        case 270 => {
            start = { primary.x, primary.y - 4 };
            end = { primary.x, primary.y - 2 };
            secondary = start;
        }
        case _ => throw std::invalid_argument("special track rotation is not cardinal");
    }
    //Native66EEE0 cleared the track-state pointer. Consequently669FA0's
    //station operation131 is a no-op; its returned outside-anchor pointer
    //becomes live before the subsequent ordinary-root ownership operations.
    const auto station = d.which_track_type == 1 ? 13u : 18u;
    for (const auto operation : std::array { station, 128u, 130u, 131u }) {
        (void)editor.track_command_line(c, operation, start, end);
    }
    c.construction.interaction_cursor_anchor = { start.x, start.y };
    state.secondary_track = &track(c, cell_id(secondary));
    state.track = &track(c, cell_id(primary));
    const auto mode = d.which_track_type == 1 ? 11 : 16;
    editor.set_mode(c, mode);
    auto endpoint = primary;
    for (const auto operation : std::array { 135u, static_cast<std::uint32_t>(mode), 128u }) {
        (void)editor.track_command_line(c, operation, primary, endpoint);
    }
    set_world_cell_direction(&track(c, cell_id(endpoint)).base, state.direction);
    for (const auto operation : std::array { 131u, 133u, 134u, 129u }) {
        (void)editor.track_command_line(c, operation, primary, endpoint);
    }
    state.path_points = { endpoint };
    editor.refresh_origin(c);
    auto opposite = WorldCellCoordinates { c.construction.interaction_secondary_anchor[0], c.construction.interaction_secondary_anchor[1] };
    auto other_end = opposite;
    for (const auto operation : std::array { 135u, static_cast<std::uint32_t>(mode), 128u }) {
        (void)editor.track_command_line(c, operation, opposite, other_end);
    }
    const std::uint8_t direction = c.construction.interaction_cursor_anchor[0] > other_end.x ? 4 : c.construction.interaction_cursor_anchor[0] < other_end.x ? 64 : c.construction.interaction_cursor_anchor[1] > other_end.y ? 16 : 1;
    auto& opposite_cell = track(c, cell_id(other_end)).base;
    or_world_cell_neighbor_mask_bits(c, &opposite_cell, direction);
    set_world_cell_direction(&opposite_cell, direction);
    if (auto* adjacent = resolve_track_cell_in_direction(c, &opposite_cell, direction, 2)) {
        or_world_cell_neighbor_mask_bits(c, &adjacent->base, opposite_world_cell_direction(direction));
    }
    for (const auto operation : std::array { 131u, 133u, 134u, 129u }) {
        (void)editor.track_command_line(c, operation, opposite, other_end);
    }
    select_parent_origin(c, state, owner);
    owner.is_track_ride_valid = 0;
    result.primary_track_anchor = primary;
    result.secondary_track_anchor = opposite;
}
} //namespace

bool is_special_ride_construction_ready(WorldRuntime& c, ThingId id) {
    const auto* owner = construction_detail::object(c, id);
    return owner && !owner->base.header.retirement_pending && !owner->activated && construction_detail::definition(c, owner->definition_id).balance->Info.IsSpecial == 1 && registered_group(c, id) && is_special_ride_group_ready_for_advisor_action(c, id);
}

std::optional<SpecialRideConstructionResult> activate_special_ride_for_construction(WorldRuntime& c, ParkConstructionRuntime& editor, ThingId id, bool record_action) {
    if (!is_special_ride_construction_ready(c, id)) {
        return std::nullopt;
    }
    auto& owner = *construction_detail::object(c, id);
    auto& d = construction_detail::definition(c, owner.definition_id);
    const auto angle = std::bit_cast<int>(owner.angle);
    if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
        throw std::invalid_argument("special ride rotation is not cardinal");
    }
    if (!d.balance->Info.Shape) {
        throw std::logic_error("special ride Shape is missing");
    }
    auto* script = world_operations::find_script(c, owner.script_id);
    if (!script) {
        throw std::logic_error("special ride RSSE owner is missing");
    }
    SpecialRideConstructionResult result {};
    result.object = id;
    const auto entry_direction = marker_direction(d.cached_entry_direction, angle);
    result.entry_approach = neighbor(c, owner.entry_cell, opposite_world_cell_direction(entry_direction));
    result.exit_approach = neighbor(c, owner.exit_cell, marker_direction(d.cached_exit_direction, angle));
    //Native5E8230 ignores a failed debit (the debug readiness override can
    //deliberately make that possible). Preserve that command contract.
    (void)try_spend_theme_golden_tickets(&get_or_create_theme_global_info_manager(c), d.balance->UsageInfo.GoldenTicketCost);
    result.changed_neighbor_cells = clear_neighbor_kinds(c, owner);
    world_operations::set_script_slot(c, script, 10, 1);
    c.view.secondary_cursor_record_snapshot = &owner.base.header;
    c.construction.park_object = &owner;
    editor.state.object_rotation_degrees = angle;
    update_park_object_entry_exit_neighbor_flags(c, owner, true);
    editor.set_mode(c, 4, record_action);
    apply_set_ride_action(c, &d.id, 0);
    editor.state.queue_placement_enabled = true;
    editor.state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
    editor.refresh_origin(c, record_action);
    select_object_definition_and_placement_grid(c, d.id);
    configure_approaches(c, editor, owner, result, entry_direction);
    initialize_track(c, editor, owner, result);
    //Native5E88BC..5E8908 installs the queue handler, dispatches it once,
    //then captures/focuses the entry only below AutoRotate.ZoomLevel.
    editor.set_mode(c, 20, record_action);
    (void)editor.dispatch(c, { 0, 0 }, WorldInputModifierMask::none, false);
    begin_park_object_connection_camera(c, &owner);
    owner.has_been_opened = 1;
    owner.activated = 1;
    world_operations::select_park_object_mesh_animation_variant(c, owner.mesh_instance_id, ParkObjectMeshVariant::closed);
    if (auto* group = registered_group(c, id)) {
        populate_special_ride_registry_entry(c, *group, 0);
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    return result;
}
} //namespace simcoaster
