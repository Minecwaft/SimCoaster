#include "simcoaster/entity_catalog.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_weather.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_staff.hpp"
#include <new>
#include <stdexcept>

#include <cstring>
#include <bit>

namespace simcoaster {
namespace {
MapObjectBase* base(ThingHeader* record) {
    return reinterpret_cast<MapObjectBase*>(record);
}

ThingId first_id(const WorldRuntime& c) {
    return c.entities.active_head ? static_cast<ThingId>(c.entities.active_head->id_storage) : 0;
}

ThingId next_id(const WorldRuntime& c, const ThingHeader* current) {
    const auto* next = c.entities[current->id].active_next;
    return next ? static_cast<ThingId>(next->id_storage) : 0;
}

void ensure_mode(WorldRuntime& c, std::uint8_t& initialized) {
    if ((initialized & 1) == 0) {
        initialized |= 1;
        initialize_game_mode_selector(c);
        //Original also registers the empty nullsub_2 with atexit.
    }
}

void update_awards(WorldRuntime& c, void* receiver) {
    ensure_mode(c, c.mode.award_mode_initialized);
    if (c.mode.game == GameMode::code_0) {
        world_operations::evaluate_awards(c, receiver);
    }
}

void check_closed_park_gate(WorldRuntime& c, World* world) {
    if (!world->saved.park_gates || world->saved.game_tick % 30 != 0 || !world->saved.park_closed) {
        return;
    }
    auto* gate = c.entities[world->saved.park_gates].payload;
    auto* script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(gate)->script_id);
    if (world_operations::script_slot(c, script, 1) != 1 || count_eligible_person_records(c, 4)) {
        return;
    }
    for (ThingId id = first_id(c); id;) {
        auto* record = c.entities[id].payload;
        if (!record->retirement_pending && is_staff_record_kind(record->kind) && !record_passes_grid_predicates(c, base(record))) {
            return;
        }
        id = next_id(c, record);
    }
    gate = c.entities[world->saved.park_gates].payload;
    script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(gate)->script_id);
    (void)world_operations::script_slot(c, script, 1); //Native code really performs this redundant read.
    script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(gate)->script_id);
    world_operations::set_script_slot(c, script, 0, 0);
}
} //namespace

EventRegistry* get_world_event_subscription_registry(WorldRuntime& c) {
    if (!c.simulation.event_registry) {
        void* storage = c.ports.memory.allocate(Allocation::event_registry, sizeof(EventRegistry));
        if (!storage) {
            throw std::bad_alloc();
        }
        try {
            c.simulation.event_registry = initialize_world_event_subscription_registry(c, static_cast<EventRegistry*>(storage));
        } catch (...) {
            //Constructed standard-container members unwind before the owner is freed.
            c.ports.memory.release(Allocation::event_registry, storage);
            throw;
        }
    }
    return c.simulation.event_registry;
}

std::int32_t next_world_prng_value(World& world) {
    const auto state = std::rotr(1664525u * world.saved.random_seed + 1013904223u, 13);
    world.saved.random_seed = state;
    return std::bit_cast<std::int32_t>(std::bit_cast<std::int32_t>(state) < 0 ? 0u - state : state);
}

//Game.exe 6589936: table reset does not free existing payloads or queue nodes.
void reset_used_thing_record_table(WorldRuntime& c) {
    cancel_prepared_mission_transition(c);
    c.missions.mission_transition.failure.reset();
    ++c.missions.mission_transition.world_epoch;
    c.entities[0] = {};
    for (std::size_t id = 1; id < thing_slot_count; ++id) {
        auto& entry = c.entities[id];
        entry.payload = nullptr;
        entry.allocation_kind = ThingKind::unknown_0;
        entry.id_storage = static_cast<std::uint32_t>(id);
        entry.active_next = entry.active_previous = nullptr;
        entry.free_next = id + 1 < thing_slot_count ? &c.entities[id + 1] : nullptr;
    }
    c.entities.active_head = nullptr;
    c.entities.free_head = &c.entities[1];
}

