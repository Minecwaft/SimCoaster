#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

bool route_world_handyman_to_litter_cell(StaffWorld& c, HandymanRuntimeRecord& h, CellId id) {
    auto& s = h.staff;
    const auto activity = classify_staff_activity(s);
    if (activity != StaffActivityStatus::idle && activity != StaffActivityStatus::looking_for_work)
        return false;
    //The native explicit-route helper writes this even on a failed patrol gate.
    h.target_litter_cell = id;
    if (!id || id > map_cell_count || !is_cell_inside_staff_patrol_area(s, id))
        return false;
    auto& w = world(c);
    w.saved.random_seed = id;
    const auto dx = std::clamp(random(c) & 127u, 1u, 9u);
    const auto dy = std::clamp(random(c) & 127u, 1u, 9u);
    const auto index = static_cast<std::uint32_t>(id) - 1u;
    if (!route(c,
               s,
               static_cast<std::uint16_t>(((index & 127u) << 8) | 13u * dx),
               static_cast<std::uint16_t>((index >> 7 << 8) | 13u * dy)))
        return false;
    auto& target = w.map_cells.at(id - 1u);
    target.litter_collector = s.base.header.id;
    target.litter_collection_tick = w.saved.game_tick;
    set_world_staff_state_by_type(c, s, StaffState::walking_to_litter);
    return true;
}

void call_world_cell_handyman(StaffWorld& c, CellId id) {
    std::size_t seen{};
    for (auto next = world(c).saved.first_handyman; next;) {
        if (++seen > c.entities.size())
            throw std::logic_error("Cyclic context Handyman roster");
        auto& h = record<HandymanRuntimeRecord>(c, next);
        if (route_world_handyman_to_litter_cell(c, h, id))
            return;
        next = h.staff.next_staff;
    }
}

void cancel_world_cell_litter_assignment(StaffWorld& c, CellId id) {
    if (!id || id > map_cell_count)
        return;
    auto& target = world(c).map_cells.at(id - 1u);
    if (const auto collector = target.litter_collector) {
        enter_world_staff_state(c, record<StaffRuntimeRecord>(c, collector), StaffState::idle);
        target.litter_collector = 0;
        target.litter_collection_tick = 0;
    }
}

namespace {
void set_state(StaffWorld& c, HandymanRuntimeRecord& h, StaffState state) {
    set_world_staff_state_by_type(c, h.staff, state);
}

void walk_to_litter(StaffWorld& c, HandymanRuntimeRecord& h) {
    auto& s = h.staff;
    if (c.simulation.map_cell_records[h.target_litter_cell - 1].litter_collector != s.base.header.id) {
        h.target_litter_cell = 0;
        choose_and_start_handyman_task(c, h);
        return;
    }
    const auto progress = navigate(c, s);
    if (progress == NavigationResult::arrived
        && c.simulation.map_cell_records[h.target_litter_cell - 1].litter_selector) {
        reset_position(c, s.base, s.mobile, s.mobile.accurate_destination_x, s.mobile.accurate_destination_y);
        set_state(c, h, StaffState::cleaning_up);
    } else if (progress != NavigationResult::moving) {
        h.target_litter_cell = 0;
        choose_and_start_handyman_task(c, h);
    }
    thought(c, s, MobileThoughtCode::staff_going_to_job);
}

void walk_to_object(StaffWorld& c, HandymanRuntimeRecord& h, bool toilet) {
    auto& s = h.staff;
    const auto progress = navigate(c, s);
    if (progress == NavigationResult::moving) {
        thought(c, s, MobileThoughtCode::staff_going_to_job);
        return;
    }
    bool arrived = false;
    if (progress == NavigationResult::arrived) {
        const auto id = toilet ? h.toilet_to_clean : h.bin_to_clean;
        if (toilet || (id > 0 && id < c.entities.size())) {
            auto& o = object(c, id);
            arrived = (!toilet || o.entry_cell == cell(s.base)) && claim_owner(c, o) == s.base.header.id;
        }
    }
    set_state(c, h, arrived ? (toilet ? StaffState::cleaning_toilet : StaffState::cleaning_bin) : StaffState::idle);
}
} //namespace

