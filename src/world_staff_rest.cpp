#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace staff_detail {
void detach_staff_room_occupancy(StaffWorld& c, StaffRuntimeRecord& staff, ThingId removed_training_room) {
    if (staff.room_occupancy_detaching)
        return;
    staff.room_occupancy_detaching = true;

    struct Reset {
        bool& flag;

        ~Reset() {
            flag = false;
        }
    } reset{ staff.room_occupancy_detaching };

    const auto association = world_staff_association_state(staff);
    const auto decrement = [&](ThingId id, ParkObjectCapabilities capability) {
        //A whole-World teardown may already have destroyed this room. Never
        //reinterpret a retired payload or debit an unrelated reused object ID.
        if (auto* room = live_room(c, id, capability)) {
            auto* state = script(c, *room);
            world_operations::set_script_slot(c,
                                              state,
                                              0,
                                              fixed::subtract(world_operations::script_slot(c, state, 0), 1));
        }
    };
    if (association == StaffState::resting) {
        const auto room = staff.rest_area;
        if (room) {
            //Native exit observers see both the room link and original count.
            notify(c, WorldEvent{ StaffRestAreaEvent{ staff.base.header.id, 0 } });
            decrement(room, ParkObjectCapabilities::chills_you_out);
            staff.rest_area = 0;
        }
    } else if (association == StaffState::researching) {
        decrement(staff.research_room, ParkObjectCapabilities::thinking_space);
        staff.research_room = 0;
    } else if (association == StaffState::training) {
        //Training rooms have UsageInfo.SingleInstance=1 in the shipped SAM.
        //During event10 the dying room is already absent from the object list.
        decrement(removed_training_room ? removed_training_room : first_training_room(c),
                  ParkObjectCapabilities::training_room);
        staff.currently_learning = ThemeAbility::none;
        staff.going_to_learn = ThemeAbility::none;
    }
}
} //namespace staff_detail

ThingId find_nearest_staff_rest_area(StaffWorld& c, const StaffRuntimeRecord& staff) {
    ThingId best = 0;
    auto best_distance = std::numeric_limits<std::uint32_t>::max();
    for (auto id = world(c).saved.first_park_object; id;) {
        auto& room = object(c, id);
        if (has(room, ParkObjectCapabilities::chills_you_out) && room.can_load) {
            const auto squared = distance(staff.base, room.base);
            if (squared < best_distance) {
                best_distance = squared;
                best = id;
            }
        }
        id = room.next_park_object;
    }
    return best;
}

bool try_send_staff_to_nearest_rest_area(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = balance(c);
    if (byte(staff.energy) >= b.StaffSlackingEnergy)
        return false;
    const auto id = find_nearest_staff_rest_area(c, staff);
    if (!id)
        return false;
    auto& room = object(c, id);
    if (distance(staff.base, room.base) > static_cast<std::uint32_t>(b.MinDistanceForSlacking))
        return false;
    if (!route(c, staff, room.entry_cell))
        return false;
    staff.rest_area = id;
    return true;
}

void handle_staff_rest_area_arrival(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto progress = navigate(c, staff);
    if (progress == NavigationResult::unreachable) {
        staff.happiness_score =
            percentage(static_cast<double>(staff.happiness_score) - balance(c).HappyHitCosRoomInaccessible);
        thought(c, staff, MobileThoughtCode::staff_confused);
        enter_world_staff_state(c, staff, StaffState::idle);
    } else if (progress == NavigationResult::arrived) {
        auto& room = object(c, staff.rest_area);
        auto* state = script(c, room);
        const auto count = world_operations::script_slot(c, state, 0);
        if (count >= balance(c).StaffRoomCapacity) {
            (void)route(c, staff, room.entry_cell);
            staff.mobile.navigation.navigation_mode = 0;
            enter_world_staff_state(c, staff, StaffState::waiting_for_empty_staff_room);
        } else {
            world_operations::set_script_slot(c, state, 0, fixed::add(count, 1));
            increment_park_object_use_history_counters(room);
            enter_world_staff_state(c, staff, StaffState::resting);
            notify(c, WorldEvent{ StaffRestAreaEvent{ staff.base.header.id, 1 } });
        }
    }
}

void leave_staff_rest_area(StaffWorld& c, StaffRuntimeRecord& staff) {
    enter_world_staff_state(c, staff, StaffState::idle);
}

void recover_staff_energy_and_happiness_at_rest_area(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = balance(c);
    const auto& agency = c.staff.recruitment.balance.agencies.at(staff.agency);
    const double fraction = static_cast<double>(staff.experience) / b.MaxExperience;
    double recovery = b.MinRecuperationRate + fraction;
    float energy_delta = static_cast<float>(recovery);
    if (agency.RecuperationModifier && recovery != 0.0) {
        recovery += agency.RecuperationModifier * recovery * static_cast<double>(0.01f);
        energy_delta = static_cast<float>(recovery);
    }
    if (recovery > b.MaxRecuperationRate)
        energy_delta = b.MaxRecuperationRate;
    double happiness_delta = fraction * 10.0 * b.MinHappinessRecuperationRate;
    if (agency.HappinessRecuperationModifier && happiness_delta != 0.0)
        happiness_delta += agency.HappinessRecuperationModifier * happiness_delta * static_cast<double>(0.01f);
    if (happiness_delta > b.MaxHappinessRecuperationRate)
        happiness_delta = b.MaxHappinessRecuperationRate;
    staff.energy = percentage(static_cast<double>(staff.energy) + energy_delta);
    staff.happiness_score = percentage(staff.happiness_score + happiness_delta);
    staff.motivation = percentage(staff.motivation + happiness_delta);
    if (byte(staff.energy) == 100) {
        leave_staff_rest_area(c, staff);
        choose_staff_task_or_rest_area(c, staff);
    }
}

void seek_staff_rest_area(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto id = find_nearest_staff_rest_area(c, staff);
    staff.rest_area = id;
    if (!id) {
        enter_world_staff_state(c, staff, StaffState::idle);
        return;
    }
    auto* state = script(c, object(c, id));
    if (world_operations::script_slot(c, state, 0) >= balance(c).StaffRoomCapacity) {
        if (navigate(c, staff) != NavigationResult::moving) {
            const auto x = static_cast<std::uint16_t>((staff.base.x & 65280u) | static_cast<std::uint8_t>(random(c)));
            const auto y = static_cast<std::uint16_t>((staff.base.y & 65280u) | static_cast<std::uint8_t>(random(c)));
            (void)route(c, staff, x, y);
        }
    } else {
        const auto retry = find_nearest_staff_rest_area(c, staff);
        if (retry && route(c, staff, object(c, retry).entry_cell)) {
            staff.rest_area = retry;
            enter_world_staff_state(c, staff, StaffState::walking_to_rest_area);
        }
    }
    //The native penalty also runs after a successful retry.
    if (!(random(c) & 15u))
        staff.happiness_score =
            percentage(static_cast<double>(staff.happiness_score) - balance(c).HappyHitCosNoRestArea);
}
} //namespace simcoaster
