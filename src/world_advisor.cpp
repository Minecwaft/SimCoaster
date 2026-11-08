#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_calendar.hpp"
#include "advisor_action_catalog.hpp"
#include "advisor_scalar_scores.hpp"
#include "advisor_response_catalog.hpp"
#include "coaster_editor_internal.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace simcoaster {
void queue_world_advisor_help_action(AdvisorWorld& c, std::int32_t action) {
    const auto advisor = [&]() -> AdvisorRecord& {
        const auto* world = c.referenced_world();
        if (!world)
            throw std::logic_error("Help has no primary World");
        const auto id = world->saved.advisor;
        auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
        if (!header || header->kind != ThingKind::advisor)
            throw std::logic_error("Help has no live Advisor");
        return *reinterpret_cast<AdvisorRecord*>(header);
    };
    advisor().from_ui_panel = 0;
    advisor().from_help_button = 1;
    try {
        const auto candidate = world_operations::make_advisor_action_candidate(c, action);
        //Re-resolve after candidate scoring, as native503A8E does.
        (void)world_operations::queue_advisor_action_candidate(c, &advisor(), candidate);
    } catch (...) {
        advisor().from_help_button = 0;
        throw;
    }
    advisor().from_help_button = 0;
}

namespace {
const advisor_catalog::Descriptor* descriptor(std::int32_t action) {
    if (action == 645)
        return nullptr;
    const auto found = std::ranges::find(advisor_catalog::descriptors, action, &advisor_catalog::Descriptor::action);
    return found == advisor_catalog::descriptors.end() ? nullptr : &*found;
}

void ensure_policy(AdvisorWorld& c) {
    if (!(c.advisor.advisor_candidate_initialized & 1u)) {
        c.advisor.advisor_candidate_initialized |= 1u;
        world_operations::load_advisor_behavior_configuration(c);
    }
}

AdvisorActionHistoryEntry& history(AdvisorWorld& c, std::int32_t action) {
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (action == 645)
        return c.advisor.no_action_history;
    return manager.history.at(static_cast<std::size_t>(action));
}

bool allowed(AdvisorWorld& c, const advisor_catalog::Descriptor& action) {
    auto& view = c.advisor.presentation;
    if (!view.mode_initialized) {
        view.mode_initialized = true;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3)
        return action.context == 3;
    if (view.map_source_recreation)
        return action.context == 5;
    if (view.map_source_view)
        return action.context == 7;
    if (has_active_world_animation_target(c)) {
        const auto* world = c.referenced_world();
        if (!world)
            throw std::logic_error("advisor animation World");
        const auto* payload = c.entities.at(world->saved.animation_manager).payload;
        if (!payload)
            throw std::logic_error("advisor animation manager");
        const auto* animation = reinterpret_cast<const AnimationManagerRecord*>(payload);
        return (is_world_animation_profile_active(c, animation, AnimationPlaybackProfile::code_1)
                && action.context == 5)
            || (is_world_animation_profile_active(c, animation, AnimationPlaybackProfile::code_2)
                && action.context == 4)
            || (is_world_animation_profile_active(c, animation, AnimationPlaybackProfile::code_0)
                && action.context == 6);
    }
    return action.context == 2
        || (action.context == 0 && c.mode.game == GameMode::code_0)
        || (action.context == 1 && c.mode.game == GameMode::code_2);
}

std::uint32_t unscaled_clock(AdvisorWorld& c) {
    if (!c.resources.meshes)
        throw std::logic_error("advisor unscaled clock is not bound");
    return static_cast<std::uint32_t>(c.resources.meshes->clocks.unscaled.count());
}

World& primary_world(AdvisorWorld& c) {
    return require_bound(c.referenced_world(), "advisor primary World is not bound");
}

World& current_world(AdvisorWorld& c) {
    return require_bound(c.simulation.current, "advisor current World is not bound");
}

AdvisorRecord& primary_advisor(AdvisorWorld& c) {
    auto* value = c.entities.at(primary_world(c).saved.advisor).payload;
    if (!value)
        throw std::logic_error("advisor primary record is not bound");
    return *reinterpret_cast<AdvisorRecord*>(value);
}

//Both callers pass minimum byte100 == 0. Its unsigned-byte comparison is
//therefore always true; counting does not depend on that unresolved field.
int count_qualifying_objects(AdvisorWorld& c, ObjectUiCategory category) {
    int count = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (row->payload->kind != ThingKind::park_object)
            continue;
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
        const auto* definition = get_park_object_definition(c, &object);
        if (!definition)
            throw std::logic_error("advisor park-object definition");
        if (definition->which_ui_type == category && is_special_ride_group_id_unregistered(c, object.base.header.id))
            ++count;
    }
    return count;
}

