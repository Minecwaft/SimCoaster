#include "simcoaster/world.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_theme.hpp"
#include <cstring>
#include <stdexcept>
#include <memory>

namespace simcoaster {
StaffCandidate* initialize_world_prefix_record(StaffCandidate* record) {
    record->creation_tick = 0;
    record->type = StaffType::handyman;
    record->agency_index = 0;
    record->resource_variant = record->valid = record->on_pointer = 0;
    return record; //Name/salary/timeout/experience and byte19 are untouched.
}

World* initialize_world_structure_prefix(World* world) {
    for (auto& candidate : world->staff_candidates) {
        initialize_world_prefix_record(&candidate);
    }
    world->staff_pool.last_update_tick = 0;
    world->staff_pool.opening_pool_generated = 0;
    world->staff_pool.stop_producing.fill(0);
    return world; //Six linked counters and trailing bytes are untouched.
}

ArrivalState* initialize_world_arrival_state(ArrivalState* state) {
    state->time_signature = 0;
    state->gates_open = 1;
    state->arrival_rate = 0;
    state->target_capacity = 5;
    state->offloading = 0;
    state->people_on_bus = 0;
    return state; //Two trailing bytes untouched.
}

MapCell* initialize_world_map_cell_record(MapCell* cell) {
    std::memset(cell, 0, sizeof *cell);
    cell->base.record_kind = CellStorageKind::map;
    cell->base.index = 16959;
    return cell; //Constructor WORD value; live grid IDs are assigned later.
}

TrackCell* initialize_world_track_cell_record(TrackCell* cell) {
    std::memset(cell, 0, sizeof *cell);
    cell->base.record_kind = CellStorageKind::track;
    return cell;
}

RegionEffectCell* initialize_world_re_cell_record(RegionEffectCell* cell) {
    std::memset(cell, 0, sizeof *cell);
    return cell;
}

MapCell* register_world_map_cell_range(WorldRuntime& c, MapCell* base, std::uint32_t stride) {
    c.simulation.map_range_base = base;
    c.simulation.map_record_stride = stride;
    c.simulation.map_range_end = reinterpret_cast<MapCell*>(reinterpret_cast<std::byte*>(base) + std::size_t { stride } * map_cell_count);
    return base;
}

TrackCell* register_world_track_cell_range(WorldRuntime& c, TrackCell* base, std::uint32_t stride) {
    c.simulation.track_range_base = base;
    c.simulation.track_record_stride = stride;
    c.simulation.track_range_end = reinterpret_cast<TrackCell*>(reinterpret_cast<std::byte*>(base) + std::size_t { stride } * map_cell_count);
    return base;
}

//Game.exe6583488. No root vptr, full-World memset, allocation or runtime setup.
World* construct_world_structure(WorldRuntime& c, World* world) {
    initialize_world_structure_prefix(world);
    initialize_world_calendar_state(c, &world->calendar);
    initialize_world_arrival_state(&world->arrival);
    for (auto& cell : world->map_cells) {
        initialize_world_map_cell_record(&cell);
    }
    for (auto& cell : world->track_cells) {
        initialize_world_track_cell_record(&cell);
    }
    for (auto& cell : world->region_cells) {
        initialize_world_re_cell_record(&cell);
    }
    register_world_map_cell_range(c, world->map_cells.data(), sizeof(MapCell));
    register_world_track_cell_range(c, world->track_cells.data(), sizeof(TrackCell));
    //150 object-control constructor callbacks are verified no-ops (stride36).
    world->saved.first_handyman = 0;
    world->saved.first_gardener = 0;
    world->saved.first_mechanic = 0;
    world->saved.first_entertainer = 0;
    world->saved.first_guard = 0;
    world->saved.first_researcher = 0;
    world->saved.first_park_object = 0;
    world->saved.park_closed = 1;
    c.entities.free_head = c.entities.data();
    c.entities.active_head = nullptr;
    c.attractions.special_ride_group_count = 0;
    return world;
}

int set_block_save_region(BlockSavePrefix* saver, int index, void* base, std::uint32_t size, const char*) {
    saver->regions[index] = { base, size };
    return index;
}

//Game.exe6583936. Does not overwrite the separate primary World global.
int initialize_world_structure_globals(WorldRuntime& c, World* world) {
    c.simulation.current = world;
    c.simulation.map_cell_records = world->map_cells.data();
    c.simulation.track_cell_records = world->track_cells.data();
    c.simulation.region_effects = { world->region_cells.data(), world->region_profiles.data() };
    reset_used_thing_record_table(c);
    set_block_save_region(&c.simulation.world_block_saver, 0, world, sizeof(World), "World Struture");
    return 1;
}

//Game.exe4359232: publish the primary pointer only after construction returns.
//Failed allocation publishes NULL but leaves the other World aliases intact.
int create_world_structure(WorldRuntime& c) {
    auto* allocation = static_cast<World*>(c.ports.memory.allocate(Allocation::world, sizeof(World)));
    World* constructed = nullptr;
    if (allocation) {
        try {
            constructed = construct_world_structure(c, std::construct_at(allocation));
        } catch (...) {
            c.ports.memory.release(Allocation::world, allocation);
            throw;
        }
    }
    c.simulation.reference = constructed;
    return constructed ? initialize_world_structure_globals(c, constructed) != 0 : 0;
}

MechanicHqRecord* initialize_world_mechanic_hq_state(WorldRuntime& c, MechanicHqRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 10);
    record->next_object = 0;
    register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &record->base.header, 7);
    return record;
}

