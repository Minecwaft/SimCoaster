#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace simcoaster {
struct WorldRuntime;
struct Stream;
struct RideDescriptionBalance;
enum class TrackRideTransition : std::int32_t {
    inactive = 0,
    pending_open = 1,
    active = 2,
    pending_close = 3,
    reset_for_load = 4
};
enum class TrackRideFault : std::int32_t {
    none = 0,
    worn = 1,
    broken = 2
};
enum class TrackSimulationFlag : std::uint32_t {
    active = 1,
    initial_first = 2,
    follow_peer = 4,
    follow_route = 8,
    random_node = 16,
    return_or_unload = 32,
    open_approach = 64,
    render_hidden = 0x200,
    secondary_surface = 0x2000,
    drive_enabled = 0x4000,
    retarget_on_contact = 0x8000,
    rebuild_target = 0x10000,
    cross_region_waypoint = 0x20000,
    collision_contact = 0x40000,
    approach_damping = 0x80000,
    needs_type_setup = 0x100000,
    collision_sound_this_step = 0x200000,
    primary = 0x400000,
    four_point_bob = 0x1000000,
    secondary_trailing = 0x2000000,
    spawn_effect_pending = 0x4000000,
};

struct TrackRideRider {
    std::uint32_t person_id{};
    std::int32_t model_object_index{ -1 }, seat_slot{};
};

struct TrackRideGeometryRegion {
    std::uint32_t flags{};
    std::int32_t origin_x{}, origin_y{}, extent_x{}, extent_y{};
    std::optional<std::int32_t> linked_node_slot;
};

struct TrackRideLinkNode {
    std::uint32_t flags{};
    std::int32_t pool_slot{}, x{}, y{};
    std::optional<std::size_t> geometry;
};

struct TrackRideAuxiliary {
    std::uint32_t flags{};
    std::int32_t x{}, y{};
    std::vector<std::size_t> geometry;
    std::optional<std::int32_t> linked_node_slot;
    //Grid refresh supplies this ordinal; native save restoration passes0.
    //It is runtime metadata and is deliberately absent from the save image.
    std::uint32_t track_cell_ordinal{};
    std::uint32_t runtime_flags{}, mesh_instance_id{};
    std::uint32_t definition_metadata_word_10{}; //Catalog +0x10; 3500 selects tunnel behavior.
};

//Host fields have native alignment. Saved pointer words are ignored, never
//reinterpreted as addresses; relationships are reconstructed separately.
struct TrackRideSimulation {
    //Original fixed pool slot, separate from the persisted 172-byte image.
    std::int32_t pool_slot{ -1 };
    std::uint32_t flags{};
    std::uint32_t mesh_template_index{};
    std::uint32_t mesh_instance_id{};
    std::uint32_t secondary_mesh_instance_id{};
    std::uint32_t mesh_animation_sequence_id{};
    std::uint32_t pending_effect_code{};
    std::uint32_t queued_effect_code{};
    std::uint32_t unknown_1c{};
    std::uint32_t positioned_sound_request{};
    std::int32_t particle_model_node_index{};
    std::int32_t model_node_index{};
    std::uint32_t particle_runtime_handle{};
    std::int32_t position_x{};
    std::int32_t position_y{};
    std::int32_t velocity_x{};
    std::int32_t velocity_y{};
    std::int32_t velocity_x_secondary{};
    std::int32_t velocity_y_secondary{};
    std::int32_t speed_magnitude{};
    std::int32_t steering_heading{};
    std::int32_t heading{};
    std::int32_t desired_heading{};
    std::int32_t angular_velocity{};
    std::int32_t angular_velocity_change{};
    std::int32_t collision_radius{};
    std::int32_t thrust_variation{};
    std::int32_t target_x{};
    std::int32_t target_y{};
    std::int32_t target_offset_x{};
    std::int32_t target_offset_y{};
    std::int32_t selected_track_node_slot{};
    std::int32_t selected_peer_simulation_pool_index{};
    std::int32_t unknown_motion_0e{};
    std::int32_t close_budget_remaining{};
    std::int32_t retarget_budget_remaining{};
    std::int32_t simulation_tick_count{};
    float render_height{};
    std::int32_t boundary_correction_count{};
    std::uint32_t boundary_window_steps{};
    std::optional<std::size_t> active_geometry;
    std::vector<TrackRideRider> riders;
};

struct TrackRide {
    std::uint32_t handle{}, object_definition_id{}, orientation{};
    std::int32_t template_tag{}, origin_x{}, origin_y{}, initial_close_budget{}, collision_radius{}, region_extent{};
    std::int32_t interpolation_percent{}, first_mesh_template_index{}, mesh_template_variant_count{},
        transition_state{};
    std::array<std::int32_t, 4> start_values{}, end_values{}, current_values{};
    std::uint32_t simulation_record_limit{};
    std::int32_t simulation_target_distance_threshold{};
    std::int32_t simulation_spawn_x{}, simulation_spawn_y{}, simulation_target_x{}, simulation_target_y{};
    std::vector<std::size_t> geometry;
    std::vector<TrackRideLinkNode> nodes;
    //Subtype-2 creates three global rings without native owner links. Keep
    //their host ownership explicit so destroying that owner cannot leak them.
    //Reconstructed with geometry; not part of the original save wire image.
    std::vector<std::size_t> unlinked_geometry;
    std::vector<TrackRideAuxiliary> auxiliary;
    std::vector<TrackRideSimulation> simulations;
    std::vector<TrackRideRider> queued_riders, completed_riders;
    std::optional<std::size_t> first_simulation, transition_node;
    std::uint32_t attached_rider_count{};
    TrackRideFault fault{}; //RSSE re-establishes this transient mode after load.
};

struct TrackRideManager {
    std::array<std::unique_ptr<TrackRide>, 64> rides;
    //Indexed in original pool scan order; inactive slots may be reused.
    std::vector<TrackRideGeometryRegion> geometry;
    std::uint32_t simulation_step_counter{}, random_state{};
    TrackRide* find(std::uint32_t handle);
};

//The ordinary subtype constructors and all thirteen auxiliary track-piece
//cases are rebuilt. Tag -11 consumes the shared Bumper.sam receiver.
bool build_standard_track_ride_geometry(TrackRideManager&, TrackRide&);
bool build_described_track_ride_geometry(TrackRideManager&, TrackRide&, const RideDescriptionBalance&);
bool add_track_ride_geometry_piece(TrackRideManager&, TrackRide&, TrackRideAuxiliary&);
//Adds records to the supplied manager; caller owns teardown of earlier state.
//An occupied saved slot or invalid chunk is rejected.
bool restore_track_ride_save_section(WorldRuntime&, Stream&, TrackRideManager&);
//Finds the first unused original simulation-pool slot, or nullopt for an
//exhausted or inconsistent manager. Used when restoring or creating cars.
std::optional<std::int32_t> first_free_track_ride_simulation_slot(const TrackRideManager&);
//Serializes the kind-1 track section in native order. Runtime pointers are
//encoded as zero; they are ignored and rebound by the original loader.
bool write_track_ride_save_section(Stream&, const TrackRideManager&);
} //namespace simcoaster
