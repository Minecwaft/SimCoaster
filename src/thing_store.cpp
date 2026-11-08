#include "simcoaster/thing_store.hpp"
#include "simcoaster/world.hpp"

namespace simcoaster {
ThingStore::~ThingStore() {
    while (!owned_.empty()) {
        auto allocation = owned_.extract(owned_.begin());
        allocation.mapped()->release(Allocation::payload, allocation.key());
    }
}

void* ThingStore::allocate_payload(AllocationPort& services, std::size_t size) {
    auto* pointer = services.allocate(Allocation::payload, size);
    if (!pointer || ownership_ == EntityOwnership::allocator)
        return pointer;
    try {
        owned_.emplace(pointer, &services);
    } catch (...) {
        services.release(Allocation::payload, pointer);
        throw;
    }
    return pointer;
}

void ThingStore::release_payload(AllocationPort& services, void* pointer) {
    if (const auto found = owned_.find(pointer); found != owned_.end()) {
        auto* owner = found->second;
        owned_.erase(found);
        owner->release(Allocation::payload, pointer);
    } else {
        if (pointer && ownership_ == EntityOwnership::store)
            throw std::logic_error("Mismatched native runtime allocation lifetime");
        services.release(Allocation::payload, pointer);
    }
}
} //namespace simcoaster
