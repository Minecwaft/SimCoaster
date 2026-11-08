#pragma once
#include "save_schema.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace simcoaster {
struct Stream;

//Motion is either velocity or attachment offset in the native union. Effect
//tail holds either a template name or runtime bounds; attractor tail similarly
//holds a name or runtime links. Their saved bytes share the same storage.
struct ParticleIndexLinks {
    [[=save::Field{ "next", 0 }]] std::int16_t next{};
    [[=save::Field{ "previous", 1 }]] std::int16_t previous{};
};

struct ParticleEffect {
    [[=save::Field{ "runtime_state", 0 }]] std::int16_t runtime_state{};
    [[=save::Field{ "updates_suspended", 1 }]] std::int16_t updates_suspended{};
    [[=save::Field{ "group_id", 2 }]] std::int8_t group_id{};
    [[=save::Field{ "screen_space", 3 }]] std::uint8_t screen_space{};
    [[=save::Field{ "particle_head", 4 }]] std::int16_t particle_head{};
    [[=save::Field{ "particle_count", 5 }]] std::uint16_t particle_count{};
    [[=save::Field{ "generation", 6 }]] std::uint16_t generation{};
    [[=save::Field{ "template_index", 7 }]] std::uint16_t template_index{};
    [[=save::Field{ "emission_mode", 8 }]] std::int16_t emission_mode{};
    [[=save::Field{ "expiry_spawn_id", 9 }]] std::int16_t expiry_spawn_id{};
    [[=save::Field{ "motion_jitter_range", 10 }]] std::int16_t motion_jitter_range{};
    [[=save::Field{ "position", 11 }]] std::array<std::int32_t, 3> position{};
    [[=save::Field{ "remaining_lifetime", 12 }]] std::int32_t remaining_lifetime{};
    [[=save::Field{ "drag_1024", 13 }]] std::int16_t drag_1024{};
    [[=save::Field{ "hold_lifetime", 14 }]] std::int16_t hold_lifetime{};
    [[=save::Field{ "gravity", 15 }]] std::int16_t gravity{};
    [[=save::Field{ "render_hidden", 16 }]] std::int16_t render_hidden{};
    [[=save::Field{ "motion", 17 }]] std::array<std::int32_t, 3> motion{};
    [[=save::Field{ "initial_particle_velocity", 18 }]] std::array<std::int32_t, 3> initial_particle_velocity{};
    [[=save::Field{ "position_spread", 19 }]] std::array<std::int32_t, 3> position_spread{};
    [[=save::Field{ "particle_velocity_jitter", 20 }]] std::int16_t particle_velocity_jitter{};
    [[=save::Field{ "screen_depth", 21 }]] std::int16_t screen_depth{};
    [[=save::Field{ "phase", 22 }]] std::int32_t phase{};
    [[=save::Field{ "color_flags", 23 }]] std::uint32_t color_flags{};
    [[=save::Field{ "random_velocity_range", 24 }]] std::int32_t random_velocity_range{};
    [[=save::Field{ "initial_particle_count", 25 }]] std::int32_t initial_particle_count{};
    [[=save::Field{ "maximum_particles", 26 }]] std::int16_t maximum_particles{};
    [[=save::Field{ "emission_delay_counter", 27 }]] std::int16_t emission_delay_counter{};
    [[=save::Field{ "emission_rates", 28 }]] std::array<std::int8_t, 4> emission_rates{};
    [[=save::Field{ "inherit_emitter_velocity", 29 }]] std::uint8_t inherit_emitter_velocity{};
    [[=save::Field{ "random_rotation_enabled", 30 }]] std::uint8_t random_rotation_enabled{};
    [[=save::Field{ "unknown_110", 31 }]] std::uint8_t unknown_110{};
    [[=save::Field{ "velocity_jitter_enabled", 32 }]] std::uint8_t velocity_jitter_enabled{};
    [[=save::Field{ "render_flags", 33 }]] std::uint32_t render_flags{};
    [[=save::Field{ "initial_particle_size", 34 }]] std::int16_t initial_particle_size{};
    [[=save::Field{ "collide_with_ground", 35 }]] std::int16_t collide_with_ground{};
    [[=save::Field{ "initial_particle_lifetime", 36 }]] std::int32_t initial_particle_lifetime{};
    [[=save::Field{ "particle_drag_1024", 37 }]] std::int32_t particle_drag_1024{};
    [[=save::Field{ "particle_gravity", 38 }]] std::int32_t particle_gravity{};
    [[=save::Field{ "velocity_jitter_range", 39 }]] std::array<std::int32_t, 3> velocity_jitter_range{};
    [[=save::Field{ "unknown_144", 40 }]] std::array<std::uint8_t, 4> unknown_144{};
    [[=save::Field{ "sprite_set_id", 41 }]] std::int16_t sprite_set_id{};
    [[=save::Field{ "sprite_frame_count", 42 }]] std::int16_t sprite_frame_count{};
    [[=save::Field{ "particle_expiry_template", 43 }]] std::int32_t particle_expiry_template{};
    [[=save::Field{ "ignore_global_attractors", 44 }]] std::int32_t ignore_global_attractors{};
    [[=save::Field{ "nonnegative_y_spread", 45 }]] std::int16_t nonnegative_y_spread{};
    [[=save::Field{ "render_position_scale_permille", 46 }]] std::int16_t render_position_scale_permille{};
    [[=save::Field{ "unknown_164", 47 }]] std::array<std::uint8_t, 2> unknown_164{};
    [[=save::Field{ "final_particle_size", 48 }]] std::int16_t final_particle_size{};
    [[=save::Field{ "fixed_spread_radius", 49 }]] std::int16_t fixed_spread_radius{};
    [[=save::Field{ "expire_particles_with_effect", 50 }]] std::uint8_t expire_particles_with_effect{};
    [[=save::Field{ "attached_to_parent", 51 }]] std::uint8_t attached_to_parent{};
    [[=save::Field{ "spread_yaw", 52 }]] std::int32_t spread_yaw{};
    [[=save::Field{ "direction_magnitude", 53 }]] std::int32_t direction_magnitude{};
    [[=save::Field{ "linked_effect", 54 }]] std::uint32_t linked_effect{};
    [[=save::Field{ "relative_link", 55 }]] std::int16_t relative_link{};
    [[=save::Field{ "linked_is_attractor", 56 }]] std::uint8_t linked_is_attractor{};
    [[=save::Field{ "expiry_spawn_is_attractor", 57 }]] std::uint8_t expiry_spawn_is_attractor{};
    [[=save::Field{ "expire_linked_child", 58 }]] std::int16_t expire_linked_child{};
    [[=save::Field{ "rotation_step_min", 59 }]] std::int16_t rotation_step_min{};
    [[=save::Field{ "skip_emission_scaling", 60 }]] std::uint8_t skip_emission_scaling{};
    [[=save::Field{ "suppress_when_filter_enabled", 61 }]] std::uint8_t suppress_when_filter_enabled{};
    [[=save::Field{ "box_position_spread", 62 }]] std::uint8_t box_position_spread{};
    [[=save::Field{ "unknown_195", 63 }]] std::uint8_t unknown_195{};
    [[=save::Field{ "phase_offset_radius", 64 }]] std::int16_t phase_offset_radius{};
    [[=save::Field{ "rotation_step_max", 65 }]] std::int16_t rotation_step_max{};
    [[=save::Field{ "phase_increment", 66 }]] std::int32_t phase_increment{};
    [[=save::Field{ "ring_radius", 67 }]] std::int32_t ring_radius{};
    [[=save::Field{ "links", 68 }]] ParticleIndexLinks links{};
    [[=save::Field{ "initial_lifetime", 69 }]] std::int32_t initial_lifetime{};
    [[=save::Field{ "color_palette", 70 }]] std::array<std::uint32_t, 16> color_palette{};
    [[=save::Field{ "tail", 71 }]] std::array<std::uint8_t, 40> tail{};
};

