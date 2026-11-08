#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/rsse_people.hpp"
#include "simcoaster/world_research.hpp"
#include <optional>

namespace simcoaster {
namespace {
MapCell& global_cell(VisitorWorld& c, CellId id) {
    if (!c.simulation.map_cell_records || !id || id > map_cell_count)
        throw std::out_of_range("roaming map cell");
    return c.simulation.map_cell_records[id - 1u];
}

bool face(PersonRuntimeView& person, const MapObjectBase& target) {
    const auto from = static_cast<std::uint32_t>(person_detail::cell_id(person.base)) - 1u,
               to = static_cast<std::uint32_t>(person_detail::cell_id(target)) - 1u;
    const auto dx = static_cast<std::int32_t>(to & 127u) - static_cast<std::int32_t>(from & 127u),
               dy = static_cast<std::int32_t>(to >> 7) - static_cast<std::int32_t>(from >> 7);
    if (!(dx + dy))
        return false;
    person.mobile.sprite_angle = compute_world_sprite_angle_from_delta(-dx, dy);
    return true;
}
} //namespace

void start_person_temporary_appearance_animation(VisitorWorld& c, PersonRuntimeView& person, std::uint32_t animation) {
    if (!(person.base.header.id % 3u)) {
        if (animation == 4)
            c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                               &person,
                                                                               c.resources.kids_catalog_slot,
                                                                               126);
        else if (animation >= 5 && animation <= 9)
            c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                               &person,
                                                                               c.resources.kids_catalog_slot,
                                                                               77);
        else if (animation == 10)
            c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                               &person,
                                                                               c.resources.kids_catalog_slot,
                                                                               213);
    }
    set_sprite_animation_next(person.mobile.animation, animation);
    const auto tick = person_detail::base_world(c).saved.game_tick;
    person.saved_state = person.state;
    person.time_of_last_spot_animation = tick;
    world_operations::enter_world_person_state(c, &person, PersonState::temporary_appearance);
}