//Game.exe 6590848 and inline operations in 6591088.
void activate_used_thing_metadata_entry(WorldRuntime& c, ThingMetadata* entry, ThingHeader* payload) {
    c.entities.free_head = entry->free_next;
    entry->free_next = nullptr;
    entry->active_next = c.entities.active_head;
    if (c.entities.active_head) {
        c.entities.active_head->active_previous = entry;
    }
    c.entities.active_head = entry;
    entry->allocation_kind = payload->kind;
    entry->payload = payload; //active_previous is already null in a valid free row.
}

ThingId* allocate_and_link_world_used_thing(WorldRuntime& c, ThingId* out, ThingHeader* payload) {
    if (auto* entry = c.entities.free_head) {
        activate_used_thing_metadata_entry(c, entry, payload);
        *out = static_cast<ThingId>(entry->id_storage);
    } else {
        *out = 0;
    }
    return out;
}

//Game.exe 6611216, also inlined at the end of deserialize_world_state.
void rebuild_used_thing_free_list(WorldRuntime& c) {
    c.entities.free_head = nullptr;
    ThingMetadata* previous = nullptr;
    for (std::size_t id = 1; id < thing_slot_count; ++id) {
        auto& entry = c.entities[id];
        entry.free_next = nullptr;
        if (!entry.payload) {
            if (previous) {
                previous->free_next = &entry;
            } else {
                c.entities.free_head = &entry;
            }
            previous = &entry;
        }
    }
    //A full table stays exhausted; the original retained a stale free_head here.
}

//Game.exe 6516992. The panel-navigation pointers are deliberately untouched.
MapObjectBase* initialize_world_map_object_base(WorldRuntime& c, MapObjectBase* record, CellId cell, std::uint32_t kind) {
    record->header.id = 0;
    record->header.kind = static_cast<ThingKind>(static_cast<std::uint8_t>(kind));
    record->header.retirement_pending = 0;
    record->map_parent = record->map_child = 0;
    allocate_and_link_world_used_thing(c, &record->header.id, &record->header);
    const std::uint32_t index = static_cast<std::uint32_t>(cell) - 1;
    record->x = static_cast<std::uint16_t>(((index & 127) << 8) | 128);
    record->y = static_cast<std::uint16_t>(((index >> 7) << 8) | 128);
    if (!c.simulation.map_cell_records || !cell || cell > map_cell_count) {
        throw std::out_of_range("unbound or invalid map cell");
    }
    link_world_used_thing_into_map_cell(c, &c.simulation.map_cell_records[cell - 1], record->header.id);
    return record;
}

ThingId link_world_used_thing_into_map_cell(WorldRuntime& c, MapCell* cell, ThingId id) {
    auto* record = base(c.entities[id].payload);
    if (cell->who) {
        base(c.entities[cell->who].payload)->map_parent = id;
    }
    const ThingId previous = cell->who;
    record->map_parent = 0;
    record->map_child = previous;
    cell->who = id;
    return previous;
}

MapObjectBase* unlink_world_used_thing_from_map_cell(WorldRuntime& c, MapCell* cell, ThingId id) {
    auto* record = base(c.entities[id].payload);
    if (record->map_parent) {
        base(c.entities[record->map_parent].payload)->map_child = record->map_child;
        if (record->map_child) {
            base(c.entities[record->map_child].payload)->map_parent = record->map_parent;
        }
    } else {
        cell->who = record->map_child;
        if (cell->who) {
            base(c.entities[cell->who].payload)->map_parent = 0;
        }
    }
    record->map_parent = record->map_child = 0;
    return record;
}

bool is_world_used_thing_mobile(const ThingHeader* record) {
    return record->kind == ThingKind::person || is_staff_record_kind(record->kind) || record->kind == ThingKind::chat_presence;
}

