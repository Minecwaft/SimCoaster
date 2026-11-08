#pragma once
#include "world_domains_fwd.hpp"
#include <cstdint>

namespace simcoaster {
struct DefinitionGrid;
struct ParkObjectRuntimeView;
void apply_set_ride_action(AttractionWorld&, const std::uint16_t* definition_id, std::uint32_t secondary);
void select_object_definition_and_placement_grid(AttractionWorld&,
                                                 std::uint16_t definition_id,
                                                 DefinitionGrid* override_grid = nullptr);
void update_park_object_entry_exit_neighbor_flags(AttractionWorld&, ParkObjectRuntimeView&, bool clear);
} //namespace simcoaster