std::int32_t score_open_park(AdvisorWorld& c) {
    if (!current_world(c).saved.park_closed)
        return 0;
    const bool rides = count_qualifying_objects(c, ObjectUiCategory::rides) > 0;
    const bool sideshows = count_qualifying_objects(c, ObjectUiCategory::sideshows) > 0;
    if (!rides && !sideshows)
        return 0;
    return fixed::word(static_cast<std::uint32_t>(c.advisor.balance->OpenPark.ScorePerWaitingPerson)
                       * static_cast<std::uint32_t>(count_people_waiting_at_ticket_booths(c)));
}

std::int32_t score_build_rides(AdvisorWorld& c) {
    if (!c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter || c.construction.mode)
        return 0;
    std::uint32_t live_definitions = 0;
    for (auto* definition = first_object_catalog_entry(&c.resources.object_catalog); definition;
         definition = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (definition->which_ui_type != ObjectUiCategory::rides)
            continue;
        const auto* control = find_world_object_control(current_world(c).object_controls, definition->id);
        if (!control)
            throw std::logic_error("advisor ride definition control");
        if (fixed::word(control->live_instance_count) > 0)
            ++live_definitions;
    }
    if (live_definitions)
        return 0;
    const auto elapsed =
        scale_world_ticks_to_filetime_delta(primary_world(c).calendar, current_world(c).saved.game_tick);
    const auto days = std::bit_cast<std::int64_t>(elapsed) / 864000000000ll;
    const auto& policy = c.advisor.balance->RidesBuildRides;
    return fixed::word(static_cast<std::uint32_t>(policy.Score)
                       + static_cast<std::uint32_t>(policy.AdditionalPerDay) * static_cast<std::uint32_t>(days));
}

bool timing_group_expired(AdvisorWorld& c, const advisor_catalog::Descriptor& row) {
    if (row.timing_group == -1)
        return false;
    ensure_policy(c);
    if (!c.advisor.balance)
        throw std::logic_error("advisor timing balance");
    const auto group = static_cast<std::size_t>(row.timing_group);
    const auto& policy = c.advisor.balance->GroupTimes.at(group);
    if (fixed::word(c.timing.first_hour_time / 60u) < policy.MinTimeUntilFirstMessage)
        return false;
    const auto elapsed =
        (current_world(c).saved.game_tick >> 2) - (c.advisor.behavior.group_last_played_world_tick.at(group) >> 2);
    //The native predicate really is greater-than. True suppresses a scan row.
    return elapsed > static_cast<std::uint32_t>(policy.MinTimeForMessage);
}
} //namespace

void mark_advisor_context_menu_used(AdvisorWorld& c) {
    get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[4] = 0;
}

std::int32_t count_people_waiting_at_ticket_booths(AdvisorWorld& c) {
    auto& world = current_world(c);
    std::uint32_t count = 0;
    for (std::int32_t booth = 0; booth < c.visitors.environment.FixedItemInfo.NumTicketBooths; ++booth) {
        const auto cell = get_ticket_booth_cell(c.visitors.environment, static_cast<std::size_t>(booth));
        for (auto id = world.map_cells.at(static_cast<std::size_t>(cell) - 1).who; id;) {
            const auto* header = c.entities.at(id).payload;
            if (!header)
                throw std::logic_error("advisor entrance cell record");
            const auto& object = *reinterpret_cast<const MapObjectBase*>(header);
            if (header->kind == ThingKind::person
                && reinterpret_cast<const PersonRuntimeView*>(header)->state == PersonState::wait_for_park_open)
                ++count;
            id = object.map_child;
        }
    }
    auto* header = c.entities.at(primary_world(c).saved.park_analyser).payload;
    if (!header)
        throw std::logic_error("advisor park analyser");
    //This is the third global AnalyserMetric in the recovered save layout.
    reinterpret_cast<ParkAnalyserState*>(header)->metrics[2] = { world.saved.game_tick, count, 1 };
    return fixed::word(count);
}