void unlink_world_used_thing_map_and_event_lists(WorldRuntime& c, ThingHeader* record) {
    const auto* object = base(record);
    const CellId cell = static_cast<CellId>(((object->y >> 8) << 7) + (object->x >> 8) + 1);
    if (!c.simulation.map_cell_records || !cell || cell > map_cell_count) {
        throw std::out_of_range("unbound or invalid map cell");
    }
    unlink_world_used_thing_from_map_cell(c, &c.simulation.map_cell_records[cell - 1], record->id);
    if (record->kind == ThingKind::park_object) {
        world_operations::destroy_mesh(c, reinterpret_cast<ParkObjectRuntimeView*>(record)->mesh_instance_id);
    }
    if (is_world_used_thing_mobile(record) && c.view.cursor_hit == record) {
        c.view.cursor_hit = nullptr;
    }
    remove_world_used_thing_from_all_events(c, get_world_event_subscription_registry(c), record);
}

std::uint32_t append_pending_world_used_thing_removal(WorldRuntime& c, ThingId id) {
    return append_pending_world_used_thing_removal(c, c.entities.removals, id);
}

std::uint32_t append_pending_world_used_thing_removal(WorldRuntime& c, RemovalQueue& queue, ThingId id) {
    auto* node = static_cast<RemovalNode*>(c.ports.memory.allocate(Allocation::removal_node, sizeof(RemovalNode)));
    if (!node) {
        throw std::bad_alloc();
    }
    if (node) {
        node->previous = nullptr;
        node->id = id;
        node->next = nullptr;
    }
    node->previous = queue.tail;
    if (queue.tail) {
        queue.tail->next = node;
    } else {
        queue.head = node;
    }
    queue.tail = node;
    return ++queue.count;
}

ThingId* pop_pending_world_used_thing_removal(WorldRuntime& c, ThingId* out) {
    return pop_pending_world_used_thing_removal(c, c.entities.removals, out);
}

ThingId* pop_pending_world_used_thing_removal(WorldRuntime& c, RemovalQueue& queue, ThingId* out) {
    auto* tail = queue.tail;
    if (!tail) {
        return out; //Empty pop leaves *out untouched.
    }
    queue.tail = tail->previous;
    if (queue.tail) {
        queue.tail->next = nullptr;
    } else {
        queue.head = nullptr;
    }
    const ThingId id = tail->id;
    --queue.count;
    c.ports.memory.release(Allocation::removal_node, tail);
    *out = id;
    return out;
}

void unlink_and_free_world_used_thing(WorldRuntime& c, ThingMetadata* entry) {
    if (entry->active_next) {
        entry->active_next->active_previous = entry->active_previous;
    }
    if (entry->active_previous) {
        entry->active_previous->active_next = entry->active_next;
    } else {
        c.entities.active_head = entry->active_next;
    }
    entry->active_next = entry->active_previous = nullptr;
    entry->free_next = c.entities.free_head;
    c.entities.free_head = entry;
    if (entry->payload && is_staff_record_kind(entry->payload->kind)) {
        release_staff_membership_storage(c, *reinterpret_cast<StaffRuntimeRecord*>(entry->payload));
    }
    c.entities.release_payload(c.ports.memory, entry->payload);
    entry->payload = nullptr;
}

void drain_pending_world_used_thing_removals(WorldRuntime& c) {
    while (c.entities.removals.count) {
        if (!c.entities.removals.tail) {
            throw std::logic_error("removal queue count disagrees with its nodes");
        }
        ThingId id = c.entities.removals.tail->id;
        pop_pending_world_used_thing_removal(c, &id);
        unlink_and_free_world_used_thing(c, &c.entities[id]);
    }
}

void release_world_used_thing_removal_queue_nodes(WorldRuntime& c) {
    if (c.entities.removal_queue_released & 1) {
        return;
    }
    c.entities.removal_queue_released |= 1;
    delete_world_used_thing_removal_queue(c, &c.entities.removals, 0);
}

RemovalQueue* delete_world_used_thing_removal_queue(WorldRuntime& c, RemovalQueue* queue, std::uint32_t flags) {
    auto* node = queue->tail;
    queue->head = queue->tail = nullptr;
    while (node) {
        auto* previous = node->previous;
        c.ports.memory.release(Allocation::removal_node, node);
        node = previous;
    }
    queue->count = 0; //Queue teardown does not release used-thing payloads.
    if (flags & 1) {
        c.ports.memory.release(Allocation::removal_queue, queue);
    }
    return queue;
}

