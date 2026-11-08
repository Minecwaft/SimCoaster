#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include <bit>
#include <cstring>

namespace simcoaster {
namespace {
double restored_clock_scale(std::int32_t speed) {
    return speed / 80.0;
}

float restored_view_value(std::int32_t value) {
    return static_cast<float>(value);
}

void restore_animation_presentation(WorldRuntime& c, AnimationManagerRecord* record) {
    auto* buttons = c.ports.ui.find_ui_control(c, c.ui.interaction_toolbar, 64);
    c.ports.ui.set_selected_ui_checkbutton_id(c, buttons, record->saved_toolbar_button_id ? record->saved_toolbar_button_id : 67);
    if (auto speed = c.animation.control.saved_speed_percent) {
        if (speed <= 10) {
            speed = 10;
        }
        if (speed >= 400) {
            speed = 400;
        }
        c.timing.speed_percent = speed;
        c.timing.scale = restored_clock_scale(speed);
        c.animation.control.saved_speed_percent = 0;
    }
    if (auto* window = record->playback_window) {
        c.ports.ui.dispatch_ui_control_event(c, window, 4, 0, 0);
        c.view.first_person_view_distance = restored_view_value(record->saved_first_person_distance);
        c.view.fog_distance_start = restored_view_value(record->saved_fog_start);
        c.view.fog_distance_end = restored_view_value(record->saved_fog_end);
        c.ports.camera.restore_world_view_from_special_modes(c);
        c.ports.ui.restore_persistent_roots_after_transition(c);
    }
    record->playback_window = nullptr;
}
} //namespace

int reset_world_animation_runtime_state(WorldRuntime& c, AnimationManagerRecord* record) {
    cancel_prepared_mission_transition(c);
    record->playback_window = nullptr;
    c.animation.thing = 0;
    record->saved_toolbar_button_id = 0;
    c.animation.control = {};
    return 0;
}

AnimationManagerRecord* initialize_world_animation_state(WorldRuntime& c, AnimationManagerRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 22);
    reset_world_animation_runtime_state(c, record);
    return record;
}

MissionControllerRecord* initialize_world_cutscene_state(WorldRuntime& c, MissionControllerRecord* record) {
    initialize_world_map_object_base(c, &record->base, 1, 21);
    record->cutscene_state = MissionCutsceneState::idle;
    record->cutscene_object = 0;
    record->research_project_add_attempt_pending = 1;
    record->mission_advisor_action_pending = 0;
    for (int category : { 35, 36, 37, 38, 41 }) {
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &record->base.header, category);
    }
    if (!get_or_create_theme_global_info_manager(c).end_of_game_event_processed) {
        initialize_mission_objective_tracking(c);
    }
    return record;
}

void clear_global_animation_target(WorldRuntime& c) {
    c.animation.thing = 0;
    cancel_prepared_mission_transition(c);
}

bool has_active_world_animation_target(const WorldRuntime& c) {
    return c.animation.thing != 0;
}

bool is_world_animation_profile_active(const WorldRuntime& c, const AnimationManagerRecord* record, AnimationPlaybackProfile profile) {
    return c.animation.thing && record->playback_profile == profile;
}

std::uint8_t request_global_animation_close_if_pending(WorldRuntime& c) {
    const auto enabled = c.animation.control.skip_enabled;
    if (enabled) {
        c.animation.control.completion_requested = 1;
    }
    return enabled;
}

void latch_world_animation_completion_if_target_active(WorldRuntime& c) {
    if (c.animation.thing) {
        c.animation.control.completion_requested = 1;
    }
}

std::intptr_t handle_world_animation_playback_window_event(WorldRuntime& c, UiControl* control, int event, std::uint32_t id, std::intptr_t value) {
    if (event == 65546 && id == 27 && c.animation.control.skip_enabled) {
        c.animation.control.completion_requested = 1;
    }
    return c.ports.ui.invoke_secondary_ui_event_callback(c, control, event, id, value);
}

