#include "simcoaster/save_stream.hpp"
#include "simcoaster/game_restore.hpp"
#include "simcoaster/particle_save.hpp"
#include "simcoaster/resource_variant_io.hpp"
#include "simcoaster/rsse_save.hpp"
#include "simcoaster/rsse_tour.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_camera.hpp"

namespace simcoaster {
namespace {
using save::read;
using save::write;

bool marker(Stream& stream, std::uint32_t expected) {
    std::uint32_t actual {};
    return read(stream, actual) && actual == expected;
}

bool restore_clocks(Stream& stream, RestoredClockState& clocks, GameRestoreBackends& backends) {
    if (!marker(stream, 1296388947u) || !read(stream, clocks.saved_simulation_snapshot_ms) || !read(stream, clocks.saved_unscaled_snapshot_ms)) {
        return false;
    }
    std::uint32_t sample {};
    if (!marker(stream, 1129074507u) || !read(stream, sample)) {
        return false;
    }
    clocks.high_resolution_offset_ms = sample - backends.high_resolution_time_ms();
    if (!marker(stream, 1447120468u)) {
        return false;
    }
    std::uint32_t discarded {}, step_delta {}, aux_delta {}, world_delta {};
    if (!read(stream, clocks.simulation_step_time_ms) || !read(stream, clocks.auxiliary_step_time_ms) || !read(stream, clocks.world_tick_boundary_ms) || !read(stream, clocks.simulation_substep_count) || !read(stream, clocks.scheduler_saved_reset_word) || !read(stream, discarded) || !read(stream, step_delta) || !read(stream, aux_delta) || !read(stream, world_delta)) {
        return false;
    }
    clocks.simulation_step_time_ms = clocks.simulation_frame_time_ms + step_delta;
    clocks.auxiliary_step_time_ms = clocks.simulation_frame_time_ms + aux_delta;
    clocks.world_tick_boundary_ms = clocks.simulation_frame_time_ms + world_delta;
    backends.restore_clock_snapshots(clocks.saved_simulation_snapshot_ms, clocks.saved_unscaled_snapshot_ms);
    return true;
}
} //namespace

bool restore_world_through_rsse(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, ParticleRuntime& particles, RestoredClockState& clocks, TrackRideManager& rides, GameRestoreBackends& backends) {
    if (!deserialize_world_state(c, world, stream, saves) || !restore_resource_variant_save_section(c, stream) || !restore_particle_save_section(particles, stream)) {
        return false;
    }
    expire_screen_particle_effects(particles);
    if (!marker(stream, 1346458196u) || restore_world_event_subscriptions(c, get_world_event_subscription_registry(c), stream) != 0 || !restore_clocks(stream, clocks, backends)) {
        return false;
    }
    if (!marker(stream, 1196644691u) || !restore_mesh_instance_save_section(c, stream, backends) || !marker(stream, 1381194067u) || !restore_track_ride_save_section(c, stream, rides) || !restore_rsse_tour_save_section(c, stream) || !restore_rsse_script_save_section(c, stream)) {
        return false;
    }
    return true;
}

bool write_world_through_rsse(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, const ParticleRuntime& particles, const RestoredClockState& clocks, const TrackRideManager& rides, GameSaveBackends& backends) {
    if (!serialize_world_state(c, world, stream, saves)
        || !write_resource_variant_save_section(c, stream)
        || !write_particle_save_section(particles, stream, false)
        || !write(stream, 1346458196u)
        || save_world_event_subscriptions(c, get_world_event_subscription_registry(c), stream) != 0
        || !write(stream, 1296388947u)
        || !write(stream, clocks.saved_simulation_snapshot_ms)
        || !write(stream, clocks.saved_unscaled_snapshot_ms)
        || !write(stream, 1129074507u)
        || !write(stream, backends.high_resolution_time_ms())
        || !write(stream, 1447120468u)
        || !write(stream, clocks.simulation_step_time_ms)
        || !write(stream, clocks.auxiliary_step_time_ms)
        || !write(stream, clocks.world_tick_boundary_ms)
        || !write(stream, clocks.simulation_substep_count)
        || !write(stream, clocks.scheduler_saved_reset_word)
        || !write(stream, 0u)
        || !write(stream, clocks.simulation_step_time_ms - clocks.simulation_frame_time_ms)
        || !write(stream, clocks.auxiliary_step_time_ms - clocks.simulation_frame_time_ms)
        || !write(stream, clocks.world_tick_boundary_ms - clocks.simulation_frame_time_ms)
        || !write(stream, 1196644691u)
        || !write_mesh_instance_save_section(c, stream, backends)
        || !write(stream, 1381194067u)
        || !write_track_ride_save_section(stream, rides)
        || !write_rsse_tour_save_section(c, stream)
        || !write_rsse_script_save_section(c, stream)) {
        return false;
    }
    return true;
}

bool write_world_through_camera(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, const ParticleRuntime& particles, const RestoredClockState& clocks, const TrackRideManager& rides, GameSaveBackends& backends) {
    return write_world_through_rsse(c, world, stream, saves, particles, clocks, rides, backends) && write(stream, 1381192517u) && write_camera_save_section(c, stream);
}

bool restore_world_through_camera(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, ParticleRuntime& particles, RestoredClockState& clocks, TrackRideManager& rides, GameRestoreBackends& backends) {
    return restore_world_through_rsse(c, world, stream, saves, particles, clocks, rides, backends) && marker(stream, 1381192517u) && restore_camera_save_section(c, stream);
}

bool write_world_through_coaster_snapshot(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, const ParticleRuntime& particles, const RestoredClockState& clocks, const TrackRideManager& rides, GameSaveBackends& backends, const CoasterSaveSection& coaster, const CoasterLimitsResolver& resolve) {
    return write_world_through_camera(c, world, stream, saves, particles, clocks, rides, backends) && write(stream, 1262570821u) && write_coaster_save_section(stream, coaster, resolve);
}

bool restore_world_through_coaster_snapshot(WorldRuntime& c, World& world, PositionedStream& stream, SaveServices& saves, ParticleRuntime& particles, RestoredClockState& clocks, TrackRideManager& rides, GameRestoreBackends& backends, CoasterSaveSection& coaster, const CoasterLimitsResolver& resolve) {
    return restore_world_through_camera(c, world, stream, saves, particles, clocks, rides, backends) && marker(stream, 1262570821u) && read_coaster_save_section(stream, coaster, resolve);
}
} //namespace simcoaster