void update_person_roaming(VisitorWorld& c, PersonRuntimeView& person) {
    auto* entry_world = &person_detail::base_world(c);
    const auto entry_tick = entry_world->saved.game_tick;
    const auto random = static_cast<std::uint32_t>(next_world_prng_value(*entry_world));
    person.force_thought_31 = 0;
    const auto no_umbrella = [&] {
        return person.umbrella_state == PersonUmbrellaState::none
            || person.umbrella_state == PersonUmbrellaState::inactive;
    };
    if (entry_tick - person.time_of_last_spot_animation > 100u
        && truncate_park_object_condition(person.happiness) > 80
        && no_umbrella()) {
        start_person_temporary_appearance_animation(
            c,
            person,
            static_cast<std::uint32_t>(get_person_sprite_generation_variant_code(c, person)));
        return;
    }
    if (truncate_park_object_condition(person.needs.sickness) == 100 && !(random % 3u)) {
        start_person_temporary_appearance_animation(c, person, 12);
        initialize_world_litter_record(c, global_cell(c, person_detail::cell_id(person.base)), LitterSelector::vomit);
        c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                           &person,
                                                                           c.resources.kids_catalog_slot,
                                                                           204);
        person.needs.sickness = 0;
        return;
    }
    if (truncate_park_object_condition(person.needs.litter_meter) >= 90
        && !(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) & 3u)) {
        const auto bin = find_nearest_eligible_litter_bin(c, person, 3);
        if (bin) {
            person.major_destination = bin;
            world_operations::enter_world_person_state(c, &person, PersonState::use_litter_bin);
            return;
        }
        auto& cell = global_cell(c, person_detail::cell_id(person.base));
        const auto selector = static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 5u + 1u;
        if (!cell.litter_selector) {
            initialize_world_litter_record(c, cell, static_cast<LitterSelector>(selector));
            person.needs.litter_meter = 0;
        }
    }
    if (!truncate_park_object_condition(person.happiness) || person.exit_level <= 0 || entry_world->saved.park_closed) {
        decrease_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::big);
        if (!try_set_mobile_entity_path_to_park_exit(c, person)) {
            person.mobile.navigation.navigation_mode = 1;
            (void)try_set_mobile_entity_path_to_park_exit(c, person);
        }
        world_operations::enter_world_person_state(c, &person, PersonState::walk_to_exit);
        //The original deliberately continues into nearby targets and decisions.
    }
    const auto fireworks = find_nearby_unstarted_fireworks(c, person);
    if (fireworks && face(person, person_detail::map_object(c, fireworks)))
        return;
    if (query_world_used_thing_type_in_square_radius(c,
                                                     std::to_underlying(ThingKind::entertainer),
                                                     person_detail::cell_id(person.base),
                                                     1,
                                                     true)) {
        const auto entertainer = find_nearest_entertainer_to_entity(c, person.base);
        const auto& record = *reinterpret_cast<const StaffRuntimeRecord*>(&person_detail::map_object(c, entertainer));
        if (record.state == StaffState::entertaining) {
            if (face(person, record.base)) {
                person.force_thought_31 = 1;
                return;
            }
            person.force_thought_31 = 0;
        }
    }
    if (truncate_park_object_condition(person.happiness) < c.visitors.needs_balance.PeepInfo.PrankHappinessThreshold
        && !person.toilet_booby_flag) {
        const auto chance = random % 100u;
        if (chance < static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.PrankLikelihood)
            || person.force_prank) {
            const auto prank = person.prank;
            person.force_prank = 0;
            std::optional<PersonActionCode> action;
            (do {
                match (prank) {
                    case PersonPrank::stink_bomb => {
                        if (chance < static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.StinkbombLikelihood)) {
                            initialize_world_litter_record(
                                c,
                                entry_world->map_cells.at(person_detail::cell_id(person.base) - 1u),
                                LitterSelector::stink_bomb);
                            c.ports.resources.submit_mobile_entity_positioned_template_request(
                                c,
                                &person,
                                c.resources.kids_catalog_slot,
                                207);
                            action = PersonActionCode::stink_bomb;
                        }
                    }
                    case PersonPrank::toilet_booby_trap => {
                        person.needs.toilet_need = 80;
                        person.toilet_booby_flag = 1;
                    }
                    case PersonPrank::litter_bin => {
                        const auto bin = find_nearest_eligible_litter_bin(c, person, 6);
                        if (!bin)
                            do_return;
                        const auto& object = person_detail::park_object(c, bin);
                        auto* script = world_operations::find_script(c, object.script_id);
                        if (world_operations::script_slot(c, script, 10))
                            do_return;
                        person.major_destination = bin;
                        world_operations::enter_world_person_state(c, &person, PersonState::litter_prank);
                        return;
                    }
                    case PersonPrank::vegetation_fire => {
                        const auto target = find_nearest_vegetation_for_fire_prank(c, person, 6);
                        if (!target)
                            do_return;
                        person.major_destination = target;
                        world_operations::enter_world_person_state(c, &person, PersonState::vegetation_fire_prank);
                        return;
                    }
                    case PersonPrank::drop_litter => {
                        initialize_world_litter_record(c,
                                                       entry_world->map_cells.at(person_detail::cell_id(person.base) - 1u),
                                                       static_cast<LitterSelector>(random % 5u + 1u));
                        action = PersonActionCode::scatter_litter;
                        person.prank = PersonPrank::none;
                        world_operations::enter_world_person_state(c, &person, PersonState::travel_to_destination);
                    }
                    case PersonPrank::balloon => {
                        auto id =
                            static_cast<ThingId>(entry_world->map_cells.at(person_detail::cell_id(person.base) - 1u).who);
                        std::size_t count = 0;
                        while (id) {
                            if (count++ >= thing_slot_count)
                                throw std::length_error("cyclic balloon-prank cell list");
                            auto& target = person_detail::map_object(c, id);
                            if (id != person.base.header.id && target.header.kind == ThingKind::person) {
                                auto& victim = *reinterpret_cast<PersonRuntimeView*>(&target);
                                if (victim.balloon_script || victim.remaining_balloon_life) {
                                    discard_person_balloon(c, victim, true);
                                    action = PersonActionCode::balloon_prank;
                                    break;
                                }
                            }
                            id = target.map_child;
                        }
                    }
                    case PersonPrank::vicious_rumour or PersonPrank::whinge_to_staff => {
                        person.prank_timer = static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.WhingeDuration);
                        person.found_whinge_victim = 0;
                        world_operations::enter_world_person_state(c,
                                                                   &person,
                                                                   prank == PersonPrank::vicious_rumour
                                                                       ? PersonState::spread_vicious_rumours
                                                                       : PersonState::whinge_to_staff);
                        set_sprite_animation_next(person.mobile.animation, 3);
                        world_operations::set_mobile_entity_thought_bubble(c,
                                                                           &person.mobile.thought_history,
                                                                           MobileThoughtCode::whinge_to_staff,
                                                                           false);
                        return;
                    }
                    case _ => {}
                }
            });
            if (action) {
                world_operations::set_mobile_entity_thought_bubble(c,
                                                                   &person.mobile.thought_history,
                                                                   MobileThoughtCode::naughty,
                                                                   false);
                WorldEvent event{ PersonActionEvent{ person.base.header.id, *action } };
                dispatch_world_event_to_subscribers(c,
                                                    get_world_event_subscription_registry(c),
                                                    &event,
                                                    deliver_world_record_event,
                                                    nullptr);
                const auto value = static_cast<double>(person.happiness) + 1.0;
                person.happiness = value <= 100.0 ? (value >= 0.0 ? static_cast<float>(value) : 0.f) : 100.f;
            }
        }
    }
    match (random & 3u) {
        case 0 or 2 => {
            if (entry_tick > person.time_started_idling + 30u) {
                select_person_park_object_destination_by_score(c, person);
                if (person.major_destination)
                    world_operations::enter_world_person_state(c, &person, PersonState::travel_to_object_queue);
                else {
                    start_person_temporary_appearance_animation(c, person, no_umbrella() ? 4u : 3u);
                    decrease_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::small);
                    person.time_started_idling = entry_tick;
                }
            }
            return;
        }
        case 1 => {
            if (try_set_mobile_entity_destination(c, person.base, person.mobile))
                world_operations::enter_world_person_state(c, &person, PersonState::travel_to_destination);
            else
                person.time_started_idling = entry_tick;
            return;
        }
        case 3 => {
            if (next_crt_random(c) >= 16383)
                return;
            if (next_crt_random(c) >= 16383) {
                const auto target = find_nearest_active_person_record(c, person.base);
                if (target)
                    (void)try_set_mobile_entity_path_to_cell(
                        c,
                        person.base,
                        person.mobile,
                        person_detail::cell_id(person_detail::map_object(c, target)));
            } else {
                const auto desk = select_best_available_info_desk_for_person(c, person);
                if (desk) {
                    auto& object = person_detail::park_object(c, desk);
                    if (get_info_desk_promoted_attraction_id(c, object)) {
                        (void)try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, object.entry_cell);
                        person.major_destination = desk;
                        world_operations::enter_world_person_state(c, &person, PersonState::walk_to_info_desk);
                    }
                }
            }
            return;
        }
        case _ => {}
    }
}
} //namespace simcoaster