struct ParticleAttractor {
    [[=save::Field{ "runtime_state", 0 }]] std::int32_t runtime_state{};
    [[=save::Field{ "target_group", 1 }]] std::int32_t target_group{};
    [[=save::Field{ "generation", 2 }]] std::uint16_t generation{};
    [[=save::Field{ "attached_to_parent", 3 }]] std::uint8_t attached_to_parent{};
    [[=save::Field{ "collide_with_ground", 4 }]] std::uint8_t collide_with_ground{};
    [[=save::Field{ "position", 5 }]] std::array<std::int32_t, 3> position{};
    [[=save::Field{ "motion", 6 }]] std::array<std::int32_t, 3> motion{};
    [[=save::Field{ "drag_1024", 7 }]] std::int32_t drag_1024{};
    [[=save::Field{ "gravity", 8 }]] std::int32_t gravity{};
    [[=save::Field{ "radius_parameter", 9 }]] std::int32_t radius_parameter{};
    [[=save::Field{ "mode", 10 }]] std::int32_t mode{};
    [[=save::Field{ "jitter_range", 11 }]] std::array<std::int32_t, 3> jitter_range{};
    [[=save::Field{ "strength", 12 }]] std::int32_t strength{};
    [[=save::Field{ "suppress_when_filter_enabled", 13 }]] std::uint8_t suppress_when_filter_enabled{};
    [[=save::Field{ "expire_with_screen_effects", 14 }]] std::uint8_t expire_with_screen_effects{};
    [[=save::Field{ "expiry_spawn_id", 15 }]] std::int16_t expiry_spawn_id{};
    [[=save::Field{ "unknown_72", 16 }]] std::uint8_t unknown_72{};
    [[=save::Field{ "expiry_spawn_is_attractor", 17 }]] std::uint8_t expiry_spawn_is_attractor{};
    [[=save::Field{ "hold_lifetime", 18 }]] std::int16_t hold_lifetime{};
    [[=save::Field{ "remaining_lifetime", 19 }]] std::int32_t remaining_lifetime{};
    [[=save::Field{ "tail_prefix", 20 }]] std::array<std::uint8_t, 16> tail_prefix{};
    [[=save::Field{ "links", 21 }]] ParticleIndexLinks links{};
    [[=save::Field{ "tail_suffix", 22 }]] std::array<std::uint8_t, 4> tail_suffix{};
};

