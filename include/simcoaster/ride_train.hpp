#pragma once
#include "coaster_save.hpp"
#include <array>
#include <deque>
#include <optional>
#include <span>
#include <string_view>

namespace simcoaster {
struct DynamicTextureMeshBalance;

//The mesh loader supplies model-local bounds and attachment record indices.
//Template IDs are renderer-owned handles; the simulation owns the resulting
//configuration.
struct RideCarMeshMetadata {
    std::uint32_t template_id {};
    float min_z {}, max_z {}, root_z {};
    std::vector<std::uint32_t> head_nodes; //Resource-record indices, not model node indices.
    std::optional<std::size_t> primary_node {};
};

using RideCarMeshLoader = std::function<std::optional<RideCarMeshMetadata>(std::uint32_t, std::string_view)>;

struct RideCarType {
    std::uint32_t unique_id {}, template_id {};
    bool bend {};
    float vertical_offset {}, animation_speed {}, front_extent {}, rear_extent {};
    std::vector<std::uint32_t> head_nodes;
    std::optional<std::size_t> primary_node {};
};

struct RideTrainConfiguration {
    std::uint32_t maximum_trains {}, maximum_cars {}, head_count_per_train {}, head_count_all_trains {};
    std::uint32_t front_type {}, centre_type {}, rear_type {}, release_rate_ms {};
    float front_spacing {}, standard_spacing {}, end_spacing {};
    float winch_speed { 5 }, minimum_speed { 5 }, maximum_speed_at_minimum_setting { 80 }, maximum_speed_at_maximum_setting { 130 };
    float acceleration_per_height { 1.5f }, uphill_modifier { 1 }, downhill_modifier { 1 }, friction_multiplier { 0.8f };
    std::vector<RideCarType> car_types;
};

inline std::uint32_t ride_train_car_type(const RideTrainConfiguration& c, std::uint32_t i) {
    if (!i) {
        return c.front_type;
    }
    return i == c.maximum_cars - 1 ? c.rear_type : c.centre_type;
}

//Executes the native car-type construction and model-derived spacing/capacity
//calculation. Unavailable or invalid model resources are explicit failures.
std::optional<RideTrainConfiguration> build_ride_train_configuration(const DynamicTextureMeshBalance&, const RideCarMeshLoader&);
enum class RideTrainFlag : std::uint32_t {
    selected = 1,
    resources_active = 2,
    layout_dirty = 4,
    winch_driven = 8,
    speed_fade = 16,
    passenger_exchange_zone = 32,
    spacing_contact = 64,
    release_gap_pending = 128,
    scream_active = 0x100
};
enum class RideTrainCarFlag : std::uint32_t {
    active = 1,
    winch_driven = 2
};
enum class RideTrainTransition : std::uint32_t {
    target_15 = 1,
    winch_slow = 2,
    winch_fast = 4,
    speed_fade = 8,
    unpowered = 16
};

struct RideTrainCar {
    std::uint32_t flags {}, type_index {}, index {}, mesh_slot {}, passenger_capacity {}, reset_word {};
    float track_offset {}, relative_track_position {}, track_position {};
    std::array<float, 3> previous_sample {}, current_sample {}, mesh_transform_row2 {};
    std::vector<std::uint32_t> persons_a, persons_b;
};

struct RideTrain {
    std::uint32_t flags {}, car_count {}, passenger_count {}, lead_car {}, center_car {}, selected_car {};
    float distance {}, speed {}, activation_lower {}, activation_upper {}, rear_track_position {}, remaining_update_seconds {}, sound_fade_blend {};
    std::array<float, 3> lead_position {}, center_position {};
    std::uint32_t transition_state {}, positioned_request_a {}, positioned_request_b {}, rsse_value_slot_7 {}, rsse_value_slot_8 {};
    std::uint32_t rsse_value_slot_5 {}, rsse_value_slot_6 {}, passenger_voice_request {}, passenger_scream_request {};
    std::uint32_t available_passenger_slots {}, first_active_car {}, last_active_car {};
    std::vector<RideTrainCar> cars;
};

//Physical allocation and logical admission limits are distinct. The optional
//read stop represents a ring cursor, not a person ID or an empty-queue marker.
struct RidePassengerQueue {
    std::uint32_t capacity {}, entry_limit {}, auxiliary_occupancy {}, read_index {};
    std::optional<std::uint32_t> read_stop;
    std::deque<std::uint32_t> ids;

    std::size_t physical_space() const {
        const auto occupied = ids.size() + auxiliary_occupancy;
        return capacity > occupied ? capacity - occupied : 0;
    }

