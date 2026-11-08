#include "simcoaster/world_object_placement.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "simcoaster/world_theme.hpp"
#include <bit>

namespace simcoaster {
namespace {
using namespace construction_detail;

void record_word(ConstructionWorld& c, std::uint8_t opcode_value, std::uint16_t word) {
    if (c.construction.action_recording_disabled || !c.construction.action_record_streams_initialized) {
        return;
    }
    if (!c.construction.action_record_stream) {
        throw std::logic_error("Relocation action stream is not bound");
    }
    const std::array opcode { static_cast<std::byte>(opcode_value) };
    const auto value = save::detail::encode(word);
    (void)c.construction.action_record_stream->write(opcode);
    (void)c.construction.action_record_stream->write(value);
}
} //namespace

void ParkConstructionRuntime::retire_relocation_reference(ThingId id) {
    if (state.relocation && state.relocation->related == id) {
        state.relocation->related = 0;
        state.relocation->related_identity = nullptr;
        state.relocation->related_creation_time = 0;
    }
}

void ParkConstructionRuntime::bind_relocation_parent(ConstructionWorld& c) {
    if (!state.relocation) {
        return;
    }
    const auto& transaction = *state.relocation;
    ThingHeader* header {};
    if (transaction.related && transaction.related < c.entities.size()) {
        auto* candidate = c.entities[transaction.related].payload;
        if (candidate && candidate == transaction.related_identity && candidate->kind == ThingKind::park_object && !candidate->retirement_pending && reinterpret_cast<ParkObjectRuntimeView*>(candidate)->creation_file_time == transaction.related_creation_time) {
            header = candidate;
        }
    }
    c.view.secondary_cursor_record_snapshot = header;
    c.construction.park_object = header ? reinterpret_cast<ParkObjectRuntimeView*>(header) : nullptr;
    refresh_origin(c);
}

bool ParkConstructionRuntime::begin_object_relocation(ConstructionWorld& c, ParkObjectRuntimeView& source) {
    using namespace construction_detail;
    auto& d = definition(c, source.definition_id);
    const WorldCellCoordinates tile { source.base.x >> 8, source.base.y >> 8 };
    const auto angle = std::bit_cast<std::int32_t>(source.angle);
    if (!bounds(tile) || source.base.header.retirement_pending || (angle != 0 && angle != 90 && angle != 180 && angle != 270)) {
        return false;
    }
    ParkObjectRelocationState transaction;
    transaction.source = source.base.header.id;
    transaction.definition = source.definition_id;
    transaction.source_creation_time = source.creation_file_time;
    transaction.angle = angle;
    transaction.related = source.upgrade_parent; //Native5E4C90 copies WORD+56.
    if (transaction.related) {
        auto* related = object(c, transaction.related);
        if (!related || related->base.header.retirement_pending) {
            return false;
        }
        transaction.related_identity = &related->base.header;
        transaction.related_creation_time = related->creation_file_time;
    }
    c.view.secondary_cursor_record_snapshot = &source.base.header;
    refresh_origin(c, true);
    if (d.balance->Info.HasQueue) {
        transaction.queue_exit_direction = trace_queue(c, source);
        transaction.queue_points = state.path_points; //Actual674AF0 value snapshot.
    }
    const auto* source_identity = &source.base.header;
    const auto source_id = source.base.header.id;
    set_mode(c, 51, true);
    (void)dispatch(c, tile, WorldInputModifierMask::none, false);
    //Startup is ordinary demolition, including refund and event33. No caller
    //ever reads the retired source pointer again in the reached native59 path.
    if (c.entities.at(source_id).payload == source_identity && source_identity->kind == ThingKind::park_object && !source_identity->retirement_pending) {
        return false;
    }
    state.relocation = std::move(transaction);
    //Activation4D6160 precedes the explicit510770 startup commands.
    set_mode(c, 59, true);
    apply_set_ride_action(c, &d.id, d.balance->AddOn.UpgradeType == 1 ? static_cast<std::uint32_t>(d.which_track_type) : 0u);
    state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    record_word(c, 35, static_cast<std::uint16_t>(state.queue_placement_enabled));
    set_construction_track_type(c, static_cast<std::uint32_t>(d.which_track_type), true);
    set_mode(c, 59, true);
    apply_set_ride_action(c, &d.id, 0);
    select_object_definition_and_placement_grid(c, d.id);
    state.rotation_degrees = angle;
    state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
    state.queue_placement_enabled = d.balance->Info.HasQueue != 0;
    record_word(c, 10, static_cast<std::uint16_t>(angle));
    bind_relocation_parent(c);
    refresh_origin(c, true);
    return true;
}

bool ParkConstructionRuntime::preview_object_relocation(ConstructionWorld& c, WorldCellCoordinates p) {
    using namespace construction_detail;
    bind_relocation_parent(c);
    const auto& d = definition(c, c.construction.selected_object_definition_id);
    RestoreValue suppression { c.construction.cell_costs_suppressed };
    c.construction.cell_costs_suppressed = 1;
    RestoreValue adjustment { state.coordinate_adjustment_disabled };
    state.skip_nonpath_preview = false;
    state.cost = d.balance->Upgrades[0].CostOfUpgrade;
    state.candidate_overwrite_priority = d.balance->Info.OverwritePriority;
    if (!process_grid(c, p, state.rotation_degrees, 0, false, true)) {
        state.skip_nonpath_preview = true;
    }
    auto* parent = c.construction.park_object;
    const auto distance = d.balance->AddOn.UpgradeDistance;
    if (d.balance->AddOn.UpgradeType == 2 && distance) {
        if (!parent || parent->base.header.retirement_pending) {
            state.skip_nonpath_preview = true;
        } else {
            const auto& shape = *definition(c, parent->definition_id).balance->Info.Shape;
            const auto min_x = fixed::subtract(parent->base.x >> 8, distance);
            const auto min_y = fixed::subtract(parent->base.y >> 8, distance);
            const auto max_x = fixed::add(fixed::add(parent->base.x >> 8, shape.width), distance);
            const auto max_y = fixed::add(fixed::add(parent->base.y >> 8, shape.height), distance);
            state.coordinate_adjustment_disabled = true;
            //65E170 fills exclusive maxima but accepts a cursor exactly at MAX.
            for (auto y = std::max(min_y, 0); y < std::min(max_y, 128); ++y) {
                for (auto x = std::max(min_x, 0); x < std::min(max_x, 128); ++x) {
                    auto& cell = map(c, cell_id({ x, y }));
                    const auto type = cell.base.common.type;
                    if ((cell.base.common.flags & 0x40u) || root_track(c, cell.base).base.common.type == 25 || world_track_reference_matches_type_set(c, cell.base.index) || type == 3 || footprint(type) || type == 7 || type == 30 || cell.is_obstacle) {
                        continue;
                    }
                    (void)command(c, cell.base, 0x122u);
                }
            }
            if (p.x < min_x || p.x > max_x || p.y < min_y || p.y > max_y) {
                state.validation_failed = true;
            }
        }
    }
    state.coordinate_adjustment_disabled = false;
    (void)process_grid(c, p, state.rotation_degrees, 0, false, false);
    state.validation_failed = state.validation_failed || state.skip_nonpath_preview;
    state.status_latch = state.status_latch || state.validation_failed;
    return !state.status_latch;
}

void ParkConstructionRuntime::dispatch_object_relocation(ConstructionWorld& c, WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    using namespace construction_detail;
    bind_relocation_parent(c);
    RestoreValue suppression { c.construction.cell_costs_suppressed };
    c.construction.cell_costs_suppressed = 1; //Cell costs only; the object factory still charges.
    state.entry_neighbor = nullptr;
    state.track = state.secondary_track = nullptr;
    const auto placed = process_grid(c, p, state.rotation_degrees, 0, true, false);
    if (!placed) {
        return;
    }
    auto* owner = object(c, placed->created_object);
    if (!owner) {
        throw std::logic_error("Relocated replacement object is missing");
    }
    const auto& d = definition(c, owner->definition_id);
    const bool control = std::to_underlying(modifiers) == 12;
    bool resume_connection = false;
    const bool queue = state.queue_placement_enabled && !c.construction.selected_track_type;
    if (queue) {
        const auto selector = construction_track_type(c);
        if (selector == 1 || selector == 2) {
            finish_placement(c, *placed, true);
        }
        if (selector && !c.construction.selected_track_type) {
            owner->is_track_ride_valid = 0;
            c.construction.origin_cell = cell_id({ owner->base.x >> 8, owner->base.y >> 8 });
            state.origin = coordinate(c.construction.origin_cell);
            state.object_rotation_degrees = std::bit_cast<std::int32_t>(owner->angle);
            c.construction.park_object = owner;
            c.view.secondary_cursor_record_snapshot = &owner->base.header;
            c.construction.interaction_cursor_anchor = { state.origin.x, state.origin.y };
            c.construction.interaction_secondary_anchor = c.construction.interaction_cursor_anchor;
            if (d.which_track_type != 3) {
                auto first = state.origin, second = state.origin;
                state.direction = object_anchor(first, state.object_rotation_degrees);
                (void)object_anchor(second, state.object_rotation_degrees, true);
                c.construction.interaction_cursor_anchor = { first.x, first.y };
                c.construction.interaction_secondary_anchor = { second.x, second.y };
            }
        }
        if (selector == 1 || selector == 2) {
            finish_queue_placement(c, *placed, true);
        } else {
            finish_placement(c, *placed, true);
        }
    } else {
        if (!control || d.balance->UsageInfo.TrainingRoom || d.balance->AddOn.UpgradeType || d.balance->UsageInfo.ThinkingSpace) {
            set_mode(c, 0);
        }
        if (d.balance->Info.GotoExitMode && !control) {
            const bool ride = d.which_ui_type == ObjectUiCategory::rides;
            const auto endpoint = ride ? owner->exit_cell : owner->entry_cell;
            const auto& cell = map(c, endpoint).base;
            const auto direction = ride ? get_world_cell_direction(&cell) : opposite_world_cell_direction(get_world_cell_direction(&cell));
            std::array<std::uint8_t, 3> probes {};
            if (direction == 1) {
                probes = { 1, 4, 64 };
            } else if (direction == 4) {
                probes = { 1, 4, 16 };
            } else if (direction == 16) {
                probes = { 16, 4, 64 };
            } else if (direction == 64) {
                probes = { 1, 16, 64 };
            }
            if (probes[0]) {
                const auto candidate = stepped(coordinate(endpoint), direction);
                bool connected = false;
                if (bounds(candidate)) {
                    const auto& candidate_cell = map(c, cell_id(candidate)).base;
                    for (const auto probe : probes) {
                        if (const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &candidate_cell, probe)) {
                            connected = connected || neighbor->base.common.type == 1;
                        }
                    }
                }
                if (!connected) {
                    set_mode_preserving_path(c, 1);
                    resume_connection = true;
                }
            }
        }
    }
    if (!control) {
        state.rotation_degrees = 0;
    }
    if (resume_connection) {
        resume_object_connection_placement(c, p);
    }
}

