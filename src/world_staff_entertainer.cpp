#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

void update_entertainer_idle_and_work_state(StaffWorld& c, EntertainerRuntimeRecord& e) {
    auto& s = e.staff;
    if (evaluate_world_staff_energy_and_activity(c, s)) {
        return;
    }
    if (!is_world_staff_too_tired_for_task(c, s)) {
        const auto& b = c.staff.recruitment.balance.Entertainer;
        const auto radius = interpolate(c, s, b.MaxActiveRange, b.MaxActiveRange); //Both endpoints are Max in the executable.
        if (random(c) % 3u == 0 && query_world_used_thing_type_in_square_radius(c, 1, cell(s.base), radius, true)) {
            set_world_staff_state_by_type(c, s, StaffState::entertaining);
            return;
        }
    }
    if (s.state == StaffState::entertaining) {
        //GetCurrentTheme's historical mapping uses the resource-source list index,
        //distinct from the ThemeGlobalInfo source ordinal used for the animation.
        const auto source = c.ports.assets.selected_resource_source(c);
        const auto theme = source == 0 ? 4 : source == 2 ? 2 : source == 3 ? 3 : 1;
        if (theme == 1 && s.mobile.sprite_id == 1) {
            (void)create_runtime_template_at_mobile_entity_resource_position(c, s.mobile, 1, 12, 0);
        } else if (theme == 2 && s.mobile.sprite_id == 0) {
            (void)create_runtime_template_at_mobile_entity_resource_position(c, s.mobile, 37, 8, 20);
        }
    }
    if (world(c).saved.game_tick & 3u) {
        if (try_set_mobile_entity_destination(c, s.base, s.mobile)) {
            set_world_staff_state_by_type(c, s, StaffState::patrolling);
        }
    } else {
        set_world_staff_state_by_type(c, s, StaffState::idle);
    }
}

void update_world_entertainer_state(StaffWorld& c, EntertainerRuntimeRecord& e) {
    auto& s = e.staff;
    match (s.state) {
        case StaffState::idle => {
            if (!(random(c) & 15u)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, s.mobile, c.resources.staff_catalog_slot, 165);
            }
            start_task_after_idle_delay(c, s);
        }
        case StaffState::patrolling => {
            if (!(random(c) & 15u)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, s.mobile, c.resources.staff_catalog_slot, 164);
            }
            if (navigate(c, s) == NavigationResult::moving) {
                decrease_staff_energy_and_happiness_during_task(c, s);
            } else {
                update_entertainer_idle_and_work_state(c, e);
            }
        }
        case StaffState::walking_to_rest_area => handle_staff_rest_area_arrival(c, s);
        case StaffState::resting              => recover_staff_energy_and_happiness_at_rest_area(c, s);
        case StaffState::entertaining         => {
            decrease_staff_energy_and_happiness_during_task(c, s);
            if (task_expired(c, s, e.time_started_entertaining)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, s.mobile, c.resources.staff_catalog_slot, 135);
                update_entertainer_idle_and_work_state(c, e);
            }
        }
        case _ => update_world_staff_activity_state(c, s);
    }
}
} //namespace simcoaster
