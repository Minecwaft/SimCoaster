#pragma once
#include "world_save.hpp"
#include <memory_resource>
#include <set>

namespace simcoaster {
inline constexpr std::size_t world_event_slot_count = 45;

class EventNodeResource final : public std::pmr::memory_resource {
    AllocationPort& services_;
    void* do_allocate(std::size_t, std::size_t) override;
    void do_deallocate(void*, std::size_t, std::size_t) override;
    bool do_is_equal(const std::pmr::memory_resource&) const noexcept override;

public:
    explicit EventNodeResource(AllocationPort& services) : services_(services) {}
};

struct EventSubscriptionSlot {
    std::pmr::set<ThingId> subscribers;

    explicit EventSubscriptionSlot(std::pmr::memory_resource* resource) : subscribers(resource) {}
};

struct EventRegistry {
    EventNodeResource resource;
    std::array<EventSubscriptionSlot, world_event_slot_count> slots;

    template <std::size_t... I>
    EventRegistry(AllocationPort& services, std::index_sequence<I...>)
        : resource(services),
          slots{ (static_cast<void>(I), EventSubscriptionSlot{ &resource })... } {}

    explicit EventRegistry(AllocationPort& services)
        : EventRegistry(services, std::make_index_sequence<world_event_slot_count>{}) {}

    EventRegistry(const EventRegistry&) = delete;
    EventRegistry& operator=(const EventRegistry&) = delete;
};

using EventDelivery = void (*)(WorldRuntime&, ThingHeader*, WorldEvent*, void*);
void deliver_world_record_event(WorldRuntime&, ThingHeader*, WorldEvent*, void*);
EventRegistry* initialize_world_event_subscription_registry(WorldRuntime&, EventRegistry*);
void destroy_world_event_subscription_registry(WorldRuntime&, EventRegistry*);
int clear_world_event_subscription_registry(WorldRuntime&, EventRegistry*);
bool register_world_used_thing_for_event(WorldRuntime&, EventRegistry*, ThingHeader*, int category);
int remove_world_used_thing_from_all_events(WorldRuntime&, EventRegistry*, ThingHeader*);
int dispatch_world_event_to_subscribers(WorldRuntime&, EventRegistry*, WorldEvent*, EventDelivery, void* state);
//Release the wrapper's live payload after delivery, including receiver replacement
//and exception paths. Payload destruction is deliberately a raw release.
void dispatch_heap_payload_event(WorldRuntime&, CompanyEmailMessage* payload);
int save_world_event_subscriptions(WorldRuntime&, EventRegistry*, Stream&);
int restore_world_event_subscriptions(WorldRuntime&, EventRegistry*, Stream&);
} //namespace simcoaster
