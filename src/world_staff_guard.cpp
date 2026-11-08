#include "simcoaster/world_challenge.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
PersonRuntimeView* live_person(StaffWorld& c, ThingId id) {
    if (!id || id >= c.entities.size())
        return nullptr;
    auto* value = c.entities[id].payload;
    return value && value->kind == ThingKind::person && !value->retirement_pending
             ? reinterpret_cast<PersonRuntimeView*>(value)
             : nullptr;
}

void state(StaffWorld& c, GuardRuntimeRecord& g, StaffState requested) {
    set_world_staff_state_by_type(c, g.staff, requested);
}

CellId patrol_point(StaffWorld& c) {
    const auto& p = c.visitors.environment.FixedItemInfo;
    return static_cast<CellId>(world(c).saved.game_tick & 1u ? p.EntranceBPosX + (p.EntranceBPosY << 7) + 1
                                                             : p.EntranceAPosX + (p.EntranceAPosY << 7) + 1);
}

CellId release_point(const StaffQuery& c, std::uint32_t index) {
    const auto& p = c.staff.recruitment.balance.KidRelease.at(index);
    return static_cast<CellId>(p.PosX + (p.PosY << 7) + 1);
}

void pursuit_progress(StaffWorld& c, PersonRuntimeView& person, std::int32_t outcome) {
    auto& analyser = record<ParkAnalyserState>(c, world(c).saved.park_analyser);
    ++analyser.misbehaving_kids;
    ++analyser.misbehaving_kids_ever;
    if (!outcome)
        ++analyser.misbehaving_kids_that_got_away;
    auto& challenges = record<ChallengeManagerRecord>(c, world(c).saved.challenge_manager);
    add_progress_to_matching_world_challenges(
        challenges,
        static_cast<ChallengeType>(person.generation == PersonGeneration::child ? 40 : 39),
        1,
        0,
        0);
    add_progress_to_matching_world_challenges(challenges, static_cast<ChallengeType>(38), 1, 0, 0);
    add_progress_to_matching_world_challenges(challenges, static_cast<ChallengeType>(48), 1, 0, 0);
}

bool catchable(const PersonRuntimeView& person) {
    const auto value = static_cast<std::uint32_t>(person.state);
    //Native switch rejects named non-roaming states; out-of-range high values
    //fall through as catchable, rather than a bounded6..13/22-only test.
    return value >= 6u && !(value >= 14u && value <= 21u) && !(value >= 23u && value <= 30u);
}

void catch_person(StaffWorld& c, PersonRuntimeView& person) {
    if (person.balloon_script) {
        world_operations::restart_resource_variant(c,
                                                   person.balloon_script,
                                                   { ResourceProgramFamily::balloon_discard, 0 });
        person.balloon_script = 0;
        person.happiness =
            percentage(static_cast<double>(person.happiness)
                       - static_cast<std::uint8_t>(c.visitors.needs_balance.PeepInfo.MediumHappinessChange));
        person.remaining_balloon_life = 0;
    } else if (person.remaining_balloon_life) {
        person.happiness =
            percentage(static_cast<double>(person.happiness)
                       - static_cast<std::uint8_t>(c.visitors.needs_balance.PeepInfo.MediumHappinessChange));
        person.remaining_balloon_life = 0;
    }
    release_world_sprite_animation_resource_handle(c, person.mobile.animation);
    enter_world_person_state(c, person, PersonState::caught_by_guard);
}

void prosecute(StaffWorld& c, GuardRuntimeRecord& g) {
    auto& s = g.staff;
    decrease_staff_energy_and_happiness_during_task(c, s);
    auto* target = live_person(c, g.perpetrator);
    const auto abandon = [&] {
        //Native 5D431A calls 5AEE30 even after event33 cleared mPerp, and that
        //callee dereferences its Person. A retired target is cancellation, not a
        //fabricated escape/challenge result. Valid live pursuits retain theirs.
        if (target)
            pursuit_progress(c, *target, 0);
        set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
        state(c, g, StaffState::idle);
        g.perpetrator = 0;
    };
    if (!target || !g.prosecution_timestamp) {
        abandon();
        return;
    }
    --g.prosecution_timestamp;
    auto& person = *target;
    const auto progress = navigate(c, s);
    if (progress == NavigationResult::unreachable) {
        abandon();
        return;
    }
    if (progress == NavigationResult::arrived && cell(s.base) != cell(person.base)) {
        if (!route(c, s, cell(person.base)))
            abandon();
        return;
    }
    if (cell(s.base) != cell(person.base))
        return;
    if (catchable(person)) {
        set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
        set_sprite_animation_next(s.mobile.animation, 30);
        pursuit_progress(c, person, 2);
        c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                           s.mobile,
                                                                           c.resources.staff_catalog_slot,
                                                                           137);
        const auto order = random(c) & 1u;
        bool reached = route(c, s, release_point(c, order));
        if (!reached) {
            s.mobile.navigation.navigation_mode = 1;
            reached = route(c, s, release_point(c, order ^ 1u));
        }
        if (reached) {
            state(c, g, StaffState::walking_to_entrance);
            catch_person(c, person);
            return;
        }
        c.ports.diagnostics.log_debug_message(c, "Guard can't get to either release point - releasing kid\n");
    }
    abandon();
}

void face_toward(MapObjectBase& base, MobileEntityState& mobile, const MapObjectBase& target) {
    const auto x = static_cast<std::int32_t>(std::bit_cast<std::int16_t>(target.x))
                 - std::bit_cast<std::int16_t>(base.x),
               y = static_cast<std::int32_t>(std::bit_cast<std::int16_t>(target.y))
                 - std::bit_cast<std::int16_t>(base.y);
    if (x + y)
        mobile.sprite_angle = compute_world_sprite_angle_from_delta(-x, y);
}

