#include "world_staff_internal.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_mail.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
bool below_threshold(StaffWorld& c, const ParkObjectRuntimeView& o) {
    return byte(o.repair_state) < get_park_object_balance(c, o).RideBreakDown.Imminent;
}

bool in_bounds(const StaffAreaBounds& b, CellId id) {
    const auto n = static_cast<std::uint32_t>(id) - 1u;
    const auto x = static_cast<int>(n & 127u), y = static_cast<int>(n >> 7);
    return x >= std::min(b.start_x, b.end_x)
        && x <= std::max(b.start_x, b.end_x)
        && y >= std::min(b.start_y, b.end_y)
        && y <= std::max(b.start_y, b.end_y);
}
} //namespace

bool is_park_object_in_staff_work_area(StaffWorld& c, const StaffRuntimeRecord& s, ParkObjectRuntimeView& o) {
    const auto& b = s.area.bounds;
    if ((static_cast<std::uint16_t>(b.start_x) + (static_cast<std::uint16_t>(b.start_y) << 7)) == 0
        && static_cast<std::uint16_t>(b.end_x) + (static_cast<std::uint16_t>(b.end_y) << 7) == 16383)
        return true;
    return staff_area_accepts_park_object(c, b, o);
}

bool staff_area_accepts_park_object(StaffWorld& c, const StaffAreaBounds& b, ParkObjectRuntimeView& o) {
    CellId target;
    if (has(o, ParkObjectCapabilities::has_queue))
        target = get_back_of_queue(c, &o);
    else if (has(o, ParkObjectCapabilities::requires_watering)) {
        //Native iteration uses the original bounds, while membership normalizes them.
        for (auto x = static_cast<std::int64_t>(b.start_x); x <= b.end_x; ++x)
            for (auto y = static_cast<std::int64_t>(b.start_y); y <= b.end_y; ++y) {
                const auto id = find_nearest_path_cell_around_object(c, o, static_cast<CellId>(x + y * 128 + 1));
                if (id && primary(c).map_cells.at(id - 1).base.common.type == 1 && in_bounds(b, id))
                    return true;
            }
        return false;
    } else
        target = o.entry_cell;
    if (!target)
        return false;
    if (in_bounds(b, target))
        return true;
    auto& source = primary(c).map_cells.at(target - 1).base;
    const auto corner = get_world_cell_corner_neighbor_mask(c, &source), direction = get_world_cell_direction(&source);
    const auto next =
        corner ? static_cast<std::uint8_t>(~direction & corner) : opposite_world_cell_direction(direction);
    const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &source, next);
    return neighbor && neighbor->base.common.type == 1 && neighbor->base.index && in_bounds(b, neighbor->base.index);
}

ThingId select_mechanic_work_target_candidate(StaffWorld& c, MechanicHqRecord& hq, const StaffRuntimeRecord& staff) {
    const auto head = world(c).saved.first_park_object;
    if (hq.next_object) {
        auto cursor = head;
        while (cursor && cursor != hq.next_object)
            cursor = object(c, cursor).next_park_object;
        hq.next_object = cursor ? object(c, cursor).next_park_object : head;
    } else
        hq.next_object = head;
    const auto start = hq.next_object;
    if (!start)
        return 0;
    ThingId urgent = 0, ordinary = 0;
    auto urgent_distance = std::numeric_limits<std::uint32_t>::max(), ordinary_distance = urgent_distance;
    do {
        auto& candidate = object(c, hq.next_object);
        hq.next_object = candidate.next_park_object ? candidate.next_park_object : head;
        const auto ability = staff.first_known_ability == ThemeAbility::ride_upgrading;
        const auto service = candidate.requested_service != 0, broken = candidate.state == ParkObjectState::broken_down;
        const auto eligible = broken
                           || below_threshold(c, candidate)
                           || (service
                               && !has(candidate, ParkObjectCapabilities::provides_relief)
                               && !has(candidate, ParkObjectCapabilities::holds_litter)
                               && !has(candidate, ParkObjectCapabilities::requires_watering))
                           || (candidate.state == ParkObjectState::upgrade_requested
                               && ability
                               && (!claim_owner(c, candidate) || claim_owner(c, candidate) == staff.base.header.id));
        if (eligible && !claim_owner(c, candidate)) {
            const auto squared = distance(staff.base, candidate.base);
            if (broken || service) {
                if (squared < urgent_distance) {
                    urgent_distance = squared;
                    urgent = candidate.base.header.id;
                }
            } else if (squared < ordinary_distance) {
                ordinary_distance = squared;
                ordinary = candidate.base.header.id;
            }
        }
    } while (hq.next_object != start);
    return urgent ? urgent : ordinary;
}

