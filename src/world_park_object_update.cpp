#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/rsse_state.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
ResearchCatalogDefinition& definition(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    return require_bound<std::out_of_range>(get_park_object_definition(c, &object), "park-object definition");
}

PersonRuntimeView& person(AttractionWorld& c, ThingId id) {
    auto* result = c.entities.at(id).payload;
    if (!result) {
        throw std::invalid_argument("missing park-object person");
    }
    return *reinterpret_cast<PersonRuntimeView*>(result);
}

ParkObjectRuntimeView& destination(AttractionWorld& c, const PersonRuntimeView& record) {
    auto* result = c.entities.at(record.major_destination).payload;
    if (!result) {
        throw std::invalid_argument("missing person destination");
    }
    return *reinterpret_cast<ParkObjectRuntimeView*>(result);
}

bool has(const ParkObjectRuntimeView& object, ParkObjectCapabilities flag) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
}

CellId cell(const ParkObjectRuntimeView& object) {
    return static_cast<CellId>(((object.base.y >> 8) << 7) + (object.base.x >> 8) + 1);
}

//Unordered native comparisons take the zero branch; NaN is not full condition.
float clamped_condition(double value) {
    return value >= 0.0 ? (value <= 100.0 ? static_cast<float>(value) : 100.0f) : 0.0f;
}

std::int32_t script_integer(double value) {
    const auto truncated = std::trunc(value), limit = std::ldexp(1.0, 63);
    return std::isfinite(truncated) && truncated >= -limit && truncated < limit ? numeric::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(truncated))) : 0;
}

void finish_person_exit(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto* script = world_operations::find_script(c, object.script_id);
    const auto id = static_cast<ThingId>(world_operations::script_slot(c, script, 1));
    if ((has(object, ParkObjectCapabilities::is_choosable) || definition(c, object).is_photo_booth) && id) {
        if (world_operations::try_complete_person_park_object_exit(c, &person(c, id))) {
            world_operations::set_script_slot(c, script, 1, 0);
        }
    }
}
} //namespace

double calculate_park_object_condition_decay(AttractionWorld& c, ParkObjectRuntimeView& object, float speed, float capacity, float, bool use_requested_capacity) {
    auto* script = world_operations::find_script(c, object.script_id);
    auto occupancy = world_operations::script_slot(c, script, 5);
    if (occupancy < 0) {
        occupancy = 0;
    } else if (occupancy > numeric::word(definition(c, object).max_capacity)) {
        occupancy = numeric::word(definition(c, object).max_capacity);
    }
    const auto speed_factor = static_cast<float>(static_cast<double>(speed) / numeric::word(definition(c, object).max_speed) * static_cast<double>(0.9f));
    const bool speed_at_redline = !(static_cast<double>(numeric::word(definition(c, object).research.upgrades.at(object.current_upgrade).redline_speed)) > speed);
    const auto adjusted_speed = static_cast<float>(speed_at_redline ? (static_cast<double>(speed_factor) + static_cast<double>(1.1f)) * 0.5 : static_cast<double>(speed_factor) + static_cast<double>(0.1f));
    const auto capacity_factor = static_cast<float>((use_requested_capacity ? static_cast<double>(capacity) : occupancy) / numeric::word(definition(c, object).max_capacity) * static_cast<double>(0.9f));
    //The capacity redline uses occupied seats *0.1, even when the caller supplies
    //a different requested capacity. Express comparisons directly, without FPU flags.
    const bool capacity_at_redline = !(static_cast<double>(numeric::word(definition(c, object).research.upgrades.at(object.current_upgrade).redline_capacity)) > static_cast<double>(occupancy) * static_cast<double>(0.1f));
    const auto adjusted_capacity = static_cast<float>(capacity_at_redline ? (static_cast<double>(capacity_factor) + static_cast<double>(1.1f)) * 0.5 : static_cast<double>(capacity_factor) + static_cast<double>(0.1f));
    return numeric::word(definition(c, object).research.upgrades.at(object.current_upgrade).wear_rate) * (static_cast<double>(adjusted_capacity) + adjusted_speed) * 0.5;
}

