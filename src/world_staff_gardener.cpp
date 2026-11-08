#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

void start_world_gardener_task(StaffWorld& c, GardenerRuntimeRecord& g) {
    auto& s = g.staff;
    if (evaluate_world_staff_energy_and_activity(c, s)) {
        return;
    }
    if (!is_world_staff_too_tired_for_task(c, s)) {
        g.plant_to_water = select_staff_service_target(c, s, ParkObjectCapabilities::requires_watering);
        if (g.plant_to_water) {
            auto& target = object(c, g.plant_to_water);
            if (!target.requested_service) {
                (void)handle_park_object_maintenance_request(c, target, true);
            }
            set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
            set_sprite_animation_next(s.mobile.animation, 15);
            s.state = StaffState::walking_to_water_plant;
            return;
        }
    }
    enter_world_staff_state(c, s, try_set_mobile_entity_destination(c, s.base, s.mobile) ? StaffState::patrolling : StaffState::idle);
}

void update_world_gardener_state(StaffWorld& c, GardenerRuntimeRecord& g) {
    auto& s = g.staff;
    match (s.state) {
        case StaffState::idle       => start_task_after_idle_delay(c, s);
        case StaffState::patrolling => {
            if (!(random(c) & 15u)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, s.mobile, c.resources.staff_catalog_slot, 160);
            }
            if (navigate(c, s) == NavigationResult::moving) {
                decrease_staff_energy_and_happiness_during_task(c, s);
            } else {
                start_world_gardener_task(c, g);
            }
        }
        case StaffState::walking_to_rest_area   => handle_staff_rest_area_arrival(c, s);
        case StaffState::resting                => recover_staff_energy_and_happiness_at_rest_area(c, s);
        case StaffState::walking_to_water_plant => {
            auto& target = object(c, g.plant_to_water);
            const auto progress = navigate(c, s);
            if (progress == NavigationResult::moving) {
                thought(c, s, MobileThoughtCode::staff_going_to_job);
            } else if (progress == NavigationResult::arrived && claim_owner(c, target) == s.base.header.id) {
                g.time_started_watering = world(c).saved.game_tick;
                set_sprite_animation_next(s.mobile.animation, 25);
                s.state = StaffState::watering_plant;
            } else {
                enter_world_staff_state(c, s, StaffState::idle);
            }
        }
        case StaffState::watering_plant => {
            decrease_staff_energy_and_happiness_during_task(c, s);
            face_cell_direction(c, s);
            if (task_expired(c, s, g.time_started_watering)) {
                complete_park_object_service_task(c, object(c, g.plant_to_water));
                set_sprite_animation_next(s.mobile.animation, 3);
                enter_world_staff_state(c, s, StaffState::idle);
            }
        }
        case _ => update_world_staff_activity_state(c, s);
    }
}
} //namespace simcoaster
