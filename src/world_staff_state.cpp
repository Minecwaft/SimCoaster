#include "simcoaster/resource_variant.hpp"
#include "world_staff_internal.hpp"
#include "simcoaster/world_mission_controller.hpp"

namespace simcoaster {
using namespace staff_detail;

StaffState world_staff_association_state(const StaffRuntimeRecord& staff) noexcept {
    auto state = staff.state;
    if (state == StaffState::frozen
        || state == StaffState::frozen_editing_patrol_area
        || state == StaffState::being_whinged_at)
        state = staff.saved_state;
    if (state == StaffState::training_paused)
        return StaffState::training;
    if (state == StaffState::walking_to_training_room_paused)
        return StaffState::walking_to_training_room;
    return state;
}

bool world_staff_has_hidden_base_sprite(const StaffRuntimeRecord& staff) noexcept {
    const auto state = world_staff_association_state(staff);
    return state == StaffState::resting
        || state == StaffState::training
        || state == StaffState::researching
        || state == StaffState::picked_up;
}

void
enter_world_staff_state(StaffWorld& c, StaffRuntimeRecord& s, StaffState requested, ThingId removed_training_room) {
    if (requested == StaffState::walking_to_obstacle && !resolve_mission_obstacle(c))
        requested = StaffState::idle;
    if (requested == StaffState::walking_to_training_room && !first_training_room(c)) {
        s.going_to_learn = ThemeAbility::none;
        requested = StaffState::idle;
    }
    const auto previous = s.state;
    const auto association = world_staff_association_state(s);
    const bool wrapper = requested == StaffState::frozen
                      || requested == StaffState::frozen_editing_patrol_area
                      || requested == StaffState::being_whinged_at;
    auto next_association = wrapper ? association : requested;
    if (next_association == StaffState::training_paused)
        next_association = StaffState::training;
    else if (next_association == StaffState::walking_to_training_room_paused)
        next_association = StaffState::walking_to_training_room;
    const bool hidden = world_staff_has_hidden_base_sprite(s);
    if (association != next_association) {
        //Native transitions can leave an occupied room through pickup, research
        //assignment, firing or demolition as well as the ordinary leave helper.
        //Detach once before another state/resource can replace that ownership.
        const bool cancelled_training =
            association == StaffState::training && s.currently_learning != ThemeAbility::none;
        if (association == StaffState::resting
            || association == StaffState::training
            || association == StaffState::researching) {
            detach_staff_room_occupancy(c, s, removed_training_room);
            //Entry may recursively choose another state (notably patrolling→rest).
            //Its old occupancy is already gone; do not let that recursion release
            //it again or mistake a newly assigned room for the departed one.
            s.state = StaffState::idle;
        }
        if (cancelled_training)
            restore_cancelled_training_availability(c, s);
        if ((association == StaffState::walking_to_rest_area || association == StaffState::waiting_for_empty_staff_room)
            && next_association != StaffState::resting
            && next_association != StaffState::walking_to_rest_area
            && next_association != StaffState::waiting_for_empty_staff_room)
            s.rest_area = 0;
        if (association == StaffState::walking_to_research_room
            && next_association != StaffState::researching
            && next_association != StaffState::walking_to_research_room)
            s.research_room = 0;
        if (association == StaffState::walking_to_training_room
            && next_association != StaffState::training
            && next_association != StaffState::walking_to_training_room) {
            s.currently_learning = ThemeAbility::none;
            s.going_to_learn = ThemeAbility::none;
        }
    }
    const bool remains_hidden = next_association == StaffState::resting
                             || next_association == StaffState::training
                             || next_association == StaffState::researching
                             || next_association == StaffState::picked_up;
    if (hidden && !remains_hidden && !s.mobile.animation.sprite_script)
        set_world_sprite_animation_resource(c,
                                            s.mobile.animation,
                                            3,
                                            static_cast<std::int32_t>(s.mobile.sprite_category),
                                            static_cast<std::int32_t>(s.mobile.sprite_id));
    match (requested) {
        case StaffState::idle or StaffState::waiting_for_empty_staff_room => {
            s.time_started_idling = previous == StaffState::patrolling ? world(c).saved.game_tick : 0;
            idle_animation(s);
        }
        case StaffState::patrolling => {
            if (try_send_staff_to_nearest_rest_area(c, s)) {
                enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
                return;
            }
            animation(c, s, 15);
        }
        case StaffState::walking_to_rest_area => animation(c, s, 1);
        case StaffState::resting              => {
            clear_thought(c, s);
            release_world_sprite_animation_resource_handle(c, s.mobile.animation);
        }
        case StaffState::walking_to_strike => {
            set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
            animation(c, s, 15);
        }
        case StaffState::striking => {
            s.mobile.sprite_angle = 0;
            set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
            animation(c, s, 27);
        }
        case StaffState::walking_to_obstacle => {
            const auto& g = get_park_object_balance(c, object(c, primary(c).saved.current_obstacle)).Obstacle;
            (void)route(c, s, static_cast<CellId>(g.EntranceX + (g.EntranceY << 7) + 1));
            s.mobile.navigation.navigation_mode = 1;
            set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
            animation(c, s, 1);
        }
        case StaffState::walking_to_training_room => {
            const auto room = first_training_room(c);
            if (!room) {
                s.going_to_learn = ThemeAbility::none;
                enter_world_staff_state(c, s, StaffState::idle);
                return;
            }
            const auto destination = object(c, room).entry_cell;
            if (!route(c, s, destination)) {
                s.mobile.navigation.navigation_mode = 1;
                (void)try_set_mobile_entity_path_to_cell(c, s.base, s.mobile, destination);
            }
            set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::boost_25);
            thought(c, s, MobileThoughtCode::staff_going_to_training);
            animation(c, s, 1);
        }
        case StaffState::training => {
            clear_thought(c, s);
            s.mobile.thought_history.last_thought = MobileThoughtCode::none;
            release_world_sprite_animation_resource_handle(c, s.mobile.animation);
            s.mobile.navigation.navigation_mode = 0;
            if (s.going_to_learn != ThemeAbility::none) {
                if (s.experience >= 10000u) {
                    s.currently_learning = s.going_to_learn;
                    s.ability_progress = 0;
                }
            } else {
                cancel_staff_training_request(c, s);
                return;
            }
        }
        case StaffState::stunned => {
            s.time_started_idling = world(c).saved.game_tick;
            animation(c, s, 28);
        }
        case StaffState::picked_up => {
            release_world_sprite_animation_resource_handle(c, s.mobile.animation);
            clear_thought(c, s);
            world_operations::begin_world_staff_pickup(c, s);
        }
        case StaffState::frozen or StaffState::frozen_editing_patrol_area => {
            //Occupancy uses the underlying association above, but resumption must
            //retain the actual state (including UI-paused training: 37/38).
            if (previous != StaffState::frozen && previous != StaffState::frozen_editing_patrol_area) {
                s.saved_state = previous;
                //Native skips saving TRAINING as well as its idle-animation changes.
                //The approved correction saves the session, retaining its pose/speed.
                if (previous != StaffState::training) {
                    s.mobile.sprite_angle = 0;
                    idle_animation(s);
                }
            }
        }
        case StaffState::being_whinged_at => {
            if (previous != StaffState::being_whinged_at && previous != StaffState::frozen) {
                s.saved_state = previous;
                s.state = requested;
                idle_animation(s);
                s.mobile.whinge_timeout = 10;
            }
        }
        case StaffState::researching => {
            s.time_started_researching = world(c).saved.game_tick;
            clear_thought(c, s);
            release_world_sprite_animation_resource_handle(c, s.mobile.animation);
        }
        case StaffState::walking_to_research_room => {
            thought(c, s, MobileThoughtCode::staff_going_to_research);
            animation(c, s, 1);
        }
        case StaffState::be_forced_to_destination => {
            s.mobile.base_speed = speed(c, s, mobile_base_speed_variants[4]);
            animation(c, s, 2);
        }
        case _ => ;
    } s.state = requested;
}