std::uint32_t complete_world_animation_state(WorldRuntime& c, AnimationManagerRecord* record, RsseScriptState* script) {
    restore_animation_presentation(c, record);
    const auto slot0 = world_operations::script_slot(c, script, 0);
    const auto target = c.animation.thing;
    c.animation.thing = 0;
    world_operations::set_script_slot(c, script, 0, 0);
    world_operations::set_script_slot(c, script, 1, 0);
    if (c.mode.source_recreation == 1) {
        c.ports.ui.finish_intro_sequence(c, 0);
    }
    if (record->playback_profile == AnimationPlaybackProfile::code_2) {
        //The native getter's ignored result still reads this metadata row.
        auto* volatile cutscene = c.entities[c.simulation.reference->saved.mission_controller].payload;
        (void)cutscene;
        dispatch_initial_mission_event_once(c);
        c.animation.control.objectives_open_counter = 1;
        record->playback_profile = AnimationPlaybackProfile::code_0;
    }
    {
        WorldEvent event { AnimationCompletedEvent { target, slot0 } };
        dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
    }
    if (!c.missions.mission_transition.failure && c.ports.assets.selected_resource_source(c) == 1 && find_first_park_object_record_id_by_definition_id(c, 1750) == target) {
        c.ports.ui.request_resource_source_change_overlay(c, 2, 0);
    }
    c.resources.renderer_cache.generation = c.resources.renderer_cache_generation;
    c.resources.renderer_cache.flags |= 0x40000000u;
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    return world_operations::normalize_special_ride_owner_vectors(c);
}

void update_world_animation_state(WorldRuntime& c, AnimationManagerRecord* record) {
    if (c.animation.thing) {
        if (const auto* prepared = c.missions.mission_transition.prepared.get(); prepared && prepared->target == c.animation.thing) {
            const auto* object = c.animation.thing < c.entities.size() ? c.entities[c.animation.thing].payload : nullptr;
            if (prepared->target_invalidated || prepared->world != c.simulation.current || prepared->world_epoch != c.missions.mission_transition.world_epoch || object != prepared->target_identity || !object || object->retirement_pending || object->kind != ThingKind::park_object || reinterpret_cast<const ParkObjectRuntimeView*>(object)->definition_id != prepared->target_definition || reinterpret_cast<const ParkObjectRuntimeView*>(object)->creation_file_time != prepared->target_creation_time) {
                restore_animation_presentation(c, record);
                c.animation.thing = 0;
                c.animation.control.completion_requested = 0;
                auto* header = c.simulation.current ? c.entities[c.simulation.current->saved.mission_controller].payload : nullptr;
                if (header && header->kind == ThingKind::mission_controller) {
                    auto& controller = *reinterpret_cast<MissionControllerRecord*>(header);
                    if (controller.cutscene_state == MissionCutsceneState::removed_obstacle_playback) {
                        get_or_create_theme_global_info_manager(c).obstacle_removed_message_received = 0;
                    }
                    controller.cutscene_state = MissionCutsceneState::idle;
                    controller.cutscene_object = 0;
                }
                cancel_prepared_mission_transition(c);
                c.missions.mission_transition.failure = MissionTransitionFailure { MissionTransitionFailureKind::missing_target, {}, "Mission playback stopped because its completion object is no longer available." };
                update_objectives_panel_and_queue_mission_advisor_action(c);
                return;
            }
        }
        const auto script_id = c.entities.payload<ParkObjectRuntimeView>(c.animation.thing)->script_id;
        auto* script = world_operations::find_script(c, script_id);
        if (world_operations::script_slot(c, script, 1) == 1) {
            c.animation.control.completion_requested = 1;
        }
        if (c.animation.control.completion_requested) {
            complete_world_animation_state(c, record, script);
        }
    }
    if (c.animation.control.objectives_open_counter) {
        const auto next = static_cast<std::uint16_t>(c.animation.control.objectives_open_counter + 1);
        c.animation.control.objectives_open_counter = next;
        if (std::bit_cast<std::int16_t>(next) >= 3) {
            c.animation.control.objectives_open_counter = 0;
            c.ports.ui.open_objectives_panel(c);
        }
    }
}
} //namespace simcoaster
