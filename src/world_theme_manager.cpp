#include "simcoaster/world_theme.hpp"
#include <stdexcept>

namespace simcoaster {
bool reset_shares_finance_contexts(SharesFinanceState* state) {
    if (!state) {
        throw std::invalid_argument("null shares contexts");
    }
    //Comparison values live inside their rebuilt report owner. Native freed
    //only the outer allocation here, leaking its separately allocated delta.
    state->ownership_context.reset();
    state->ownership_context = std::make_unique<SharesOwnershipContext>();
    state->share_price_context.reset();
    state->share_price_context = std::make_unique<SharePriceContext>();
    for (auto& record : state->record_slots) {
        record.reset();
    }
    return true;
}

int reset_mission_state_and_scan_cdk_files(MissionWorld& c, ThemeGlobalInfoManager* manager) {
    if (!manager) {
        throw std::invalid_argument("null theme manager");
    }
    rebuild_theme_cdk_registrations(c, manager);
    manager->mission_number = 0;
    manager->current_promotion_index = 1;
    manager->challenge_failures_remaining = 5;
    manager->obstacle_removed_message_received = 0;
    manager->mission_obstacle_research_complete = 0;
    manager->golden_ticket_balance = 0;
    for (std::size_t i = 0; i < manager->theme_ability_activation_flags.size(); ++i) {
        manager->theme_ability_activation_flags[i] = i == 1 || i == 7 || i == 9;
    }
    manager->mission_obstacle_research_complete = 0;
    manager->obstacle_removed_message_received = 0;
    for (auto& row : manager->objective_entries) {
        row.elapsed_days = 0;
        row.deadline_days = 0;
        row.start_filetime_low = 0;
        row.start_filetime_high = 0;
        row.current_value = 0;
        row.target_value = 0;
        row.source_filter_ordinal = MissionObjectiveSource::all_sources;
        row.state = MissionObjectiveState::inactive;
        row.objective_kind = MissionObjectiveMetric::none;
        //Baseline and opaque tail survive this reset loop. A successful mission
        //load below independently clears the baseline along with tracking state.
    }
    manager->global_park_award_flags.fill(0);
    manager->reset_zero_reserved_dword = 0;
    manager->language_word_filters_enabled = 1;
    manager->manager_startup_action_pending = 1;
    manager->mission_world_event_latch = 0;
    manager->advisor_eligibility_flags.fill(1);
    manager->research_lab_action_532_eligible = 1;
    (void)load_mission_balance_data(c, manager);
    reset_shares_finance_contexts(&c.finance.shares_finance);
    manager->end_of_game_event_processed = 0;
    manager->lobby_area_available_flags.fill(0);
    manager->lobby_area_available_flags[14] = 1;
    return 0;
}

ThemeGlobalInfoManager* construct_theme_global_info_manager(MissionWorld& c, ThemeGlobalInfoManager* manager) {
    if (!manager) {
        throw std::invalid_argument("null theme manager");
    }
    manager->swear_word_list.clear();
    manager->allowed_word_list.clear();
    manager->theme_info_records.clear();
    for (auto& record : manager->cdk_registrations) {
        record = CdkRegistrationRecord {};
    }
    //The native receiver binding-map constructor resets all fields before CDK
    //file discovery. Natural typed storage replaces that pointer map.
    sam::reset_integer_record(manager->mission_research);
    for (auto& row : manager->objective_entries) {
        row.start_filetime_low = 0;
        row.start_filetime_high = 0;
    }
    manager->save_file_basename.clear();
    for (auto& row : manager->history) {
        row.last_played_world_tick = 0;
        row.last_response_variant_index = -1;
        row.is_active = 0;
        row.cancel_count = 0;
        row.play_count = 0;
    }
    reset_mission_state_and_scan_cdk_files(c, manager);
    //Selected ThemeInfo and use_gmi_suffix are established by later paths.
    return manager;
}

ThemeGlobalInfoManager& get_or_create_theme_global_info_manager(MissionWorld& c) {
    if (c.missions.theme_global_info_manager) {
        return *c.missions.theme_global_info_manager;
    }
    auto created = std::make_unique<ThemeGlobalInfoManager>();
    construct_theme_global_info_manager(c, created.get());
    c.missions.owned_theme_manager = std::move(created);
    c.missions.theme_global_info_manager = c.missions.owned_theme_manager.get();
    return *c.missions.theme_global_info_manager;
}

void release_theme_global_info_manager(MissionWorld& c) {
    c.missions.owned_theme_manager.reset();
    c.missions.theme_global_info_manager = nullptr;
}
} //namespace simcoaster
