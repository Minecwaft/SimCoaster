#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
//These native proxy wrappers return immediately while dispatch is unavailable.
//A live audio backend remains required when both original guards are enabled.
void ResourceHostPort::request_target_recycle(WorldRuntime& c, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
}

void ResourceHostPort::submit_resource_target_parameter_request(WorldRuntime& c, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
}

std::uint32_t ResourceHostPort::update_positioned_request_position(WorldRuntime& c, std::uint32_t, const std::array<std::int32_t, 3>&) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
    return 0;
}

namespace {
std::int32_t coordinate(double value) {
    const auto limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || value < -limit || value >= limit) {
        throw std::domain_error("RSSE unrepresentable effect coordinate");
    }
    return numeric::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}

std::array<std::int32_t, 3> coordinates(const std::array<float, 3>& p, double scale = 1.0) {
    std::array<std::int32_t, 3> result {};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = coordinate(static_cast<double>(p[i]) * scale);
    }
    return result;
}

ResearchCatalogDefinition& definition(ScriptWorld& c, const RsseScriptState& state) {
    auto* owner = c.entities.at(state.owner_used_thing_id).payload;
    if (!owner) {
        throw std::invalid_argument("RSSE missing effect owner");
    }
    const auto* object = reinterpret_cast<const ParkObjectRuntimeView*>(owner);
    return require_bound<std::invalid_argument>(find_object_definition_by_id(&c.resources.object_catalog, object->definition_id), "RSSE missing effect definition");
}

bool particle(RsseActionType type) {
    return type == RsseActionType::particle || type == RsseActionType::directed_particle;
}

bool sound(RsseActionType type) {
    return type >= RsseActionType::local_ride_sound && type <= RsseActionType::object_ride_sound;
}

std::uint32_t sound_catalog(ScriptWorld& c, const RsseScriptState& state, RsseActionType type) {
    return match (type) -> std::uint32_t {
        case RsseActionType::local_ride_sound     => c.resources.local_rides_catalog_slot;
        case RsseActionType::local_ambient_sound  => c.resources.local_ambient_catalog_slot;
        case RsseActionType::global_ride_sound    => c.resources.global_rides_catalog_slot;
        case RsseActionType::kids_sound           => c.resources.kids_catalog_slot;
        case RsseActionType::staff_sound          => c.resources.staff_catalog_slot;
        case RsseActionType::global_ambient_sound => c.resources.ambient_catalog_slot;
        case RsseActionType::ui_sound             => c.resources.ui_catalog_slot;
        case RsseActionType::object_ride_sound    => get_object_ride_sound_catalog_slot(definition(c, state));
        case _                                    => 0;
    };
}

std::uint32_t positioned(ScriptWorld& c, std::uint32_t catalog, std::uint32_t key, const std::array<std::int32_t, 3>& p) {
    return submit_positioned_template_request(c, 0, catalog, key, p[0], p[1], p[2]);
}

std::int32_t intensity(std::int32_t level, std::int32_t speed, std::int32_t divisor) {
    return numeric::word(numeric::bits(level) + numeric::bits(speed)) / divisor;
}

unsigned voice_group(std::int32_t group) {
    return group == 1 ? 0u : group < 4 ? 1u : group < 8 ? 2u : 3u;
}

void initialize_rsse_action_node(ScriptWorld& c, RsseScriptState& state, std::list<RsseAction>::iterator node, std::int32_t key, std::int32_t scale, std::int32_t action_key) {
    node->handle = dispatch_rsse_action_operation(c, state, node->type, node->source_node_id, key, scale);
    if (particle(node->type) || sound(node->type)) {
        node->source_clone_index = c.ports.meshes.find_embedded_node_resource_record(c, state.mesh_instance_handle, particle(node->type) ? 256u : 512u, node->source_node_id);
    }
    if (node->handle) {
        node->key = action_key;
    } else {
        state.actions.erase(node);
        --c.scripts.states.action_node_count;
    }
}
} //namespace

std::uint32_t get_object_definition_particle_template_index(const ResearchCatalogDefinition& object, std::uint32_t selector) {
    return object.resource_tail.particle_template_indices.at(selector);
}

