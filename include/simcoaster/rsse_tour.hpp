#pragma once
#include "world_domains_fwd.hpp"
#include "save_schema.hpp"
#include <array>
#include <cstdint>
#include <memory>

namespace simcoaster {
struct Stream;
enum class RsseTourElementState : std::int32_t {
    inactive = 0,
    docked = 1,
    taking_off = 2,
    roaming = 3,
    seeking_approach = 4,
    outer_approach = 5,
    final_approach = 6,
    landing = 7,
    holding = 8,
    retiring = 9
};
enum class RsseTourEffect : std::int32_t {
    normal = 1,
    smoke = 2,
    fire = 3,
    fire_force_unload = 4
};

//Explicit wire order for the original flying-ride save record. These names
//describe observed fields; host ownership and pointer widths are independent.
struct RsseTourEntry {
    [[=save::Field{ "owner_handle", 0 }]] std::uint32_t owner_handle{};
    [[=save::Field{ "current_x", 1 }]] std::int32_t current_x{};
    [[=save::Field{ "current_y", 2 }]] std::int32_t current_y{};
    [[=save::Field{ "current_z", 3 }]] std::int32_t current_z{};
    [[=save::Field{ "heading", 4 }]] std::int32_t heading{};
    [[=save::Field{ "pitch", 5 }]] std::int32_t pitch{};
    [[=save::Field{ "roll", 6 }]] std::int32_t roll{};
    [[=save::Field{ "previous_x", 7 }]] std::int32_t previous_x{};
    [[=save::Field{ "previous_y", 8 }]] std::int32_t previous_y{};
    [[=save::Field{ "previous_z", 9 }]] std::int32_t previous_z{};
    [[=save::Field{ "previous_heading", 10 }]] std::int32_t previous_heading{};
    [[=save::Field{ "previous_pitch", 11 }]] std::int32_t previous_pitch{};
    [[=save::Field{ "previous_roll", 12 }]] std::int32_t previous_roll{};
    [[=save::Field{ "render_x", 13 }]] float render_x{};
    [[=save::Field{ "render_y", 14 }]] float render_y{};
    [[=save::Field{ "render_z", 15 }]] float render_z{};
    [[=save::Field{ "render_roll", 16 }]] float render_roll{};
    [[=save::Field{ "render_pitch", 17 }]] float render_pitch{};
    [[=save::Field{ "render_heading", 18 }]] float render_heading{};
    [[=save::Field{ "target_x", 19 }]] std::int32_t target_x{};
    [[=save::Field{ "target_y", 20 }]] std::int32_t target_y{};
    [[=save::Field{ "target_z", 21 }]] std::int32_t target_z{};
    [[=save::Field{ "previous_target_x", 22 }]] std::int32_t previous_target_x{};
    [[=save::Field{ "previous_target_y", 23 }]] std::int32_t previous_target_y{};
    [[=save::Field{ "previous_target_z", 24 }]] std::int32_t previous_target_z{};
    [[=save::Field{ "velocity_x", 25 }]] std::int32_t velocity_x{};
    [[=save::Field{ "reserved_velocity_y", 26 }]] std::int32_t reserved_velocity_y{};
    [[=save::Field{ "velocity_z", 27 }]] std::int32_t velocity_z{};
    [[=save::Field{ "heading_change", 28 }]] std::int32_t heading_change{};
    [[=save::Field{ "vertical_velocity", 29 }]] std::int32_t vertical_velocity{};
    [[=save::Field{ "forward_speed", 30 }]] std::int32_t forward_speed{};
    [[=save::Field{ "propulsion_setting", 31 }]] std::int32_t propulsion_setting{};
    [[=save::Field{ "element_state", 32 }]] RsseTourElementState element_state{};
    [[=save::Field{ "flight_time_remaining_ticks", 33 }]] std::int32_t flight_time_remaining_ticks{};
    [[=save::Field{ "target_quadrant", 34 }]] std::uint32_t target_quadrant{};
    [[=save::Field{ "signed_passenger_count", 35 }]] std::int32_t signed_passenger_count{};
    [[=save::Field{ "passenger_ids", 36 }]] std::array<std::uint32_t, 9> passenger_ids{};
    [[=save::Field{ "retire_when_empty", 37 }]] std::int32_t retire_when_empty{};
    [[=save::Field{ "particle_transition_pending", 38 }]] std::uint32_t particle_transition_pending{};
    [[=save::Field{ "approach_heading", 39 }]] std::int32_t approach_heading{};
    [[=save::Field{ "mesh_instance_handle", 40 }]] std::uint32_t mesh_instance_handle{};
    [[=save::Field{ "particle_runtime_handle", 41 }]] std::uint32_t particle_runtime_handle{};
    [[=save::Field{ "initialize_render_pose", 42 }]] std::int32_t initialize_render_pose{};
    [[=save::Field{ "cached_animation_selector", 43 }]] std::int32_t cached_animation_selector{};
    [[=save::Field{ "cached_animation_value", 44 }]] std::uint32_t cached_animation_value{};
    [[=save::Field{ "cached_animation_mode", 45 }]] std::int32_t cached_animation_mode{};
    [[=save::Field{ "mesh_object_start", 46 }]] std::uint32_t mesh_object_start{};
    [[=save::Field{ "mesh_object_count", 47 }]] std::uint32_t mesh_object_count{};
    [[=save::Field{ "positioned_sound_request", 48 }]] std::uint32_t positioned_sound_request{};
};

struct RsseTourRecord {
    [[=save::Field{ "target_element_count", 0 }]] std::uint32_t target_element_count{};
    [[=save::Field{ "requested_element_count", 1 }]] std::uint32_t requested_element_count{};
    [[=save::Field{ "reserved", 2 }]] std::uint32_t reserved{};
    [[=save::Field{ "element_count", 3 }]] std::uint32_t element_count{};
    [[=save::Field{ "flight_duration_ticks", 4 }]] std::int32_t flight_duration_ticks{};
    [[=save::Field{ "reference_x", 5 }]] std::int32_t reference_x{};
    [[=save::Field{ "reference_y", 6 }]] std::int32_t reference_y{};
    [[=save::Field{ "reference_z", 7 }]] std::int32_t reference_z{};
    [[=save::Field{ "separation_scale", 8 }]] std::int32_t separation_scale{};
    [[=save::Field{ "attitude_scale", 9 }]] std::int32_t attitude_scale{};
    [[=save::Field{ "base_propulsion_setting", 10 }]] std::uint32_t base_propulsion_setting{};
    [[=save::Field{ "landing_approach_reserved", 11 }]] std::int32_t landing_approach_reserved{};
    [[=save::Field{ "approach_radius", 12 }]] std::int32_t approach_radius{};
    [[=save::Field{ "active_effect_mode", 13 }]] RsseTourEffect active_effect_mode{};
    [[=save::Field{ "tour_enabled", 14 }]] std::int32_t tour_enabled{};
    [[=save::Field{ "docked_element_ready", 15 }]] std::int32_t docked_element_ready{};
    [[=save::Field{ "departure_heading", 16 }]] std::int32_t departure_heading{};
    [[=save::Field{ "fixed_approach_heading_enabled", 17 }]] std::int32_t fixed_approach_heading_enabled{};
    [[=save::Field{ "onboard_passenger_count", 18 }]] std::int32_t onboard_passenger_count{};
    [[=save::Field{ "short_approach_enabled", 19 }]] std::int32_t short_approach_enabled{};
    [[=save::Field{ "speed_adjustment_tenths", 20 }]] std::int32_t speed_adjustment_tenths{};
    [[=save::Field{ "script_state_id", 21 }]] std::uint32_t script_state_id{};
    [[=save::Field{ "owner_used_thing_id", 22 }]] std::uint16_t owner_used_thing_id{};
    [[=save::Field{ "padding", 23 }]] std::uint16_t padding{};
    [[=save::Field{ "entries", 24 }]] std::array<RsseTourEntry, 20> entries{};
};

static_assert(save::record_width<RsseTourEntry> == 228);
static_assert(save::record_width<RsseTourRecord> == 4652);

struct RsseTourBounds {
    std::int32_t max_x{}, min_x{}, max_z{}, min_z{};
};

struct RsseTourManager {
    bool initialized{};
    std::uint32_t restore_init_flag{};
    std::array<std::unique_ptr<RsseTourRecord>, 100> slots{};
    RsseTourBounds bounds{};
    std::array<std::int32_t, 4096> sine_table{};
};

bool write_rsse_tour_save_section(ScriptWorld&, Stream&);
bool restore_rsse_tour_save_section(ScriptWorld&, Stream&);
void refresh_rsse_tour_mesh_instances(ScriptWorld&);
} //namespace simcoaster
