#include "world_person_use_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"

namespace simcoaster {
using namespace person_use;

bool world_operations::builtin::try_complete_person_park_object_exit(WorldRuntime& c, PersonRuntimeView* person) {
    if (!person) {
        throw std::invalid_argument("null exiting person");
    }
    return simcoaster::try_complete_person_park_object_exit(c, *person);
}

bool try_complete_person_park_object_exit(VisitorWorld& c, PersonRuntimeView& person) {
    if (!person.major_destination) {
        return true;
    }
    auto& object = person_detail::park_object(c, person.major_destination);
    const auto position = get_park_object_entry_exit_position(c, object, true);
    reset_mobile_entity_position(c, person.base, person.mobile, position[0], position[1]);
    if (!object.exit_cell || !c.simulation.map_cell_records) {
        throw std::logic_error("object exit has no map cell");
    }
    auto& exit = c.simulation.map_cell_records[object.exit_cell - 1];
    auto direction = get_world_cell_direction(&exit.base);
    if (object.exit_cell == object.entry_cell) {
        direction = opposite_world_cell_direction(direction);
    }
    const auto* next = resolve_world_cell_from_selector_flag_byte(c, &exit.base, direction);
    if (!next) {
        throw std::logic_error("object exit has no outward cell");
    }
    const auto id = next->base.index;
    const auto type = c.simulation.map_cell_records[id - 1].base.common.type;
    if (type == 3 || type == 9 || !try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, id)) {
        close_park_object_after_blocked_person_exit(c, object);
        return false;
    }
    record_person_park_object_visit(c, person, object);
    world_operations::enter_world_person_state(c, &person, PersonState::after_object_exit);
    return true;
}

void record_person_park_object_visit(VisitorWorld& c, PersonRuntimeView& person, ParkObjectRuntimeView& object) {
    insert_person_previous_ride_history_entry(person, object.base.header.id);
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition) {
        throw std::logic_error("visited object definition");
    }
    match (definition->which_ui_type) {
        case ObjectUiCategory::rides     => ++person.number_of_rides;
        case ObjectUiCategory::shops     => ++person.number_of_shops;
        case ObjectUiCategory::sideshows => ++person.number_of_sideshows;
        case _                           => ;
    } const auto payment = person.price_to_pay_at_end;
    if (payment) {
        record_park_object_payment_and_challenge_progress(c, object, payment);
        sprite_effect(c, person, 208);
        person.cash = fixed::subtract(person.cash, fixed::word(payment));
        person.cash_spent = fixed::add(person.cash_spent, fixed::word(payment));
    }
    increment_park_object_use_history_counters(object);
    adjust(person.needs.fatigue_effect_meter, -static_cast<double>(get_park_object_balance(c, object).UsageInfo.FatigueEffect));
    if (get_park_object_balance(c, object).Info.RideTypeStringIndex == 23) {
        person.queue_position = static_cast<std::uint8_t>(world_operations::script_slot(c, world_operations::find_script(c, object.script_id), 11));
    }
    if (!person.queue_position) {
        adjust(person.happiness, -static_cast<double>(static_cast<std::uint8_t>(c.visitors.needs_balance.PeepInfo.MediumHappinessChange)));
        person.won_last_thang = 0;
        adjust(person.won_score, -35.0);
        if (get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows) {
            thought(c, person, MobileThoughtCode::sideshow_loss);
        }
        return;
    }
    apply_park_object_use_effects(c, person, object);
    const auto satisfaction = static_cast<std::uint8_t>(3 * (int(byte(person.happiness)) - int(byte(person.happiness_at_queue_entry))) + 50);
    const auto category = get_park_object_definition(c, &object)->which_ui_type;
    if (category == ObjectUiCategory::sideshows) {
        append_park_analyser_byte_history(analyser(c), 20, satisfaction);
    } else if (category == ObjectUiCategory::shops) {
        const auto& use = get_park_object_balance(c, object).UsageInfo;
        if (use.SpecialIngredient >= 1 && use.SpecialIngredient <= 4) {
            append_park_analyser_byte_history(analyser(c), static_cast<std::size_t>(use.SpecialIngredient + 10), satisfaction);
        }
        if (use.AppearanceEffect >= 1 && use.AppearanceEffect <= 3) {
            append_park_analyser_byte_history(analyser(c), static_cast<std::size_t>(use.AppearanceEffect + 15), satisfaction);
        }
    }
    short_history(object, 4);
    if (get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows) {
        thought(c, person, MobileThoughtCode::sideshow_win);
    }
}

void handle_person_park_object_event(VisitorWorld& c, PersonRuntimeView& person, const WorldEvent& event) {
    if (event.type() != 10) {
        return;
    }
    const auto removed = event.get<ParkObjectRemovedEvent>().park_object_id;
    //Native60D120 forgets this optional follow-up destination. A booth can be
    //demolished while its prospective customer is still on the parent ride;
    //clear it before the used-thing slot is freed or reused by another object.
    if (person.photo_booth == removed) {
        person.photo_booth = 0;
    }
    if (person.major_destination == removed) {
        if (person.state == PersonState::object_interaction) {
            if (person.mobile.animation.sprite_script) {
                set_sprite_animation_next(person.mobile.animation, 1);
            } else {
                set_world_sprite_animation_resource(c, person.mobile.animation, 3, static_cast<std::int32_t>(person.mobile.sprite_category), static_cast<std::int32_t>(person.mobile.sprite_id));
            }
            person.mobile.sprite_under_ride_control = 0;
            person.mobile.no_floating_things_on_ride = 0;
            sprite_effect(c, person, 128);
        } else if (is_person_in_park_object_queue_state(&person)) {
            dismiss_person_from_park_object_queue(c, &person, false);
        }
        adjust(person.happiness, -static_cast<double>(static_cast<std::uint8_t>(c.visitors.needs_balance.PeepInfo.SmallHappinessChange)));
        person.major_destination = 0;
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
    }
    if (person.saved_major_destination == removed) {
        person.saved_major_destination = 0;
    }
    for (std::size_t i = 0; i < person.previous_rides.size(); ++i) {
        if (person.previous_rides[i] == removed) {
            person.previous_rides[i] = 0;
            person.previous_temporary_rides[i] = 0;
        }
    }
}
} //namespace simcoaster
