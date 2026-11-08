#include "simcoaster/numeric_word.hpp"
#include "simcoaster/rsse_people.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t truncate_word(double value) {
    const auto limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || value < -limit || value >= limit)
        throw std::domain_error("RSSE nonrepresentable motion value");
    return numeric::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}

float sine(float index) {
    return simulation_sine_at_rounded_index(index);
}

PersonRuntimeView& person(ScriptWorld& c, std::uint32_t id) {
    auto* payload = c.entities.at(static_cast<std::uint16_t>(id)).payload;
    if (!payload)
        throw std::invalid_argument("RSSE missing attached person");
    return *reinterpret_cast<PersonRuntimeView*>(payload);
}

RsseSourceTransform transform(ScriptWorld& c,
                              const RsseScriptState& state,
                              std::int32_t node,
                              std::uint32_t flags,
                              bool direction = false) {
    auto result = resolve_rsse_action_source_transform(c, state, node, flags, direction);
    if (!result || (direction && !result->direction))
        throw std::invalid_argument("RSSE unresolved walk node or direction");
    return *result;
}

std::uint32_t progress(ScriptWorld& c, const RsseWalkRecord& walk) {
    const auto start = walk.phase_start_ms;
    const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
    const auto duration = walk.phase_end_ms - start;
    if (!duration)
        throw std::domain_error("RSSE zero walk interval");
    return (1000u * now - 1000u * start) / duration;
}

std::array<std::int32_t, 3> particle_position(const std::array<float, 3>& p) {
    std::array<std::int32_t, 3> result{};
    for (std::size_t i = 0; i < 3; ++i)
        result[i] = numeric::word(static_cast<std::uint32_t>(truncate_word(p[i])) << 10);
    return result;
}

void float_person(ScriptWorld& c,
                  RsseScriptState& state,
                  RsseWalkRecord& walk,
                  PersonRuntimeView& entity,
                  std::array<float, 3> p) {
    const auto person_factor = static_cast<float>(static_cast<double>(walk.person_id & 7u) * static_cast<double>(0.1f)
                                                  + static_cast<double>(0.4f));
    const auto rotation_factor =
        static_cast<float>(static_cast<double>(walk.person_id & 3u) * 0.25 + static_cast<double>(0.4f));
    const auto speed =
        static_cast<float>(static_cast<double>(state.operating_speed) * static_cast<double>(0.01f) + 0.5);
    const auto horizontal = static_cast<float>(state.walk_float_horizontal_scale_milli * static_cast<double>(0.001f));
    const auto vertical = static_cast<float>(state.walk_float_vertical_scale_milli * static_cast<double>(0.001f));
    const auto clock_as_float = static_cast<float>(c.ports.clock.get_simulation_clock_time_ms(c));
    const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
    const auto envelope = sine(static_cast<float>(static_cast<double>(numeric::word(now - state.walk_float_start_ms)) * static_cast<double>(3.1415901f) / state.walk_float_duration_ms * simulation_sine_samples_per_radian));
    const auto scaled_time = static_cast<double>(clock_as_float) * speed;
    const auto rounded_time = static_cast<float>(scaled_time);
    const auto phase = scaled_time * person_factor;
    const auto rounded_phase = static_cast<float>(phase);
    const auto height_sine =
        sine(static_cast<float>(phase * static_cast<double>(0.004f) * simulation_sine_samples_per_radian));
    p[1] = static_cast<float>((static_cast<double>(height_sine) * 3.0 + 12.0) * envelope * vertical + p[1]);
    const auto x_sine = sine(static_cast<float>(
        static_cast<double>(rounded_phase) * static_cast<double>(0.0033333334f) * simulation_sine_samples_per_radian));
    p[0] = static_cast<float>(static_cast<double>(x_sine) * envelope * rotation_factor * 3.0 * horizontal + p[0]);
    const auto z_sine = sine(
        static_cast<float>((static_cast<double>(rounded_time) * rotation_factor * static_cast<double>(0.0033333334f)
                            + static_cast<double>(1.5707964f))
                           * simulation_sine_samples_per_radian));
    p[2] = static_cast<float>(static_cast<double>(z_sine) * envelope * person_factor * 3.0 * horizontal + p[2]);
    const auto rotation_time = c.ports.clock.get_simulation_clock_time_ms(c);
    const auto divisor = static_cast<std::uint32_t>(truncate_word(static_cast<double>(rotation_factor) * 300.0));
    const auto offset = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(rotation_time / divisor));
    walk.direction = static_cast<std::int16_t>((walk.direction + offset) % 8);
    world_operations::update_mobile_entity_sprite_pose(
        c,
        entity,
        p,
        static_cast<std::uint32_t>(static_cast<std::int32_t>(walk.direction)),
        MobileSpriteCategory::floating);
}
} //namespace