std::uint32_t get_object_ride_sound_catalog_slot(const ResearchCatalogDefinition& object) {
    return object.resource_tail.ride_sound_catalog_slot;
}

std::uint32_t dispatch_resource_manager_operation(ScriptWorld& c, std::uint32_t target, std::int32_t operation, std::int32_t value) {
    return c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized ? c.ports.resources.process_resource_operation(c, target, operation, value) : 0;
}

std::uint32_t dispatch_rsse_action_operation(ScriptWorld& c, RsseScriptState& state, RsseActionType type, std::int32_t node, std::int32_t key, std::int32_t scale) {
    if (particle(type)) {
        const auto source = resolve_rsse_action_source_transform(c, state, node, 256, type == RsseActionType::directed_particle);
        if (!source) {
            return 0;
        }
        auto effect = numeric::bits(key);
        if (effect & 32768u) {
            effect = get_object_definition_particle_template_index(definition(c, state), effect & 32767u);
        }
        std::uint32_t handle;
        if (type == RsseActionType::directed_particle) {
            if (!source->direction) {
                throw std::invalid_argument("RSSE directed action has unwritten source direction");
            }
            handle = c.ports.effects.create_particle_effect_with_direction(c, effect, coordinates(source->position, 1024.0), coordinates(*source->direction));
        } else {
            handle = c.ports.effects.create_particle_effect(c, effect, coordinates(source->position, 1024.0), 0);
        }
        if (scale != 1000) {
            c.ports.effects.set_particle_render_scale(c, handle, numeric::low_short(scale));
        }
        return handle;
    }
    if (!sound(type)) {
        return 0;
    }
    //The object catalog lookup precedes node resolution only for kind10.
    const auto catalog = type == RsseActionType::object_ride_sound ? sound_catalog(c, state, type) : 0u;
    if (type == RsseActionType::object_ride_sound && !catalog) {
        return 0;
    }
    const auto source = resolve_rsse_action_source_transform(c, state, node, 512);
    if (!source) {
        return 0;
    }
    return positioned(c, type == RsseActionType::object_ride_sound ? catalog : sound_catalog(c, state, type), numeric::bits(key), coordinates(source->position));
}

std::array<float, 3> get_rsse_mesh_effect_origin(ScriptWorld& c, const RsseScriptState& state) {
    std::array<float, 3> result {};
    if (state.mesh_instance_handle) {
        if (const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
            result[1] = c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle);
            result[0] = static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5);
            result[2] = static_cast<float>((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 0.5);
        }
    }
    return result;
}

void set_rsse_repair_effect(ScriptWorld& c, RsseScriptState& state, bool enabled) {
    if (enabled) {
        if (!state.owner_used_thing_id) {
            return;
        }
        const auto effect = definition(c, state).repair_particle_effect;
        if (!effect) {
            return;
        }
        std::array<std::int32_t, 3> p {};
        if (state.mesh_instance_handle) {
            if (const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
                const auto height = c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle);
                const auto x = static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5);
                p = { coordinate(static_cast<double>(x) * 1024.0), coordinate(static_cast<double>(height) * 1024.0), coordinate((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 512.0) };
            }
        }
        state.repair_particle_handle = c.ports.effects.create_particle_effect(c, effect, p, 0);
    } else if (state.repair_particle_handle) {
        c.ports.effects.set_particle_effect_lifetime(c, state.repair_particle_handle, -2);
        state.repair_particle_handle = 0;
    }
}

