#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/rsse_people.hpp"
#include "simcoaster/object_balance.hpp"

namespace simcoaster {
namespace {
int read_slot(VisitorWorld& c, const ParkObjectRuntimeView& object, int index) {
    auto* state = world_operations::find_script(c, object.script_id);
    return world_operations::script_slot(c, state, index);
}

void write_slot(VisitorWorld& c, const ParkObjectRuntimeView& object, int index, int value) {
    auto* state = world_operations::find_script(c, object.script_id);
    world_operations::set_script_slot(c, state, index, value);
}

void dispatch_action(VisitorWorld& c, PersonRuntimeView& person, PersonActionCode action) {
    WorldEvent event { PersonActionEvent { person.base.header.id, action } };
    dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
}

void dirty_bin_effect(VisitorWorld& c, const ParkObjectRuntimeView& bin) {
    (void)remove_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::clean_bin, person_detail::cell_id(bin.base));
    (void)add_world_re_cell_effect_profile(c.simulation.region_effects, RegionEffectProfileId::dirty_bin, person_detail::cell_id(bin.base));
}

void fallback_litter(VisitorWorld& c, PersonRuntimeView& person) {
    const auto selector = static_cast<LitterSelector>(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 5u + 1u);
    if (!c.simulation.map_cell_records) {
        throw std::logic_error("litter arrival has no map cells");
    }
    initialize_world_litter_record(c, c.simulation.map_cell_records[person_detail::cell_id(person.base) - 1u], selector);
}

void finish_prank(VisitorWorld& c, PersonRuntimeView& person, PersonActionCode action) {
    world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, MobileThoughtCode::naughty, false);
    dispatch_action(c, person, action);
    const auto value = static_cast<double>(person.happiness) + 1.0;
    //Unlike the roaming controller's reward, these arrivals map NaN to zero.
    person.happiness = person_detail::meter(value);
}

void note_whinge_victim(VisitorWorld& c, PersonRuntimeView& person, PersonActionCode action) {
    if (!(std::bit_cast<std::int32_t>(person.prank_timer) % 30)) {
        world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, MobileThoughtCode::whinge_to_staff, false);
    }
    if (!person.found_whinge_victim) {
        dispatch_action(c, person, action);
        //Native only clamps existing happiness here; it adds no reward.
        if (person.happiness <= 100.f) {
            if (person.happiness < 0.f) {
                person.happiness = 0.f;
            }
        } else {
            person.happiness = 100.f;
        }
        person.found_whinge_victim = 1;
    }
}

CellId wrapped_scan_cell(WorldCellCoordinates point) {
    return static_cast<CellId>((static_cast<std::uint32_t>(point.y) << 7) + static_cast<std::uint32_t>(point.x) + 1u);
}

template <class Visit>
void occupants(VisitorWorld& c, CellId cell, Visit&& visit) {
    auto& world = person_detail::base_world(c);
    if (!cell || cell > map_cell_count) {
        throw std::out_of_range("guest interaction outside map cells");
    }
    auto id = static_cast<ThingId>(world.map_cells[cell - 1u].who);
    std::size_t visited = 0;
    while (id) {
        if (visited++ >= thing_slot_count) {
            throw std::length_error("cyclic guest interaction occupants");
        }
        auto& object = person_detail::map_object(c, id);
        visit(id, object);
        id = object.map_child;
    }
}

template <class Visit>
void prank_area(VisitorWorld& c, PersonRuntimeView& person, std::int32_t width, Visit&& visit) {
    --person.prank_timer;
    if (std::bit_cast<std::int32_t>(person.prank_timer) <= 0) {
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
        return;
    }
    const auto x = fixed::subtract(person.base.x >> 8, width / 2), y = fixed::subtract(person.base.y >> 8, width / 2);
    const auto end_x = fixed::add(x, width), end_y = fixed::add(y, width);
    for (auto cy = y; cy < end_y; ++cy) {
        for (auto cx = x; cx < end_x; ++cx) {
            visit(WorldCellCoordinates { cx, cy });
        }
    }
}
} //namespace

