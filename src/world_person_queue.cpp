#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/rsse_people.hpp"

namespace simcoaster {
namespace {
ParkObjectRuntimeView& target(VisitorWorld& c, PersonRuntimeView& person) {
    return person_detail::park_object(c, person.major_destination);
}

WeatherCategory current_weather(VisitorWorld& c) {
    return reinterpret_cast<WeatherRecord*>(&person_detail::map_object(c, person_detail::primary_world(c).saved.weather))->type;
}

bool wet(WeatherCategory weather) {
    return weather == WeatherCategory::rain || weather == WeatherCategory::storm || weather == WeatherCategory::snow;
}

void idle(VisitorWorld& c, PersonRuntimeView& person) {
    world_operations::enter_world_person_state(c, &person, PersonState::roaming);
}

void thought(VisitorWorld& c, PersonRuntimeView& person, MobileThoughtCode code) {
    world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, code, false);
}

void walkaway(VisitorWorld& c, PersonRuntimeView& person, ParkObjectRuntimeView& object, bool suppress) {
    (void)remove_person_from_park_object_queue(c, &object, person.base.header.id);
    dismiss_person_from_park_object_queue(c, &person, suppress);
    record_park_object_queue_walkaway(object);
    idle(c, person);
}

void excessive_queue_history(ParkObjectRuntimeView& object) {
    if (!object.history) {
        throw std::logic_error("queue walkaway has no object history");
    }
    ++object.history->dword_history_30[10].temporary;
}
} //namespace

bool route_person_to_current_queue_position(VisitorWorld& c, PersonRuntimeView& person) {
    const auto position = get_person_park_object_queue_position(c, &target(c, person), person.base.header.id);
    if (position == -1) {
        return false;
    }
    person.queue_position = static_cast<std::uint8_t>(position);
    const auto location = find_park_object_queue_destination(c, target(c, person), static_cast<std::uint32_t>(position));
    const auto cell = static_cast<std::uint32_t>(location.cell) - 1u;
    const auto x = static_cast<std::uint16_t>(((cell & 127u) << 8) | location.x_offset), y = static_cast<std::uint16_t>((((cell >> 7) & 255u) << 8) | location.y_offset);
    if (!request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y)) {
        return false;
    }
    world_operations::enter_world_person_state(c, &person, PersonState::move_to_queue_position);
    return true;
}

void update_person_en_route_to_park_object_queue(VisitorWorld& c, PersonRuntimeView& person) {
    auto* payload = c.entities.at(person.major_destination).payload;
    if (!payload) {
        person.major_destination = 0;
        idle(c, person);
        return;
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(payload);
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    const auto abandon = [&] {
        decrease_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::big);
        person.major_destination = 0;
        idle(c, person);
    };
    if (!object.can_load || navigation == NavigationResult::unreachable) {
        abandon();
        return;
    }
    if (navigation != NavigationResult::arrived) {
        if (person_detail::base_world(c).saved.park_closed) {
            abandon();
            return;
        }
        bool valid = true;
        if (person.major_destination) {
            if (get_park_object_balance(c, object).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::balloon) && (person.umbrella_state == PersonUmbrellaState::active || person.umbrella_state == PersonUmbrellaState::creation_pending)) {
                valid = false;
            }
            if (valid && get_park_object_balance(c, object).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::umbrella) && !wet(current_weather(c))) {
                valid = false;
            }
        }
        if (!valid) {
            person.major_destination = 0;
            idle(c, person);
            return;
        }
        ++person.mobile.count;
        if (person.mobile.count > 11) {
            person.mobile.count = 0;
            reselect_person_ride_target_from_nearby_cells(c, person);
        }
        return;
    }
    if (!person.major_destination) {
        idle(c, person);
        return;
    }
    const auto back = get_back_of_queue(c, &object);
    if (back != person_detail::cell_id(person.base)) {
        const auto moved_back = get_back_of_queue(c, &object);
        if (!moved_back || !try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, moved_back)) {
            idle(c, person);
        }
        return;
    }
    if (!has_spare_park_object_queue_capacity(c, object)) {
        idle(c, person);
        return;
    }
    const auto difference = compute_person_ride_excitement_difference(c, person, object);
    if (get_park_object_excitement(c, object, true) && std::abs(difference) >= 45) {
        thought(c, person, difference < 0 ? MobileThoughtCode::ride_not_exciting_enough : MobileThoughtCode::ride_too_exciting);
        insert_person_temporary_ride_history_entry(person, person.major_destination);
        person.major_destination = 0;
        person.time_started_idling = 0;
        idle(c, person);
        return;
    }
    if (park_object_queue_exceeds_position_limit(c, object)) {
        insert_person_temporary_ride_history_entry(person, person.major_destination);
        thought(c, person, MobileThoughtCode::queue_too_long);
        person.major_destination = 0;
        idle(c, person);
        record_park_object_queue_walkaway(object);
        excessive_queue_history(object);
        return;
    }
    person.happiness_at_queue_entry = person.happiness;
    append_person_to_park_object_queue(c, object, person.base.header.id);
    if (!route_person_to_current_queue_position(c, person)) {
        (void)remove_person_from_park_object_queue(c, &object, person.base.header.id);
        dismiss_person_from_park_object_queue(c, &person, false);
        idle(c, person);
    }
}