void update_park_object_condition(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto& original_definition = definition(c, object);
    if (!original_definition.research.upgrades.at(object.current_upgrade).wear_rate) {
        return;
    }
    auto* running_script = world_operations::find_script(c, object.script_id);
    if (world_operations::script_slot(c, running_script, 9) <= 0 && !has(object, ParkObjectCapabilities::runs_continuously)) {
        return;
    }
    if (c.simulation.current->saved.game_tick & 63u) {
        return;
    }
    auto* condition_script = world_operations::find_script(c, object.script_id);
    float decay = 0;
    if (world_operations::script_slot(c, condition_script, 5)) {
        auto* decay_script = world_operations::find_script(c, object.script_id);
        const auto speed = static_cast<float>(get_rsse_script_state_operating_speed(c, decay_script));
        const auto capacity = static_cast<float>(world_operations::script_slot(c, decay_script, 2));
        const auto duration = static_cast<float>(world_operations::script_slot(c, decay_script, 3));
        decay = static_cast<float>(calculate_park_object_condition_decay(c, object, speed, capacity, duration, false));
    }
    object.repair_state = clamped_condition(static_cast<double>(object.repair_state) - decay);
    if (definition(c, object).is_special != 1) {
        object.working_life = clamped_condition(static_cast<double>(object.working_life) - 0.02 * decay);
    }
    if (truncate_park_object_condition(object.repair_state) < original_definition.research.breakdown_warn_player) {
        world_operations::set_script_slot(c, condition_script, 8, 1);
    }
    const auto condition = static_cast<double>(truncate_park_object_condition(object.repair_state));
    if (condition < original_definition.research.breakdown_imminent && condition + decay >= original_definition.research.breakdown_imminent) {
        auto* world = c.simulation.current;
        const auto id = object.base.header.id;
        const auto definition_id = object.definition_id;
        auto candidate = world_operations::make_advisor_action_candidate(c, 64);
        candidate.thing_id = id;
        candidate.secondary = definition_id;
        candidate.response_no = std::numeric_limits<std::uint32_t>::max();
        auto* advisor = c.entities.at_payload<AdvisorRecord>(world->saved.advisor);
        if (!advisor) {
            throw std::invalid_argument("missing breakdown advisor");
        }
        (void)world_operations::queue_advisor_action_candidate(c, advisor, candidate);
    }
}

void tick_world_park_object(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto* entry_script = world_operations::find_script(c, object.script_id);
    if (has(object, ParkObjectCapabilities::is_fireworks)) {
        auto* script = world_operations::find_script(c, object.script_id);
        if (world_operations::script_slot(c, script, 0) == 1) {
            (void)remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::fireworks, cell(object));
            world_operations::set_script_slot(c, entry_script, 0, 2);
        }
    }
    if (object.state == ParkObjectState::non_usable || object.state == ParkObjectState::condemned) {
        return;
    }
    if (definition(c, object).is_special != 1 && !object.can_load && !object.has_mesh_darkening_been_applied) {
        const auto flags = std::to_underlying(object.capabilities);
        bool darken = !(flags & std::to_underlying(ParkObjectCapabilities::is_choosable)) && !(flags & std::to_underlying(ParkObjectCapabilities::is_usable));
        if (!darken) {
            auto* script = world_operations::find_script(c, object.script_id);
            if (world_operations::script_slot(c, script, 9) <= 0) {
                script = world_operations::find_script(c, object.script_id);
                darken = world_operations::script_slot(c, script, 5) <= 0;
            }
        }
        if (darken) {
            world_operations::set_park_object_mesh_normal_intensity(c, object.mesh_instance_id, 0.5f, 1, 0, 0);
            object.has_mesh_darkening_been_applied = 1;
        }
    }
    if (object.state != ParkObjectState::broken_down && object.can_load && !(c.simulation.current->saved.game_tick & 7u)) {
        update_park_object_condition(c, object);
        if ((!truncate_park_object_condition(object.working_life) || !truncate_park_object_condition(object.repair_state)) && !has(object, ParkObjectCapabilities::provides_relief)) {
            if (!world_operations::script_slot(c, entry_script, 4)) {
                if (truncate_park_object_condition(object.working_life) && definition(c, object).is_special != 1) {
                    object.working_life = clamped_condition(static_cast<double>(object.working_life) - 5.0);
                }
                auto* script = world_operations::find_script(c, object.script_id);
                world_operations::set_script_slot(c, script, 4, 1);
            }
        }
    }
    if (has(object, ParkObjectCapabilities::provides_relief) && park_object_requires_staff_service(c, object)) {
        auto* script = world_operations::find_script(c, object.script_id);
        world_operations::set_script_slot(c, script, 8, 1);
    }
    if (object.first_in_queue) {
        auto* payload = c.entities.at(object.first_in_queue).payload;
        if (!payload || !is_person_in_park_object_queue_state(reinterpret_cast<PersonRuntimeView*>(payload))) {
            object.first_in_queue = 0;
        }
    }
    if (object.prebuilt_track_open) {
        if (!entry_script) {
            throw std::invalid_argument("missing prebuilt-track script");
        }
        if (world_operations::find_script(c, entry_script->sound_script_state_id)) {
            world_operations::complete_prebuilt_coaster_catalog(c, object);
            world_operations::clear_park_object_coaster_geometry_peer_links(c, object);
            object.prebuilt_track_open = 0;
        }
    }
}

