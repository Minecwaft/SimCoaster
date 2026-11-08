#pragma once
#include "entity_catalog.hpp"
#include "world_types.hpp"
#include <unordered_map>

namespace simcoaster {
struct AllocationPort;
enum class EntityOwnership {
    allocator,
    store
};

//Stable slots and deferred removal have one owner. Supplied-storage records
//may be linked without transferring ownership; owned allocations enter through
//allocate_payload and are released even when staging or construction throws.
class ThingStore {
    std::array<ThingMetadata, thing_slot_count> slots_ {};
    std::unordered_map<void*, AllocationPort*> owned_;
    EntityOwnership ownership_;

public:
    ThingMetadata *active_head {}, *free_head {};
    RemovalQueue removals {};
    std::uint8_t removal_queue_released {};

    explicit ThingStore(EntityOwnership ownership = EntityOwnership::allocator) : ownership_(ownership) {}

    ~ThingStore();
    ThingStore(const ThingStore&) = delete;
    ThingStore& operator=(const ThingStore&) = delete;

    decltype(auto) operator[](this auto& self, std::size_t id) {
        return (self.slots_[id]);
    }

    decltype(auto) at(this auto& self, std::size_t id) {
        return self.slots_.at(id);
    }

    constexpr std::size_t size() const {
        return thing_slot_count;
    }

    auto begin(this auto& self) {
        return self.slots_.begin();
    }

    auto end(this auto& self) {
        return self.slots_.end();
    }

    auto data(this auto& self) {
        return self.slots_.data();
    }

    void* allocate_payload(AllocationPort&, std::size_t);
    void release_payload(AllocationPort&, void*);

    template <class T, class Self>
    auto at_payload(this Self& self, std::size_t id) {
        using Payload = std::conditional_t<std::is_const_v<Self>, std::add_const_t<T>, T>;
        const auto& slot = self.slots_.at(id);
        return entity_assume<Payload>(slot.payload, slot.allocation_kind == ThingKind::unknown_0 && slot.payload ? slot.payload->kind : slot.allocation_kind);
    }

    template <class T, class Self>
    auto payload(this Self& self, std::size_t id) {
        using Payload = std::conditional_t<std::is_const_v<Self>, std::add_const_t<T>, T>;
        const auto& slot = self.slots_[id];
        return entity_assume<Payload>(slot.payload, slot.allocation_kind == ThingKind::unknown_0 && slot.payload ? slot.payload->kind : slot.allocation_kind);
    }

    template <class T, class Self>
    auto get(this Self& self, ThingId id) {
        using Payload = std::conditional_t<std::is_const_v<Self>, std::add_const_t<T>, T>;
        auto* header = id && id < self.size() ? self.slots_[id].payload : nullptr;
        return header ? entity_cast<Payload>(header, header->kind) : nullptr;
    }
};
} //namespace simcoaster
