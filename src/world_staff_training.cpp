#include "world_staff_internal.hpp"
#include "world_staff_mail.hpp"
#include <memory>

namespace simcoaster {
using namespace staff_detail;

namespace staff_detail {
void queue_action(StaffWorld& c, std::int32_t action, bool use_primary) {
    const auto candidate = world_operations::make_advisor_action_candidate(c, action);
    auto& owner = use_primary ? primary(c) : world(c);
    (void)world_operations::queue_advisor_action_candidate(c,
                                                           &record<AdvisorRecord>(c, owner.saved.advisor),
                                                           candidate);
}

void post_staff_mail(StaffWorld& c, StaffRuntimeRecord& s, CompanyEmailMessageKind kind, std::uint32_t argument) {
    auto* storage = static_cast<CompanyEmailMessage*>(
        c.ports.memory.allocate(Allocation::event_payload, sizeof(CompanyEmailMessage)));
    if (!storage)
        throw std::bad_alloc();
    auto* email = std::construct_at(storage);
    try {
        staff_mail_detail::initialize(c, *email, s, kind, argument);
    } catch (...) {
        std::destroy_at(email);
        c.ports.memory.release(Allocation::event_payload, email);
        throw;
    }
    dispatch_heap_payload_event(c, email);
}

void post_training_mail(StaffWorld& c, StaffRuntimeRecord& s, bool completed, ThemeAbility ability) {
    post_staff_mail(c,
                    s,
                    completed ? CompanyEmailMessageKind::staff_training_complete
                              : CompanyEmailMessageKind::staff_training_cancelled,
                    static_cast<std::uint32_t>(ability));
}
} //namespace staff_detail

//namespace staff_detail
void update_staff_training_travel_state(StaffWorld& c, StaffRuntimeRecord& s) {
    const auto progress = navigate(c, s);
    if (progress == NavigationResult::unreachable) {
        const auto choice = next_crt_random(c) % 50;
        if (choice == 0 || choice == 49)
            thought(c, s, MobileThoughtCode::staff_going_to_training);
        else if (choice == 25)
            thought(c, s, MobileThoughtCode::staff_confused);
        s.happiness_score = percentage(static_cast<double>(s.happiness_score) - balance(c).HappyHitCosRoomInaccessible);
    } else if (progress == NavigationResult::arrived) {
        const auto room = first_training_room(c);
        if (!room) {
            cancel_staff_training_request(c, s);
            return;
        }
        if (s.going_to_learn == ThemeAbility::none) {
            cancel_staff_training_request(c, s);
            return;
        }
        auto* state = script(c, object(c, room));
        world_operations::set_script_slot(c, state, 0, fixed::add(world_operations::script_slot(c, state, 0), 1));
        enter_world_staff_state(c, s, StaffState::training);
        if (world_staff_association_state(s) == StaffState::training)
            notify(c, WorldEvent{ StaffTrainingStartedEvent{ s.base.header.id } });
    }
}

void finish_world_staff_training(StaffWorld& c, StaffRuntimeRecord& s) {
    notify(c, WorldEvent{ StaffTrainingCompletedEvent{ s.base.header.id } });
    post_training_mail(c, s, true, s.first_known_ability);
    enter_world_staff_state(c, s, StaffState::idle);
}

void update_staff_training_session(StaffWorld& c, StaffRuntimeRecord& s) {
    s.ability_progress += static_cast<std::uint32_t>(balance(c).SkillIncrement);
    if (fixed::word(s.ability_progress) < balance(c).ProgessUpperLimit)
        return;
    complete_world_staff_ability_learning(s);
    queue_action(c, 373);
    finish_world_staff_training(c, s);
    s.motivation = 100;
    s.experience += static_cast<std::uint32_t>(balance(c).TrainingExtraExperience);
    const auto modifier = c.staff.recruitment.balance.agencies.at(s.agency).TrainingExperienceGainModifier;
    if (modifier && s.experience)
        s.experience += s.experience * static_cast<std::uint32_t>(modifier) / 100u;
    if (s.experience > static_cast<std::uint32_t>(balance(c).MaxExperience))
        s.experience = static_cast<std::uint32_t>(balance(c).MaxExperience);
    update_objectives_panel_and_queue_mission_advisor_action(c);
    choose_staff_task_or_rest_area(c, s);
}

void cancel_staff_training_request(StaffWorld& c, StaffRuntimeRecord& s, ThingId removed_training_room) {
    const auto association = world_staff_association_state(s);
    const auto ability = s.currently_learning != ThemeAbility::none ? s.currently_learning : s.going_to_learn;
    if (association != StaffState::training
        && association != StaffState::walking_to_training_room
        && ability == ThemeAbility::none)
        return;
    //Native 62E7BD ignores travelling and UI-paused trainees, and 62E859 can
    //debit a different room after demolition. Cancel the actual association;
    //only employees who arrived own an occupancy/count to release.
    enter_world_staff_state(c, s, StaffState::idle, removed_training_room);
    s.currently_learning = ThemeAbility::none;
    s.going_to_learn = ThemeAbility::none;
    if (ability != ThemeAbility::none) {
        queue_action(c, 457, false);
        post_training_mail(c, s, false, ability);
    }
    choose_staff_task_or_rest_area(c, s);
}

void update_world_staff_striking_behavior(StaffWorld& c, StaffRuntimeRecord& s) {
    const auto draw = static_cast<std::uint8_t>(random(c));
    if (!(draw & 7u))
        s.mobile.sprite_angle = std::min(s.mobile.sprite_angle + draw - 128u, 2047u);
    const auto category = world_staff_type_for_used_thing(s.base.header);
    if (world(c).saved.park_closed
        && !get_cached_eligible_person_record_count(c, record<ParkAnalyserState>(c, world(c).saved.park_analyser), 4)) {
        hq(c).strikes.at(static_cast<std::size_t>(category)).strike_active = 0;
        notify(c, WorldEvent{ StaffStrikeEvent{ category, 5, 0 } });
    }
    if (!striking(c, s)) {
        const auto& f = c.visitors.environment.FixedItemInfo;
        if (route(c, s, static_cast<CellId>(f.EntranceAPosX + (f.EntranceAPosY << 7) + 1))) {
            thought(c, s, MobileThoughtCode::staff_going_to_job);
            enter_world_staff_state(c, s, StaffState::patrolling);
        } else
            enter_world_staff_state(c, s, StaffState::idle);
    }
}
} //namespace simcoaster