void reconcile_park_object_queue_head_with_script_admission(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto* script = world_operations::find_script(c, object.script_id);
    const auto admitted = world_operations::script_slot(c, script, 0);
    const auto head = object.first_in_queue;
    if (admitted == head || person(c, head).state != PersonState::wait_for_object_script) {
        if (object.first_in_queue) {
            auto& record = person(c, head);
            remove_person_from_park_object_queue(c, &object, object.first_in_queue);
            dismiss_person_from_park_object_queue(c, &record, false);
        }
    } else {
        auto& record = person(c, head);
        auto& target = destination(c, record);
        script = world_operations::find_script(c, target.script_id);
        if (world_operations::script_slot(c, script, 0) != target.first_in_queue) {
            remove_person_from_park_object_queue(c, &target, record.base.header.id);
            world_operations::enter_world_person_state(c, &record, PersonState::object_interaction);
        }
    }
}

void park_object_let_person_on(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto* script = world_operations::find_script(c, object.script_id);
    if (!(has(object, ParkObjectCapabilities::is_choosable) || definition(c, object).is_photo_booth) || !object.can_load) {
        return;
    }
    if (world_operations::script_slot(c, script, 2) <= 0) {
        reconcile_park_object_queue_head_with_script_admission(c, object);
        return;
    }
    const auto full = [&] {
        const auto occupancy = world_operations::script_slot(c, script, 5);
        const auto capacity = world_operations::script_slot(c, script, 2);
        return occupancy >= capacity && definition(c, object).which_track_type != 3 && definition(c, object).which_track_type != 2;
    };
    if (world_operations::script_slot(c, script, 0) || full() || (world_operations::script_slot(c, script, 9) && !has(object, ParkObjectCapabilities::runs_continuously)) || object.person_being_loaded) {
        if (object.person_being_loaded && person(c, object.person_being_loaded).state != PersonState::admission_step) {
            object.person_being_loaded = 0;
        }
    } else if (object.first_in_queue) {
        auto& record = person(c, object.first_in_queue);
        if (record.state == PersonState::wait_in_object_queue && !record.queue_position) {
            record.been_admitted = 1;
            record.price_to_pay_at_end = destination(c, record).price_per_use;
            object.person_being_loaded = object.first_in_queue;
        }
    }
}

void transition_park_object_state(AttractionWorld& c, ParkObjectRuntimeView& object, ParkObjectState requested) {
    if (requested == ParkObjectState::broken_down || requested == ParkObjectState::upgrade_requested || requested == ParkObjectState::condemned) {
        reconcile_park_object_queue_head_with_script_admission(c, object);
        object.can_load = 0;
        object.person_being_loaded = 0;
        auto* script = world_operations::find_script(c, object.script_id);
        world_operations::set_script_slot(c, script, 6, 1);
        world_operations::select_park_object_mesh_animation_variant(c, object.mesh_instance_id, ParkObjectMeshVariant::closed);
        const auto dispatch = [&](auto value) {
            WorldEvent event { value };
            dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
        };
        match (requested) {
            case ParkObjectState::broken_down       => dispatch(ParkObjectBrokenDownEvent { object.base.header.id });
            case ParkObjectState::upgrade_requested => dispatch(ParkObjectUpgradeRequestedEvent { object.base.header.id });
            case ParkObjectState::condemned         => dispatch(ParkObjectCondemnedEvent { object.base.header.id });
            case _                                  => {
            }
        }
    }
    object.state = requested; //Notifications observe the previous state.
}

