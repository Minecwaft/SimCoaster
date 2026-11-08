#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

ThingId select_first_thinking_space_object_if_pathable(StaffWorld& c, StaffRuntimeRecord& staff) {
    for (auto id = world(c).saved.first_park_object; id;) {
        auto& room = object(c, id);
        if (has(room, ParkObjectCapabilities::thinking_space)) {
            if (!route(c, staff, room.entry_cell)) {
                return 0; //Do not try a later room.
            }
            staff.research_room = id;
            return id;
        }
        id = room.next_park_object;
    }
    return 0;
}

void start_world_researcher_task(StaffWorld& c, ResearcherRuntimeRecord& staff) {
    if (evaluate_world_staff_energy_and_activity(c, staff)) {
        return;
    }
    if (!is_world_staff_too_tired_for_task(c, staff)) {
        if (select_first_thinking_space_object_if_pathable(c, staff)) {
            enter_world_staff_state(c, staff, StaffState::walking_to_research_room);
            staff.state = StaffState::walking_to_research_room;
            return;
        }
        staff.happiness_score = percentage(static_cast<double>(staff.happiness_score) - 2.0);
    }
    enter_world_staff_state(c, staff, StaffState::idle);
    staff.state = StaffState::idle;
}

void update_world_researcher_state(StaffWorld& c, ResearcherRuntimeRecord& staff) {
    match (staff.state) {
        case StaffState::idle => {
            if (!(random(c) & 15u)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, staff.mobile, c.resources.staff_catalog_slot, 169);
            }
            start_task_after_idle_delay(c, staff);
        }
        case StaffState::patrolling => {
            if (!(random(c) & 15u)) {
                c.ports.resources.submit_mobile_entity_positioned_template_request(c, staff.mobile, c.resources.staff_catalog_slot, 168);
            }
            if (navigate(c, staff) == NavigationResult::moving) {
                decrease_staff_energy_and_happiness_during_task(c, staff);
            } else {
                start_world_researcher_task(c, staff);
            }
        }
        case StaffState::walking_to_rest_area => handle_staff_rest_area_arrival(c, staff);
        case StaffState::resting              => recover_staff_energy_and_happiness_at_rest_area(c, staff);
        case _                                => update_world_staff_activity_state(c, staff);
    }
}

void arrive_at_research_room(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto progress = navigate(c, staff);
    if (progress == NavigationResult::unreachable) {
        staff.happiness_score = percentage(static_cast<double>(staff.happiness_score) - balance(c).HappyHitCosRoomInaccessible);
        thought(c, staff, MobileThoughtCode::staff_confused);
        enter_world_staff_state(c, staff, StaffState::idle);
    } else if (progress == NavigationResult::arrived) {
        auto* state = script(c, object(c, staff.research_room));
        world_operations::set_script_slot(c, state, 0, fixed::add(world_operations::script_slot(c, state, 0), 1));
        enter_world_staff_state(c, staff, StaffState::researching);
        release_world_sprite_animation_resource_handle(c, staff.mobile.animation);
    }
}

void world_operations::builtin::set_staff_research_mode(WorldRuntime& c, StaffRuntimeRecord* staff, std::uint8_t mode) {
    if (!staff) {
        throw std::invalid_argument("null staff research-mode receiver");
    }
    auto& s = *staff;
    const auto previous = std::exchange(s.research_mode, mode);
    s.researching = mode;
    if (previous == mode) {
        return;
    }
    if (mode) {
        if (select_first_thinking_space_object_if_pathable(c, s)) {
            enter_world_staff_state(c, s, StaffState::walking_to_research_room);
            return;
        }
        const auto room = find_nearest_staff_rest_area(c, s);
        if (room && route(c, s, object(c, room).entry_cell)) {
            s.rest_area = room;
            enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
            return;
        }
        enter_world_staff_state(c, s, StaffState::idle);
    } else if (s.base.header.kind != ThingKind::researcher) {
        //Merely travelling to a Thinking Space does not own slot0 occupancy.
        //The common transition releases only an arrived researcher, across all
        //six staff roles, and recreates a sprite only when it was hidden.
        enter_world_staff_state(c, s, StaffState::idle);
        choose_staff_task_or_rest_area(c, s);
    }
}
} //namespace simcoaster