std::optional<RsseSourceTransform> resolve_rsse_action_source_transform(ScriptWorld& c,
                                                                        const RsseScriptState& state,
                                                                        std::int32_t node,
                                                                        std::uint32_t flags,
                                                                        bool with_direction) {
    RsseSourceTransform result;
    if (node < 0 || !state.mesh_instance_handle) {
        if (state.mesh_instance_handle)
            if (const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
                result.position = { static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5),
                                    c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle),
                                    static_cast<float>((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 0.5) };
            }
        return result; //The native fallback deliberately leaves direction unwritten.
    }
    const auto index = c.ports.meshes.find_embedded_node_resource_record(c, state.mesh_instance_handle, flags, node);
    if (index == -1)
        return std::nullopt;
    result.position = world_operations::get_mesh_clone_attachment_position(c, state.mesh_instance_handle, index)
                          .value_or(std::array<float, 3>{});
    if (with_direction) {
        std::array<float, 3> direction{ 0, 0, 1024 };
        if (const auto basis = world_operations::get_mesh_clone_direction(c, state.mesh_instance_handle, index)) {
            const auto sign = (basis->flags & 16u) ? -1.0 : 1.0;
            const auto x = sign * basis->axis[0], y = sign * basis->axis[1], z = sign * basis->axis[2];
            const auto length = static_cast<float>(std::sqrt(z * z + y * y + x * x));
            const auto normalized_x = static_cast<float>(x / length), normalized_z = static_cast<float>(z / length);
            direction = { static_cast<float>(static_cast<double>(normalized_x) * 1024.0),
                          static_cast<float>(y / length * 1024.0),
                          static_cast<float>(static_cast<double>(normalized_z) * 1024.0) };
        }
        result.direction = direction;
    }
    return result;
}

MobileSpriteCategory get_person_sprite_generation_variant_code(ScriptWorld& c, const PersonRuntimeView& entity) {
    const auto flag = world_operations::get_person_sprite_gender_flag(c, entity);
    return match (entity.generation) -> MobileSpriteCategory {
        case PersonGeneration::child => MobileSpriteCategory::child;
        case PersonGeneration::parent =>
            flag ? MobileSpriteCategory::parent_flag_set : MobileSpriteCategory::parent_flag_clear;
        case PersonGeneration::grandparent =>
            flag ? MobileSpriteCategory::grandparent_flag_set : MobileSpriteCategory::grandparent_flag_clear;
        case _ => MobileSpriteCategory::none;
    };
}