void tick_open_park_object_state(AttractionWorld& c, ParkObjectRuntimeView& object) {
    park_object_let_person_on(c, object);
    auto* script = world_operations::find_script(c, object.script_id);
    if (world_operations::script_slot(c, script, 7)) {
        const auto state = truncate_park_object_condition(object.working_life) ? ParkObjectState::broken_down : ParkObjectState::condemned;
        world_operations::select_park_object_mesh_animation_variant(c, object.mesh_instance_id, state == ParkObjectState::broken_down ? ParkObjectMeshVariant::construction : ParkObjectMeshVariant::condemned);
        transition_park_object_state(c, object, state);
    } else {
        finish_person_exit(c, object);
    }
}

void update_world_park_object_state(AttractionWorld& c, ParkObjectRuntimeView& object) {
    (do {
        match (object.state) {
            case ParkObjectState::normal                                                                          => tick_open_park_object_state(c, object);
            case ParkObjectState::broken_down or ParkObjectState::upgrade_requested or ParkObjectState::condemned => {
                reconcile_park_object_queue_head_with_script_admission(c, object);
                finish_person_exit(c, object);
            }
            case ParkObjectState::non_usable => {
                if (!has(object, ParkObjectCapabilities::requires_watering)) {
                    do_return;
                }
                auto* script = world_operations::find_script(c, object.script_id);
                auto* weather = c.entities.at_payload<WeatherRecord>(c.simulation.current->saved.weather);
                if (!weather) {
                    throw std::invalid_argument("missing vegetation weather");
                }
                auto* scale_script = world_operations::find_script(c, object.script_id);
                const auto old_scale = static_cast<float>(world_operations::script_slot(c, scale_script, 14) / 1000);
                const auto scale = match (weather->type) -> float {
                    case WeatherCategory::heatwave => c.weather.vegetation_weather.heatwave_wither;
                    case WeatherCategory::rain     => c.weather.vegetation_weather.rain_wither;
                    case WeatherCategory::storm    => c.weather.vegetation_weather.storm_wither;
                    case WeatherCategory::snow     => c.weather.vegetation_weather.snow_wither;
                    case _                         => 1.0f;
                };
                if (scale != old_scale) {
                    world_operations::set_script_slot(c, script, 14, script_integer(static_cast<double>(scale) * 1000.0));
                }
                const auto condition = world_operations::script_slot(c, script, 0), previous = world_operations::script_slot(c, script, 10);
                object.repair_state = static_cast<float>(numeric::word(33u * (3u - static_cast<std::uint32_t>(condition))));
                if (condition != previous) {
                    world_operations::set_script_slot(c, script, 10, condition);
                    if (previous >= 0 && previous <= 3) {
                        (void)remove_world_re_cell_effect_profile(c.simulation.region_effects, static_cast<RegionEffectProfileId>(10 + previous), cell(object));
                    }
                    if (condition >= 0 && condition <= 3) {
                        (void)add_world_re_cell_effect_profile(c.simulation.region_effects, static_cast<RegionEffectProfileId>(10 + condition), cell(object));
                    }
                    if (condition == 3) {
                        auto* tags = c.entities.at_payload<TagSystemRecord>(c.simulation.current->saved.tag_system);
                        if (!tags) {
                            throw std::invalid_argument("missing vegetation tags");
                        }
                        constexpr std::array<std::int32_t, 4> keys { 44, 45, 46, 47 };
                        (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(c, tags, keys, object.base.header.id);
                    }
                }
            }
            case _ => {
            }
        }
    });
}
} //namespace simcoaster
