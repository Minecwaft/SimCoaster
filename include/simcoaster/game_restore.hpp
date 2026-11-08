#pragma once
#include "mesh_restore.hpp"
#include "coaster_save.hpp"
#include "track_ride_restore.hpp"
#include <cstdint>

namespace simcoaster {
struct WorldRuntime;
struct World;
struct Stream;
struct PositionedStream;
struct SaveServices;
struct ParticleRuntime;

struct RestoredClockState {
    std::uint32_t saved_simulation_snapshot_ms{}, saved_unscaled_snapshot_ms{};
    std::uint32_t high_resolution_offset_ms{}, simulation_frame_time_ms{};
    std::uint32_t simulation_step_time_ms{}, auxiliary_step_time_ms{}, world_tick_boundary_ms{};
    std::uint32_t simulation_substep_count{}, scheduler_saved_reset_word{};
};

//Renderer/model operations are explicit external boundaries.
struct GameRestoreBackends : MeshRestoreBackend {
    virtual ~GameRestoreBackends() = default;
    virtual std::uint32_t high_resolution_time_ms() = 0;
    virtual void restore_clock_snapshots(std::uint32_t simulation_ms, std::uint32_t unscaled_ms) = 0;
};

struct GameSaveBackends : MeshSaveBackend {
    virtual ~GameSaveBackends() = default;
    virtual std::uint32_t high_resolution_time_ms() = 0;
};

//Writes the native World-through-RSSE slice, including inter-section tags.
//The caller captures clock snapshots and initializes the particle runtime.
bool write_world_through_rsse(WorldRuntime&,
                              World&,
                              PositionedStream&,
                              SaveServices&,
                              const ParticleRuntime&,
                              const RestoredClockState&,
                              const TrackRideManager&,
                              GameSaveBackends&);
//Extends the same slice through the following camera section.
bool write_world_through_camera(WorldRuntime&,
                                World&,
                                PositionedStream&,
                                SaveServices&,
                                const ParticleRuntime&,
                                const RestoredClockState&,
                                const TrackRideManager&,
                                GameSaveBackends&);
bool restore_world_through_camera(WorldRuntime&,
                                  World&,
                                  PositionedStream&,
                                  SaveServices&,
                                  ParticleRuntime&,
                                  RestoredClockState&,
                                  TrackRideManager&,
                                  GameRestoreBackends&);
//Transfers the coaster snapshot after the camera; geometry and renderer
//reconstruction are still required before the snapshot becomes live state.
bool write_world_through_coaster_snapshot(WorldRuntime&,
                                          World&,
                                          PositionedStream&,
                                          SaveServices&,
                                          const ParticleRuntime&,
                                          const RestoredClockState&,
                                          const TrackRideManager&,
                                          GameSaveBackends&,
                                          const CoasterSaveSection&,
                                          const CoasterLimitsResolver&);
bool restore_world_through_coaster_snapshot(WorldRuntime&,
                                            World&,
                                            PositionedStream&,
                                            SaveServices&,
                                            ParticleRuntime&,
                                            RestoredClockState&,
                                            TrackRideManager&,
                                            GameRestoreBackends&,
                                            CoasterSaveSection&,
                                            const CoasterLimitsResolver&);
//Starts at the World section and stops immediately after RSSE. Particle and
//resource-variant runtimes must already be initialized by the game startup.
bool restore_world_through_rsse(WorldRuntime&,
                                World&,
                                PositionedStream&,
                                SaveServices&,
                                ParticleRuntime&,
                                RestoredClockState&,
                                TrackRideManager&,
                                GameRestoreBackends&);
} //namespace simcoaster