void first_aid(StaffWorld& c, GuardRuntimeRecord& g) {
    auto& s = g.staff;
    decrease_staff_energy_and_happiness_during_task(c, s);
    const auto abandon = [&] {
        set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
        state(c, g, StaffState::idle);
        g.patient = 0;
    };
    auto* target = live_person(c, g.patient);
    if (!target) {
        abandon();
        return;
    }
    const auto remaining = fixed::word(g.first_aid_timestamp);
    --g.first_aid_timestamp;
    if (remaining <= 0) {
        abandon();
        return;
    }
    auto& patient = *target;
    if (patient.state != PersonState::first_aid_recovery) {
        abandon();
        return;
    }
    const auto progress = navigate(c, s);
    if (progress == NavigationResult::unreachable
        || (progress == NavigationResult::arrived && cell(s.base) != cell(patient.base))) {
        abandon();
        return;
    }
    if (cell(s.base) != cell(patient.base))
        return;
    if (sprite_animation_matches_resource_or_next_anim(c, s.mobile.animation, 33)) {
        face_toward(patient.base, patient.mobile, s.base);
        face_toward(s.base, s.mobile, patient.base);
        if (g.first_aid_timestamp == 1) {
            const auto remaining_sickness =
                fixed::subtract(byte(patient.needs.sickness), calculate_staff_work_value(c, &s));
            patient.happiness = percentage(patient.happiness);
            patient.needs.sickness = static_cast<float>(std::max(remaining_sickness, 0));
        }
    } else {
        set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
        set_sprite_animation_next(s.mobile.animation, 33);
        g.first_aid_timestamp = 10;
        patient.first_aid_timeout = 11;
    }
}

void escort(StaffWorld& c, GuardRuntimeRecord& g) {
    auto& s = g.staff;
    const auto progress = navigate(c, s);
    auto* target = live_person(c, g.perpetrator);
    if (target && progress == NavigationResult::moving)
        return;
    if (target) {
        set_sprite_animation_next(s.mobile.animation, 32);
        auto& person = *target;
        release_escorted_person(c, person, cell(s.base));
        if (person.base.header.kind == ThingKind::person && !person.base.header.retirement_pending) {
            person.happiness = 10;
            world_operations::set_mobile_entity_thought_bubble(c,
                                                               &person.mobile.thought_history,
                                                               MobileThoughtCode::unhappy,
                                                               false);
        }
    }
    g.perpetrator = 0;
    s.mobile.navigation.navigation_mode = 0;
    state(c, g, route(c, s, patrol_point(c)) ? StaffState::returning_to_patrol : StaffState::idle);
}
} //namespace

bool start_guard_prosecution(StaffWorld& c, GuardRuntimeRecord& g, ThingId target) {
    auto& s = g.staff;
    const auto activity = classify_staff_activity(s);
    auto* person = live_person(c, target);
    if (!person || is_world_staff_too_tired_for_task(c, s) || static_cast<std::uint32_t>(activity) > 1u)
        return false;
    c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                       s.mobile,
                                                                       c.resources.staff_catalog_slot,
                                                                       136);
    if (!route(c, s, cell(person->base)))
        return false;
    g.perpetrator = target;
    set_world_staff_state_by_type(c, s, StaffState::prosecuting);
    return true;
}

bool start_guard_first_aid(StaffWorld& c, GuardRuntimeRecord& g, ThingId target) {
    auto& s = g.staff;
    const auto activity = classify_staff_activity(s);
    auto* person = live_person(c, target);
    if (!person
        || is_world_staff_too_tired_for_task(c, s)
        || s.first_known_ability != ThemeAbility::first_aid
        || static_cast<std::uint32_t>(activity) > 1u)
        return false;
    c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                       s.mobile,
                                                                       c.resources.staff_catalog_slot,
                                                                       136);
    if (!route(c, s, cell(person->base)))
        return false;
    g.patient = target;
    set_world_staff_state_by_type(c, s, StaffState::first_aiding);
    return true;
}

void update_world_guard_idle_or_patrol_state(StaffWorld& c, GuardRuntimeRecord& g) {
    auto& s = g.staff;
    if (evaluate_world_staff_energy_and_activity(c, s))
        return;
    state(c,
          g,
          (world(c).saved.game_tick & 3u) && try_set_mobile_entity_destination(c, s.base, s.mobile)
              ? StaffState::patrolling
              : StaffState::idle);
}

void update_world_guard_state(StaffWorld& c, GuardRuntimeRecord& g) {
    auto& s = g.staff;
    match (s.state) {
        case StaffState::idle => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   167);
            start_task_after_idle_delay(c, s);
        }
        case StaffState::patrolling => {
            if (!(random(c) & 15u))
                c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                                   s.mobile,
                                                                                   c.resources.staff_catalog_slot,
                                                                                   166);
            if (navigate(c, s) == NavigationResult::moving)
                decrease_staff_energy_and_happiness_during_task(c, s);
            else
                update_world_guard_idle_or_patrol_state(c, g);
        }
        case StaffState::walking_to_rest_area => handle_staff_rest_area_arrival(c, s);
        case StaffState::resting              => recover_staff_energy_and_happiness_at_rest_area(c, s);
        case StaffState::prosecuting          => prosecute(c, g);
        case StaffState::walking_to_entrance  => escort(c, g);
        case StaffState::returning_to_patrol  => {
            if (navigate(c, s) != NavigationResult::moving)
                state(c, g, route(c, s, patrol_point(c)) ? StaffState::patrolling : StaffState::idle);
        }
        case StaffState::first_aiding => first_aid(c, g);
        case _                        => update_world_staff_activity_state(c, s);
    }
}
} //namespace simcoaster