void spawn_rsse_upgrade_effect(ScriptWorld& c, RsseScriptState& state) {
    const auto& owner = definition(c, state);
    if (!owner.balance) {
        throw std::logic_error("RSSE upgrade balance is not bound");
    }
    const auto effect = owner.balance->Info.UpgradeParticleEffect;
    if (!effect || !state.mesh_instance_handle) {
        return;
    }
    const auto bounds = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle);
    if (!bounds) {
        return;
    }
    const auto height = c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle);
    const std::array position { static_cast<float>((static_cast<double>(bounds->max[0]) + bounds->min[0]) * 0.5), height, static_cast<float>((static_cast<double>(bounds->max[2]) + bounds->min[2]) * 0.5) };
    const auto handle = c.ports.effects.create_particle_effect(c, static_cast<std::uint32_t>(effect), coordinates(position, 1024.0), 0);
    //Particle callbacks may replace the owner's mesh; query it again.
    if (state.mesh_instance_handle) {
        if (const auto spread = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle)) {
            const auto width = static_cast<float>(static_cast<double>(spread->max[0]) - spread->min[0]);
            c.ports.effects.set_particle_effect_spread(c, handle, { coordinate(static_cast<double>(width) * 512.0), coordinate((static_cast<double>(spread->max[1]) - spread->min[1]) * 256.0), coordinate((static_cast<double>(spread->max[2]) - spread->min[2]) * 512.0) });
        }
    }
    (void)positioned(c, c.resources.global_rides_catalog_slot, 0xB8u, coordinates(position));
}

std::uint32_t start_rsse_scream(ScriptWorld& c, std::uint32_t previous, std::int32_t group, std::int32_t level, std::int32_t speed, const std::array<std::int32_t, 3>& p) {
    if (previous) {
        c.ports.resources.request_target_recycle(c, previous);
        c.ports.diagnostics.log_debug_message(c, "RSSE: Started screaming without stopping first");
    }
    if (!group) {
        return 0;
    }
    const auto request = positioned(c, c.resources.kids_catalog_slot, 0x47u + voice_group(group), p);
    if (request) {
        (void)dispatch_resource_manager_operation(c, request, 6, std::clamp(intensity(level, speed, 2), 0, 100));
    }
    return request;
}

std::uint32_t set_rsse_scream_level(ScriptWorld& c, std::uint32_t request, std::int32_t level, std::int32_t speed) {
    return request ? dispatch_resource_manager_operation(c, request, 6, std::clamp(intensity(level, speed, 2), 0, 100)) : 0;
}

std::uint32_t submit_rsse_passenger_voice(ScriptWorld& c, std::int32_t group, std::int32_t level, std::int32_t speed, const std::array<std::int32_t, 3>& p) {
    const auto bucket = std::min(intensity(level, speed, 50), 3);
    if (!group || bucket < 0) {
        return 0;
    }
    return positioned(c, c.resources.kids_catalog_slot, 0x4Bu + 4u * voice_group(group) + static_cast<std::uint32_t>(bucket), p);
}

std::uint32_t submit_rsse_scream(ScriptWorld& c, std::int32_t group, const std::array<std::int32_t, 3>& p) {
    if (!group) {
        return 0;
    }
    const auto key = group == 1 ? 0x69u : group < 4 ? 0x6Au : group < 8 ? 0x6Cu : 0x6Du;
    return positioned(c, c.resources.kids_catalog_slot, key, p);
}