    std::size_t logical_space() const {
        const auto occupied = ids.size() + auxiliary_occupancy;
        return entry_limit > occupied ? entry_limit - occupied : 0;
    }

    bool push(std::uint32_t id) {
        if (!physical_space()) {
            return false;
        }
        ids.push_back(id);
        return true;
    }

    std::uint32_t pop(bool honor_stop = false) {
        if (ids.empty() || (honor_stop && read_stop == read_index)) {
            return 0;
        }
        const auto id = ids.front();
        ids.pop_front();
        read_index = (read_index + 1u) % capacity;
        return id;
    }
};
enum class RideDetailState : std::uint32_t {
    inactive = 1,
    vehicles_only = 2,
    passenger_mode_4 = 4,
    passenger_mode_8 = 8,
    run_16 = 0x10,
    run_32 = 0x20,
    paused = 0x40
};

struct RideTrainState {
    RideTrainConfiguration configuration;
    std::uint32_t active_train_count {}, peep_capacity_limit {};
    std::uint32_t runtime_flags {}, detail_state_flags {}, placement_flags {}, current_train_index {};
    std::int64_t last_simulation_time_ms {};
    float track_sample_spacing { 3.0f }, animation_speed_multiplier { 1.0f };
    RidePassengerQueue queue_a, queue_b;
    //Completed exits beyond B's native allocation retain ownership here. B's
    //physical capacity/read-stop semantics and the train timetable stay native.
    std::deque<std::uint32_t> exit_overflow;
    std::vector<RideTrain> trains;
};

void enqueue_completed_ride_exit(RideTrainState&, std::uint32_t person);
void promote_completed_ride_exits(RideTrainState&);
std::uint32_t dequeue_completed_ride_exit(RideTrainState&);

//Restore calls these mesh-owner operations in native order for every
//configured car with a loaded mesh template.
struct RideTrainMeshBackend {
    virtual ~RideTrainMeshBackend() = default;
    virtual std::optional<std::uint32_t> create_car_mesh(std::uint32_t mesh_template) = 0;
    virtual bool refresh_car_mesh(std::uint32_t slot, std::uint32_t mesh_template) = 0;
    virtual void unregister_car_mesh(std::uint32_t slot) = 0;
    virtual void activate_car_animation(std::uint32_t slot) = 0;
};

//Fresh initialization creates an inactive mesh for every configured car and
//applies the default per-train capacity. Parent placement is separate.
bool create_ride_train_state(RideTrainConfiguration, float track_sample_spacing, RideTrainMeshBackend&, RideTrainState&);
//Does not rebuild coaster track geometry or bind the parent runtime manager.
bool restore_ride_train_state(const CoasterRecordSave&, RideTrainConfiguration, float track_sample_spacing, RideTrainMeshBackend&, RideTrainState&);
//Recompute offsets/bounds and optionally redistribute peep capacity, as in the
//original. A zero requested count retains each train's count.
bool update_ride_train_layout(RideTrainState&, std::uint32_t desired_capacity, std::uint32_t requested_cars, bool distribute_capacity);
bool set_ride_train_peep_capacity(RideTrainState&, std::int32_t desired_capacity);

//Renderer, passenger and RSSE operations invoked by train selection and
//motion.
struct RideTrainEffectBackend {
    virtual ~RideTrainEffectBackend() = default;
    virtual void submit_train_spacing_effect(const RideTrain&) = 0;
    virtual std::uint32_t set_rsse_value(std::uint32_t request, std::uint32_t slot, std::int32_t value) = 0;
};

struct RideTrainLifecycleBackend : virtual RideTrainEffectBackend {
    virtual ~RideTrainLifecycleBackend() = default;
    virtual void register_car_mesh(std::uint32_t slot) = 0;
    virtual void unregister_car_mesh(std::uint32_t slot) = 0;
    virtual void reset_non_bend_car_animation(std::uint32_t slot) = 0;
    virtual void initialize_train_rsse(RideTrain&) = 0;
    virtual void clear_train_rsse(RideTrain&) = 0;
    virtual void release_train_passenger_meshes(RideTrain&, bool second_group) = 0;
};

bool set_ride_train_count(RideTrainState&, std::uint32_t count, RideTrainLifecycleBackend&);
bool initialize_ride_train_resources(RideTrainState&, RideTrain&, RideTrainLifecycleBackend&);
bool stop_ride_train_resources(RideTrainState&, RideTrain&, RideTrainLifecycleBackend&);
void resume_ride_train_motion(RideTrainState&);
bool adjust_ride_train_speed_for_spacing(RideTrainState&, std::uint32_t index, float last_sample_position, RideTrainEffectBackend&);
void set_ride_train_transition(RideTrain&, RideTrainTransition, std::int32_t control, bool recording_disabled, RideTrainEffectBackend&);
//Applies an already computed speed profile: selection/count, release spacing,
//initial car coordinates, speed, and activation bounds.
bool reseed_ride_trains_from_profile(RideTrainState&, std::span<const float> sample_speeds, float last_sample_position, RideTrainLifecycleBackend&, std::size_t component_count = 0);

//Samples are produced by the coaster geometry builder; geometry_flags belongs
//to the geometry object attached to this sample (bit 0x800 drives the winch).
struct RideMotionSample {
    std::array<float, 3> position {}, acceleration {};
    float profile_speed {};
    std::uint32_t sample_flags {}, geometry_flags {};
    std::size_t brace_row {}, mesh_row {};
    float heading_angle {}, pitch_angle {}, bank_angle {}, attachment_fraction {};
};

struct RideMotionSettings {
    float last_sample_position {}, end_fade_start {}, activation_upper {}, speed_cap {};
    bool recording_disabled {};
};

//Rows form two paired attachment chains. Indices refer to the corresponding
//row span and remain stable while the geometry is sampled.
struct RideTrackAttachmentRow {
    float sample_position {}, geometry_fraction {}, geometry_axis_angle {};
    std::uint32_t geometry_index {};
    std::int32_t source_mesh_resource_index { -1 };
    std::array<float, 3> incoming_tangent {}, outgoing_tangent {}, incoming_control {}, position {}, outgoing_control {};
    std::optional<std::size_t> previous, next;
};

struct RideTrackGeometry {
    std::span<const RideMotionSample> samples;
    std::span<const RideTrackAttachmentRow> brace_rows, mesh_rows;
    std::uint32_t frame_flags {};
    bool use_sample_bank {};
    //Zero means all supplied samples; backing storage may include a sentinel.
    std::size_t component_count {};
};

//rotation is heading, pitch, roll; original_heading is before a frame flip.
struct RideTrackFrame {
    std::array<float, 3> position {}, rotation {};
    float original_heading {};
};

std::array<float, 3> evaluate_coaster_attachment_bezier(const RideTrackAttachmentRow&, const RideTrackAttachmentRow&, float fraction);
bool sample_coaster_attachment_frame(const RideTrackAttachmentRow&, const RideTrackAttachmentRow&, const RideTrackAttachmentRow*, const RideTrackAttachmentRow*, float fraction, const std::array<float, 3>& position, std::uint32_t frame_flags, RideTrackFrame&);
bool sample_ride_track_frame(const RideTrackGeometry&, float sample_position, RideTrackFrame&);
bool sample_ride_track_segment_frame(const RideTrackGeometry&, std::size_t brace_row, std::size_t mesh_row, float fraction, RideTrackFrame&);
//Recreates the local mesh matrix, rotates the optional vertical displacement,
//writes the translation and retains the third basis row for bend-car sampling.
std::array<float, 16> build_ride_car_transform(RideTrainCar&, const RideCarType&, RideTrackFrame&);

struct RideTrainMotionBackend : virtual RideTrainEffectBackend {
    //The renderer installs the reconstructed transform and optional animation
    //divisor. A bend car has no ordinary animation-divisor update.
    virtual bool set_car_pose(RideTrainCar&, const std::array<float, 16>&, std::optional<float> animation_divisor) = 0;
    virtual void update_bend_car_pose(RideTrainCar&) = 0;
};

//Advances a full animation-clock interval. The first call seeds the clock and
//uses zero elapsed time. Invalid geometry or a failed renderer boundary may
//leave partial updates, matching the underlying nontransactional runtime.
bool update_ride_train_motion(RideTrainState&, const RideTrackGeometry&, const RideMotionSettings&, std::int64_t animation_time_ms, RideTrainMotionBackend&);
//Install poses without advancing physics, changing lifecycle or sending sound
//transitions. Used once after restoring the saved train/car state.
bool install_ride_train_poses(RideTrainState&, const RideTrackGeometry&, float last_sample_position, RideTrainMotionBackend&);

struct RideBendSample {
    float yaw {}, pitch {}, elevation {};
};

struct RideBendPose {
    std::size_t center {};
    std::vector<RideBendSample> samples;
};

std::optional<RideBendPose> build_bend_car_pose(std::span<const RideMotionSample>, std::size_t component_count, const RideTrainCar&, const RideCarType&, float spacing, float elevation_low, float elevation_high);
//Updates only the train and capacity portion of an existing coaster snapshot.
bool capture_ride_train_state(const RideTrainState&, CoasterRecordSave&);
} //namespace simcoaster
