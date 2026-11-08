#include "simcoaster/require_bound.hpp"
#include "world_person_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <stdexcept>

namespace simcoaster {
namespace {
ParkObjectRuntimeView& destination(VisitorWorld& c, const PersonRuntimeView& person) {
    auto* record = c.entities.at(person.major_destination).payload;
    if (!record) {
        throw std::logic_error("Person destination is missing");
    }
    return *reinterpret_cast<ParkObjectRuntimeView*>(record);
}

ResearchCatalogDefinition& definition(VisitorWorld& c, const ParkObjectRuntimeView& object) {
    return require_bound<std::out_of_range>(get_park_object_definition(c, &object), "Person destination definition");
}

ChallengeManagerRecord& challenges(VisitorWorld& c) {
    auto* record = c.entities.at(person_detail::base_world(c).saved.challenge_manager).payload;
    if (!record) {
        throw std::logic_error("Person challenge manager is missing");
    }
    return *reinterpret_cast<ChallengeManagerRecord*>(record);
}

bool handles_sprite(const ParkObjectRuntimeView& object) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::ride_handles_sprite)) != 0;
}
} //namespace

void world_operations::builtin::enter_world_person_state(WorldRuntime& c, PersonRuntimeView* person, PersonState state) {
    if (!person) {
        throw std::invalid_argument("null Person state receiver");
    }
    simcoaster::enter_world_person_state(c, *person, state);
}

void world_operations::builtin::release_mobile_entity_sprite_and_optional_person_effects(WorldRuntime& c, PersonRuntimeView& person, bool optional) {
    simcoaster::release_mobile_entity_sprite_and_optional_person_effects(c, person.base, person.mobile, optional);
}

void release_world_person_umbrella_resource(VisitorWorld& c, PersonRuntimeView& person) {
    if (person.umbrella_script) {
        const auto handle = person.umbrella_script;
        person.umbrella_state = PersonUmbrellaState::inactive;
        world_operations::release_resource_pool_handle(c, handle);
        person.umbrella_script = 0;
    }
}

void release_and_clear_mobile_entity_thought_script_handle(VisitorWorld& c, MobileEntityThoughtHistory& thought) {
    if (thought.thought_script) {
        world_operations::release_resource_pool_handle(c, thought.thought_script);
        thought.thought_script = 0;
    }
}

void release_mobile_entity_sprite_and_optional_person_effects(VisitorWorld& c, MapObjectBase& base, MobileEntityState& mobile, bool optional) {
    release_world_sprite_animation_resource_handle(c, mobile.animation);
    if (!optional) {
        return;
    }
    release_and_clear_mobile_entity_thought_script_handle(c, mobile.thought_history);
    if (base.header.kind != ThingKind::person) {
        return;
    }
    auto& person = *reinterpret_cast<PersonRuntimeView*>(&base);
    if (person.balloon_script || person.remaining_balloon_life) {
        world_operations::release_resource_pool_handle(c, person.balloon_script);
        person.balloon_script = 0;
    }
    if (person.umbrella_state != PersonUmbrellaState::none) {
        release_world_person_umbrella_resource(c, person);
    }
}

CellId get_ticket_booth_cell(const PersonEnvironmentBalance& balance, std::size_t index) {
    const auto& booth = balance.TicketBooth.at(index);
    return static_cast<CellId>((static_cast<std::uint32_t>(booth.PosY) << 7) + static_cast<std::uint32_t>(booth.PosX) + 1u);
}

bool roll_type3_object_chance(VisitorWorld& c, ParkObjectRuntimeView& object) {
    (void)get_park_object_definition(c, &object);
    //The original tests only the low byte, includes equality, and consumes the
    //primary World's generator even when the caller is using another World.
    const auto threshold = static_cast<std::uint8_t>(object.chance_of_winning);
    const bool result = threshold >= next_world_prng_value(person_detail::primary_world(c)) % 100u;
    auto* script = world_operations::find_script(c, object.script_id);
    world_operations::set_script_slot(c, script, 11, result);
    return result;
}

void add_progress_to_matching_world_challenges(ChallengeManagerRecord& manager, ChallengeType type, std::int32_t delta, ThingId target, bool secondary) {
    for (auto& record : manager.records) {
        if ((record.status == ChallengeStatus::preparing || record.status == ChallengeStatus::active) && record.type == type && (!target || record.target_object == target)) {
            auto& value = secondary ? record.value2 : record.value;
            value = fixed::add(value, delta);
        }
    }
}

