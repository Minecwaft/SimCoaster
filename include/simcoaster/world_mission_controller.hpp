#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
CompanyEmailMessage*
construct_objective_complete_company_email(MissionWorld&, CompanyEmailMessage*, std::int32_t text_row_index);
bool start_world_animation_playback(MissionWorld&,
                                    AnimationManagerRecord*,
                                    ThingId target,
                                    std::int32_t script_slot0,
                                    AnimationPlaybackProfile);
void tick_mission_progress_from_controller_record(MissionWorld&, MissionControllerRecord*);
void handle_world_cutscene_event(MissionWorld&, MissionControllerRecord*, WorldEvent*);
bool start_mission_completion_presentation(MissionWorld&, MissionControllerRecord*);
bool process_mission_completion_directives(MissionWorld&, MissionControllerRecord*);
bool prepare_mission_completion(MissionWorld&, ThingId target = 0);
void cancel_prepared_mission_transition(MissionWorld&);
std::optional<MissionTransitionFailure> take_mission_transition_failure(MissionWorld&);
ParkObjectRuntimeView* resolve_mission_obstacle(MissionWorld&);
void unlock_lobby_area_for_mission(ThemeGlobalInfoManager&, std::int32_t mission);
bool try_spend_theme_golden_tickets(ThemeGlobalInfoManager*, std::int32_t amount);
bool advance_to_next_mission(MissionWorld&, ThemeGlobalInfoManager*);
bool mark_theme_available_by_ordinal(MissionWorld&, ThemeGlobalInfoManager*, std::int32_t ordinal);
int find_world_current_obstacle(MissionWorld&, World*);
} //namespace simcoaster