bool is_world_staff_too_tired_for_task(const StaffQuery& c, const StaffRuntimeRecord& s) {
    return byte(s.energy) < balance(c).RestLevel;
}

void decrease_staff_energy_and_happiness_during_task(const StaffQuery& c, StaffRuntimeRecord& s) {
    const auto fraction = 1.0 - static_cast<double>(s.experience) / balance(c).MaxExperience;
    s.energy = percentage(s.energy + fraction * static_cast<double>(-.1f));
    s.happiness_score = percentage(s.happiness_score + fraction * static_cast<double>(-.05f));
}

bool evaluate_world_staff_energy_and_activity(StaffWorld& c, StaffRuntimeRecord& s) {
    const auto& fixed = c.visitors.environment.FixedItemInfo;
    if (striking(c, s) && get_world_park_gates_rsse_slot1(c, world(c)) == 1) {
        s.mobile.navigation.navigation_mode = 1;
        const auto xcell = static_cast<std::uint8_t>(fixed.StrikeAreaStartX)
                         + bounded_random(c, static_cast<std::uint32_t>(fixed.StrikeAreaSizeX));
        const auto x = static_cast<std::uint16_t>((xcell << 8) | static_cast<std::uint8_t>(random(c)));
        const auto ycell = static_cast<std::uint8_t>(fixed.StrikeAreaStartY)
                         + bounded_random(c, static_cast<std::uint32_t>(fixed.StrikeAreaSizeY));
        const auto y = static_cast<std::uint16_t>((ycell << 8) | static_cast<std::uint8_t>(random(c)));
        if (route(c, s, x, y)) {
            enter_world_staff_state(c, s, StaffState::walking_to_strike);
            return true;
        }
    }
    const auto energy = byte(s.energy);
    if (energy > balance(c).RestLevel) {
        s.mobile.base_speed = speed(c, s, mobile_base_speed_variants[energy < 100 ? energy / 20 : 4]);
        const auto happiness = byte(s.happiness_score);
        if (happiness <= 10)
            thought(c, s, MobileThoughtCode::staff_angry);
        else if (happiness > 97 && !(random(c) & 15u))
            thought(c, s, MobileThoughtCode::staff_happy);
    } else {
        thought(c, s, MobileThoughtCode::staff_tired);
        const auto room = find_nearest_staff_rest_area(c, s);
        if (room && route(c, s, object(c, room).entry_cell)) {
            s.rest_area = room;
            enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
            return true;
        }
        if (!(random(c) & 15u))
            s.happiness_score = percentage(static_cast<double>(s.happiness_score) - balance(c).HappyHitCosNoRestArea);
        if (classify_staff_activity(s) == StaffActivityStatus::working) {
            enter_world_staff_state(c, s, StaffState::idle);
            return true;
        }
    }
    if (!s.research_mode || s.base.header.kind == ThingKind::researcher)
        return false;
    if (byte(s.energy) < balance(c).RestLevel) {
        const auto room = find_nearest_staff_rest_area(c, s);
        if (room && route(c, s, object(c, room).entry_cell)) {
            s.rest_area = room;
            enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
            return false;
        }
        enter_world_staff_state(c, s, StaffState::idle);
        return false;
    }
    if (select_first_thinking_space_object_if_pathable(c, s))
        enter_world_staff_state(c, s, StaffState::walking_to_research_room);
    return false;
}

