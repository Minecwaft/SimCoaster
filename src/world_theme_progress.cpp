#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null mission World");
}

void read_controller(MissionWorld& c, World* world) {
    auto* volatile ignored = c.entities.at(require_world(world).saved.mission_controller).payload;
    (void)ignored;
}

ThemeGlobalSaveInfo& require_info(ThemeGlobalSaveInfo* info) {
    return require_bound<std::runtime_error>(info, "mission ThemeInfo failed to load");
}
} //namespace

MissionObjectiveTrackingEntry* get_global_mission_objective_tracking_entry(MissionWorld& c, std::int32_t index) {
    //The native wrapper repeats already-cached singleton reads without any
    //intervening effect. Natural storage needs only the final accessor.
    return get_mission_objective_tracking_entry(&get_or_create_theme_global_info_manager(c), index);
}

void set_mission_objective_values(ThemeGlobalInfoManager* manager,
                                  std::int32_t index,
                                  std::int32_t elapsed,
                                  std::int32_t current) {
    auto* entry = get_mission_objective_tracking_entry(manager, index);
    entry->elapsed_days = elapsed;
    entry->current_value = current;
}

void set_mission_objective_state(ThemeGlobalInfoManager* manager, std::int32_t index, MissionObjectiveState state) {
    get_mission_objective_tracking_entry(manager, index)->state = state;
}

void update_mission_objective_progress(MissionWorld& c) {
    if (require_world(c.simulation.current).saved.game_tick & 3u)
        return;
    for (std::int32_t index = 0; index < 5; ++index) {
        auto* entry = get_global_mission_objective_tracking_entry(c, index);
        if (entry->objective_kind == MissionObjectiveMetric::none)
            continue;
        const auto filter = entry->source_filter_ordinal;
        const auto selected = c.ports.assets.selected_resource_source(c);
        if ((filter != static_cast<MissionObjectiveSource>(selected) && filter != MissionObjectiveSource::all_sources)
            || entry->state != MissionObjectiveState::tracking)
            continue;

        FileTime now;
        calculate_world_calendar_filetime(c, &require_world(c.simulation.current).calendar, &now);
        const auto timestamp = std::uint64_t{ now.low } | (std::uint64_t{ now.high } << 32);
        const auto start =
            std::uint64_t{ entry->start_filetime_low } | (std::uint64_t{ entry->start_filetime_high } << 32);
        const auto elapsed = static_cast<std::int32_t>(std::bit_cast<std::int64_t>(timestamp - start) / 864000000000LL);
        const auto current = read_mission_objective_metric(c, entry);
        //Native reads the metric even on the same day. That read can perform
        //asset I/O and modify this old row before its elapsed value is compared.
        if (entry->elapsed_days == elapsed)
            continue;
        set_mission_objective_values(&get_or_create_theme_global_info_manager(c), index, elapsed, current);
        if (elapsed >= entry->deadline_days) {
            if (is_mission_objective_target_met(&get_or_create_theme_global_info_manager(c), index))
                set_mission_objective_state(&get_or_create_theme_global_info_manager(c),
                                            index,
                                            MissionObjectiveState::target_met);
            else if (entry->deadline_days != 0)
                set_mission_objective_state(&get_or_create_theme_global_info_manager(c),
                                            index,
                                            MissionObjectiveState::deadline_expired);
        }
        {
            WorldEvent event{ MissionObjectiveProgressEvent{} };
            dispatch_world_event_to_subscribers(c,
                                                get_world_event_subscription_registry(c),
                                                &event,
                                                deliver_world_record_event,
                                                nullptr);
        }
        update_objectives_panel_and_queue_mission_advisor_action(c);
    }
}

