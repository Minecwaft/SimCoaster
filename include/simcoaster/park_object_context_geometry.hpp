#pragma once
#include "world_types.hpp"

namespace simcoaster {
struct WorldRuntime;
std::optional<WorldCellCoordinates> find_park_object_track_edit_endpoint(WorldRuntime&, const ParkObjectRuntimeView&);
//Native465D10: trace the unlinked kart/water endpoint, preserve its overlap
//bookkeeping, and move camera focus by endpoint minus the fresh mouse pick.
//A zero fresh cursor suppresses only the camera delta, not the trace.
bool adjust_park_object_context_track_camera(WorldRuntime&, const ParkObjectRuntimeView&, CellId fresh_cursor);
//465B90, gated by485B30 in52F870. Uses the current geometry placement,
//truncates the combined cell/focus delta, and returns to the cell center.
bool adjust_park_object_context_coaster_camera(WorldRuntime&, const ParkObjectRuntimeView&, CellId fresh_cursor);
} //namespace simcoaster
