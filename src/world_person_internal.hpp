#pragma once
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/world_person.hpp"
#include <stdexcept>

namespace simcoaster::person_detail {
inline World& base_world(VisitorWorld& c) {
    return require_bound(c.simulation.current, "base World is required");
}

inline World& primary_world(VisitorWorld& c) {
    return require_bound(c.simulation.reference, "primary World is required");
}

inline CellId cell_id(const MapObjectBase& base) {
    return static_cast<CellId>(((base.y >> 8) << 7) + (base.x >> 8) + 1);
}

inline MapObjectBase& map_object(VisitorWorld& c, ThingId id) {
    auto* record = c.entities.at(id).payload;
    if (!record)
        throw std::logic_error("missing map object");
    return *reinterpret_cast<MapObjectBase*>(record);
}

inline ParkObjectRuntimeView& park_object(VisitorWorld& c, ThingId id) {
    return *reinterpret_cast<ParkObjectRuntimeView*>(&map_object(c, id));
}

//Optional remembered destinations may outlive their object. Keep this
//separate from the strict lookup used while an object owns the Person.
inline ParkObjectRuntimeView* find_live_park_object(VisitorWorld& c, ThingId id) {
    if (!id || id >= c.entities.size())
        return nullptr;
    auto* record = c.entities[id].payload;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object)
        return nullptr;
    return reinterpret_cast<ParkObjectRuntimeView*>(record);
}

inline std::uint32_t squared_cell_distance(CellId a, CellId b) {
    const auto x = static_cast<std::uint32_t>(a) - 1u, y = static_cast<std::uint32_t>(b) - 1u;
    const auto dx = static_cast<std::int32_t>(x & 127u) - static_cast<std::int32_t>(y & 127u);
    const auto dy = std::bit_cast<std::int32_t>((x >> 7) - (y >> 7));
    return static_cast<std::uint32_t>(dx) * static_cast<std::uint32_t>(dx)
         + static_cast<std::uint32_t>(dy) * static_cast<std::uint32_t>(dy);
}

inline float meter(double value) {
    return value >= 0 ? (value <= 100 ? static_cast<float>(value) : 100.0f) : 0.0f;
}

inline void initialize_mode(VisitorWorld& c) {
    if (!(c.visitors.mobile_effects_mode_initialized & 1)) {
        c.visitors.mobile_effects_mode_initialized |= 1;
        initialize_game_mode_selector(c);
    }
}
} //namespace simcoaster::person_detail
