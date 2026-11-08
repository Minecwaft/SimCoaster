#pragma once
#include "world.hpp"

namespace simcoaster {
bool is_special_ride_group_id_unregistered(const WorldRuntime&, ThingId);
SpecialRideRequirementType special_ride_type_for_ability_id(ThemeAbility);
void populate_special_ride_registry_entry(WorldRuntime&, SpecialRideRegistryGroup&, ThingId);
void append_special_ride_registry_entry(WorldRuntime&, ThingId);
} //namespace simcoaster