void increment_park_object_use_history_counters(ParkObjectRuntimeView& object) {
    if (object.history) {
        ++object.history->dword_history_144[5].temporary;
        ++object.history->dword_history_30[2].temporary;
    }
}

void record_park_object_queue_walkaway(ParkObjectRuntimeView& object) {
    ++object.num_walk_aways;
    if (object.history) {
        ++object.history->dword_history_30[3].temporary;
    }
}

void handle_person_litter_bin_use_arrival(VisitorWorld& c, PersonRuntimeView& person) {
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (navigation == NavigationResult::arrived) {
        bool dropped = true;
        if (person.major_destination) {
            auto& bin = person_detail::park_object(c, person.major_destination);
            increment_park_object_use_history_counters(bin);
            const auto capacity = read_slot(c, bin, 3), contents = read_slot(c, bin, 4), on_fire = read_slot(c, bin, 5);
            if (contents < capacity && !on_fire) {
                write_slot(c, bin, 0, 1);
                if (park_object_requires_staff_service(c, bin)) {
                    dirty_bin_effect(c, bin);
                }
                dropped = false;
            }
        }
        if (dropped) {
            fallback_litter(c, person);
        }
    } else if (navigation == NavigationResult::unreachable) {
        fallback_litter(c, person);
    } else {
        return;
    }
    person.needs.litter_meter = 0;
    world_operations::enter_world_person_state(c, &person, PersonState::roaming);
}

void handle_person_litter_bin_prank_arrival(VisitorWorld& c, PersonRuntimeView& person) {
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (navigation == NavigationResult::arrived) {
        if (!person.major_destination) {
            return;
        }
        auto& bin = person_detail::park_object(c, person.major_destination);
        if (!read_slot(c, bin, 10)) {
            write_slot(c, bin, 2, 1);
            const auto contents = read_slot(c, bin, 4);
            if (contents >= read_slot(c, bin, 12)) {
                dirty_bin_effect(c, bin);
            }
            finish_prank(c, person, PersonActionCode::litter_bin_prank);
        }
    } else if (navigation != NavigationResult::unreachable) {
        return;
    }
    world_operations::enter_world_person_state(c, &person, PersonState::roaming);
}

void handle_person_vegetation_fire_prank_arrival(VisitorWorld& c, PersonRuntimeView& person) {
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (navigation == NavigationResult::arrived) {
        if (person.major_destination) {
            auto& vegetation = person_detail::park_object(c, person.major_destination);
            if (!read_slot(c, vegetation, 13)) {
                write_slot(c, vegetation, 11, 1);
                finish_prank(c, person, PersonActionCode::vegetation_fire_prank);
            }
        }
    } else if (navigation != NavigationResult::unreachable) {
        return;
    }
    world_operations::enter_world_person_state(c, &person, PersonState::roaming);
}

void spread_rumours_in_cell(VisitorWorld& c, PersonRuntimeView& person, WorldCellCoordinates point) {
    occupants(c, wrapped_scan_cell(point), [&](ThingId id, MapObjectBase& base) {
        if (id == person.base.header.id || base.header.kind != ThingKind::person) {
            return;
        }
        auto& target = *reinterpret_cast<PersonRuntimeView*>(&base);
        const auto state = target.state, effective = state == PersonState::temporary_appearance ? target.saved_state : state;
        if ((std::to_underlying(effective) < 14 || std::to_underlying(effective) > 17) && state != PersonState::temporary_appearance && target.generation != PersonGeneration::child && state != PersonState::walk_to_exit) {
            world_operations::enter_world_person_state(c, &target, PersonState::listen_to_vicious_rumours);
            note_whinge_victim(c, person, PersonActionCode::spread_vicious_rumours);
        }
    });
    set_sprite_animation_next(person.mobile.animation, 3);
}