void position_rsse_walking_person(ScriptWorld& c,
                                  RsseScriptState& state,
                                  RsseWalkRecord& walk,
                                  std::int32_t source,
                                  std::int32_t destination,
                                  RsseWalkLookupMode mode,
                                  std::int32_t progress_milli) {
    const auto from = transform(c, state, source, mode == RsseWalkLookupMode::mesh_to_node ? 128u : 2048u).position;
    const auto to = transform(c, state, destination, mode == RsseWalkLookupMode::node_to_mesh ? 128u : 2048u).position;
    const auto amount = static_cast<double>(std::min(progress_milli, 1000));
    std::array<float, 3> p{};
    for (auto i : { 0u, 2u })
        p[i] =
            static_cast<float>(((static_cast<double>(to[i]) - from[i]) * amount + static_cast<double>(from[i]) * 1000.0)
                               * static_cast<double>(0.001f));
    if ((std::to_underlying(walk.flags) & 1u) != 0)
        p[1] =
            static_cast<float>(((static_cast<double>(to[1]) - from[1]) * amount + static_cast<double>(from[1]) * 1000.0)
                               * static_cast<double>(0.001f));
    else
        p[1] = c.ports.meshes.query_prepared_mesh_surface_height(c, p[0], p[2]);
    auto& entity = person(c, walk.person_id);
    if (walk.phase != RsseWalkPhase::active) {
        world_operations::update_mobile_entity_sprite_pose(
            c,
            entity,
            p,
            static_cast<std::uint32_t>(static_cast<std::int32_t>(walk.direction)),
            MobileSpriteCategory::walking);
        return;
    }
    if (state.walk_float_duration_ms) {
        float_person(c, state, walk, entity, p);
        return;
    }
    if (walk.kind == RsseWalkKind::stand)
        world_operations::update_mobile_entity_sprite_pose(
            c,
            entity,
            p,
            static_cast<std::uint32_t>(static_cast<std::int32_t>(walk.direction)),
            MobileSpriteCategory::standing);
    if (walk.kind == RsseWalkKind::pose13)
        world_operations::update_mobile_entity_sprite_pose(
            c,
            entity,
            p,
            static_cast<std::uint32_t>(static_cast<std::int32_t>(walk.direction)),
            MobileSpriteCategory::rsse_pose13);
    if (walk.kind == RsseWalkKind::person_variant) {
        const auto category = get_person_sprite_generation_variant_code(c, entity);
        world_operations::update_mobile_entity_sprite_pose(
            c,
            entity,
            p,
            static_cast<std::uint32_t>(static_cast<std::int32_t>(walk.direction)),
            category);
    }
    if (walk.kind == RsseWalkKind::mesh_attach || walk.kind == RsseWalkKind::hide || walk.kind == RsseWalkKind::beam)
        world_operations::release_mobile_entity_sprite_and_optional_person_effects(c, entity, true);
}

void update_rsse_walk_records(ScriptWorld& c, RsseScriptState& state) {
    if (state.walk_records.empty())
        return;
    c.ports.meshes.prepare_mesh_surface_query_geometry(c, state.mesh_instance_handle);
    for (auto& walk : state.walk_records) {
        match (walk.phase) {
            case RsseWalkPhase::entering => {
                const auto amount = progress(c, walk);
                position_rsse_walking_person(c, state, walk, walk.entry_start_node, walk.entry_end_node, walk.kind == RsseWalkKind::mesh_attach ? RsseWalkLookupMode::node_to_mesh : RsseWalkLookupMode::node_to_node, numeric::word(amount));
                if (amount >= 1000) {
                    walk.phase = RsseWalkPhase::active;
                    walk.phase_start_ms = c.ports.clock.get_simulation_clock_time_ms(c);
                    if (walk.kind == RsseWalkKind::mesh_attach) {
                        const auto index = c.ports.meshes.find_embedded_node_resource_record(c,
                                                                                             state.mesh_instance_handle,
                                                                                             128,
                                                                                             walk.entry_end_node);
                        if (index != -1)
                            world_operations::bind_world_person_sprite_to_mesh_model_object(c,
                                                                                            state.mesh_instance_handle,
                                                                                            index,
                                                                                            walk.person_id);
                    }
                    if (walk.kind == RsseWalkKind::beam) {
                        const auto position = transform(c, state, walk.entry_end_node, 2048).position;
                        (void)c.ports.effects.create_particle_effect(c, 19, particle_position(position), 0);
                    }
                }
            }
            case RsseWalkPhase::active => {
                if (walk.kind == RsseWalkKind::mesh_attach)
                    position_rsse_walking_person(c,
                                                 state,
                                                 walk,
                                                 walk.entry_start_node,
                                                 walk.entry_end_node,
                                                 RsseWalkLookupMode::node_to_mesh,
                                                 1);
                else if (walk.kind == RsseWalkKind::hide || walk.kind == RsseWalkKind::beam)
                    position_rsse_walking_person(c,
                                                 state,
                                                 walk,
                                                 walk.entry_start_node,
                                                 walk.entry_end_node,
                                                 RsseWalkLookupMode::node_to_node,
                                                 1);
                else {
                    const auto resolved = transform(c, state, walk.entry_end_node, 2048, true);
                    const auto& direction = *resolved.direction;
                    walk.direction = static_cast<std::int16_t>(
                        truncate_word(
                            (6.28318
                             - (std::atan2(static_cast<double>(direction[2]), static_cast<double>(direction[0]))
                                - 1.570795))
                                * 1.2732406201955062
                            + 0.5)
                        % 8);
                    position_rsse_walking_person(c,
                                                 state,
                                                 walk,
                                                 walk.entry_end_node,
                                                 walk.entry_end_node,
                                                 RsseWalkLookupMode::node_to_node,
                                                 0);
                }
            }
            case RsseWalkPhase::exiting => {
                const auto amount = progress(c, walk);
                position_rsse_walking_person(c, state, walk, walk.exit_start_node, walk.exit_end_node, walk.kind == RsseWalkKind::mesh_attach ? RsseWalkLookupMode::mesh_to_node : RsseWalkLookupMode::node_to_node, numeric::word(amount));
                if (amount >= 1000)
                    walk.phase = RsseWalkPhase::complete;
            }
            case RsseWalkPhase::complete => {
                position_rsse_walking_person(c,
                                             state,
                                             walk,
                                             walk.exit_end_node,
                                             walk.exit_end_node,
                                             RsseWalkLookupMode::node_to_node,
                                             0);
            }
            case _ => {}
        }
    }
    if (state.walk_float_duration_ms
        && static_cast<std::uint32_t>(state.walk_float_duration_ms) + state.walk_float_start_ms
               < c.ports.clock.get_simulation_clock_time_ms(c))
        state.walk_float_duration_ms = 0;
}

