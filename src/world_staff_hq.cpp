#include "simcoaster/world_grid.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
bool eligible_group(StaffWorld& c, const SpecialRideRegistryGroup& group) {
    return group.thing_id && !(primary(c).map_cells.at(cell(object(c, group.thing_id).base) - 1).base.common.flags & 64u);
}

ThingId unassigned(StaffWorld& c, StaffType role, ThemeAbility ability, const StaffHqRecord& hq, ThingId excluded = 0) {
    for (auto id = head(world(c), role); id;) {
        const auto& s = record<StaffRuntimeRecord>(c, id);
        if (id != excluded && !s.base.header.retirement_pending && world_staff_association_state(s) != StaffState::training && s.first_known_ability == ability && std::ranges::none_of(hq.staff_slots, [&](const auto& row) {
                return row.assigned_staff == id;
            })) {
            return id;
        }
        id = s.next_staff;
    }
    return 0;
}

void clear_slot(StaffWorld& c, StaffHqRecord& hq, ThingId id) {
    auto& s = record<StaffRuntimeRecord>(c, id);
    //Registration accepts abilities from every staff role. Sever actual ID
    //backlinks rather than clearing a potentially superseded slot by index.
    for (auto& row : hq.staff_slots) {
        if (row.assigned_staff == id) {
            row.assigned_staff = 0;
        }
    }
    s.slot_location = -1;
}
} //namespace

bool is_special_ride_group_ready_for_advisor_action(StaffWorld& c, ThingId id) {
    if (c.attractions.force_special_ride_group_ready) {
        return true;
    }
    for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i) {
        const auto& group = c.attractions.special_ride_groups.at(i);
        if (group.thing_id != id) {
            continue;
        }
        const auto cost = get_park_object_balance(c, object(c, id)).UsageInfo.GoldenTicketCost;
        if (get_or_create_theme_global_info_manager(c).golden_ticket_balance < cost) {
            return false;
        }
        for (const auto& member : group.members) {
            if (member.type == SpecialRideRequirementType::end) {
                break;
            }
            if (!member.assigned_staff) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool assign_thing_to_matching_special_ride_requirement(StaffWorld& c, ThingId id) {
    for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i) {
        auto& group = c.attractions.special_ride_groups.at(i);
        if (!eligible_group(c, group)) {
            continue;
        }
        for (auto& member : group.members) {
            if (member.assigned_staff) {
                continue;
            }
            auto& s = record<StaffRuntimeRecord>(c, id);
            if (std::to_underlying(member.type) != static_cast<std::uint32_t>(world_staff_type_for_used_thing(s.base.header)) || s.first_known_ability != member.ability) {
                continue;
            }
            member.assigned_staff = id;
            if (is_special_ride_group_ready_for_advisor_action(c, group.thing_id)) {
                queue_action(c, get_park_object_balance(c, object(c, group.thing_id)).Info.Id == 1160 ? 543 : 487);
            }
            return true;
        }
    }
    return false;
}

bool clear_thing_id_from_eligible_special_ride_group(StaffWorld& c, ThingId id) {
    for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i) {
        auto& group = c.attractions.special_ride_groups.at(i);
        if (!eligible_group(c, group)) {
            continue;
        }
        for (auto& member : group.members) {
            if (member.assigned_staff == id) {
                member.assigned_staff = 0;
                return true;
            }
        }
    }
    return false;
}

bool register_world_staff_in_staff_hq_ability_slot(StaffWorld& c, StaffHqRecord& hq, ThingId id) {
    auto& s = record<StaffRuntimeRecord>(c, id);
    if (s.slot_location != -1) {
        return false;
    }
    for (std::size_t i = 0; i < hq.staff_slots.size(); ++i) {
        auto& row = hq.staff_slots[i];
        if (row.assigned_staff || row.required_ability != std::to_underlying(s.first_known_ability)) {
            continue;
        }
        //The native helper computes the role and discards it; only ability
        //matches.
        (void)world_staff_type_for_used_thing(s.base.header);
        row.assigned_staff = id;
        s.slot_location = static_cast<std::int32_t>(i);
        return true;
    }
    return false;
}

void remove_world_staff_from_hq_and_refill_ability_slot(StaffWorld& c, StaffHqRecord& hq, ThingId id, bool retiring) {
    auto& s = record<StaffRuntimeRecord>(c, id);
    const auto ability = s.first_known_ability;
    //Arrival42 already removed a trainee from the available-ability count.
    //Native retirement33 decremented it a second time while state8/37 survived.
    if (!retiring || world_staff_association_state(s) != StaffState::training) {
        --hq.trained_staff.at(static_cast<std::size_t>(ability));
    }
    if (retiring) {
        //Native eligibility gates apply to filling requirements, not to an ID's
        //lifetime: locked/flag64 groups cannot retain a soon-reusable actor ID.
        for (std::uint32_t i = 0; i < c.attractions.special_ride_group_count; ++i) {
            for (auto& member : c.attractions.special_ride_groups.at(i).members) {
                if (member.assigned_staff == id) {
                    member.assigned_staff = 0;
                }
            }
        }
    } else {
        if (ability == ThemeAbility::none) {
            return;
        }
        (void)clear_thing_id_from_eligible_special_ride_group(c, id);
    }
    const bool assigned = std::ranges::any_of(hq.staff_slots, [&](const auto& row) {
        return row.assigned_staff == id;
    });
    if (!assigned) {
        if (retiring) {
            s.slot_location = -1;
        }
        return;
    }
    ThingId replacement = 0;
    const auto role = world_staff_type_for_used_thing(s.base.header);
    if (ability != ThemeAbility::none && (role == StaffType::gardener || role == StaffType::handyman || role == StaffType::mechanic)) {
        replacement = unassigned(c, role, ability, hq, id);
    }
    clear_slot(c, hq, id);
    if (replacement) {
        (void)register_world_staff_in_staff_hq_ability_slot(c, hq, replacement);
    }
}

