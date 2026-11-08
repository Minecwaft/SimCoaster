#pragma once
#include "simcoaster/world_domains_fwd.hpp"
#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include <cmath>
#include <limits>
#include <algorithm>

namespace simcoaster::person_use {
enum class Ingredient : std::int32_t {
    none = 0,
    fat = 1,
    salt = 2,
    ice = 3,
    sugar = 4
};
//Payment jump tables and goods-cost branches establish these numeric roles.
enum class Shop : std::int32_t {
    none = 0,
    gift = 1,
    food = 2,
    restaurant = 3,
    drinks_and_sweets = 4,
    costume = 5,
    balloon = 6,
    photo = 7,
    umbrella = 8
};

inline bool has(const ParkObjectRuntimeView& o, ParkObjectCapabilities flag) {
    return (std::to_underlying(o.capabilities) & std::to_underlying(flag)) != 0;
}

template <class T>
T& record(VisitorWorld& c, ThingId id) {
    return *reinterpret_cast<T*>(&person_detail::map_object(c, id));
}

inline ParkAnalyserState& analyser(VisitorWorld& c, bool primary = false) {
    return record<ParkAnalyserState>(c, (primary ? person_detail::primary_world(c) : person_detail::base_world(c)).saved.park_analyser);
}

inline void progress(ScriptWorld& c, ChallengeType type, std::int32_t amount) {
    add_progress_to_matching_world_challenges(record<ChallengeManagerRecord>(c, person_detail::base_world(c).saved.challenge_manager), type, amount, 0, false);
}

inline void adjust(float& value, double amount) {
    value = person_detail::meter(static_cast<double>(value) + amount);
}

inline std::int32_t integer(double value) {
    const auto integral = std::trunc(value), limit = std::ldexp(1.0, 63);
    if (!std::isfinite(integral) || integral < -limit || integral >= limit) {
        return 0;
    }
    return fixed::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(integral)));
}

inline std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(integer(value));
}

inline void thought(VisitorWorld& c, PersonRuntimeView& p, MobileThoughtCode code) {
    world_operations::set_mobile_entity_thought_bubble(c, &p.mobile.thought_history, code, false);
}

inline std::uint32_t random(VisitorWorld& c) {
    return static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c)));
}

inline void short_history(ParkObjectRuntimeView& o, std::size_t index) {
    if (o.history) {
        ++o.history->dword_history_30.at(index).temporary;
    }
}

inline void sprite_effect(VisitorWorld& c, PersonRuntimeView& p, std::int32_t effect) {
    c.ports.resources.submit_mobile_entity_positioned_template_request(c, p.mobile, c.resources.kids_catalog_slot, effect);
}
} //namespace simcoaster::person_use