void set_world_mechanic_state(StaffWorld& c, MechanicRuntimeRecord& m, StaffState requested) {
    auto& s = m.staff;
    if (requested == StaffState::walking_to_breakdown) {
        set_sprite_animation_next(s.mobile.animation, 15);
        s.state = requested;
    } else if (requested == StaffState::repairing) {
        set_sprite_animation_next(s.mobile.animation, 26);
        auto& target = object(c, m.object_to_repair);
        const auto work = static_cast<std::uint32_t>(calculate_staff_work_value(c, &s));
        if (target.state == ParkObjectState::upgrade_requested)
            m.repair_duration =
                work
                * static_cast<std::uint32_t>(
                    get_park_object_balance(c, target).Upgrades.at(target.current_upgrade + 1u).DurationOfUpgrade);
        else
            m.repair_duration = work * (100u - byte(target.repair_state)) / 100u;
        s.state = requested;
    } else
        enter_world_staff_state(c, s, requested);
}

void choose_mechanic_repair_task(StaffWorld& c, MechanicRuntimeRecord& m) {
    auto& s = m.staff;
    if (evaluate_world_staff_energy_and_activity(c, s))
        return;
    if (!is_world_staff_too_tired_for_task(c, s)) {
        m.object_to_repair =
            select_mechanic_work_target_candidate(c, record<MechanicHqRecord>(c, world(c).saved.mechanic_hq), s);
        if (m.object_to_repair) {
            auto& target = object(c, m.object_to_repair);
            if (is_park_object_in_staff_work_area(c, s, target) && route(c, s, target.entry_cell)) {
                target.assigned_staff_member = s.base.header.id;
                target.time_marked_for_maintenance = world(c).saved.game_tick;
                if (!target.requested_service && target.state != ParkObjectState::upgrade_requested) {
                    auto candidate = world_operations::make_advisor_action_candidate(c, 67);
                    candidate.thing_id = m.object_to_repair;
                    candidate.secondary = target.definition_id;
                    (void)world_operations::queue_advisor_action_candidate(
                        c,
                        &record<AdvisorRecord>(c, world(c).saved.advisor),
                        candidate);
                }
                if (target.state != ParkObjectState::broken_down
                    && !target.requested_service
                    && below_threshold(c, target))
                    (void)handle_park_object_maintenance_request(c, target, true);
                set_world_mechanic_state(c, m, StaffState::walking_to_breakdown);
                return;
            }
        }
    }
    set_world_mechanic_state(c,
                             m,
                             try_set_mobile_entity_destination(c, s.base, s.mobile) ? StaffState::patrolling
                                                                                    : StaffState::idle);
}

namespace staff_detail {
void arrive_repair(StaffWorld& c, MechanicRuntimeRecord& m) {
    auto& s = m.staff;
    auto& target = object(c, m.object_to_repair);
    if (claim_owner(c, target) != s.base.header.id) {
        m.object_to_repair = 0;
        choose_mechanic_repair_task(c, m);
    } else {
        const auto progress = navigate(c, s);
        if (progress == NavigationResult::arrived) {
            if (target.state == ParkObjectState::broken_down
                || target.state == ParkObjectState::upgrade_requested
                || target.requested_service) {
                set_world_mechanic_state(c, m, StaffState::repairing);
                const std::array<std::int32_t, 4> keys{ 6, 7, 8, 9 };
                (void)mark_tag_messages_by_key_set_and_format_word_for_deletion(
                    c,
                    &record<TagSystemRecord>(c, world(c).saved.tag_system),
                    keys,
                    m.object_to_repair);
            } else
                choose_mechanic_repair_task(c, m);
        } else if (progress == NavigationResult::unreachable) {
            m.object_to_repair = 0;
            choose_mechanic_repair_task(c, m);
        }
    }
    thought(c, s, MobileThoughtCode::staff_going_to_job);
}

void repair_tick(StaffWorld& c, MechanicRuntimeRecord& m) {
    decrease_staff_energy_and_happiness_during_task(c, m.staff);
    auto& target = object(c, m.object_to_repair);
    if (m.repair_duration) {
        --m.repair_duration;
        return;
    }
    if (target.state == ParkObjectState::broken_down) {
        auto* state = script(c, target);
        world_operations::set_script_slot(c, state, 4, 0);
        if (world_operations::script_slot(c, state, 7) > 0)
            return;
    }
    complete_park_object_mechanic_task(c, target, m.staff.base.header.id);
    m.object_to_repair = 0;
    choose_mechanic_repair_task(c, m);
}
} //namespace staff_detail

void update_world_mechanic_state(StaffWorld& c, MechanicRuntimeRecord& m) {
    auto& s = m.staff;
    match (s.state) {
        case StaffState::idle => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   163);
            start_task_after_idle_delay(c, s);
        }
        case StaffState::patrolling => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   162);
            if (navigate(c, s) == NavigationResult::moving)
                decrease_staff_energy_and_happiness_during_task(c, s);
            else
                choose_mechanic_repair_task(c, m);
        }
        case StaffState::walking_to_rest_area => handle_staff_rest_area_arrival(c, s);
        case StaffState::resting              => recover_staff_energy_and_happiness_at_rest_area(c, s);
        case StaffState::walking_to_breakdown => arrive_repair(c, m);
        case StaffState::repairing            => repair_tick(c, m);
        case _                                => update_world_staff_activity_state(c, s);
    }
}
} //namespace simcoaster
