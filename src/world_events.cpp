#include "simcoaster/world_events.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/save_schema.hpp"
#include <memory>
#include <new>
#include <stdexcept>

namespace simcoaster {
void* EventNodeResource::do_allocate(std::size_t bytes, std::size_t alignment) {
    if (alignment > alignof(std::max_align_t)) {
        throw std::bad_alloc();
    }
    auto* result = services_.allocate(Allocation::event_tree_node, bytes);
    if (!result) {
        throw std::bad_alloc();
    }
    return result;
}

void EventNodeResource::do_deallocate(void* pointer, std::size_t, std::size_t) {
    services_.release(Allocation::event_tree_node, pointer);
}

bool EventNodeResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}

EventRegistry* initialize_world_event_subscription_registry(WorldRuntime& c, EventRegistry* storage) {
    return std::construct_at(storage, c.ports.memory);
}

void destroy_world_event_subscription_registry(WorldRuntime&, EventRegistry* registry) {
    std::destroy_at(registry);
}

int clear_world_event_subscription_registry(WorldRuntime&, EventRegistry* registry) {
    for (auto& slot : registry->slots) {
        slot.subscribers.clear();
    }
    return 0;
}

bool register_world_used_thing_for_event(WorldRuntime&, EventRegistry* registry, ThingHeader* record, int category) {
    if (category < 0 || static_cast<std::size_t>(category) >= registry->slots.size()) {
        throw std::out_of_range("world event category");
    }
    return registry->slots[static_cast<std::size_t>(category)].subscribers.insert(record->id).second;
}

int remove_world_used_thing_from_all_events(WorldRuntime&, EventRegistry* registry, ThingHeader* record) {
    int removed = 0;
    for (auto& slot : registry->slots) {
        removed += static_cast<int>(slot.subscribers.erase(record->id));
    }
    return removed;
}

void deliver_world_record_event(WorldRuntime& c, ThingHeader* record, WorldEvent* event, void*) {
    dispatch_world_used_thing_event(c, record, event);
}

void dispatch_heap_payload_event(WorldRuntime& c, CompanyEmailMessage* payload) {
    WorldEvent event { HeapPayloadEvent { payload } };
    try {
        dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
    } catch (...) {
        destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
        throw;
    }
    destroy_heap_payload_event(c, &event.get<HeapPayloadEvent>());
}

int dispatch_world_event_to_subscribers(WorldRuntime& c, EventRegistry* registry, WorldEvent* event, EventDelivery deliver, void* state) {
    const auto code = event->type();
    if (code < 0 || static_cast<std::size_t>(code) >= registry->slots.size()) {
        throw std::out_of_range("world event category");
    }
    if (code == 33) {
        const auto id = event->get<RetirementEvent>().retired_id;
        remove_world_used_thing_from_all_events(c, registry, c.entities.at(id).payload);
    }
    auto& subscribers = registry->slots[static_cast<std::size_t>(code)].subscribers;
    auto current = subscribers.begin();
    while (current != subscribers.end()) {
        const auto id = *current;
        deliver(c, c.entities.at(id).payload, event, state);
        //Read the live set after callbacks; a receiver can unsubscribe itself.
        current = subscribers.upper_bound(id);
    }
    return 1;
}

namespace {
template <save::Scalar T>
bool transfer(Stream& stream, T& value, bool writing) {
    return save::transfer_value(value, writing ? save::Direction::write : save::Direction::read, [&](std::span<std::byte> bytes) {
        return (writing ? stream.write(bytes) : stream.read(bytes)) == static_cast<std::int32_t>(bytes.size());
    });
}
} //namespace

int save_world_event_subscriptions(WorldRuntime&, EventRegistry* registry, Stream& stream) {
    auto slots = static_cast<std::uint32_t>(registry->slots.size());
    if (!transfer(stream, slots, true)) {
        return 1;
    }
    for (const auto& slot : registry->slots) {
        auto count = static_cast<std::uint32_t>(slot.subscribers.size());
        if (!transfer(stream, count, true)) {
            return 1;
        }
        for (auto id : slot.subscribers) {
            if (!transfer(stream, id, true)) {
                return 1;
            }
        }
    }
    return 0;
}

int restore_world_event_subscriptions(WorldRuntime& c, EventRegistry* registry, Stream& stream) {
    clear_world_event_subscription_registry(c, registry);
    std::uint32_t count = 0;
    if (!transfer(stream, count, false) || count != registry->slots.size()) {
        return 1;
    }
    for (auto& slot : registry->slots) {
        if (!transfer(stream, count, false) || count > thing_slot_count) {
            return 1;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            ThingId id = 0;
            if (!transfer(stream, id, false) || id >= thing_slot_count || !slot.subscribers.insert(id).second) {
                return 1;
            }
        }
    }
    return 0;
}
} //namespace simcoaster
