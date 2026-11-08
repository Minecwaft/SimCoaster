#pragma once
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster::object_access {
inline World& world(WorldRuntime& c) {
    return require_bound(c.simulation.current, "Park-object UI World is not bound");
}

template <class T>
T& record(WorldRuntime& c, ThingId id, ThingKind kind) {
    if (!id || id >= c.entities.size())
        throw std::out_of_range("Park-object UI record ID");
    auto* header = c.entities[id].payload;
    if (!header || header->id != id || header->kind != kind || header->retirement_pending)
        throw std::out_of_range("Park-object UI record is no longer live");
    return *reinterpret_cast<T*>(header);
}

inline std::u16string component(const std::array<char16_t, 33>& name) {
    const auto end = std::ranges::find(name, char16_t{});
    if (end == name.end())
        throw std::length_error("Unterminated park-object name component");
    return { name.begin(), end };
}
} //namespace simcoaster::object_access
