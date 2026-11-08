#pragma once
#include "world_domains_fwd.hpp"
#include "track_ride_restore.hpp"

namespace simcoaster {
struct RsseScriptState;
enum class TrackRideOrientationFlags : std::uint32_t {
    none = 0,
    use_y = 1,
    flip_x = 2,
    flip_y = 4
};
//Placement supplies an already transformed internal origin. Handles encode
//signed subtype and first free row 0..63, independently of World thing IDs.
std::uint32_t create_track_ride(WorldRuntime&, std::int32_t template_tag, std::int32_t origin_x, std::int32_t origin_y, TrackRideOrientationFlags, std::uint16_t definition_id);
void destroy_track_ride(WorldRuntime&, std::uint32_t handle);
//Grid edits retain the owner, station/grid nodes and constructor geometry.
//Rebuild its dynamic path with these operations before reopening vehicles.
void prune_track_ride_dynamic_path(WorldRuntime&, std::uint32_t handle);
bool append_track_ride_auxiliary(WorldRuntime&, std::uint32_t handle, TrackRideAuxiliary);
enum class RsseBumpCommand : std::uint32_t {
    board_person = 1,
    pop_person = 2,
    start = 3,
    create_vehicle = 4,
    get_valid = 5,
    close = 6,
    begin_open = 7,
    set_broken = 8,
    set_worn = 9,
    close_release = 10,
    count_active = 11,
    attach_queued = 12,
    set_step_budget = 13,
    set_boundary_budget = 14,
    remove_vehicle = 16,
    set_endpoint_closed = 17
};
void begin_track_ride_open(WorldRuntime&, std::uint32_t handle);
void close_track_ride(WorldRuntime&, std::uint32_t handle, bool release_vehicles = false);
void set_track_ride_fault(WorldRuntime&, std::uint32_t handle, TrackRideFault);
void start_track_ride_simulations(WorldRuntime&, std::uint32_t handle);
void set_track_ride_close_budget(WorldRuntime&, std::uint32_t handle, std::int32_t budget);
TrackRideSimulation* create_track_ride_simulation(WorldRuntime&, std::uint32_t handle, std::uint32_t flags = 0);
int remove_track_ride_simulation(WorldRuntime&, std::uint32_t handle, bool prefer_empty);
int enqueue_track_ride_rider(WorldRuntime&, std::uint32_t handle, std::uint32_t person);
std::uint32_t dequeue_track_ride_rider(WorldRuntime&, std::uint32_t handle);
int attach_queued_track_ride_riders(WorldRuntime&, std::uint32_t handle);
void update_track_ride_tuning(TrackRide&, std::int32_t percentage);
void update_track_ride_simulations(WorldRuntime&);
void interpolate_track_ride_render_poses(WorldRuntime&, float step_fraction);
void step_rsse_bump_instruction(ScriptWorld&, RsseScriptState&);
} //namespace simcoaster