void record_person_departure_visit_duration(VisitorWorld& c, ParkAnalyserState& analyser, std::uint32_t elapsed) {
    const auto delta = scale_world_ticks_to_filetime_delta(person_detail::primary_world(c).calendar, elapsed);
    const auto days = static_cast<std::uint32_t>(std::bit_cast<std::int64_t>(delta) / 864000000000ll);
    auto& history = analyser.medium_history;
    history.current_entry = fixed::add(history.current_entry, 1);
    if (history.current_entry >= history.num_entries) {
        history.current_entry = 0;
        history.wrapped = 1;
    }
    if (history.current_entry < 0) {
        throw std::out_of_range("negative Person visit-history index");
    }
    history.data.at(static_cast<std::size_t>(history.current_entry)) = days;
}

void enter_world_person_state(VisitorWorld& c, PersonRuntimeView& person, PersonState state) {
    person.state = state;
    auto& animation = person.mobile.animation;
    match (state) {
        case PersonState::initial_route or PersonState::walk_to_entrance or PersonState::cross_entrance or PersonState::use_litter_bin or PersonState::travel_to_object_queue or PersonState::admission_step or PersonState::crossing_road_to_leave => {
            set_sprite_animation_next(animation, 1);
            return;
        }
        case PersonState::select_entrance or PersonState::roaming or PersonState::waiting_to_cross_road => {
            set_sprite_animation_next(animation, 3);
            return;
        }
        case PersonState::wait_for_park_open => {
            set_sprite_animation_next(animation, 3);
            person.park_opening_waiting_time = next_world_prng_value(person_detail::base_world(c)) % 150u + 200u;
            return;
        }
        case PersonState::admission_response => {
            for (std::int32_t i = 0; i < c.visitors.environment.FixedItemInfo.NumTicketBooths; ++i) {
                if (person_detail::cell_id(person.base) == get_ticket_booth_cell(c.visitors.environment, static_cast<std::size_t>(i))) {
                    person.mobile.sprite_angle = static_cast<std::uint32_t>(c.visitors.environment.TicketBooth.at(static_cast<std::size_t>(i)).Angle);
                    set_sprite_animation_next(animation, 3);
                    break;
                }
            }
            return;
        }
        case PersonState::travel_to_destination => {
            if (person.mobile.destination_set_successfully) {
                request_mobile_entity_path_to_destination(c, person.base, person.mobile, person.mobile.accurate_destination_x, person.mobile.accurate_destination_y);
            }
            set_sprite_animation_next(animation, 1);
            return;
        }
        case PersonState::temporary_appearance => {
            person.time_of_last_spot_animation = person_detail::base_world(c).saved.game_tick;
            return;
        }
        case PersonState::wait_in_object_queue => {
            set_sprite_animation_next(animation, 3);
            person.time_started_idling = person_detail::base_world(c).saved.game_tick;
            person.queue_move_delay = static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<double>(person.queue_position) * 1.2));
            return;
        }
        case PersonState::move_to_queue_position => {
            set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::normal, person.umbrella_state);
            set_sprite_animation_next(animation, 1);
            return;
        }
        case PersonState::wait_for_object_script => {
            set_sprite_animation_next(animation, 3);
            person.queue_position = static_cast<std::uint8_t>(roll_type3_object_chance(c, destination(c, person)));
            return;
        }
        case PersonState::after_object_exit => {
            if (!handles_sprite(destination(c, person))) {
                release_world_sprite_animation_resource_handle(c, animation);
            }
            if (animation.sprite_script) {
                set_sprite_animation_next(animation, 1);
            } else {
                set_world_sprite_animation_resource(c, animation, 1, static_cast<std::int32_t>(person.mobile.sprite_category), static_cast<std::int32_t>(person.mobile.sprite_id));
            }
            person.mobile.sprite_under_ride_control = 0;
            person.mobile.no_floating_things_on_ride = 0;
            if (definition(c, destination(c, person)).requires_teleport) {
                update_mobile_entity_render_pose(c, person.base, person.mobile, 0);
                (void)create_runtime_template_at_mobile_entity_resource_position(c, person.mobile, 19, 0, 0);
            }
            if ((person.balloon_script || person.remaining_balloon_life) && definition(c, destination(c, person)).appearance_effect != PersonAppearanceEffect::balloon) {
                person.balloon_script = world_operations::allocate_resource_variant(c, { ResourceProgramFamily::balloon_idle, 0 }, { 14, 0, person.balloon_colour, 0, 0, 0 });
            }
            return;
        }
        case PersonState::object_interaction => {
            const auto& object_definition = definition(c, destination(c, person));
            if (object_definition.requires_teleport) {
                (void)create_runtime_template_at_mobile_entity_resource_position(c, person.mobile, 19, 0, 0);
            }
            release_and_clear_mobile_entity_thought_script_handle(c, person.mobile.thought_history);
            if (person.balloon_script || person.remaining_balloon_life) {
                if (object_definition.appearance_effect == PersonAppearanceEffect::umbrella) {
                    discard_person_balloon(c, person, true);
                } else {
                    world_operations::release_resource_pool_handle(c, person.balloon_script);
                    person.balloon_script = 0;
                }
            }
            if (person.umbrella_state != PersonUmbrellaState::none) {
                release_world_person_umbrella_resource(c, person);
            }
            person.mobile.no_floating_things_on_ride = 1;
            if (handles_sprite(destination(c, person))) {
                person.mobile.sprite_under_ride_control = 1;
                set_sprite_animation_next(animation, 3);
            } else {
                release_mobile_entity_sprite_and_optional_person_effects(c, person.base, person.mobile, false);
            }
            auto& object = destination(c, person);
            auto target = object.definition_id;
            for (auto channel : { 17, 52, 58 }) {
                add_progress_to_matching_world_challenges(challenges(c), static_cast<ChallengeType>(channel), 1, target, false);
            }
            const bool photo = definition(c, object).is_photo_booth != 0;
            if (photo) {
                //This relation is mUpgradeParent, not the separate mPhotoBooth field.
                auto* related = c.entities.at(object.upgrade_parent).payload;
                if (!related) {
                    throw std::logic_error("photo-booth related object is missing");
                }
                target = reinterpret_cast<const ParkObjectRuntimeView*>(related)->definition_id;
            }
            add_progress_to_matching_world_challenges(challenges(c), static_cast<ChallengeType>(57), 1, target, photo);
            return;
        }
        case PersonState::caught_by_guard => {
            release_and_clear_mobile_entity_thought_script_handle(c, person.mobile.thought_history);
            person.major_destination = 0;
            if (person.balloon_script || person.remaining_balloon_life) {
                discard_person_balloon(c, person, false);
            }
            if (person.umbrella_state != PersonUmbrellaState::none) {
                release_world_person_umbrella_resource(c, person);
            }
            release_world_sprite_animation_resource_handle(c, animation);
            return;
        }
        case PersonState::walk_to_exit => {
            person.major_destination = 0;
            set_sprite_animation_next(animation, 1);
            return;
        }
        case PersonState::listen_to_vicious_rumours => {
            //Entry really overwrites the saved state with this same state. The tick
            //dispatcher later recognizes that value and falls back to roaming.
            person.saved_state = PersonState::listen_to_vicious_rumours;
            person.state = PersonState::listen_to_vicious_rumours;
            set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::normal, person.umbrella_state);
            set_sprite_animation_next(animation, 3);
            person.mobile.whinge_timeout = 10;
            return;
        }
        case PersonState::first_aid_recovery => {
            set_sprite_animation_next(animation, 3);
            person.first_aid_timeout = 100;
            return;
        }
        case PersonState::departure_routing => {
            if (person.paid_admission) {
                auto* analyser = c.entities.at(person_detail::base_world(c).saved.park_analyser).payload;
                if (!analyser) {
                    throw std::logic_error("Person departure analyser is missing");
                }
                record_person_departure_visit_duration(c, *reinterpret_cast<ParkAnalyserState*>(analyser), 4u * ((person_detail::base_world(c).saved.game_tick - person.arrival_date) >> 2));
            }
            set_sprite_animation_next(animation, 3);
            return;
        }
        case _ => return;
    }
}
} //namespace simcoaster