namespace staff_detail {
void restore_cancelled_training_availability(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto id = world(c).saved.staff_hq;
    if (!id || id >= c.entities.size()) {
        return;
    }
    auto* value = c.entities[id].payload;
    if (!value || value->kind != ThingKind::staff_hq || value->retirement_pending) {
        return;
    }
    auto& control = *reinterpret_cast<StaffHqRecord*>(value);
    //Cancellation retains the old known ability. Balance the earlier event42
    //without pretending a new ability completed or emitting event43/mail.
    ++control.trained_staff.at(static_cast<std::size_t>(staff.first_known_ability));
    if (staff.first_known_ability != ThemeAbility::none) {
        (void)register_world_staff_in_staff_hq_ability_slot(c, control, staff.base.header.id);
        (void)assign_thing_to_matching_special_ride_requirement(c, staff.base.header.id);
    }
}
} //namespace staff_detail

void rebuild_world_staff_hq_ability_slots(StaffWorld& c, StaffHqRecord& hq) {
    for (auto& row : hq.staff_slots) {
        if (row.assigned_staff) {
            clear_slot(c, hq, row.assigned_staff);
        }
    }
    initialize_world_staff_hq_ability_slots(c, &hq);
    if (!is_obstacle_mission_research_enabled(c)) {
        return;
    }
    ThingId candidate = 0;
    for (const auto& requirement : get_or_create_theme_global_info_manager(c).mission_research.obstacle_staff) {
        for (std::int32_t i = 0; i < requirement.number; ++i) {
            const auto role = requirement.staff_type;
            if (role == StaffType::handyman || role == StaffType::mechanic || role == StaffType::gardener) {
                candidate = unassigned(c, role, static_cast<ThemeAbility>(requirement.ability), hq);
            }
            if (candidate) {
                (void)register_world_staff_in_staff_hq_ability_slot(c, hq, candidate);
            }
        }
    }
}

void handle_world_staff_control_state_event(StaffWorld& c, StaffHqRecord& hq, const WorldEvent& event) {
    match (event.type()) {
        case 12 => update_world_staff_strike_states_for_game_tick(c, hq);
        case 14 => {
            const auto id = event.get<PersonActionEvent>().person_id;
            if (!id || id >= c.entities.size() || !c.entities[id].payload) {
                return;
            }
            const auto& person = record<PersonRuntimeView>(c, id);
            if (c.simulation.region_effects.cells[cell(person.base) - 1].security <= 0) {
                return;
            }
            ThingId selected = 0;
            auto best = std::numeric_limits<std::uint32_t>::max();
            //Native selection measures from StaffHQ's position. The target-cell
            //argument passed by this caller is unused by the selection helper.
            for (auto guard_id = world(c).saved.first_guard; guard_id;) {
                const auto& guard = record<GuardRuntimeRecord>(c, guard_id);
                const auto squared = distance(hq.base, guard.staff.base);
                if (static_cast<std::uint32_t>(guard.staff.state) < 2u && squared < best) {
                    best = squared;
                    selected = guard_id;
                }
                guard_id = guard.staff.next_staff;
            }
            if (selected) {
                (void)start_guard_prosecution(c, record<GuardRuntimeRecord>(c, selected), id);
            }
        }
        case 33 => {
            const auto id = event.get<RetirementEvent>().retired_id;
            if (is_staff_record_kind(c.entities.at(id).payload->kind)) {
                remove_world_staff_from_hq_and_refill_ability_slot(c, hq, id, true);
            }
        }
        case 35 or 36 => rebuild_world_staff_hq_ability_slots(c, hq);
        case 42       => remove_world_staff_from_hq_and_refill_ability_slot(c, hq, event.get<StaffTrainingStartedEvent>().staff);
        case 43       => {
            const auto id = event.get<StaffTrainingCompletedEvent>().staff;
            ++hq.trained_staff.at(static_cast<std::size_t>(record<StaffRuntimeRecord>(c, id).first_known_ability));
            (void)register_world_staff_in_staff_hq_ability_slot(c, hq, id);
            (void)assign_thing_to_matching_special_ride_requirement(c, id);
        }
        case _ => ;
    }
}
} //namespace simcoaster