void update_rsse_attached_people(ScriptWorld& c) {
    if (!c.scripts.states.active)
        return;
    for (auto* state = c.scripts.states.head; state; state = state->next)
        if (!state->updates_suspended) {
            for (auto& bounce : state->bounce_records)
                if (bounce.person_id) {
                    const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
                    const auto elapsed = numeric::word(now - bounce.start_ms);
                    auto& entity = person(c, bounce.person_id);
                    const auto index = c.ports.meshes.find_embedded_node_resource_record(c,
                                                                                         state->mesh_instance_handle,
                                                                                         2048,
                                                                                         bounce.attachment_node_id);
                    if (index < 0)
                        continue;
                    auto p = world_operations::get_mesh_clone_attachment_position(c, state->mesh_instance_handle, index)
                                 .value_or(std::array<float, 3>{});
                    const auto root_height =
                        c.ports.meshes.get_mesh_root_translation_height(c, state->mesh_instance_handle);
                    if (state->value_slots.size() <= 7)
                        throw std::out_of_range("RSSE bounce requires script slot7");
                    const auto variation = numeric::word(bounce.person_id) % 7;
                    if (state->value_slots[7])
                        world_operations::update_mobile_entity_sprite_pose(c,
                                                                           entity,
                                                                           p,
                                                                           static_cast<std::uint32_t>(variation),
                                                                           MobileSpriteCategory::standing);
                    else {
                        const auto phase =
                            static_cast<double>(
                                truncate_word(
                                    (static_cast<double>(state->operating_speed) * static_cast<double>(0.005f) + 0.8)
                                    * elapsed)
                                % 1000)
                            * static_cast<double>(0.003125f);
                        const auto angle = static_cast<float>(phase);
                        const auto sample = sine(static_cast<float>(phase * simulation_sine_samples_per_radian));
                        if (static_cast<double>(sample) * (variation + 12.0) + state->bounce_base_height + root_height
                            >= p[1])
                            p[1] = static_cast<float>(
                                static_cast<double>(sine(static_cast<float>(static_cast<double>(angle)
                                                                            * simulation_sine_samples_per_radian)))
                                    * (variation + 12.0)
                                + state->bounce_base_height
                                + root_height);
                        world_operations::update_mobile_entity_sprite_pose(c,
                                                                           entity,
                                                                           p,
                                                                           static_cast<std::uint32_t>(elapsed / 500),
                                                                           MobileSpriteCategory::bouncing);
                    }
                }
            update_rsse_walk_records(c, *state);
        }
}
} //namespace simcoaster
