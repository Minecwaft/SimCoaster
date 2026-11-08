#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
bool sound_action(RsseActionType type) {
    return type >= RsseActionType::local_ride_sound && type <= RsseActionType::object_ride_sound;
}

std::int32_t coordinate(double value) {
    const auto limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || value <= -limit || value >= limit) {
        throw std::domain_error("RSSE nonrepresentable coordinate");
    }
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}

std::array<std::int32_t, 3> coordinates(const std::array<float, 3>& value, bool particle) {
    std::array<std::int32_t, 3> result {};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto integer = std::bit_cast<std::uint32_t>(coordinate(value[i]));
        result[i] = std::bit_cast<std::int32_t>(particle ? integer << 10 : integer);
    }
    return result;
}

void update_actions(ScriptWorld& c, RsseScriptState& state) {
    for (auto& action : state.actions) {
        if (action.source_node_id < 0) {
            continue;
        }
        const auto type = action.type;
        const bool particle = type == RsseActionType::particle || type == RsseActionType::directed_particle;
        if (!particle && !(sound_action(type) && ((c.scripts.states.scheduler_tick ^ state.id) & 7u) == 0)) {
            continue;
        }
        const auto position = world_operations::get_mesh_clone_attachment_position(c, state.mesh_instance_handle, action.source_clone_index).value_or(std::array<float, 3> {});
        if (particle) {
            //Native truncates attachment coordinates before scaling to fixed point.
            //Direction components instead retain fractions until after scaling.
            std::array<float, 3> forward {};
            if (type == RsseActionType::directed_particle) {
                forward = world_operations::get_mesh_clone_attachment_forward(c, state.mesh_instance_handle, action.source_clone_index);
            }
            c.ports.effects.set_particle_effect_position(c, action.handle, coordinates(position, true));
            if (type == RsseActionType::directed_particle) {
                std::array<std::int32_t, 3> direction {};
                for (std::size_t i = 0; i < 3; ++i) {
                    direction[i] = coordinate(static_cast<double>(forward[i]) * 1024.0);
                }
                c.ports.effects.set_particle_effect_direction(c, action.handle, direction);
            }
        } else {
            c.ports.resources.update_positioned_request_position(c, action.handle, coordinates(position, false));
        }
    }
}

void update_speed_bindings(ScriptWorld& c, RsseScriptState& state) {
    if (state.mesh_instance_handle) {
        const auto divisor = static_cast<float>((static_cast<double>(state.operating_speed) * static_cast<double>(0.01f) + 0.5) * state.animation_duration_scale_milli * static_cast<double>(0.001f));
        world_operations::set_mesh_animation_divisor(c, state.mesh_instance_handle, divisor);
    }
    if (state.auxiliary_runtime_object_handle) {
        if (state.value_slots.size() > 3) {
            world_operations::set_rsse_tour_duration(c, state.auxiliary_runtime_object_handle, std::bit_cast<std::int32_t>(1000u * static_cast<std::uint32_t>(state.value_slots[3])));
        }
        world_operations::set_rsse_tour_speed(c, state.auxiliary_runtime_object_handle, state.operating_speed);
    }
    if (state.coaster_runtime_map_id) {
        c.ports.meshes.set_mesh_runtime_speed(c, state.coaster_runtime_map_id, state.operating_speed);
    }
    if (state.owner_used_thing_id) {
        const auto* owner = c.entities.at(state.owner_used_thing_id).payload;
        if (owner) {
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(owner);
            if (object.track_ride_handle) {
                c.ports.entities.update_track_ride_interpolated_values(c, object.track_ride_handle, state.operating_speed);
            }
        }
    }
    if (state.child_script_state_id) {
        auto* child = find_rsse_script_state_by_id(c, state.child_script_state_id);
        if (!child) {
            throw std::invalid_argument("RSSE missing speed child");
        }
        child->operating_speed = state.operating_speed;
    }
}

void emit_destroy_effect(ScriptWorld& c, RsseScriptState& state, RsseScriptCleanupFlags flags) {
    const auto* owner = c.entities.at(state.owner_used_thing_id).payload;
    if (!owner) {
        throw std::invalid_argument("RSSE missing destruction owner");
    }
    const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(owner);
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, object.definition_id);
    if (!definition) {
        throw std::invalid_argument("RSSE missing destruction definition");
    }
    const auto effect = definition->destroy_particle_effect;
    if (!effect || !(std::to_underlying(flags) & std::to_underlying(RsseScriptCleanupFlags::destroy_effect))) {
        return;
    }
    std::array<std::int32_t, 3> position {};
    if (state.mesh_instance_handle) {
        if (const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
            const auto height = c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle);
            const auto center_x = static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5);
            position = { coordinate(static_cast<double>(center_x) * 1024.0), coordinate(static_cast<double>(height) * 1024.0), coordinate((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 512.0) };
        }
    }
    const auto particle = c.ports.effects.create_particle_effect(c, effect, position, 0);
    if (state.mesh_instance_handle) {
        if (const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
            const auto width_x = static_cast<float>(static_cast<double>(bounds->max[0]) - bounds->min[0]);
            c.ports.effects.set_particle_effect_spread(c, particle, { coordinate(static_cast<double>(width_x) * 512.0), coordinate((static_cast<double>(bounds->max[1]) - bounds->min[1]) * 256.0), coordinate((static_cast<double>(bounds->max[2]) - bounds->min[2]) * 512.0) });
        }
    }
}
} //namespace

void initialize_rsse_script_state_manager(ScriptWorld& c) {
    auto& manager = c.scripts.states;
    if (manager.active) {
        return;
    }
    manager.active = 1;
    manager.next_state_id = 1;
    manager.head = nullptr;
    manager.state_count = 0;
    manager.scheduler_tick = 0;
    manager.step_control = 0;
}

