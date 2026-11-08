#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::size_t objective_index(std::int32_t index) {
    if (index < 0 || index >= 5)
        throw std::out_of_range("mission objective index");
    return static_cast<std::size_t>(index);
}

World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null objective world");
}

BankRecord& current_bank(MissionWorld& c) {
    auto* payload = c.entities.at(require_world(c.simulation.current).saved.bank_account).payload;
    if (!payload)
        throw std::invalid_argument("null objective bank");
    return *reinterpret_cast<BankRecord*>(payload);
}
} //namespace

MissionObjectiveTrackingEntry* get_mission_objective_tracking_entry(ThemeGlobalInfoManager* manager,
                                                                    std::int32_t index) {
    if (!manager)
        throw std::invalid_argument("null objective manager");
    return &manager->objective_entries[objective_index(index)];
}

void start_mission_objective_tracking(ThemeGlobalInfoManager* manager,
                                      std::int32_t index,
                                      FileTime start,
                                      std::int32_t baseline) {
    auto& entry = *get_mission_objective_tracking_entry(manager, index);
    entry.state = MissionObjectiveState::tracking;
    entry.start_filetime_low = start.low;
    entry.start_filetime_high = start.high;
    entry.percentage_baseline_value = baseline;
}

bool is_mission_objective_target_met(const ThemeGlobalInfoManager* manager, std::int32_t index) {
    if (!manager)
        throw std::invalid_argument("null objective manager");
    const auto& entry = manager->objective_entries[objective_index(index)];
    auto threshold = entry.target_value;
    if (entry.objective_kind == MissionObjectiveMetric::park_analyser_growth_percent
        || entry.objective_kind == MissionObjectiveMetric::eligible_person_count_growth_percent) {
        //ADD and IMUL retain only32 bits before signed division by100. Ordinary
        //signed C++ overflow would not express the verified native threshold.
        const auto percent = std::bit_cast<std::uint32_t>(entry.target_value) + 100u;
        threshold =
            std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(entry.percentage_baseline_value) * percent) / 100;
    }
    return entry.current_value >= threshold;
}

std::uint32_t sum_current_park_analyser_history_values_7_to_11(MissionWorld& c) {
    std::uint32_t total = 0;
    for (std::size_t series = 7; series <= 11; ++series) {
        auto& world = require_world(c.referenced_world());
        auto* payload = c.entities.at(world.saved.park_analyser).payload;
        if (!payload)
            throw std::invalid_argument("null objective park analyser");
        const auto& history = reinterpret_cast<ParkAnalyserFinanceView*>(payload)->long_histories[series];
        //Approved correction to Game.exe5B9880: its empty branches add the
        //incoming analyser pointer's bits as a statistic. An unsampled series
        //contributes zero; valid samples retain the original indexing/addition.
        if (history.num_entries <= 0 || (history.current_entry < 0 && !history.wrapped))
            continue;
        auto index = history.current_entry;
        if (index < 0)
            index = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(index)
                                                + std::bit_cast<std::uint32_t>(history.num_entries));
        if (index < 0 || static_cast<std::size_t>(index) >= history.data.size())
            throw std::out_of_range("analyser history index");
        total += history.data[static_cast<std::size_t>(index)];
    }
    return total;
}

AdvisorActionHistoryEntry* initialize_mission_objective_tracking(MissionWorld& c) {
    for (std::int32_t index = 0; index < 5; ++index) {
        auto* entry = get_mission_objective_tracking_entry(&get_or_create_theme_global_info_manager(c), index);
        const auto filter = entry->source_filter_ordinal;
        //The selected-source call is unconditional, even for AllSources and for
        //non-inactive rows. It may rebind the manager or modify the original row.
        const auto selected = c.ports.assets.selected_resource_source(c);
        if ((filter != static_cast<MissionObjectiveSource>(selected) && filter != MissionObjectiveSource::all_sources)
            || entry->state != MissionObjectiveState::inactive)
            continue;
        match (entry->objective_kind) {
            case MissionObjectiveMetric::adjusted_bank_positive or MissionObjectiveMetric::adjusted_bank_balance or MissionObjectiveMetric::park_analyser_growth_percent or MissionObjectiveMetric::eligible_person_count_growth_percent => {
                const auto kind = entry->objective_kind;
                auto* calendar_world = c.simulation.current;
                auto& start_manager = get_or_create_theme_global_info_manager(c);
                std::int32_t baseline;
                if (kind == MissionObjectiveMetric::adjusted_bank_positive
                    || kind == MissionObjectiveMetric::adjusted_bank_balance)
                    baseline = current_bank(c).balance; //Initialization uses RAW balance.
                else {
                    //Both native branches obtain (and discard) this base-World analyser
                    //before their metric helper; the aggregate uses the World reference.
                    auto* volatile analyser =
                        c.entities.at(require_world(c.simulation.current).saved.park_analyser).payload;
                    (void)analyser;
                    baseline = std::bit_cast<std::int32_t>(kind == MissionObjectiveMetric::park_analyser_growth_percent
                                                               ? sum_current_park_analyser_history_values_7_to_11(c)
                                                               : count_eligible_person_records(c, 4));
                }
                FileTime time;
                calculate_world_calendar_filetime(c, &require_world(calendar_world).calendar, &time);
                start_mission_objective_tracking(&start_manager, index, time, baseline);
            }
            case MissionObjectiveMetric::total_challenges_completed or MissionObjectiveMetric::global_park_award_count or MissionObjectiveMetric::share_ownership_percent or MissionObjectiveMetric::eligible_person_count => {
                auto& start_manager = get_or_create_theme_global_info_manager(c);
                FileTime time;
                calculate_world_calendar_filetime(c, &require_world(c.simulation.current).calendar, &time);
                start_mission_objective_tracking(&start_manager, index, time, 0);
            }
            case _ => {}
        }
    }
    {
        WorldEvent event{ MissionObjectiveProgressEvent{} };
        dispatch_world_event_to_subscribers(c,
                                            get_world_event_subscription_registry(c),
                                            &event,
                                            deliver_world_record_event,
                                            nullptr);
    }
    auto& manager = get_or_create_theme_global_info_manager(c);
    const auto theme = manager.mission_research.tech_level_theme;
    if (theme >= 0 && theme < 3) {
        const auto new_group = manager.mission_research.tech_level_new_max_group;
        auto* info = get_or_create_theme_global_info_for_ordinal(c, manager, static_cast<std::uint32_t>(theme));
        if (!info)
            throw std::runtime_error("mission technology source failed to load");
        if (new_group > info->current_tech_level)
            info->current_tech_level = new_group;
    }
    auto* volatile advisor = c.entities.at(require_world(c.simulation.current).saved.advisor).payload;
    (void)advisor;
    auto* history = &get_or_create_theme_global_info_manager(c).history[0x1E3];
    history->is_active = 0;
    return history;
}
} //namespace simcoaster