//Game.exe 6519872: notification/cleanup precede both pending flag and tombstone.
std::uint32_t retire_world_used_thing(WorldRuntime& c, ThingHeader* record) {
    if (auto* prepared = c.missions.mission_transition.prepared.get(); prepared && prepared->target == record->id && !prepared->target_consumed) {
        prepared->target_invalidated = true;
    }
    WorldEvent event { RetirementEvent { record->id } };
    dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
    cleanup_entity_payload(c, record);
    const ThingId id = record->id;
    record->retirement_pending = 1;
    const auto count = append_pending_world_used_thing_removal(c, id);
    record->kind = ThingKind::retired;
    return count;
}

void initialize_game_mode_selector(WorldRuntime& c) {
    const auto flags = c.mode.configuration;
    std::uint32_t selected;
    if (flags & 0x2000000u) {
        c.mode.game = GameMode::code_2;
        selected = 0x2000000u;
    } else if (flags & 0x1000000u) {
        c.mode.game = GameMode::online;
        selected = 0x1000000u;
    } else if ((flags & 0x800000u) || !(flags & 0x20000000u)) {
        c.mode.game = GameMode::code_0;
        selected = 0x800000u;
    } else {
        c.mode.game = GameMode::code_3;
        selected = 0x20000000u;
    }
    c.mode.configuration = (flags & 0xDC7FFFFFu) | selected;
}

void update_world_managers(WorldRuntime& c, World* world) {
    world_operations::arrivals(c, &world->arrival);
    world_operations::staff_pool(c, world);
    process_world_calendar_changes(c, &world->calendar);
    if (c.referenced_world()->saved.game_tick % 100 == 0 && c.mode.scheduler != 17) {
        update_awards(c, world->award_receiver.data());
    }
}

//Game.exe 6591568. next_id is read AFTER callbacks, never cached in advance.
void run_world_simulation_tick(WorldRuntime& c, World* world) {
    const auto tick = ++world->saved.game_tick;
    if ((tick >> 2) != c.timing.last_tick_div4) {
        c.timing.last_tick_div4 = tick >> 2;
        ++c.timing.first_hour_time;
    }
    if (c.mode.source_recreation) {
        for (ThingId id = first_id(c); id;) {
            auto* record = c.entities[id].payload;
            if (record->kind == ThingKind::advisor) {
                world_operations::record_update(c, RecordUpdate::advisor, record);
            } else if (record->kind == ThingKind::animation_manager) {
                update_world_animation_state(c, reinterpret_cast<AnimationManagerRecord*>(record));
            }
            id = next_id(c, record);
        }
        return;
    }
    bool objects_only = c.mode.scheduler == 17;
    if (!objects_only) {
        ensure_mode(c, c.mode.tick_mode_initialized);
        objects_only = c.mode.game == GameMode::code_3;
    }
    if (objects_only || world->saved.state != WorldState::transition) {
        for (ThingId id = first_id(c); id;) {
            auto* record = c.entities[id].payload;
            if (objects_only) {
                if (record->kind == ThingKind::park_object) {
                    world_operations::record_update(c, RecordUpdate::park_object_tick, record);
                    world_operations::record_update(c, RecordUpdate::park_object_state, record);
                }
            } else if (record && !record->retirement_pending) {
                update_world_used_thing(c, record);
            }
            id = next_id(c, record);
        }
    }
    check_closed_park_gate(c, world);
    drain_pending_world_used_thing_removals(c);
    update_world_managers(c, world);
    c.ports.entities.replay_tick(c);
}

void clear_world_records_before_save_restore(WorldRuntime& c, World* world) {
    cancel_prepared_mission_transition(c);
    c.missions.mission_transition.failure.reset();
    ++c.missions.mission_transition.world_epoch;
    world->saved.state = WorldState::clearing;
    for (ThingId id = first_id(c); id;) {
        auto* record = c.entities[id].payload;
        if (!record->retirement_pending) {
            retire_world_used_thing(c, record);
        }
        id = next_id(c, record);
    }
    drain_pending_world_used_thing_removals(c);
    world->saved.state = WorldState::code_1;
}
} //namespace simcoaster