bool step_rsse_action_instruction(ScriptWorld& c, RsseScriptState& state, RsseOpcode opcode) {
    const auto resolve = [&] {
        return resolve_rsse_operand(state, read_rsse_instruction_word(state));
    };
    match (opcode) {
        case RsseOpcode::ADDOBJ or RsseOpcode::ADDOBJ_EXT => {
            const auto action = state.actions.emplace(state.actions.begin());
            ++c.scripts.states.action_node_count;
            //Publish before reading operands, as the original does. Typed storage
            //initializes fields whose pre-initialization native values were undefined.
            try {
                action->type = static_cast<RsseActionType>(numeric::bits(resolve()));
                action->source_node_id = resolve();
                const auto key = resolve(), scale = opcode == RsseOpcode::ADDOBJ_EXT ? resolve() : 1000, action_key = resolve();
                initialize_rsse_action_node(c, state, action, key, scale, action_key);
            } catch (...) {
                state.actions.erase(action);
                --c.scripts.states.action_node_count;
                throw;
            }
            return true;
        }
        case RsseOpcode::KILLOBJ or RsseOpcode::FADEOBJ => {
            const auto key = resolve();
            for (auto action = state.actions.begin(); action != state.actions.end();) {
                if (action->key != key) {
                    ++action;
                    continue;
                }
                if (particle(action->type)) {
                    c.ports.effects.set_particle_effect_lifetime(c, action->handle, -2);
                } else if (sound(action->type)) {
                    if (opcode == RsseOpcode::KILLOBJ) {
                        c.ports.resources.request_target_recycle(c, action->handle);
                    } else {
                        c.ports.resources.submit_resource_target_parameter_request(c, action->handle);
                    }
                }
                action = state.actions.erase(action);
                --c.scripts.states.action_node_count;
            }
            return true;
        }
        case RsseOpcode::SETOBJPARAM => {
            const auto key = resolve(), operation = resolve(), value = resolve();
            for (auto& action : state.actions) {
                if (action.key == key && sound(action.type)) {
                    action.handle = dispatch_resource_manager_operation(c, action.handle, operation, value);
                }
            }
            return true;
        }
        case RsseOpcode::EVENT or RsseOpcode::EVENT_EXT => {
            const auto type = static_cast<RsseActionType>(numeric::bits(resolve()));
            const auto node = resolve(), key = resolve(), scale = opcode == RsseOpcode::EVENT_EXT ? resolve() : 1000;
            (void)dispatch_rsse_action_operation(c, state, type, node, key, scale);
            return true;
        }
        case RsseOpcode::STARTSCREAM => {
            const auto group = resolve(), level = resolve();
            const auto p = coordinates(get_rsse_mesh_effect_origin(c, state));
            state.scream_request = start_rsse_scream(c, state.scream_request, group, level, state.operating_speed, p);
            state.current_value = numeric::word(state.scream_request);
            return true;
        }
        case RsseOpcode::STOPSCREAM => {
            if (state.scream_request) {
                c.ports.resources.submit_resource_target_parameter_request(c, state.scream_request);
            }
            state.scream_request = 0;
            return true;
        }
        case RsseOpcode::SINGLESCREAM => {
            const auto group = resolve(), level = resolve();
            const auto p = coordinates(get_rsse_mesh_effect_origin(c, state));
            if (level >= 0) {
                (void)submit_rsse_passenger_voice(c, group, level, state.operating_speed, p);
            } else {
                (void)submit_rsse_scream(c, group, p);
            }
            return true;
        }
        case RsseOpcode::SCREAMLEVEL => {
            const auto level = resolve();
            state.scream_request = set_rsse_scream_level(c, state.scream_request, level, state.operating_speed);
            return true;
        }
        case RsseOpcode::REPAIREFFECT => {
            const auto enabled = resolve();
            set_rsse_repair_effect(c, state, enabled != 0);
            if (enabled) {
                const auto p = coordinates(c.ports.meshes.get_mesh_root_translation(c, state.mesh_instance_handle));
                (void)positioned(c, c.resources.global_rides_catalog_slot, 0x6Fu, p);
                (void)positioned(c, c.resources.global_rides_catalog_slot, 0xD3u, p);
            } else if (state.value_slots.size() > 5) {
                const auto group = state.value_slots[5];
                const auto p = coordinates(c.ports.meshes.get_mesh_root_translation(c, state.mesh_instance_handle));
                (void)submit_rsse_passenger_voice(c, group, 1, 1, p);
            }
            return true;
        }
        case RsseOpcode::DIPMUSIC => {
            const auto value = resolve();
            state.music_dip_requested = static_cast<std::uint8_t>(value);
            c.ports.audio.stage_music_mute_override(c, static_cast<std::uint16_t>(value));
            return true;
        }
        case RsseOpcode::SPARK => {
            state.spark_source_node_id = numeric::low_short(resolve());
            state.spark_target_node_id = numeric::low_short(resolve());
            (void)resolve();
            (void)resolve();
            (void)resolve_rsse_action_source_transform(c, state, state.spark_source_node_id, 256);
            (void)resolve_rsse_action_source_transform(c, state, state.spark_target_node_id, 256);
            return true;
        }
        case _ => return false;
    }
}
} //namespace simcoaster
