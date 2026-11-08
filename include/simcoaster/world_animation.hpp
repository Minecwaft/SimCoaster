#pragma once
#include "world.hpp"

namespace simcoaster {
AnimationManagerRecord* initialize_world_animation_state(WorldRuntime&, AnimationManagerRecord*);
MissionControllerRecord* initialize_world_cutscene_state(WorldRuntime&, MissionControllerRecord*);
int reset_world_animation_runtime_state(WorldRuntime&, AnimationManagerRecord*);
void clear_global_animation_target(WorldRuntime&);
bool has_active_world_animation_target(const WorldRuntime&);
bool is_world_animation_profile_active(const WorldRuntime&, const AnimationManagerRecord*, AnimationPlaybackProfile);
std::uint8_t request_global_animation_close_if_pending(WorldRuntime&);
void latch_world_animation_completion_if_target_active(WorldRuntime&);
std::intptr_t handle_world_animation_playback_window_event(WorldRuntime&, UiControl*, int event, std::uint32_t id, std::intptr_t value);
std::uint32_t complete_world_animation_state(WorldRuntime&, AnimationManagerRecord*, RsseScriptState* script_state);
//Callers discard native EAX; the Objectives tail call has a void contract.
void update_world_animation_state(WorldRuntime&, AnimationManagerRecord*);
} //namespace simcoaster