//namespace simcoaster
void update_world_staff_activity_state(StaffWorld& c, StaffRuntimeRecord& s) {
    match (s.state) {
        case StaffState::walking_to_strike or StaffState::walking_to_obstacle => {
            if (s.state == StaffState::walking_to_obstacle && !resolve_mission_obstacle(c)) {
                enter_world_staff_state(c, s, StaffState::idle);
                return;
            }
            const auto state = s.state;
            const auto progress = navigate(c, s);
            thought(c, s, MobileThoughtCode::staff_going_on_strike);
            if (progress != NavigationResult::moving) {
                if (state == StaffState::walking_to_strike)
                    s.mobile.navigation.navigation_mode = 0;
                enter_world_staff_state(c,
                                        s,
                                        state == StaffState::walking_to_strike ? StaffState::striking
                                                                               : StaffState::idle);
            }
        }
        case StaffState::striking                 => update_world_staff_striking_behavior(c, s);
        case StaffState::walking_to_training_room => update_staff_training_travel_state(c, s);
        case StaffState::training                 => update_staff_training_session(c, s);
        case StaffState::stunned                  => {
            const auto duration = static_cast<std::uint32_t>(
                integer((1.0 - static_cast<double>(s.experience) / balance(c).MaxExperience) * 50.0));
            if (world(c).saved.game_tick - s.time_started_idling > duration)
                enter_world_staff_state(c, s, StaffState::idle);
        }
        case StaffState::frozen or StaffState::frozen_editing_patrol_area => restore_saved_state(c, s);
        case StaffState::being_whinged_at                                 => {
            --s.mobile.whinge_timeout;
            if (fixed::word(s.mobile.whinge_timeout) <= 0) {
                s.state = s.saved_state;
                if (s.saved_state == StaffState::being_whinged_at)
                    enter_world_staff_state(c, s, StaffState::idle);
            } else
                s.happiness_score = percentage(static_cast<double>(s.happiness_score) - 1.0);
        }
        case StaffState::researching => {
            const auto fraction =
                static_cast<float>(1.0 - static_cast<double>(s.experience) / balance(c).MaxExperience);
            const auto energy_delta = static_cast<float>(static_cast<double>(fraction) * static_cast<double>(-.1f));
            s.energy = percentage(static_cast<double>(s.energy) + energy_delta);
            s.happiness_score = percentage(s.happiness_score + fraction * static_cast<double>(-.05f));
            if (byte(s.energy) <= balance(c).RestLevel) {
                enter_world_staff_state(c, s, StaffState::idle);
                choose_staff_task_or_rest_area(c, s);
            }
        }
        case StaffState::walking_to_research_room     => arrive_at_research_room(c, s);
        case StaffState::waiting_for_empty_staff_room => seek_staff_rest_area(c, s);
        case StaffState::be_forced_to_destination     => {
            const auto progress = navigate(c, s);
            if (progress == NavigationResult::arrived)
                set_world_staff_state_by_type(c, s, s.forced_state);
            else if (progress == NavigationResult::unreachable)
                choose_staff_task_or_rest_area(c, s);
        }
        case _ => ;
    }
}

