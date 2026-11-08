#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
struct Stream;
ThemeGlobalInfoManager* construct_theme_global_info_manager(MissionWorld&, ThemeGlobalInfoManager*);
ThemeGlobalInfoManager& get_or_create_theme_global_info_manager(MissionWorld&);
bool is_advisor_action_active(VisitorWorld&, std::int32_t action_id);
AdvisorQueueEntry& init_advisor_action_candidate(VisitorWorld&,
                                                 AdvisorQueueEntry&,
                                                 std::int32_t action,
                                                 std::uint16_t event_word,
                                                 std::uint32_t event_dword,
                                                 std::uint32_t parameter,
                                                 std::uint8_t bypass,
                                                 std::int32_t score_or_auto);
void release_theme_global_info_manager(MissionWorld&);
int reset_mission_state_and_scan_cdk_files(MissionWorld&, ThemeGlobalInfoManager*);
void rebuild_theme_cdk_registrations(MissionWorld&, ThemeGlobalInfoManager*);
bool has_fallback_object_name_mapping(std::int32_t object_id);
bool reset_shares_finance_contexts(SharesFinanceState*);
bool load_mission_balance_data(MissionWorld&, ThemeGlobalInfoManager*);
std::string mission_balance_asset_path(const MissionQuery&, std::int32_t mission);
void project_mission_objective_configuration(ThemeGlobalInfoManager&);
MissionObjectiveTrackingEntry* get_mission_objective_tracking_entry(ThemeGlobalInfoManager*, std::int32_t index);
void start_mission_objective_tracking(ThemeGlobalInfoManager*, std::int32_t index, FileTime, std::int32_t baseline);
bool is_mission_objective_target_met(const ThemeGlobalInfoManager*, std::int32_t index);
//Empty/unusable native histories consume pointer residue; this x64 port
//reports that case with domain_error rather than treating an address as data.
std::uint32_t sum_current_park_analyser_history_values_7_to_11(MissionWorld&);
AdvisorActionHistoryEntry* initialize_mission_objective_tracking(MissionWorld&);
std::int32_t read_mission_objective_metric(MissionWorld&, const MissionObjectiveTrackingEntry*);
std::int32_t sum_remaining_bank_loan_repayments(const BankRecord*);
std::int32_t count_global_park_award_flags(const ThemeGlobalInfoManager*);
ThemeGlobalSaveInfo* get_mission_required_challenge_progress(MissionWorld&,
                                                             std::int32_t* required,
                                                             std::int32_t* completed,
                                                             std::int32_t* theme);
std::int32_t* get_mission_promotion_progress_values(MissionWorld&, std::int32_t* number, std::int32_t* index);
MissionObjectiveTrackingEntry* get_global_mission_objective_tracking_entry(MissionWorld&, std::int32_t index);
void
set_mission_objective_values(ThemeGlobalInfoManager*, std::int32_t index, std::int32_t elapsed, std::int32_t current);
void set_mission_objective_state(ThemeGlobalInfoManager*, std::int32_t index, MissionObjectiveState);
void update_mission_objective_progress(MissionWorld&);
bool are_mission_completion_requirements_met(MissionWorld&);
bool can_proceed_with_current_objective(MissionWorld&);
void update_objectives_panel_and_queue_mission_advisor_action(MissionWorld&);
bool load_theme_global_balance_data(MissionWorld&, ThemeGlobalSaveInfo*);
ThemeGlobalSaveInfo*
initialize_theme_global_save_info(MissionWorld&, ThemeGlobalSaveInfo*, std::string_view source_name);
ThemeGlobalSaveInfo*
get_or_create_theme_global_info_for_ordinal(MissionWorld&, ThemeGlobalInfoManager&, std::uint32_t ordinal);
ThemeGlobalSaveInfo*
get_or_create_theme_global_info_for_source(MissionWorld&, ThemeGlobalInfoManager&, std::string_view source);
int transfer_cdk_registration_record(CdkRegistrationRecord&, Stream&, bool writing);
void clear_cdk_registration_exclusion_for_researched_object(ThemeGlobalInfoManager*, std::int32_t object_id);
bool is_obstacle_mission_research_enabled(MissionWorld&);
MissionResearchQuery query_mission_obstacle_research_status(MissionWorld&);
void refresh_theme_ability_activation_flags(ThemeGlobalInfoManager*);
void mark_obstacle_removed_message_received(ThemeGlobalInfoManager*);
void clear_research_action_532_eligibility(MissionWorld&);
std::uint32_t selected_research_theme_ordinal(MissionWorld&);
std::int32_t research_theme_current_tech_level(MissionWorld&, std::uint32_t ordinal);
} //namespace simcoaster