void ParkConstructionRuntime::resume_object_connection_placement(ConstructionWorld& c, WorldCellCoordinates p) {
    using namespace construction_detail;
    if (c.mode.game == GameMode::code_3) {
        return;
    }
    auto* owner = c.construction.park_object;
    if (!owner) {
        throw std::logic_error("Connection placement has no owner");
    }
    const auto& d = definition(c, owner->definition_id);
    //660D80's common footer installs the actual path handler. Its activation
    //resets the line, after which the outward endpoint is the initial seed.
    state.connection_placement_active = true;
    set_mode(c, 1, true);
    state.relocation.reset();
    if (!c.construction.action_recording_disabled) {
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 101, p.x * 10, 0, p.y * 10);
    }
    state.candidate_overwrite_priority = 2;
    if (c.construction.path_start_x == -1 && c.construction.path_start_y == -1) {
        const bool ride = d.which_ui_type == ObjectUiCategory::rides;
        const auto endpoint = ride ? owner->exit_cell : owner->entry_cell;
        const auto& cell = map(c, endpoint).base;
        const auto direction = ride ? get_world_cell_direction(&cell) : opposite_world_cell_direction(get_world_cell_direction(&cell));
        auto seed = coordinate(endpoint);
        if (direction == 1 || direction == 4 || direction == 16 || direction == 64) {
            seed = stepped(seed, direction);
        }
        c.construction.path_start_x = seed.x;
        c.construction.path_start_y = seed.y;
        state.path_points.clear();
        state.path_points.push_back(seed);
        world_operations::queue_scored_advisor_action(c, 147);
    }
}