void update_rsse_scripts(ScriptWorld& c) {
    auto& manager = c.scripts.states;
    if (!manager.active) {
        return;
    }
    ++manager.scheduler_tick;
    for (auto* state = manager.head; state;) {
        if (!state->updates_suspended) {
            const bool cadence = ((manager.scheduler_tick ^ state->id) & 7u) == 0;
            if (state->turbo_enabled || cadence) {
                if (state->instruction_words.data()) {
                    run_rsse_instruction_slice(c, *state);
                }
                update_speed_bindings(c, *state);
            }
            update_actions(c, *state);
        }
        //Cleanup is unconditional, including suspended states and non-cadence ticks.
        if (state->instruction_cursor < 0) {
            //A linked sound/child record may itself be the old successor. The
            //destructor returns the surviving successor after removing dependencies.
            state = destroy_rsse_script_state_by_id(c, state->id, RsseScriptCleanupFlags::none);
        } else {
            state = state->next;
        }
    }
}

RsseScriptState* destroy_rsse_script_state_record(ScriptWorld& c, RsseScriptState* state, RsseScriptCleanupFlags flags) {
    if (!state) {
        return nullptr;
    }
    if (state->music_dip_requested) {
        c.ports.audio.stage_music_mute_override(c, false);
    }
    //The original gates TOUR destruction on the mesh, even for tour handle0.
    if (state->mesh_instance_handle) {
        world_operations::destroy_rsse_tour(c, state->auxiliary_runtime_object_handle);
    }
    if (state->repair_particle_handle) {
        c.ports.effects.set_particle_effect_lifetime(c, state->repair_particle_handle, -2);
        state->repair_particle_handle = 0;
    }
    if (state->scream_request) {
        c.ports.resources.submit_resource_target_parameter_request(c, state->scream_request);
    }
    const bool detach = (std::to_underlying(flags) & std::to_underlying(RsseScriptCleanupFlags::detach_people)) != 0;
    const auto release = [&](std::int32_t node) {
        const auto index = c.ports.meshes.find_embedded_node_resource_record(c, state->mesh_instance_handle, 128, node);
        if (index != -1) {
            world_operations::release_mesh_model_object_instance(c, state->mesh_instance_handle, index);
        }
    };
    for (std::size_t i = 0; i < state->attached_person_ids.size(); ++i) {
        if (state->attached_person_ids[i]) {
            if (detach) {
                release(static_cast<std::int32_t>(i + 1));
            }
            state->attached_person_ids[i] = 0;
        }
    }
    for (auto& walk : state->walk_records) {
        if (walk.kind == RsseWalkKind::mesh_attach && walk.phase == RsseWalkPhase::active) {
            if (detach) {
                release(walk.entry_end_node);
            }
            walk.phase = RsseWalkPhase::unused;
        }
    }
    --c.scripts.states.state_count;
    if (state->previous) {
        state->previous->next = state->next;
    }
    if (state->next) {
        state->next->previous = state->previous;
    }
    while (!state->actions.empty()) {
        const auto& action = state->actions.front();
        if (action.type == RsseActionType::particle || action.type == RsseActionType::directed_particle) {
            c.ports.effects.set_particle_effect_lifetime(c, action.handle, -2);
        } else if (sound_action(action.type)) {
            c.ports.resources.request_target_recycle(c, action.handle);
        }
        state->actions.pop_front();
        --c.scripts.states.action_node_count;
    }
    auto* next = state->next;
    if (state == c.scripts.states.head) {
        c.scripts.states.head = next;
    }
    std::erase_if(c.scripts.states.owned_states, [&](const auto& instance) {
        return &instance->state == state;
    });
    return next;
}

RsseScriptState* destroy_rsse_script_state_by_id(ScriptWorld& c, std::uint32_t id, RsseScriptCleanupFlags flags) {
    auto* state = find_rsse_script_state_by_id(c, id);
    if (!state) {
        return nullptr;
    }
    if (state->owner_used_thing_id) {
        emit_destroy_effect(c, *state, flags);
        if (state->sound_occlusion_region) {
            c.ports.audio.delete_rsse_sound_occlusion_region(c, state->sound_occlusion_region);
            state->sound_occlusion_region = nullptr;
        }
    }
    //These are direct record destructions, deliberately not recursive by-ID calls.
    if (state->sound_script_state_id) {
        destroy_rsse_script_state_record(c, find_rsse_script_state_by_id(c, state->sound_script_state_id), flags);
    }
    if (state->child_script_state_id) {
        destroy_rsse_script_state_record(c, find_rsse_script_state_by_id(c, state->child_script_state_id), flags);
    }
    if (state->parent_script_state_id) {
        auto* parent = find_rsse_script_state_by_id(c, state->parent_script_state_id);
        if (!parent) {
            throw std::invalid_argument("RSSE missing destruction parent");
        }
        parent->child_script_state_id = 0;
    }
    return destroy_rsse_script_state_record(c, state, flags);
}

void shutdown_rsse_script_state_manager(ScriptWorld& c) {
    if (c.scripts.states.active) {
        for (auto* state = c.scripts.states.head; state;) {
            state = destroy_rsse_script_state_record(c, state, RsseScriptCleanupFlags::none);
        }
        c.scripts.states.head = nullptr;
    }
    c.scripts.states.active = 0;
}

void world_operations::builtin::destroy_rsse_script_state_by_id(WorldRuntime& c, std::uint32_t id, RsseScriptCleanupFlags flags) {
    (void)simcoaster::destroy_rsse_script_state_by_id(c, id, flags);
}
} //namespace simcoaster
