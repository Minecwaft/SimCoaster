#include "world_person_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_fixed_point.hpp"

namespace simcoaster {
void update_person_state_machine(VisitorWorld& c, PersonRuntimeView& person) {
    const auto enter = [&](PersonState state) {
        world_operations::enter_world_person_state(c, &person, state);
    };
    const auto navigate = [&] {
        return update_mobile_entity_navigation(c, person.base, person.mobile);
    };
    match (person.state) {
        case PersonState::initial_route => {
            if (navigate() == NavigationResult::arrived) {
                person.mobile.sprite_angle = 1024;
                enter(PersonState::select_entrance);
            }
            return;
        }
        case PersonState::select_entrance => {
            auto& world = person_detail::base_world(c);
            if (can_person_proceed_from_arrival_vehicle(c, world)) {
                const auto count = static_cast<std::uint32_t>(c.visitors.environment.FixedItemInfo.NumTicketBooths);
                if (!count) {
                    throw std::domain_error("arrival has no ticket booths");
                }
                const auto booth = static_cast<std::uint32_t>(next_world_prng_value(world)) % count;
                const auto cell = static_cast<std::uint32_t>(get_ticket_booth_cell(c.visitors.environment, booth)) - 1u;
                const auto fraction_x = static_cast<std::uint8_t>(next_world_prng_value(world)), fraction_y = static_cast<std::uint8_t>(next_world_prng_value(world));
                const auto x = static_cast<std::uint16_t>(((cell & 127u) << 8) | fraction_x), y = static_cast<std::uint16_t>((((cell >> 7) & 255u) << 8) | fraction_y);
                (void)request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y);
                enter(PersonState::walk_to_entrance);
            }
            return;
        }
        case PersonState::walk_to_entrance => {
            update_person_walking_to_park_entrance(c, person);
            return;
        }
        case PersonState::wait_for_park_open => {
            update_person_waiting_at_park_entrance(c, person);
            return;
        }
        case PersonState::admission_response => {
            update_person_admission_fee_response(c, person);
            return;
        }
        case PersonState::cross_entrance => {
            update_person_crossing_park_entrance(c, person);
            return;
        }
        case PersonState::roaming => {
            update_person_roaming(c, person);
            return;
        }
        case PersonState::travel_to_destination => {
            const auto navigation = navigate();
            if (navigation == NavigationResult::arrived || navigation == NavigationResult::unreachable) {
                if ((static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) & 3u) || !try_set_mobile_entity_destination(c, person.base, person.mobile)) {
                    enter(PersonState::roaming);
                } else {
                    enter(PersonState::travel_to_destination);
                }
            }
            return;
        }
        case PersonState::temporary_appearance => {
            if (person_detail::base_world(c).saved.game_tick > person.time_of_last_spot_animation + 10u) {
                restore_world_person_saved_state(c, person);
            }
            return;
        }
        case PersonState::use_litter_bin => {
            handle_person_litter_bin_use_arrival(c, person);
            return;
        }
        case PersonState::litter_prank => {
            handle_person_litter_bin_prank_arrival(c, person);
            return;
        }
        case PersonState::vegetation_fire_prank => {
            handle_person_vegetation_fire_prank_arrival(c, person);
            return;
        }
        case PersonState::walk_to_info_desk => {
            set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::boost_25, person.umbrella_state);
            if (navigate() == NavigationResult::arrived) {
                enter(PersonState::after_info_desk);
                person.info_desk_timer = 4;
            }
            return;
        }
        case PersonState::travel_to_object_queue => {
            update_person_en_route_to_park_object_queue(c, person);
            return;
        }
        case PersonState::wait_in_object_queue => {
            update_person_queue_state(c, person);
            return;
        }
        case PersonState::move_to_queue_position => {
            const auto navigation = navigate();
            if (navigation == NavigationResult::arrived || navigation == NavigationResult::unreachable) {
                enter(PersonState::wait_in_object_queue);
            }
            return;
        }
        case PersonState::admission_step => {
            update_person_waiting_for_park_object_admission(c, person);
            return;
        }
        case PersonState::wait_for_object_script => {
            auto& object = person_detail::park_object(c, person.major_destination);
            if (is_admission_slot_mismatched_with_queue_head(c, object)) {
                (void)remove_person_from_park_object_queue(c, &object, person.base.header.id);
                enter(PersonState::object_interaction);
            }
            return;
        }
        case PersonState::after_info_desk => {
            update_person_after_info_desk_wait(c, person);
            return;
        }
        case PersonState::after_object_exit => {
            update_person_after_park_object_exit(c, person);
            return;
        }
        case PersonState::object_interaction => {
            (void)c.entities.at(person.major_destination).payload;
            return;
        }
        case PersonState::caught_by_guard => {
            auto id = person_detail::base_world(c).saved.first_guard;
            std::size_t visited = 0;
            while (id) {
                if (visited++ >= thing_slot_count) {
                    throw std::length_error("cyclic guard roster");
                }
                const auto& guard = *reinterpret_cast<GuardRuntimeRecord*>(&person_detail::map_object(c, id));
                if (guard.perpetrator == person.base.header.id && guard.staff.state == StaffState::walking_to_entrance) {
                    return;
                }
                id = guard.staff.next_staff;
            }
            (void)retire_world_used_thing(c, &person.base.header);
            return;
        }
        case PersonState::walk_to_exit => {
            update_person_walking_to_park_exit(c, person);
            return;
        }
        case PersonState::return_to_roaming => {
            enter(PersonState::roaming);
            return;
        }
        case PersonState::whinge_to_staff => {
            update_person_whinge_to_staff(c, person);
            return;
        }
        case PersonState::spread_vicious_rumours => {
            update_person_vicious_rumours(c, person);
            return;
        }
        case PersonState::listen_to_vicious_rumours => {
            --person.mobile.whinge_timeout;
            if (std::bit_cast<std::int32_t>(person.mobile.whinge_timeout) <= 0) {
                person.state = person.saved_state;
                if (person.saved_state == PersonState::listen_to_vicious_rumours) {
                    enter(PersonState::roaming);
                }
            } else {
                person.happiness = person_detail::meter(static_cast<double>(person.happiness) - 1.0);
            }
            return;
        }
        case PersonState::first_aid_recovery => {
            update_person_first_aid_recovery_timer(c, person);
            return;
        }
        case PersonState::departure_routing => {
            update_person_departure_routing(c, person);
            return;
            //Original dispatcher has no cases for the two road-crossing display states.
        }
        case _ => return;
    }
}
} //namespace simcoaster