void world_operations::builtin::load_advisor_behavior_configuration(WorldRuntime& c) {
    //Publish the receiver before I/O, as the native lazy initialization does.
    //The reflected schema includes all260 groups, not just queue policy.
    if (!c.advisor.balance)
        c.advisor.balance = std::make_shared<AdvisorBalance>();
    const auto path =
        (std::filesystem::path{ c.source.data_directory } / "Advisor/Advisor.sam").native_encoded_string();
    const auto text = c.ports.assets.read_balance_asset(c, path);
    if (!text)
        throw std::runtime_error("Advisor.sam is missing");
    const auto parsed = sam::parse_record<AdvisorBalance, true>(*c.advisor.balance, *text);
    if (!parsed)
        throw std::runtime_error("Invalid advisor balance at byte " + std::to_string(parsed.error().offset));
    c.advisor.queue_policy.general = c.advisor.balance->GeneralAdvisor;
    c.advisor.queue_policy.groups = c.advisor.balance->MessageGroups;
    c.advisor.queue_policy.group_count = c.advisor.balance->MessageGroups_count;
    const auto visitor = load_visitor_advisor_balance(c.visitors.visitor_advisor_balance, *text);
    if (!visitor)
        throw std::runtime_error("Invalid advisor visitor policy at byte " + std::to_string(visitor.error().offset));
}

