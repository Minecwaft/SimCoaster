#include "world_person_internal.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_objects.hpp"

namespace simcoaster {
void update_person_need_and_mood_thought_bubble(VisitorWorld& c, PersonRuntimeView& person) {
    const auto hunger = truncate_park_object_condition(person.needs.hunger);
    MobileThoughtCode thought;
    const auto& balance = c.visitors.needs_balance.PeepInfo;
    if (hunger > 90 && truncate_park_object_condition(person.needs.thirst) > 90) {
        thought = MobileThoughtCode::hungry_and_thirsty;
    } else if (balance.ThoughtBubbleNeedTheLoo < truncate_park_object_condition(person.needs.toilet_need)) {
        thought = MobileThoughtCode::need_toilet;
    } else if (balance.ThoughtBubbleHungry < hunger) {
        thought = MobileThoughtCode::hungry;
    } else if (balance.ThoughtBubbleThirsty < truncate_park_object_condition(person.needs.thirst)) {
        thought = MobileThoughtCode::thirsty;
    } else if (balance.ThoughtBubbleSick < truncate_park_object_condition(person.needs.sickness)) {
        thought = MobileThoughtCode::sick;
    } else {
        const auto id = person_detail::base_world(c).saved.park_analyser;
        auto* payload = c.entities.at(id).payload;
        if (!payload) {
            throw std::logic_error("park analyser is required");
        }
        auto& grid = get_park_analyser_grid_record(*reinterpret_cast<ParkAnalyserState*>(payload), person_detail::cell_id(person.base));
        const auto litter = static_cast<float>(count_cached_analyser_litter_cells(c, grid, 4));
        const auto walkable = count_cached_analyser_walkable_cells(c, grid, 4);
        const auto ratio = static_cast<double>(litter) / walkable;
        //Unordered (including0/0) falls through to the happiness checks.
        if (ratio > balance.ThoughtBubbleMess) {
            thought = MobileThoughtCode::park_mess;
        } else {
            const auto happiness = truncate_park_object_condition(person.happiness);
            if (happiness > balance.ThoughtBubbleHappy) {
                thought = MobileThoughtCode::happy;
            } else if (happiness < balance.ThoughtBubbleUnhappy) {
                thought = MobileThoughtCode::unhappy;
            } else {
                return;
            }
        }
    }
    if (!(person.base.header.id & 0xF) && thought == MobileThoughtCode::need_toilet) {
        c.ports.resources.submit_mobile_entity_positioned_template_request(c, &person, c.resources.kids_catalog_slot, 133);
    }
    world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, thought, false);
}

void update_world_person_record(VisitorWorld& c, PersonRuntimeView& person) {
    update_mobile_entity_speed_and_cached_position(c, person.mobile);
    if ((person_detail::base_world(c).saved.game_tick & 3) == (person.base.header.id & 3)) {
        update_world_person_weather_effects(c, person);
        person.exit_level = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(person.exit_level) - 1u);
        const auto id = person_detail::cell_id(person.base);
        if (!c.simulation.region_effects.cells || !id || id > map_cell_count) {
            throw std::out_of_range("unbound or invalid person effect cell");
        }
        const auto& effect = c.simulation.region_effects.cells[id - 1];
        person.happiness = person_detail::meter(static_cast<double>(effect.happiness) + person.happiness);
        person.needs.sickness = person_detail::meter(static_cast<double>(effect.illness) + person.needs.sickness);
        person.needs.hunger = person_detail::meter(static_cast<double>(effect.hunger) + person.needs.hunger);
        //Approved correction to Game.exe 0x619CA0: its tick-only test sits
        //inside the ID-staggered four-tick branch, starving three ID classes.
        //Keep the sixteen-tick interval, staggered across all visitor IDs.
        //Approved balance adjustment: quarter the native increments to retain
        //its average base growth while extending it to the other ID classes.
        if (!((person_detail::base_world(c).saved.game_tick - person.base.header.id) & 0xF)) {
            person.needs.toilet_need = person_detail::meter(static_cast<double>(person.needs.toilet_need) + 0.25);
            person.needs.hunger = person_detail::meter(static_cast<double>(person.needs.hunger) + 0.5);
            person.needs.thirst = person_detail::meter(static_cast<double>(person.needs.thirst) + 0.5);
        }
        for (auto value : { person.needs.sickness, person.needs.hunger, person.needs.thirst }) {
            if (truncate_park_object_condition(value) == 100) {
                person.happiness = person_detail::meter(static_cast<double>(person.happiness) - 1.0);
            }
        }
        const auto toilet = truncate_park_object_condition(person.needs.toilet_need);
        if (toilet <= 80) {
            set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::normal, person.umbrella_state);
        } else {
            person.happiness = person_detail::meter(static_cast<double>(person.happiness) - 1.0);
            if (toilet == 100) {
                person.happiness = person_detail::meter(static_cast<double>(person.happiness) - 1.0);
            } else if (toilet == 95) {
                world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, MobileThoughtCode::need_toilet, false);
            }
            set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::boost_25, person.umbrella_state);
        }
        if (person.state != PersonState::object_interaction && person.state != PersonState::caught_by_guard && record_passes_grid_predicates(c, &person.base)) {
            if (!(next_world_prng_value(person_detail::base_world(c)) % 10)) {
                update_person_need_and_mood_thought_bubble(c, person);
            }
            if (person.remaining_balloon_life && !--person.remaining_balloon_life) {
                discard_person_balloon(c, person, false);
            }
        }
        release_expired_mobile_entity_thought_script(c, person.mobile.thought_history);
    }
    const auto state = person.state == PersonState::temporary_appearance ? person.saved_state : person.state;
    if (state < PersonState::wait_in_object_queue || state > PersonState::wait_for_object_script) {
        person.queue_next = person.queue_previous = 0;
    }
    age_person_temporary_ride_history(c, person);
}
} //namespace simcoaster
