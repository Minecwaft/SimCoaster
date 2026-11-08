#include "simcoaster/world_objects.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/rsse_actions.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
template <class T>
T& record(AttractionWorld& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload) {
        throw std::logic_error("missing park-object lifecycle record");
    }
    return *reinterpret_cast<T*>(payload);
}

bool active_challenge(AttractionWorld& c, ChallengeType type) {
    if (!c.simulation.current) {
        throw std::logic_error("park-object World is not bound");
    }
    const auto& manager = record<ChallengeManagerRecord>(c, c.simulation.current->saved.challenge_manager);
    return std::ranges::any_of(manager.records, [&](const auto& row) {
        return row.type == type && row.status == ChallengeStatus::active;
    });
}

bool can_open(AttractionWorld& c, ParkObjectRuntimeView& object) {
    if (object.state == ParkObjectState::broken_down || object.state == ParkObjectState::condemned || object.requested_service || object.state == ParkObjectState::upgrade_requested || !has_usable_entry_approach(c, &object) || is_park_object_restricted_by_hot_dog_scare_challenge(c, object) || c.simulation.current->saved.park_closed) {
        return false;
    }
    const auto* primary = c.referenced_world();
    if (!primary) {
        throw std::logic_error("park-object primary World is not bound");
    }
    const auto& weather = record<WeatherRecord>(c, primary->saved.weather);
    const auto& balance = get_park_object_balance(c, object);
    const auto closed = match (weather.type) -> std::int32_t {
        case WeatherCategory::heatwave => balance.WeatherModifier.ClosedDuringHeatWave;
        case WeatherCategory::rain     => balance.WeatherModifier.ClosedDuringRain;
        case WeatherCategory::storm    => balance.WeatherModifier.ClosedDuringStorm;
        case WeatherCategory::snow     => balance.WeatherModifier.ClosedDuringSnow;
        case _                         => 0;
    };
    return closed != 1 && (balance.Bumper.WhichTrackType != 3 || is_track_type_3_park_object_runtime_openable(c, &object));
}
} //namespace

bool is_park_object_open_eligible(AttractionWorld& c, ParkObjectRuntimeView& object) {
    return can_open(c, object);
}

bool is_park_object_restricted_by_hot_dog_scare_challenge(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    return get_park_object_balance(c, object).UsageInfo.SpecialIngredient == 1 && active_challenge(c, ChallengeType::code_42);
}

bool is_ride_restricted_by_safety_review_challenge(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    return get_park_object_balance(c, object).Info.WhichUIType == 0 && active_challenge(c, ChallengeType::code_46);
}

void close_park_object(AttractionWorld& c, ParkObjectRuntimeView& object) {
    object.can_load = 0;
    object.person_being_loaded = 0;
    auto* script = world_operations::find_script(c, object.script_id);
    world_operations::set_script_slot(c, script, 6, 1);
    world_operations::select_park_object_mesh_animation_variant(c, object.mesh_instance_id, ParkObjectMeshVariant::closed);
    while (object.first_in_queue) {
        reconcile_park_object_queue_head_with_script_admission(c, object);
    }
}

void open_park_object(AttractionWorld& c, ParkObjectRuntimeView& object) {
    //Native opening diagnoses invalid conditions, then performs the opening anyway.
    if (!can_open(c, object)) {
        c.ports.diagnostics.log_debug_message(c, "!!!!!!!!!! Opening non-openable ride! (non-fatal, can ignore) !!!!!!!!!! \n");
    }
    const auto mesh = object.mesh_instance_id;
    const auto script_id = object.script_id;
    object.can_load = 1;
    auto* script = world_operations::find_script(c, script_id);
    world_operations::configure_mesh_instance_deformation_animation(c, mesh);
    if (get_park_object_balance(c, object).Info.IsSpecial != 1) {
        if (!object.has_been_opened) {
            object.has_been_opened = 1;
            if (!script) {
                throw std::logic_error("opening an object without an RSSE script");
            }
            spawn_rsse_upgrade_effect(c, *script);
        }
        world_operations::set_park_object_mesh_normal_intensity(c, mesh, 1.0f, 1, 0, 0);
        object.has_mesh_darkening_been_applied = 0;
    }
    object.has_been_opened = 1;
    world_operations::set_script_slot(c, script, 6, 0);
    transition_park_object_state(c, object, ParkObjectState::normal);
}

void set_special_ingredient_objects_open(AttractionWorld& c, std::int32_t ingredient, bool open) {
    if (!c.simulation.current) {
        throw std::logic_error("park-object World is not bound");
    }
    auto id = c.simulation.current->saved.first_park_object;
    std::size_t visited = 0;
    while (id) {
        if (visited++ >= c.entities.size()) {
            throw std::length_error("cyclic park-object list");
        }
        auto& object = record<ParkObjectRuntimeView>(c, id);
        if (get_park_object_balance(c, object).UsageInfo.SpecialIngredient == ingredient) {
            if (open) {
                open_park_object(c, object);
            } else {
                close_park_object(c, object);
            }
        }
        id = object.next_park_object;
    }
}

void world_operations::builtin::set_challenge_restricted_objects_open(WorldRuntime& c, bool open) {
    set_special_ingredient_objects_open(c, 1, open);
}
} //namespace simcoaster