void update_person_queue_state(VisitorWorld& c, PersonRuntimeView& person) {
    if (!person.major_destination) {
        idle(c, person);
        return;
    }
    auto& object = target(c, person);
    if (!person.queue_position) {
        if (person.been_admitted && is_person_being_loaded_by_park_object(object, person.base.header.id)) {
            person.been_admitted = 0;
            const auto [x, y] = get_park_object_entry_exit_position(c, object, false);
            if (request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y)) {
                world_operations::enter_world_person_state(c, &person, PersonState::admission_step);
                return;
            }
            clear_park_object_person_admission_state(c, object, person.base.header.id);
            walkaway(c, person, target(c, person), false);
            return;
        }
        if (!person.queue_position && person.been_admitted && !is_person_being_loaded_by_park_object(object, person.base.header.id)) {
            walkaway(c, person, target(c, person), false);
            return;
        }
    }
    if (park_object_requires_staff_service(c, object)) {
        thought(c, person, MobileThoughtCode::sick);
        walkaway(c, person, target(c, person), false);
        return;
    }
    if (get_park_object_balance(c, object).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::umbrella) && !wet(current_weather(c))) {
        if (current_weather(c) == WeatherCategory::heatwave || (person.queue_admission_delay_active && (person_detail::primary_world(c).saved.game_tick >> 2) >= person.queue_admission_deadline_quarter_ticks)) {
            walkaway(c, person, target(c, person), true);
            return;
        }
        if (!person.queue_admission_delay_active) {
            const auto response = c.visitors.needs_balance.PeepInfo.SlowestWeatherChangeResponse;
            if (!response) {
                throw std::domain_error("zero queue weather-response interval");
            }
            const auto delay = static_cast<std::uint32_t>(next_crt_random(c) % response);
            person.queue_admission_deadline_quarter_ticks = delay;
            person.queue_admission_delay_active = 1;
            person.queue_admission_deadline_quarter_ticks = delay + (person_detail::primary_world(c).saved.game_tick >> 2);
        }
    }
    const auto position = get_person_park_object_queue_position(c, &object, person.base.header.id);
    if (position == -1) {
        walkaway(c, person, target(c, person), false);
        return;
    }
    if (person.queue_position == position) {
        if (person.queue_position > static_cast<std::uint32_t>(get_park_object_queue_position_limit(c, object))) {
            thought(c, person, MobileThoughtCode::queue_too_long);
            walkaway(c, person, target(c, person), false);
            excessive_queue_history(object);
            return;
        }
        if ((get_park_object_definition(c, &object)->which_track_type == 3 && !is_track_type_3_park_object_runtime_openable(c, &object)) || (get_park_object_balance(c, object).Bumper.WhichTrackType == 1 && !object.is_track_ride_valid)) {
            thought(c, person, MobileThoughtCode::angry);
            walkaway(c, person, target(c, person), false);
            return;
        }
    } else if (person.queue_move_delay && static_cast<std::uint32_t>(person.queue_position - position) <= 2u) {
        --person.queue_move_delay;
    } else if (object.state != ParkObjectState::broken_down && !route_person_to_current_queue_position(c, person)) {
        walkaway(c, person, target(c, person), false);
        return;
    }
    if (person_detail::base_world(c).saved.game_tick - person.time_of_last_spot_animation <= 0x1Eu) {
        if (person_detail::base_world(c).saved.game_tick > person.time_started_idling + 100u) {
            thought(c, person, MobileThoughtCode::ride_not_exciting_enough);
            walkaway(c, person, target(c, person), false);
            return;
        }
        if (!(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 0xAu)) {
            auto angle = fixed::word(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 0x320u + person.mobile.sprite_angle - 400u);
            if (angle < 0) {
                angle = fixed::add(angle, 2048);
            } else if (angle >= 2048) {
                angle = fixed::subtract(angle, 2048);
            }
            person.mobile.sprite_angle = static_cast<std::uint32_t>(angle);
        }
    } else {
        const auto happiness = truncate_park_object_condition(person.happiness);
        const bool unencumbered = person.umbrella_state == PersonUmbrellaState::none || person.umbrella_state == PersonUmbrellaState::inactive;
        if (happiness > 80) {
            if (unencumbered) {
                start_person_temporary_appearance_animation(c, person, static_cast<std::uint32_t>(get_person_sprite_generation_variant_code(c, person)));
            }
        } else if (happiness >= 20) {
            if (truncate_park_object_condition(person.needs.toilet_need) > 80) {
                thought(c, person, MobileThoughtCode::need_toilet);
                if (!(std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::provides_relief))) {
                    walkaway(c, person, target(c, person), false);
                    return;
                }
            }
        } else if (happiness < 10) {
            thought(c, person, MobileThoughtCode::unhappy);
            walkaway(c, person, target(c, person), false);
            return;
        } else if (unencumbered) {
            start_person_temporary_appearance_animation(c, person, 4);
        }
    }
}

