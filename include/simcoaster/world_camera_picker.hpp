#pragma once
#include "world_staff_interaction.hpp"

namespace simcoaster {
//Original Camera toolbar65 selects handler kind9, independently of online7
//and grid interaction mode0. The reset callback owns native501490's toolbar,
//command-family, retained editor and construction cleanup on right DOWN.
void activate_world_camera_picker(WorldRuntime&, std::function<void()> reset_ui);
void cancel_world_camera_picker(WorldRuntime&);
const RideTrackPickInteraction* world_camera_picker_state(const WorldRuntime&);
void update_world_camera_picker(WorldRuntime&, StaffInteractionCursor);
void world_camera_picker_button(WorldRuntime&, WorldPointerButton, bool down);
void retire_world_camera_picker(WorldRuntime&, ThingId);
} //namespace simcoaster