std::int32_t world_operations::builtin::evaluate_advisor_action_score(WorldRuntime& c, std::int32_t action) {
    const auto* row = descriptor(action);
    if (!row || !allowed(c, *row))
        return -1;
    const auto fixed = std::ranges::find(advisor_catalog::fixed_scores, action, &advisor_catalog::FixedScore::action);
    if (fixed != advisor_catalog::fixed_scores.end())
        return fixed->score;
    ensure_policy(c);
    if (!c.advisor.balance)
        throw std::logic_error("Advisor balance receiver is not bound");
    if (const auto score = advisor_catalog::scalar_score(*c.advisor.balance, action))
        return *score;
    if (const auto score = evaluate_advisor_staff_and_facility_score(c, action))
        return *score;
    if (const auto score = evaluate_advisor_trade_score(c, action))
        return *score;
    if (const auto score = evaluate_advisor_staff_decision_score(c, action))
        return *score;
    if (const auto score = evaluate_advisor_park_runtime_score(c, action))
        return *score;
    if (const auto score = evaluate_advisor_pricing_score(c, action))
        return *score;
    //Native726F50/726FA0: objective reminders are event-only, but still
    //require the real completion predicate and their own inactive history row.
    //There is no FirstHour or UI-selection gate in either callback.
    if (action == 483 || action == 455)
        return !is_advisor_action_active(c, action) && can_proceed_with_current_objective(c)
                 ? fixed::word(2u * static_cast<std::uint32_t>(c.advisor.balance->Challenge.Score))
                 : 0;
    if (action == 381) {
        //Native7261F0 still performs this gated history read, but its score
        //target71EA30 is an actual XOR EAX,EAX/RET body, not an unported leaf.
        if (c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter && !c.construction.mode)
            (void)history(c, 381).play_count;
        return 0;
    }
    if (action == 61 || action == 62) {
        if (!c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter || c.construction.mode)
            return 0;
        const auto& policy = *c.advisor.balance;
        if (action == 61) {
            const auto elapsed =
                scale_world_ticks_to_filetime_delta(primary_world(c).calendar, current_world(c).saved.game_tick);
            if (std::bit_cast<std::int64_t>(elapsed) / 25920000000000ll
                < policy.RidesNoUpgrades.MonthsIntoGameForConsideration)
                return 0;
        }
        if (!count_linked_staff_by_kind(c, std::to_underlying(ThingKind::researcher)))
            return 0;
        std::int32_t upgraded = 0;
        for (auto* item = first_object_catalog_entry(&c.resources.object_catalog); item;
             item = next_object_catalog_entry(&c.resources.object_catalog))
            if (item->which_ui_type == ObjectUiCategory::rides) {
                const auto* control = find_world_object_control(current_world(c).object_controls, item->id);
                if (!control)
                    throw std::logic_error("advisor ride upgrade control");
                if (fixed::word(control->research_option_index) > 0)
                    ++upgraded;
            }
        if (action == 61 ? upgraded > 0 : (!upgraded || upgraded > policy.RidesMoreUpgrades.LessAvailableThan))
            return 0;
        bool space = false;
        for (auto* item = c.entities.active_head; item; item = item->active_next)
            if (item->payload->kind == ThingKind::park_object
                && (std::to_underlying(reinterpret_cast<const ParkObjectRuntimeView*>(item->payload)->capabilities)
                    & std::to_underlying(ParkObjectCapabilities::thinking_space))
                       != 0) {
                space = true;
                break;
            }
        if (space) {
            auto* header = c.entities.at(primary_world(c).saved.research_lab).payload;
            if (!header)
                throw std::logic_error("advisor research lab");
            if (estimate_research_category_output(c,
                                                  reinterpret_cast<ResearchLabRecord*>(header),
                                                  ResearchCategory::upgrades))
                return 0;
        }
        return action == 61 ? policy.RidesNoUpgrades.Score : policy.RidesMoreUpgrades.Score;
    }
    if (action == 509 || action == 625) {
        if (action == 625 && (!c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter || c.construction.mode))
            return 0;
        if (!is_first_hour_of_gameplay(c))
            return 0;
        const auto& manager = get_or_create_theme_global_info_manager(c);
        const bool already_seen = manager.history.at(625).is_active || !manager.advisor_eligibility_flags[5];
        if (action == 625) {
            if (already_seen)
                return 0;
            world_operations::ensure_world_interaction_handler_initialized(c);
            if (c.staff.interaction.pointer_mode_code == 2)
                return 0;
            return count_qualifying_objects(c, ObjectUiCategory::shops) ? c.advisor.balance->New.ADVISORSTUTBUILDPATHS
                                                                        : 0;
        }
        return already_seen
                    && count_qualifying_objects(c, ObjectUiCategory::shops)
                    && count_qualifying_objects(c, ObjectUiCategory::rides) == 1
                 ? c.advisor.balance->New.ADVISORSTUTBUILDRIDE
                 : 0;
    }
    if (action == 1)
        return score_open_park(c);
    if (action == 375)
        return fixed::word(history(c, 375).play_count) <= 1
                 ? fixed::word(2u * static_cast<std::uint32_t>(score_open_park(c)))
                 : 0;
    if (action == 2) {
        if (!c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter
            || c.construction.mode
            || current_world(c).saved.park_closed)
            return 0;
        const bool rides = count_qualifying_objects(c, ObjectUiCategory::rides) > 0;
        const bool sideshows = count_qualifying_objects(c, ObjectUiCategory::sideshows) > 0;
        return !rides && !sideshows ? c.advisor.balance->ClosePark.Score : 0;
    }
    //Event-only tutorial callbacks727450/7275B0/727E60 (actions507/510/529)
    //test only FirstHour. Their trigger supplies the UI context; adding extra
    //eligibility gates here would change the original queueing contract.
    if (action == 507 || action == 510 || action == 529)
        return is_first_hour_of_gameplay(c) ? 10000 : 0;
    //Native7280C0, bound to action534, calls the same query and discards it.
    if (action == 534) {
        (void)is_first_hour_of_gameplay(c);
        return 0;
    }
    if (action == 516)
        return is_first_hour_of_gameplay(c) && !count_linked_staff_by_kind(c, 7) ? 10000 : 0;
    if (action == 292 || action == 393)
        return count_linked_staff_by_kind(c, 7) ? 0 : 10000;
    //Native728580/729020 use the persisted ThemeGlobalInfoManager history,
    //just like action506; they do not inspect a separate configuration flag.
    if (action == 506 || action == 541 || action == 621)
        return is_advisor_action_active(c, action) ? 0 : 10000;
    if (action == 60)
        return score_build_rides(c);
    if (action == 380) {
        if (!c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter
            || c.construction.mode
            || fixed::word(history(c, 380).play_count) > 1)
            return 0;
        return fixed::word(2u * static_cast<std::uint32_t>(score_build_rides(c)));
    }
    if (action == 538) {
        const auto query = query_mission_obstacle_research_status(c);
        if (get_or_create_theme_global_info_manager(c).mission_number != 1
            || query.status != MissionResearchStatus::complete
            || fixed::word(history(c, 538).play_count) >= 2)
            return 0;
        std::uint32_t rooms = 0;
        for (auto* item = c.entities.active_head; item; item = item->active_next)
            if (item->payload->kind == ThingKind::park_object) {
                const auto flags = reinterpret_cast<const ParkObjectRuntimeView*>(item->payload)->capabilities;
                if ((std::to_underlying(flags) & std::to_underlying(ParkObjectCapabilities::training_room)) != 0)
                    ++rooms;
            }
        return rooms && !count_linked_staff_by_kind(c, 20) ? 10000 : 0;
    }
    throw std::logic_error("Unported advisor scoring callback for action " + std::to_string(action));
}