void choose_staff_task_or_rest_area(StaffWorld& c, StaffRuntimeRecord& s) {
    match (s.base.header.kind) {
        case ThingKind::mechanic   => choose_mechanic_repair_task(c, reinterpret_cast<MechanicRuntimeRecord&>(s));
        case ThingKind::researcher => start_world_researcher_task(c, s);
        case ThingKind::handyman   => choose_and_start_handyman_task(c, reinterpret_cast<HandymanRuntimeRecord&>(s));
        case ThingKind::entertainer =>
            update_entertainer_idle_and_work_state(c, reinterpret_cast<EntertainerRuntimeRecord&>(s));
        case ThingKind::guard => update_world_guard_idle_or_patrol_state(c, reinterpret_cast<GuardRuntimeRecord&>(s));
        case ThingKind::gardener => start_world_gardener_task(c, reinterpret_cast<GardenerRuntimeRecord&>(s));
        case _                   => ;
    } const auto activity = classify_staff_activity(s);
    if (byte(s.motivation) < c.staff.recruitment.balance.Strike.MotivationPercentToStrike / 2
        && (activity == StaffActivityStatus::working || activity == StaffActivityStatus::researching)) {
        const auto room = find_nearest_staff_rest_area(c, s);
        if (room && route(c, s, object(c, room).entry_cell)) {
            s.rest_area = room;
            enter_world_staff_state(c, s, StaffState::walking_to_rest_area);
            return;
        }
        enter_world_staff_state(c, s, StaffState::idle);
    }
}

namespace staff_detail {
void start_task_after_idle_delay(StaffWorld& c, StaffRuntimeRecord& s) {
    const auto delay = static_cast<std::uint32_t>(
        integer((1.0 - static_cast<double>(s.experience) / balance(c).MaxExperience) * -50.0));
    if (world(c).saved.game_tick > s.time_started_idling - delay)
        choose_staff_task_or_rest_area(c, s);
}

void restore_saved_state(StaffWorld& c, StaffRuntimeRecord& s) {
    if (s.state == StaffState::frozen_editing_patrol_area || (world(c).saved.game_tick & 3u))
        return;
    //Re-entering TRAINING runs the arrival effects again and clears progress.
    //A temporary freeze already retains its room and hidden sprite ownership.
    if (s.saved_state == StaffState::training)
        s.state = StaffState::training;
    else
        set_world_staff_state_by_type(c, s, s.saved_state);
}
} //namespace staff_detail
} //namespace simcoaster