void ParkConstructionRuntime::finish_object_relocation(ConstructionWorld& c, WorldInputModifierMask modifiers) {
    using namespace construction_detail;
    if (!state.relocation) {
        return;
    }
    const bool cursor_seen = state.relocation->cursor_event_seen;
    //Drop the old requested-handler metadata before nested queue dispatch.
    //Native replacement cannot recursively finish the same handler twice.
    state.relocation.reset();
    state.connection_placement_active = false;
    if (!cursor_seen) {
        return; //4D6330 deliberately does nothing before mouse-up.
    }
    c.construction.prebuilt_grid_override_valid = 0;
    auto* owner = c.construction.park_object;
    if (!owner || owner->base.header.retirement_pending) {
        return;
    }
    const auto& d = definition(c, owner->definition_id);
    if (!d.balance->Info.HasQueue || d.which_ui_type != ObjectUiCategory::rides || d.balance->AddOn.UpgradeType || d.balance->AddOn.UpgradesId) {
        return;
    }
    if (!has_usable_entry_approach(c, owner)) {
        const auto back = get_back_of_queue(c, owner);
        if (!back || !owner->exit_cell) {
            world_operations::queue_scored_advisor_action(c, 510);
            return;
        }
        const auto& back_cell = map(c, back).base;
        const auto& exit = map(c, owner->exit_cell).base;
        const auto* exit_neighbor = resolve_world_cell_from_selector_flag_byte(c, &exit, get_world_cell_direction(&exit));
        auto direction = opposite_world_cell_direction(get_world_cell_direction(&back_cell));
        const auto second = advance_world_cell_direction(direction, 64);
        const std::array probes { direction, second, opposite_world_cell_direction(second) };
        MapCell* destination {};
        for (const auto probe : probes) {
            auto* candidate = resolve_world_cell_from_selector_flag_byte(c, &back_cell, probe);
            if (candidate && candidate->base.common.type == 1 && (!exit_neighbor || exit_neighbor->base.index != candidate->base.index || has_clear_entry_exit_path(c, *owner))) {
                destination = candidate;
                break;
            }
        }
        if (!destination) {
            world_operations::queue_scored_advisor_action(c, 510);
            return;
        }
        set_mode(c, 3);
        state.status_latch = state.validation_failed = false;
        const auto seed = coordinate(back);
        c.construction.path_start_x = seed.x;
        c.construction.path_start_y = seed.y;
        (void)dispatch(c, coordinate(destination->base.index), modifiers, false);
        const auto tag_id = world(c).saved.tag_system;
        auto* tag_header = c.entities.at(tag_id).payload;
        if (!tag_header || tag_header->kind != ThingKind::tag_system) {
            throw std::logic_error("Relocation completion Tag System is absent");
        }
        const std::array<std::int32_t, 1> keys { 27 };
        (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(c, reinterpret_cast<TagSystemRecord*>(tag_header), keys, owner->base.header.id);
        const auto advisor_id = world(c).saved.advisor;
        auto* advisor_header = c.entities.at(advisor_id).payload;
        if (advisor_header && advisor_header->kind == ThingKind::advisor) {
            world_operations::cancel_advisor_action_by_id(c, reinterpret_cast<AdvisorRecord*>(advisor_header), 266);
        }
        state.connection_placement_active = true;
    }
    if (!owner->can_load && has_clear_entry_exit_path(c, *owner) && is_park_object_open_eligible(c, *owner) && classify_park_object_ui_status(c, *owner, 1) == ParkObjectUiStatus::open) {
        open_park_object(c, *owner);
        auto& camera = c.view.camera;
        if (camera.interaction_snapshot_valid) {
            camera.focus_position = camera.interaction_snapshot_position;
            camera.interaction_snapshot_valid = false;
        }
    }
    if (!has_usable_entry_approach(c, owner) && !get_or_create_theme_global_info_manager(c).history.at(266).play_count) {
        world_operations::queue_scored_advisor_action(c, 266);
    }
}
} //namespace simcoaster