std::uint32_t ClockPort::read_advisor_ui_delay_clock(WorldRuntime&) {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::int32_t select_runtime_behavior_candidate(AdvisorWorld& c, AdvisorRecord& advisor, std::int32_t steps) {
    if (steps <= 0)
        return 645;
    ensure_policy(c);
    std::int32_t best = 0, winner = 645;
    auto& cursor = c.advisor.behavior.descriptor_cursor;
    for (std::int32_t processed = 0; processed < steps;) {
        if (cursor >= advisor_catalog::runtime_order.size())
            cursor = 0;
        const auto& row = advisor_catalog::descriptors[advisor_catalog::runtime_order[cursor]];
        if (row.state == 0) {
            std::int32_t score = -1;
            if (allowed(c, row) && !timing_group_expired(c, row)) {
                score = world_operations::evaluate_advisor_action_score(c, row.action);
                if (row.group != 1 && advisor.panel_up)
                    score /= 3;
            }
            if (score > best && can_queue_advisor_action_candidate(c, advisor, row.action, false)) {
                best = score;
                winner = row.action;
            }
            ++processed;
        }
        ++cursor;
    }
    return winner;
}

int find_highest_score_queued_candidate(AdvisorWorld& c, const AdvisorRecord& advisor) {
    std::int32_t best = 0;
    int selected = -1;
    bool special = false;
    for (std::size_t index = 0; index < advisor.queue.size(); ++index) {
        const auto& candidate = advisor.queue[index];
        if (!candidate.valid)
            continue;
        ensure_policy(c);
        const auto* row = descriptor(fixed::word(candidate.message));
        if (!row)
            continue;
        if (c.ui.active_root && row->group != 1 && row->group != 8)
            continue;
        //All positive group-eight entries replace the winner, even at a lower
        //score. Subsequent ordinary entries cannot replace that winner.
        if (row->group == 8 && candidate.score > 0) {
            selected = static_cast<int>(index);
            best = candidate.score;
            special = true;
        }
        if (candidate.score > best && !special) {
            selected = static_cast<int>(index);
            best = candidate.score;
        }
    }
    return selected;
}

std::int32_t select_advisor_response_variant(AdvisorWorld& c, const AdvisorQueueEntry& candidate) {
    auto variant = fixed::word(candidate.response_no);
    if (variant >= 0)
        return variant;
    ensure_policy(c);
    const auto action = fixed::word(candidate.message);
    const auto* row = descriptor(action);
    const auto count = row ? row->variants : 0;
    const auto previous = history(c, action).last_response_variant_index;
    return choose_advisor_response_variant(current_world(c),
                                           action,
                                           count,
                                           static_cast<AdvisorVariantMode>(row ? row->variant_mode : 0),
                                           previous);
}

std::int32_t choose_advisor_response_variant(World& world,
                                             std::int32_t action,
                                             std::int32_t count,
                                             AdvisorVariantMode mode,
                                             std::int32_t previous) {
    const auto next = fixed::add(previous, 1);
    const auto variant = match (mode) -> std::int32_t {
        case AdvisorVariantMode::once      => previous == count - 1 ? action : next;
        case AdvisorVariantMode::hold_last => previous >= count - 1 ? previous : next;
        case AdvisorVariantMode::cycle     => next;
        case AdvisorVariantMode::random    => do {
            if (!count)
                throw std::domain_error("zero advisor response variant count");
            do_return fixed::word(static_cast<std::uint32_t>(next_world_prng_value(world))
                                  % static_cast<std::uint32_t>(count));
        };
        case _ => action;
    };
    return variant < 0 || variant >= count ? 0 : variant;
}

bool play_queued_advisor_response(AdvisorWorld& c,
                                  AdvisorQueueEntry& candidate,
                                  std::int32_t variant,
                                  std::uint32_t& duration) {
    if (!candidate.valid)
        return false;
    candidate.valid = 0;
    ensure_policy(c);
    const auto action = fixed::word(candidate.message);
    const auto* row = descriptor(action);
    const bool scored = !candidate.score_overridden;
    if (scored) {
        std::int32_t score = -1;
        if (row) {
            c.advisor.behavior.callback_thing_id = 0;
            c.advisor.behavior.callback_secondary = 0;
            score = world_operations::evaluate_advisor_action_score(c, action);
        }
        if (score < c.advisor.queue_policy.general.MinScoreForConsideration)
            return false;
        if (c.advisor.behavior.callback_thing_id)
            candidate.thing_id = c.advisor.behavior.callback_thing_id;
        if (c.advisor.behavior.callback_secondary)
            candidate.secondary = c.advisor.behavior.callback_secondary;
    }
    const auto base = row ? row->response_base : 1014, count = row ? row->variants : 0;
    const auto response = variant < count ? fixed::add(base, variant) : base;
    if (response == 1014) {
        duration = 0;
        return false;
    }
    if (scored) {
        if (c.advisor.advisor_help_requested_action_id != 645 && c.advisor.advisor_help_requested_action_id == action) {
            c.advisor.advisor_help_override_response_id = response;
            c.advisor.advisor_help_requested_action_id = 645;
        } else
            c.advisor.advisor_help_override_response_id = 1014;
    }
    duration = world_operations::play_advisor_speech_response(c, response);
    if (scored && row && row->timing_group != -1)
        c.advisor.behavior.group_last_played_world_tick.at(static_cast<std::size_t>(row->timing_group)) =
            current_world(c).saved.game_tick;
    const auto found = std::ranges::find(advisor_catalog::responses, response, &advisor_catalog::Response::id);
    const auto tag = found == advisor_catalog::responses.end() ? 151 : found->tag;
    if (!scored || tag != 151) {
        WorldEvent event{ TagMailEvent{ tag, candidate.thing_id, candidate.secondary, -1 } };
        dispatch_world_event_to_subscribers(c,
                                            get_world_event_subscription_registry(c),
                                            &event,
                                            deliver_world_record_event,
                                            nullptr);
    }
    //Zero duration is legitimate (e.g. an inhibited speech request). Candidate
    //consumption and history updates still succeed in the original helper.
    return true;
}

void update_advisor_action_queue(AdvisorWorld& c, AdvisorRecord& advisor) {
    if (advisor.from_ui_panel) {
        ensure_policy(c);
        if (c.ports.clock.read_advisor_ui_delay_clock(c) - advisor.delay_ticks
            >= static_cast<std::uint32_t>(c.advisor.queue_policy.general.UIPanelDelay)) {
            advisor.from_ui_panel = 0;
            (void)queue_advisor_action_candidate(
                c,
                advisor,
                world_operations::make_advisor_action_candidate(c, fixed::word(advisor.delayed_message)));
            return;
        }
    } else if (!has_active_world_animation_target(c) && !c.advisor.presentation.map_source_view) {
        const auto action = select_runtime_behavior_candidate(c, primary_advisor(c), 1);
        const auto score = world_operations::evaluate_advisor_action_score(c, action);
        if (score > c.advisor.queue_policy.general.MinScoreForConsideration)
            (void)queue_advisor_action_candidate(c,
                                                 advisor,
                                                 world_operations::make_advisor_action_candidate(c, action));
    }
    if (unscaled_clock(c) < advisor.last_action_duration + advisor.last_action_started)
        return;
    (void)get_or_create_theme_global_info_manager(c);
    if (advisor.requested_last_tutorial == 1) {
        advisor.requested_last_tutorial = 0;
        return;
    }
    const auto index = find_highest_score_queued_candidate(c, advisor);
    if (index < 0)
        return;
    auto& candidate = advisor.queue[static_cast<std::size_t>(index)];
    ensure_policy(c);
    if ((candidate.valid ? candidate.score : -1) <= c.advisor.queue_policy.general.MinScoreForConsideration)
        return;
    const auto variant = select_advisor_response_variant(c, candidate);
    std::uint32_t duration{};
    if (!play_queued_advisor_response(c, candidate, variant, duration))
        return;
    advisor.last_action_started = unscaled_clock(c);
    advisor.last_action_duration = duration + 1000u;
    auto& past = history(c, fixed::word(candidate.message));
    past.last_played_world_tick = current_world(c).saved.game_tick;
    past.last_response_variant_index = variant;
    past.is_active = 1;
    ++past.play_count;
    advisor.message_playing = candidate.message;
}

bool
can_queue_advisor_action_candidate(AdvisorWorld& c, const AdvisorRecord& advisor, std::int32_t action, bool bypass) {
    ensure_policy(c);
    const auto* row = descriptor(action);
    if (!c.advisor.presentation.tutorial_on && row && row->group == 1)
        return false;
    auto& past = history(c, action);
    const auto* group = row ? &c.advisor.queue_policy.groups.at(static_cast<std::size_t>(row->group)) : nullptr;
    if (past.last_played_world_tick >> 2) {
        const auto delay=group?match(past.play_count) -> std::int32_t {
      case 0 => group->MinTimeSameMessage1;
      case 1 => group->MinTimeSameMessage2;
      case 2 => group->MinTimeSameMessage3;
      case _ => group->MinTimeSameMessageX;
    }:0;
        if (!c.simulation.current)
            throw std::logic_error("advisor World is not bound");
        if ((c.simulation.current->saved.game_tick >> 2) - (past.last_played_world_tick >> 2)
            < static_cast<std::uint32_t>(delay))
            return false;
    }
    if (!bypass && !advisor.from_help_button && group) {
        if (group->SayOnlyOnce && past.is_active)
            return false;
        if (group->DiscardAfterSlaps && std::bit_cast<std::int32_t>(past.cancel_count) >= group->DiscardAfterSlaps)
            return false;
    }
    const auto count = std::ranges::count_if(advisor.queue, [&](const auto& candidate) {
        return candidate.valid && candidate.message == static_cast<std::uint32_t>(action);
    });
    if (count >= (row ? static_cast<std::uint8_t>(row->queue_limit) : 0))
        return false;
    return advisor.from_help_button
        || (row && row->variant_mode)
        || past.last_response_variant_index != (row ? row->variants : 0) - 1;
}

bool queue_advisor_action_candidate(AdvisorWorld& c, AdvisorRecord& advisor, const AdvisorQueueEntry& incoming) {
    ensure_policy(c);
    const auto* row = descriptor(static_cast<std::int32_t>(incoming.message));
    if (row && row->group == 1)
        advisor.special_candidate = incoming;
    if (!can_queue_advisor_action_candidate(c,
                                            advisor,
                                            static_cast<std::int32_t>(incoming.message),
                                            incoming.override_only_once != 0))
        return false;
    auto slot = std::ranges::find(advisor.queue, std::uint8_t{ 0 }, &AdvisorQueueEntry::valid);
    if (slot == advisor.queue.end()) {
        auto lowest = advisor.queue.end();
        for (auto candidate = advisor.queue.begin(); candidate != advisor.queue.end(); ++candidate)
            if (candidate->valid) {
                const auto* existing = descriptor(static_cast<std::int32_t>(candidate->message));
                if (existing
                    && existing->group != 8
                    && (lowest == advisor.queue.end() || candidate->score < lowest->score))
                    lowest = candidate;
            }
        if (lowest == advisor.queue.end())
            return false;
        if (lowest->score < (incoming.valid ? incoming.score : -1))
            *lowest = incoming;
    } else
        *slot = incoming;
    //A full queue with a lower-priority incoming action still reports success.
    if (advisor.from_help_button)
        c.advisor.advisor_help_requested_action_id = static_cast<std::int32_t>(incoming.message);
    return true;
}

bool cancel_advisor_action_by_id(AdvisorWorld& c, AdvisorRecord& advisor, std::int32_t action) {
    bool cancelled = false;
    if (advisor.special_candidate.valid) {
        ensure_policy(c);
        const auto* row = descriptor(action);
        if (row && row->group == 1 && advisor.special_candidate.message == static_cast<std::uint32_t>(action))
            advisor.special_candidate.valid = 0;
    }
    for (auto& candidate : advisor.queue)
        if (candidate.valid && candidate.message == static_cast<std::uint32_t>(action)) {
            candidate.valid = 0;
            cancelled = true;
        }
    const auto current = unscaled_clock(c) >= advisor.last_action_duration + advisor.last_action_started
                           ? 645u
                           : advisor.message_playing;
    if (current == static_cast<std::uint32_t>(action)) {
        cancelled = true;
        stop_advisor_speech_response(c);
        auto& past = history(c, action);
        past.is_active = 0;
        ++past.cancel_count;
        advisor.last_action_duration = 0;
    }
    return cancelled;
}

namespace {
void stop_advisor_response(AdvisorWorld& c, AdvisorStopTargetAction target_action, bool completion) {
    auto& speech = c.advisor.speech;
    if (!speech.response_active)
        return;
    const bool had_audio_target = speech.audio_target_id != 0;
    c.ports.audio.submit_sound_target_level_request(c, speech.audio_target_id, 0);
    if (speech.speech_request_paused_for_ui && c.advisor.presentation.advisor_on && speech.audio_target_id) {
        c.ports.audio.submit_sound_target_restart_request(c, speech.audio_target_id);
        speech.speech_request_paused_for_ui = 0;
    }
    speech.response_active = 0;
    auto& presentation = c.advisor.presentation;
    (void)set_advisor_attachment_part(c, -2);
    presentation.speech_ducking_active = false;
    presentation.sound_settings_dirty = true;
    resize_advisor_ticket_particles(c, 0, 0);
    if (speech.render_slot_index >= presentation.meshes.size())
        throw std::out_of_range("advisor render slot");
    if (const auto& mesh = presentation.meshes[speech.render_slot_index]; mesh && mesh->registered) {
        if (!c.resources.meshes)
            throw std::logic_error("advisor mesh runtime");
        const auto stopped = request_mesh_playback(*mesh, 0, { { 12, 0 }, 2, 1.f }, c.resources.meshes->clocks);
        if (!stopped)
            throw std::runtime_error(describe_mesh_error(stopped.error()));
        mesh->registered = false;
    }
    if (!speech.lip_data.empty()) {
        speech.lip_data.clear();
        speech.lip_file_size = 0;
        speech.lip_cue_stream_active = 0;
    }
    if (speech.audio_target_id) {
        if (target_action == AdvisorStopTargetAction::parameter_request)
            c.ports.resources.submit_resource_target_parameter_request(c, speech.audio_target_id);
        else
            c.ports.resources.request_target_recycle(c, speech.audio_target_id);
        speech.audio_target_id = 0;
    }
    if (completion
        && had_audio_target
        && !presentation.fixed_pose_sequence
        && !presentation.repositioned_render_transforms)
        (void)submit_positioned_template_request(c,
                                                 speech.audio_target_id,
                                                 c.resources.global_speech_catalog_slot,
                                                 0x46Eu + static_cast<std::uint32_t>(next_crt_random(c) % 3),
                                                 0,
                                                 0,
                                                 0);
}
} //namespace

void stop_advisor_speech_response(AdvisorWorld& c) {
    stop_advisor_response(c, AdvisorStopTargetAction::recycle, true);
}

void stop_active_advisor_response(AdvisorWorld& c, AdvisorStopTargetAction action) {
    stop_advisor_response(c, action, false);
}

bool world_operations::builtin::queue_advisor_action_candidate(WorldRuntime& c,
                                                               AdvisorRecord* advisor,
                                                               const AdvisorQueueEntry& candidate) {
    if (!advisor)
        throw std::invalid_argument("null advisor queue");
    return simcoaster::queue_advisor_action_candidate(c, *advisor, candidate);
}

void world_operations::builtin::queue_scored_advisor_action(WorldRuntime& c, int action) {
    auto candidate = world_operations::make_advisor_action_candidate(c, action);
    if (!c.simulation.current)
        throw std::logic_error("advisor World is not bound");
    auto* payload = c.entities.at(c.simulation.current->saved.advisor).payload;
    if (!payload)
        throw std::logic_error("advisor is missing");
    (void)world_operations::queue_advisor_action_candidate(c, reinterpret_cast<AdvisorRecord*>(payload), candidate);
}

void
world_operations::builtin::cancel_advisor_action_by_id(WorldRuntime& c, AdvisorRecord* advisor, std::int32_t action) {
    if (!advisor)
        throw std::invalid_argument("null advisor cancellation");
    if (auto* world = c.resources.meshes ? c.resources.meshes->coasters() : nullptr;
        world && world->active_edit_transaction) {
        auto& transaction = world->active_edit_transaction->root();
        if (transaction.defer_lifecycle) {
            transaction.effects.emplace_back(
                coaster_editor_detail::EditTransaction::AdvisorCancellation{ advisor->base.header.id, action });
            return;
        }
    }
    (void)simcoaster::cancel_advisor_action_by_id(c, *advisor, action);
}

//Like the other resource-manager proxies, disabled dispatch returns before
//constructing a backend request. Live output belongs to the audio owner.
void AudioHostPort::submit_sound_target_level_request(WorldRuntime& c, std::uint32_t, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        missing_runtime_service();
}

void AudioHostPort::submit_sound_target_restart_request(WorldRuntime& c, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        missing_runtime_service();
}
} //namespace simcoaster