void whinge_at_staff_in_cell(VisitorWorld& c, PersonRuntimeView& person, WorldCellCoordinates point) {
    occupants(c, wrapped_scan_cell(point), [&](ThingId id, MapObjectBase& base) {
        if (id == person.base.header.id || !is_staff_record_kind(base.header.kind)) {
            return;
        }
        auto& target = *reinterpret_cast<StaffRuntimeRecord*>(&base);
        enter_world_staff_state(c, target, StaffState::being_whinged_at);
        note_whinge_victim(c, person, PersonActionCode::whinge_at_staff);
    });
    set_sprite_animation_next(person.mobile.animation, 3);
}

void update_person_whinge_to_staff(VisitorWorld& c, PersonRuntimeView& person) {
    prank_area(c, person, c.visitors.needs_balance.PeepInfo.WhingeToStaffArea, [&](WorldCellCoordinates point) {
        whinge_at_staff_in_cell(c, person, point);
    });
}

void update_person_vicious_rumours(VisitorWorld& c, PersonRuntimeView& person) {
    prank_area(c, person, c.visitors.needs_balance.PeepInfo.ViciousRumoursArea, [&](WorldCellCoordinates point) {
        spread_rumours_in_cell(c, person, point);
    });
}

void update_person_after_info_desk_wait(VisitorWorld& c, PersonRuntimeView& person) {
    if (person.info_desk_timer) {
        --person.info_desk_timer;
        return;
    }
    auto& desk = person_detail::park_object(c, person.major_destination);
    (void)get_park_object_definition(c, &desk);
    const auto promoted = get_info_desk_promoted_attraction_id(c, desk);
    if (promoted && promoted < thing_slot_count) {
        person.major_destination = promoted;
        set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::boost_50, person.umbrella_state);
        world_operations::enter_world_person_state(c, &person, PersonState::travel_to_object_queue);
    } else {
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
    }
}

void update_person_after_park_object_exit(VisitorWorld& c, PersonRuntimeView& person) {
    const auto navigation = update_mobile_entity_navigation(c, person.base, person.mobile);
    if (navigation == NavigationResult::unreachable) {
        person.major_destination = 0;
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
        return;
    }
    if (navigation != NavigationResult::arrived) {
        return;
    }
    const auto resume = [&](ParkObjectRuntimeView& object) {
        const auto cell = get_back_of_queue(c, &object);
        if (!cell || !try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, cell)) {
            return false;
        }
        world_operations::enter_world_person_state(c, &person, PersonState::travel_to_object_queue);
        return true;
    };
    if (person.photo_booth) {
        auto* target = person_detail::find_live_park_object(c, person.photo_booth);
        //Old native saves can already contain a booth ID whose payload has been
        //freed or reused. A different park object is not a photo destination.
        const auto* definition = target ? get_park_object_definition(c, target) : nullptr;
        if (definition && definition->is_photo_booth) {
            person.major_destination = person.photo_booth;
            if (resume(*target)) {
                return;
            }
        } else {
            person.photo_booth = 0;
        }
        person.major_destination = 0;
    }
    if (person.saved_major_destination) {
        person.major_destination = person.saved_major_destination;
        person.saved_major_destination = 0;
        if (auto* target = person_detail::find_live_park_object(c, person.major_destination)) {
            auto& object = *target;
            const bool already_umbrella = get_park_object_balance(c, object).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::umbrella) && person.umbrella_state != PersonUmbrellaState::none;
            if (!already_umbrella && !(get_park_object_balance(c, object).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::balloon) && (person.balloon_script || person.remaining_balloon_life)) && resume(object)) {
                return;
            }
        }
        person.major_destination = 0;
    }
    world_operations::enter_world_person_state(c, &person, PersonState::roaming);
    if (person.umbrella_state == PersonUmbrellaState::none || person.umbrella_state == PersonUmbrellaState::inactive) {
        start_person_temporary_appearance_animation(c, person, person.won_last_thang ? static_cast<std::uint32_t>(get_person_sprite_generation_variant_code(c, person)) : 10u);
    }
}
} //namespace simcoaster
