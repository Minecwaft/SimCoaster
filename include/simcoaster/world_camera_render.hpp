#pragma once
#include "mesh_scene.hpp"
#include <chrono>

namespace simcoaster {
struct WorldRuntime;
//Immediate World presentation before the next displayed frame. Input snapshots
//guard repeated calls; reapplying look uses the existing filtered attachment
//basis. Queries NEVER advance raw attachment or terrain-filter history.
void initialize_world_camera_render(WorldRuntime&);
//Native469135..469655: update the owned World camera-to-world pose once
//after the live selected embedded attachment has advanced for this frame.
//elapsed is the raw unscaled interval: native adds1ms and maps stalls over
//200ms to100ms. Raw history persists across mode exits and owner changes.
void advance_world_camera_render(WorldRuntime&, std::chrono::milliseconds elapsed);
//Native6CF8D0's frustum path uses live profile distance for flags&6,
//otherwise600. The modern GPU backend selects that path, rather than the
//alternate6CABB0 CPU terrain/ray-distance path. Near remains mode-owned.
//Raw host diagnostics with no initialized profile(distance0) retain the
//explicit far_clip_distance; this exception is not a native global default.
//This pure application does not resolve a pose or advance any history.
void apply_world_camera_projection(const WorldRuntime&, SceneCamera&);
//Pure application: renderers, pickers, sprites and audio share the cached
//native pose. Standalone SceneCamera orbit/fit behavior remains independent.
void apply_world_camera_render(const WorldRuntime&, SceneCamera&);
} //namespace simcoaster
