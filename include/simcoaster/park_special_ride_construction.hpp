#pragma once
#include "park_construction.hpp"

namespace simcoaster {
struct SpecialRideConstructionResult {
    ThingId object{};
    WorldCellCoordinates entry_approach{ -1, -1 }, exit_approach{ -1, -1 };
    WorldCellCoordinates primary_track_anchor{ -1, -1 }, secondary_track_anchor{ -1, -1 };
    std::uint32_t changed_neighbor_cells{};
};

//Native dialog readiness: registered live special, ticket threshold and all
//required staff assigned, including the original explicit debug override.
bool is_special_ride_construction_ready(WorldRuntime&, ThingId);
//Engine side of5E8230. Unready/already-activated requests do not mutate the
//park. UI handler ownership, dialog closing and camera focus belong to the
//frontend, which can consume these resulting physical construction anchors.
std::optional<SpecialRideConstructionResult>
activate_special_ride_for_construction(WorldRuntime&, ParkConstructionRuntime&, ThingId, bool record_action = true);
} //namespace simcoaster