UiMessageReceiverRecord* initialize_world_ui_message_receiver(WorldRuntime& c, UiMessageReceiverRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 17);
    for (int category : { 18, 10, 7, 8, 33, 15, 19, 34, 39, 35, 36, 42, 43 }) {
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &record->base.header, category);
    }
    return record;
}

Type23Record* initialize_world_used_thing_type23_record(WorldRuntime& c, Type23Record* record) {
    initialize_world_map_object_base(c, &record->base, 1, 23);
    record->unknown_020 = 3;
    return record;
}

StaffTrainingBudget* initialize_world_staff_training_budget_state(StaffTrainingBudget* training) {
    training->budget.fill(0);
    training->have_ever_trained = 0;
    return training;
}

void initialize_world_staff_hq_ability_slots(WorldRuntime& c, StaffHqRecord* hq) {
    const auto& requirements = get_or_create_theme_global_info_manager(c).mission_research.obstacle_staff;
    unsigned used = 0;
    //Reject invalid mission data instead of overwriting adjacent training state.
    auto write_slot = [&](StaffType staff, std::int32_t ability) {
        if (used == hq->staff_slots.size()) {
            throw std::length_error("mission staff requirements exceed ability slots");
        }
        auto& slot = hq->staff_slots[used++];
        slot.staff_type = staff;
        slot.required_ability = ability;
        slot.assigned_staff = 0;
    };
    for (const auto& requirement : requirements) {
        for (std::int32_t i = 0; i < requirement.number; ++i) {
            write_slot(requirement.staff_type, requirement.ability);
        }
    }
    while (used < 20) {
        write_slot(StaffType::unsupported_index, 0);
    }
}

StaffHqRecord* initialize_world_staff_hq_record(WorldRuntime& c, StaffHqRecord* hq) {
    initialize_world_map_object_base(c, &hq->base, 1, 9);
    initialize_world_staff_training_budget_state(&hq->training);
    hq->strikes.fill({});
    hq->staff_member_picked_up = 0;
    hq->force_strike = 0;
    hq->trained_staff.fill(0);
    for (int category : { 12, 14, 42, 43, 33, 35, 36 }) {
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &hq->base.header, category);
    }
    initialize_world_staff_hq_ability_slots(c, hq);
    return hq;
}

int initialize_world_advisor_runtime_state(WorldRuntime& c, AdvisorRecord* record) {
    for (int category : { 5, 17, 19, 23, 14, 8 }) {
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &record->base.header, category);
    }
    record->sound_id = 0;
    record->next_available_queue_pos = 0;
    record->last_action_duration = 0;
    record->last_action_started = 0;
    record->panel_up = 0;
    record->from_help_button = 0;
    c.advisor.advisor_help_override_response_id = 1014;
    c.advisor.advisor_help_requested_action_id = 645;
    record->from_ui_panel = 0;
    return 1;
}

AdvisorRecord* initialize_world_advisor_state(WorldRuntime& c, AdvisorRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 11);
    record->time_signature = 0;
    record->sound_id = 0;
    for (auto& entry : record->queue) {
        entry.valid = 0;
    }
    record->next_available_queue_pos = 0;
    record->last_action_duration = 0;
    record->special_candidate.valid = 0;
    initialize_world_advisor_runtime_state(c, record);
    return record;
}

ResearchGroup* initialize_world_research_group_entry(ResearchGroup* group) {
    group->highest_group_available = 1;
    group->current_project = -1;
    return group;
}

ResearchLabRecord* initialize_world_research_lab_state(WorldRuntime& c, ResearchLabRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 14);
    //Native invokes two arrays of no-op WORD constructors; their bytes survive.
    for (auto& group : record->groups) {
        initialize_world_research_group_entry(&group);
    }
    return record;
}
} //namespace simcoaster