struct ParticleInstance {
    [[=save::Field{ "position", 0 }]] std::array<std::int32_t, 3> position{};
    [[=save::Field{ "velocity", 1 }]] std::array<std::int32_t, 3> velocity{};
    [[=save::Field{ "argb", 2 }]] std::uint32_t argb{};
    [[=save::Field{ "initial_lifetime", 3 }]] std::int32_t initial_lifetime{};
    [[=save::Field{ "remaining_lifetime", 4 }]] std::int32_t remaining_lifetime{};
    [[=save::Field{ "state", 5 }]] std::int16_t state{};
    [[=save::Field{ "effect_index", 6 }]] std::int16_t effect_index{};
    [[=save::Field{ "size", 7 }]] std::int16_t size{};
    [[=save::Field{ "group_tag", 8 }]] std::int16_t group_tag{};
    [[=save::Field{ "sprite_frame_index", 9 }]] std::uint16_t sprite_frame_index{};
    [[=save::Field{ "rotation", 10 }]] std::uint16_t rotation{};
    [[=save::Field{ "links", 11 }]] ParticleIndexLinks links{};
};

static_assert(save::record_width<ParticleEffect> == 320);
static_assert(save::record_width<ParticleAttractor> == 104);
static_assert(save::record_width<ParticleInstance> == 52);

struct ParticleState {
    std::int32_t live_particle_count{}, update_call_count{}, creation_filter_enabled{}, particle_capacity{},
        random_state{}, unknown_reset_one_020{};
    std::uint16_t next_effect_generation{}, next_attractor_generation{};
    std::int32_t parameter_scale{};
    std::array<ParticleEffect, 120> effects{};
    std::array<ParticleAttractor, 20> attractors{};
    std::int16_t active_effect_head = -1, active_attractor_head = -1;
    std::int16_t free_effect_head{}, free_attractor_head{}, free_particle_head{};
    std::array<std::uint8_t, 18> unknown_tail{};
};

struct ParticleRuntime {
    bool initialized{};
    ParticleState state{};
    std::array<ParticleEffect, 105> templates{};
    std::array<ParticleAttractor, 20> attractor_templates{};
    std::vector<ParticleInstance> particles;
    //UI control-feedback group2 owns at most five effect handles. Geometry is
    //owned by its UI controller; these transient handles are not save fields.
    std::vector<std::uint32_t> control_particle_handles;
};

bool restore_particle_save_section(ParticleRuntime&, Stream&);
bool write_particle_save_section(const ParticleRuntime&, Stream&, bool include_live_particles);
void expire_screen_particle_effects(ParticleRuntime&);
} //namespace simcoaster