void update_person_waiting_for_park_object_admission(VisitorWorld& c, PersonRuntimeView& person) {
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (navigation != NavigationResult::arrived && navigation != NavigationResult::unreachable) {
        return;
    }
    auto& object = target(c, person);
    if (is_object_price_too_high_for_person(c, person, object)) {
        thought(c, person, MobileThoughtCode::sideshow_loss);
        decrease_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::medium);
        clear_park_object_person_admission_state(c, object, person.base.header.id);
        walkaway(c, person, target(c, person), false);
        return;
    }
    //The native admission test is CMP BYTE,1; the queue-wait test uses nonzero.
    if (person.queue_admission_delay_active == 1) {
        const auto remaining = person.queue_admission_deadline_quarter_ticks - (person_detail::primary_world(c).saved.game_tick >> 2);
        if (person.balloon_script || person.remaining_balloon_life || static_cast<double>(c.visitors.needs_balance.PeepInfo.SlowestWeatherChangeResponse) * static_cast<double>(0.66f) >= remaining) {
            clear_park_object_person_admission_state(c, object, person.base.header.id);
            walkaway(c, person, target(c, person), true);
            return;
        }
        person.queue_admission_delay_active = 0;
        person.queue_admission_deadline_quarter_ticks = 0;
    }
    if (admit_person_to_park_object(c, object, person.base.header.id)) {
        world_operations::enter_world_person_state(c, &person, PersonState::wait_for_object_script);
        return;
    }
    if (!route_person_to_current_queue_position(c, person)) {
        (void)remove_person_from_park_object_queue(c, &object, person.base.header.id);
        dismiss_person_from_park_object_queue(c, &person, false);
        idle(c, person);
    }
}
} //namespace simcoaster
