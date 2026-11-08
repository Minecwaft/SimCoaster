#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>
#include <utility>

namespace simcoaster {
bool is_special_ride_group_id_unregistered(const WorldRuntime& c, ThingId id) {
    const auto count = std::bit_cast<std::int32_t>(c.attractions.special_ride_group_count);
    for (std::int32_t i = 0; i < count; ++i) {
        if (c.attractions.special_ride_groups.at(static_cast<std::size_t>(i)).thing_id == id) {
            return false;
        }
    }
    return true;
}

SpecialRideRequirementType special_ride_type_for_ability_id(ThemeAbility ability) {
    const auto id = std::to_underlying(ability);
    if (id >= 1 && id <= 6) {
        return SpecialRideRequirementType::mechanic;
    }
    if (id == 7 || id == 8) {
        return SpecialRideRequirementType::handyman;
    }
    if (id == 9) {
        return SpecialRideRequirementType::guard;
    }
    if (id >= 10 && id <= 14) {
        return SpecialRideRequirementType::gardener;
    }
    return SpecialRideRequirementType::end;
}

void populate_special_ride_registry_entry(WorldRuntime& c, SpecialRideRegistryGroup& group, ThingId id) {
    group.thing_id = id;
    if (!id) {
        return;
    }
    const auto* payload = c.entities.at(id).payload;
    if (!payload) {
        throw std::invalid_argument("null special-ride record");
    }
    const auto definition_id = reinterpret_cast<const ParkObjectRuntimeView*>(payload)->definition_id;
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, definition_id);
    if (!definition) {
        throw std::out_of_range("missing special-ride definition");
    }
    std::size_t next = 0;
    for (std::size_t i = 0; i < definition->research.skill_set_counts.size(); ++i) {
        const auto count = std::bit_cast<std::int32_t>(definition->research.skill_set_counts[i]);
        for (std::int32_t j = 0; j < count; ++j) {
            //Native writes beyond the array for excessive positive sums. Reject the
            //actual invalid visit; installed in-range definitions match its behavior.
            const auto ability = static_cast<ThemeAbility>(i + 1);
            auto& member = group.members.at(next++);
            member.type = special_ride_type_for_ability_id(ability);
            member.ability = ability;
            member.assigned_staff = 0;
        }
    }
    for (; next < group.members.size(); ++next) {
        auto& member = group.members[next];
        member.type = SpecialRideRequirementType::end;
        member.ability = ThemeAbility::none;
        member.assigned_staff = 0;
    }
}

void append_special_ride_registry_entry(WorldRuntime& c, ThingId id) {
    const auto index = c.attractions.special_ride_group_count;
    if (index >= c.attractions.special_ride_groups.size()) {
        throw std::length_error("special-ride registry is full");
    }
    auto pending = c.attractions.special_ride_groups[index];
    populate_special_ride_registry_entry(c, pending, id);
    c.attractions.special_ride_groups[index] = pending;
    ++c.attractions.special_ride_group_count;
}
} //namespace simcoaster
