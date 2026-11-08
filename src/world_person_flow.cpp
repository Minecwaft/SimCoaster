#include "world_person_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_mail.hpp"
#include <limits>

namespace simcoaster {
AdvisorQueueEntry& init_advisor_action_candidate(VisitorWorld& c,
                                                 AdvisorQueueEntry& candidate,
                                                 std::int32_t action,
                                                 std::uint16_t event_word,
                                                 std::uint32_t event_dword,
                                                 std::uint32_t parameter,
                                                 std::uint8_t bypass,
                                                 std::int32_t score_or_auto) {
    candidate.thing_id = event_word;
    candidate.message = static_cast<std::uint32_t>(action);
    candidate.secondary = event_dword;
    if (score_or_auto < 0) {
        if (!(c.advisor.advisor_candidate_initialized & 1u)) {
            c.advisor.advisor_candidate_initialized |= 1u;
            world_operations::load_advisor_behavior_configuration(c);
        }
        candidate.score = world_operations::evaluate_advisor_action_score(c, action);
        candidate.score_overridden = 0;
    } else {
        candidate.score = score_or_auto;
        candidate.score_overridden = 1;
    }
    candidate.override_only_once = bypass;
    candidate.response_no = parameter;
    candidate.valid = 1;
    return candidate;
}

AdvisorQueueEntry world_operations::builtin::make_advisor_action_candidate(WorldRuntime& c, std::int32_t action) {
    AdvisorQueueEntry candidate{};
    init_advisor_action_candidate(c, candidate, action, 0, 0, 4294967295u, 0, -1);
    return candidate;
}

bool is_first_hour_of_gameplay(const VisitorQuery& c) {
    return c.timing.first_hour_time / 60u < static_cast<std::uint32_t>(c.visitors.flow_balance.FirstHour.Minutes);
}

std::uint32_t increment_world_visitor_count(World& world) {
    return ++world.saved.number_of_visitors_to_date;
}

bool is_advisor_action_active(VisitorWorld& c, std::int32_t action) {
    return get_or_create_theme_global_info_manager(c).history.at(static_cast<std::size_t>(action)).is_active != 0;
}

void update_person_crossing_park_entrance(VisitorWorld& c, PersonRuntimeView& person) {
    if (!person_detail::base_world(c).saved.park_closed
        && get_world_park_gates_rsse_slot1(c, person_detail::base_world(c)) == 1) {
        if (update_mobile_entity_navigation(c, person.base, person.mobile) != NavigationResult::arrived)
            return;
        person.arrival_index = increment_world_visitor_count(person_detail::base_world(c));
        if (!(c.visitors.visitor_advisor_initialized & 1u)) {
            c.visitors.visitor_advisor_initialized |= 1u;
            world_operations::load_advisor_behavior_configuration(c);
        }
        if (is_first_hour_of_gameplay(c)) {
            const auto count = count_eligible_person_records(c, 4);
            const auto& balance = c.visitors.visitor_advisor_balance.New;
            std::int32_t action = -1;
            if (count == static_cast<std::uint32_t>(balance.ADVISORSTUTWELLDONE1NUMVIS))
                action = 519;
            else if (count == static_cast<std::uint32_t>(balance.ADVISORSTUTVISITORINFONUMVIS))
                action = 520;
            if (action != -1 && !is_advisor_action_active(c, action)) {
                auto candidate = world_operations::make_advisor_action_candidate(c, action);
                auto* payload = c.entities.at(person_detail::base_world(c).saved.advisor).payload;
                if (!payload)
                    throw std::logic_error("visitor advisor is missing");
                (void)world_operations::queue_advisor_action_candidate(c,
                                                                       reinterpret_cast<AdvisorRecord*>(payload),
                                                                       candidate);
            }
        }
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
        return;
    }
    const auto booth_count = static_cast<std::uint32_t>(c.visitors.environment.FixedItemInfo.NumTicketBooths);
    if (!booth_count)
        throw std::domain_error("no crossing ticket booths");
    auto candidate = person.base.header.id % booth_count;
    for (std::int32_t index = 0; index < std::bit_cast<std::int32_t>(booth_count); ++index) {
        if (person_detail::cell_id(person.base)
            != get_ticket_booth_cell(c.visitors.environment, static_cast<std::size_t>(index)))
            continue;
        const auto destination = get_ticket_booth_cell(c.visitors.environment, candidate);
        if (try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, destination)) {
            world_operations::enter_world_person_state(c, &person, PersonState::wait_for_park_open);
            return;
        }
        candidate ^= 1u;
    }
    world_operations::enter_world_person_state(c, &person, PersonState::wait_for_park_open);
}

void update_person_walking_to_park_exit(VisitorWorld& c, PersonRuntimeView& person) {
    if (person.exit_level > 0
        && truncate_park_object_condition(person.happiness)
        && !person_detail::base_world(c).saved.park_closed
        && record_passes_grid_predicates(c, &person.base)) {
        person.mobile.navigation.navigation_mode = 0;
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
        return;
    }
    const auto result = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (result == NavigationResult::arrived) {
        person.mobile.sprite_angle = is_current_arrival_vehicle_not_size1(person_detail::base_world(c)) ? 2047u : 1024u;
        world_operations::enter_world_person_state(c, &person, PersonState::departure_routing);
    } else if (result == NavigationResult::unreachable && !try_set_mobile_entity_path_to_park_exit(c, person)) {
        person.mobile.navigation.navigation_mode = 1;
        (void)try_set_mobile_entity_path_to_park_exit(c, person);
    }
}

void update_person_departure_routing(VisitorWorld& c, PersonRuntimeView& person) {
    const auto status = get_current_arrival_vehicle_status(c, person_detail::base_world(c));
    if (status == 4) {
        if (std::to_underlying(person.prank) != 0) {
            auto* payload = c.entities.at(person_detail::base_world(c).saved.tag_system).payload;
            if (!payload)
                throw std::logic_error("departure tag system is missing");
            (void)mark_tag_messages_by_format_word_for_deletion(c,
                                                                reinterpret_cast<TagSystemRecord*>(payload),
                                                                person.base.header.id);
        }
        (void)create_runtime_template_at_mobile_entity_resource_position(c, person.mobile, 19, 0, 0);
        (void)retire_world_used_thing(c, &person.base.header);
        return;
    }
    if (status != 1 && status != 2 && status != 3)
        return;
    const auto stop = get_bus_stop_cell(c.visitors.environment, BusStopSelector::a),
               next = static_cast<CellId>(stop + 1u), current = person_detail::cell_id(person.base);
    if (current == stop || current == next)
        return;
    const auto destination = (next_world_prng_value(person_detail::base_world(c)) & 1u) ? next : stop;
    const auto index = static_cast<std::uint32_t>(destination) - 1u;
    const auto x = static_cast<std::uint16_t>(((index & 127u) << 8)
                                              | (next_world_prng_value(person_detail::base_world(c)) & 255u));
    const auto y = static_cast<std::uint16_t>((((index >> 7) & 255u) << 8)
                                              | (next_world_prng_value(person_detail::base_world(c)) & 255u));
    (void)request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y);
    world_operations::enter_world_person_state(c, &person, PersonState::walk_to_exit);
}
} //namespace simcoaster