void choose_and_start_handyman_task(StaffWorld& c, HandymanRuntimeRecord& h) {
    auto& s = h.staff;
    if (evaluate_world_staff_energy_and_activity(c, s))
        return;
    if (!is_world_staff_too_tired_for_task(c, s)) {
        const auto& b = c.staff.recruitment.balance.Handyman;
        h.target_litter_cell =
            find_nearest_available_litter_cell(c,
                                               record<ParkAnalyserState>(c, world(c).saved.park_analyser),
                                               s.base.header.id,
                                               cell(s.base),
                                               interpolate(c, s, b.MinDetectionRange, b.MaxDetectionRange));
        if (h.target_litter_cell) {
            if (is_cell_inside_staff_patrol_area(s, h.target_litter_cell)) {
                world(c).saved.random_seed = h.target_litter_cell;
                const auto dx = std::clamp(random(c) & 127u, 1u, 9u), dy = std::clamp(random(c) & 127u, 1u, 9u);
                const auto index = static_cast<std::uint32_t>(h.target_litter_cell) - 1u;
                if (route(c,
                          s,
                          static_cast<std::uint16_t>(((index & 127u) << 8) | 13u * dx),
                          static_cast<std::uint16_t>((index >> 7 << 8) | 13u * dy))) {
                    auto& target = c.simulation.map_cell_records[h.target_litter_cell - 1];
                    target.litter_collector = s.base.header.id;
                    target.litter_collection_tick = world(c).saved.game_tick;
                    set_state(c, h, StaffState::walking_to_litter);
                    return;
                }
            }
        } else {
            const auto toilet_on_tie = random(c) & 1u;
            h.toilet_to_clean = select_staff_service_target(c, s, ParkObjectCapabilities::provides_relief);
            h.bin_to_clean = select_staff_service_target(c, s, ParkObjectCapabilities::holds_litter);
            if (h.toilet_to_clean && (!h.bin_to_clean || toilet_on_tie)) {
                set_state(c, h, StaffState::walking_to_clean_toilet);
                return;
            }
            if (h.bin_to_clean && (!h.toilet_to_clean || !toilet_on_tie)) {
                set_state(c, h, StaffState::walking_to_clean_bin);
                return;
            }
        }
    }
    set_state(c, h, try_set_mobile_entity_destination(c, s.base, s.mobile) ? StaffState::patrolling : StaffState::idle);
}

void update_world_handyman_idle_state(StaffWorld& c, HandymanRuntimeRecord& h) {
    auto& s = h.staff;
    update_world_staff_tick(c, s);
    const auto interval = static_cast<std::uint32_t>(
        integer((1.0 - static_cast<double>(s.experience) / balance(c).MaxExperience) * 50.0));
    if (s.state == StaffState::idle && (!interval || world(c).saved.game_tick % interval == 0)) {
        const auto& b = c.staff.recruitment.balance.Handyman;
        if (find_nearest_available_litter_cell(c,
                                               record<ParkAnalyserState>(c, world(c).saved.park_analyser),
                                               s.base.header.id,
                                               cell(s.base),
                                               interpolate(c, s, b.MinDetectionRange, b.MaxDetectionRange)))
            choose_and_start_handyman_task(c, h);
    }
    release_expired_mobile_entity_thought_script(c, s.mobile.thought_history);
}

void update_world_handyman_state(StaffWorld& c, HandymanRuntimeRecord& h) {
    auto& s = h.staff;
    match (s.state) {
        case StaffState::idle => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   161);
            start_task_after_idle_delay(c, s);
        }
        case StaffState::patrolling => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   160);
            if (navigate(c, s) == NavigationResult::moving)
                decrease_staff_energy_and_happiness_during_task(c, s);
            else
                choose_and_start_handyman_task(c, h);
        }
        case StaffState::walking_to_rest_area => handle_staff_rest_area_arrival(c, s);
        case StaffState::resting              => recover_staff_energy_and_happiness_at_rest_area(c, s);
        case StaffState::walking_to_litter    => walk_to_litter(c, h);
        case StaffState::cleaning_up          => {
            decrease_staff_energy_and_happiness_during_task(c, s);
            if (task_expired(c, s, h.time_started_cleaning)) {
                if (c.simulation.map_cell_records[cell(s.base) - 1].litter_selector)
                    release_world_litter_record(c, c.simulation.map_cell_records[h.target_litter_cell - 1]);
                h.target_litter_cell = 0;
                choose_and_start_handyman_task(c, h);
            }
        }
        case StaffState::walking_to_clean_toilet                     => walk_to_object(c, h, true);
        case StaffState::walking_to_clean_bin                        => walk_to_object(c, h, false);
        case StaffState::cleaning_toilet or StaffState::cleaning_bin => {
            const bool toilet = s.state == StaffState::cleaning_toilet;
            decrease_staff_energy_and_happiness_during_task(c, s);
            if (!toilet)
                face_cell_direction(c, s);
            if (task_expired(c, s, h.time_started_cleaning)) {
                auto& target = object(c, toilet ? h.toilet_to_clean : h.bin_to_clean);
                complete_park_object_service_task(c, target);
                if (toilet)
                    open_park_object(c, target);
                set_sprite_animation_next(s.mobile.animation, 3);
                set_state(c, h, StaffState::idle);
            }
        }
        case _ => update_world_staff_activity_state(c, s);
    }
}
} //namespace simcoaster