bool are_mission_completion_requirements_met(MissionWorld& c) {
    if (get_or_create_theme_global_info_manager(c).end_of_game_event_processed)
        return false;
    bool complete = true;
    for (std::int32_t index = 0; index < 5; ++index) {
        const auto* entry = get_global_mission_objective_tracking_entry(c, index);
        if (entry->objective_kind != MissionObjectiveMetric::none && entry->state != MissionObjectiveState::target_met)
            complete = false;
    }
    if (get_or_create_theme_global_info_manager(c).mission_research.required_challenges_number != 0) {
        const auto theme = get_or_create_theme_global_info_manager(c).mission_research.required_challenges_theme;
        const auto required = get_or_create_theme_global_info_manager(c).mission_research.required_challenges_number;
        auto& manager = get_or_create_theme_global_info_manager(c);
        auto& info =
            require_info(get_or_create_theme_global_info_for_ordinal(c, manager, static_cast<std::uint32_t>(theme)));
        //This resolution occurs even when an earlier objective already failed.
        if (complete)
            complete = required <= info.challenges_done;
    }
    if (get_or_create_theme_global_info_manager(c).mission_research.promotion_number != 0 && complete) {
        const auto promotion = get_or_create_theme_global_info_manager(c).current_promotion_index;
        complete = get_or_create_theme_global_info_manager(c).mission_research.promotion_number <= promotion;
    }
    const auto& obstacle = get_or_create_theme_global_info_manager(c).mission_research;
    if (obstacle.obstacle_type != 0 && obstacle.no_research == 0) {
        if (!complete)
            return false;
        complete = get_or_create_theme_global_info_manager(c).obstacle_removed_message_received != 0;
    }
    if (!complete)
        return false;
    const auto balance = get_or_create_theme_global_info_manager(c).golden_ticket_balance;
    return balance >= get_or_create_theme_global_info_manager(c).mission_research.obstacle_golden_tickets;
}

bool can_proceed_with_current_objective(MissionWorld& c) {
    const auto ending = get_or_create_theme_global_info_manager(c).mission_research.ending_theme;
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (!manager.selected_theme_info)
        throw std::invalid_argument("selected mission ThemeInfo");
    const auto ordinal = manager.selected_theme_info->source_ordinal;
    const auto actual = require_info(get_or_create_theme_global_info_for_ordinal(c, manager, ordinal)).source_ordinal;
    if (static_cast<std::uint32_t>(ending) != actual && ending != 3)
        return false;
    read_controller(c, c.referenced_world());
    if (!is_obstacle_mission_research_enabled(c)) {
        read_controller(c, c.simulation.reference);
        return are_mission_completion_requirements_met(c);
    }
    if (!resolve_mission_obstacle(c))
        return false;
    read_controller(c, c.referenced_world());
    if (query_mission_obstacle_research_status(c).status != MissionResearchStatus::complete)
        return false;
    auto* payload = c.entities.at(require_world(c.simulation.reference).saved.staff_hq).payload;
    if (!payload)
        throw std::invalid_argument("mission StaffHQ");
    for (const auto& slot : reinterpret_cast<StaffHqRecord*>(payload)->staff_slots)
        if (slot.staff_type != StaffType::unsupported_index && !slot.assigned_staff)
            return false;
    const auto* balance = &get_or_create_theme_global_info_manager(c).mission_research;
    read_controller(c, c.simulation.reference);
    const auto tickets = get_or_create_theme_global_info_manager(c).golden_ticket_balance;
    return tickets >= balance->obstacle_golden_tickets;
}

void update_objectives_panel_and_queue_mission_advisor_action(MissionWorld& c) {
    if (!can_proceed_with_current_objective(c)) {
        c.ports.ui.clear_control_particle_group(c);
        return;
    }
    //Approved repair: native54D660 lacks the selector gate and can recreate
    //HUD sparkles during state20. Selector exit refreshes this same helper.
    if ((c.view.flags & 0x16u) || c.advisor.presentation.map_source_view)
        c.ports.ui.clear_control_particle_group(c);
    else {
        auto* control = c.ports.ui.find_ui_control(c, c.ui.persistent_root, 0x45u);
        control = c.ports.ui.find_ui_control(c, control, 0x48u);
        control = c.ports.ui.find_ui_control(c, control, 0x4Cu);
        c.ports.ui.configure_ui_control_particles(c, control);
    }
    const auto mission = get_or_create_theme_global_info_manager(c).mission_number;
    const auto candidate = world_operations::make_advisor_action_candidate(c,
                                                                           mission == 0   ? 527
                                                                           : mission == 1 ? 455
                                                                                          : 483);
    auto* payload = c.entities.at(require_world(c.simulation.reference).saved.advisor).payload;
    if (!payload)
        throw std::invalid_argument("mission Advisor");
    (void)world_operations::queue_advisor_action_candidate(c, reinterpret_cast<AdvisorRecord*>(payload), candidate);
}
} //namespace simcoaster
