#pragma once
#include "world_domains_fwd.hpp"
#include "world_person.hpp"

namespace simcoaster {
void initialize_staff_for_restore(StaffRuntimeRecord&);
StaffType world_staff_type_for_used_thing(const ThingHeader&);
StaffActivityStatus classify_staff_activity(const StaffRuntimeRecord&);
//Native629880, shared by pickup eligibility and World-click inspection.
bool is_world_staff_interaction_eligible(StaffWorld&, const StaffRuntimeRecord&);
//Association ownership survives UI pause/freeze state words. This is distinct
//from the native activity-label/history classification.
StaffState world_staff_association_state(const StaffRuntimeRecord&) noexcept;
bool world_staff_has_hidden_base_sprite(const StaffRuntimeRecord&) noexcept;
void update_world_staff_tick(StaffWorld&, StaffRuntimeRecord&);
void relocate_world_guard_record(StaffWorld&, StaffRuntimeRecord&);
void tick_world_entertainer_record(StaffWorld&, StaffRuntimeRecord&);
void enter_world_staff_state(StaffWorld&, StaffRuntimeRecord&, StaffState, ThingId removed_training_room = 0);
void set_world_staff_state_by_type(StaffWorld&, StaffRuntimeRecord&, StaffState);
void update_world_staff_activity_state(StaffWorld&, StaffRuntimeRecord&);
void choose_staff_task_or_rest_area(StaffWorld&, StaffRuntimeRecord&);
bool evaluate_world_staff_energy_and_activity(StaffWorld&, StaffRuntimeRecord&);
bool is_world_staff_too_tired_for_task(const StaffQuery&, const StaffRuntimeRecord&);
void decrease_staff_energy_and_happiness_during_task(const StaffQuery&, StaffRuntimeRecord&);
void update_world_mechanic_state(StaffWorld&, MechanicRuntimeRecord&);
void set_world_mechanic_state(StaffWorld&, MechanicRuntimeRecord&, StaffState);
void choose_mechanic_repair_task(StaffWorld&, MechanicRuntimeRecord&);
ThingId select_mechanic_work_target_candidate(StaffWorld&, MechanicHqRecord&, const StaffRuntimeRecord&);
void update_world_researcher_state(StaffWorld&, ResearcherRuntimeRecord&);
void update_world_handyman_idle_state(StaffWorld&, HandymanRuntimeRecord&);
void update_world_handyman_state(StaffWorld&, HandymanRuntimeRecord&);
void choose_and_start_handyman_task(StaffWorld&, HandymanRuntimeRecord&);
bool route_world_handyman_to_litter_cell(StaffWorld&, HandymanRuntimeRecord&, CellId);
void call_world_cell_handyman(StaffWorld&, CellId);
void cancel_world_cell_litter_assignment(StaffWorld&, CellId);
void update_world_gardener_state(StaffWorld&, GardenerRuntimeRecord&);
void start_world_gardener_task(StaffWorld&, GardenerRuntimeRecord&);
void update_world_entertainer_state(StaffWorld&, EntertainerRuntimeRecord&);
void update_entertainer_idle_and_work_state(StaffWorld&, EntertainerRuntimeRecord&);
void update_world_guard_state(StaffWorld&, GuardRuntimeRecord&);
void update_world_guard_idle_or_patrol_state(StaffWorld&, GuardRuntimeRecord&);
bool start_guard_prosecution(StaffWorld&, GuardRuntimeRecord&, ThingId);
bool start_guard_first_aid(StaffWorld&, GuardRuntimeRecord&, ThingId);
enum class StaffStrikeCause : std::int32_t {
    none = 0,
    forced = 1,
    happiness = 2,
    motivation = 3,
    energy = 4
};
StaffStrikeCause evaluate_world_staff_strike_thresholds(StaffWorld&, const StaffHqRecord&, StaffType);
void update_world_staff_strike_state(StaffWorld&, StaffHqRecord&, StaffType);
void update_world_staff_strike_states_for_game_tick(StaffWorld&, StaffHqRecord&);
void handle_world_staff_event(StaffWorld&, StaffRuntimeRecord&, const WorldEvent&);
void handle_world_staff_control_state_event(StaffWorld&, StaffHqRecord&, const WorldEvent&);
void cleanup_world_staff_record(StaffWorld&, StaffRuntimeRecord&);
ThingId create_hired_world_staff_record(StaffWorld&, CellId, const StaffCandidate&);
//CHRIS: native non-hired Guard/Researcher constructors (no candidate/name/pay).
ThingId create_debug_world_staff_record(StaffWorld&, CellId, StaffType);
bool hire_staff_from_prefix_slot(StaffWorld&, World&, std::size_t);
void release_world_staff_entity_links(StaffWorld&, World&, bool preserve_candidate_validity);
void cancel_pending_staff_placement(StaffWorld&, bool placement_confirmed = false);
//The interaction layer supplies a cell after its object/path validation.
ThingId place_pending_hired_staff_at_cell(StaffWorld&, CellId accepted_cell);
bool register_world_staff_in_staff_hq_ability_slot(StaffWorld&, StaffHqRecord&, ThingId);
void remove_world_staff_from_hq_and_refill_ability_slot(StaffWorld&, StaffHqRecord&, ThingId, bool retiring = false);
void rebuild_world_staff_hq_ability_slots(StaffWorld&, StaffHqRecord&);
bool assign_thing_to_matching_special_ride_requirement(StaffWorld&, ThingId);
bool clear_thing_id_from_eligible_special_ride_group(StaffWorld&, ThingId);
bool is_special_ride_group_ready_for_advisor_action(StaffWorld&, ThingId);
CellId find_nearest_available_litter_cell(StaffWorld&, ParkAnalyserState&, ThingId, CellId, std::int32_t radius);
bool is_park_object_in_staff_work_area(StaffWorld&, const StaffRuntimeRecord&, ParkObjectRuntimeView&);
bool staff_area_accepts_park_object(StaffWorld&, const StaffAreaBounds&, ParkObjectRuntimeView&);
void refresh_world_staff_area_membership_for_bounds(StaffWorld&, StaffAreaMembership&);
void update_park_object_world_staff_area_membership(StaffWorld&, StaffAreaMembership&, ParkObjectRuntimeView&);
void set_world_object_staff_area_bounds_from_packed_cells(StaffWorld&, StaffRuntimeRecord&, CellId, CellId);
void reset_world_object_staff_area_bounds_to_full_map(StaffRuntimeRecord&);
void release_staff_membership_storage(StaffWorld&, StaffRuntimeRecord&);
void handle_dropped_staff_member(StaffWorld&, StaffRuntimeRecord&, CellId);
bool can_world_staff_learn_theme_ability(StaffWorld&, const StaffRuntimeRecord&, ThemeAbility);
void complete_park_object_service_task(StaffWorld&, ParkObjectRuntimeView&);
void release_world_litter_record(StaffWorld&, MapCell&);
void start_world_researcher_task(StaffWorld&, ResearcherRuntimeRecord&);
ThingId select_first_thinking_space_object_if_pathable(StaffWorld&, StaffRuntimeRecord&);
ThingId find_nearest_staff_rest_area(StaffWorld&, const StaffRuntimeRecord&);
bool try_send_staff_to_nearest_rest_area(StaffWorld&, StaffRuntimeRecord&);
void handle_staff_rest_area_arrival(StaffWorld&, StaffRuntimeRecord&);
void recover_staff_energy_and_happiness_at_rest_area(StaffWorld&, StaffRuntimeRecord&);
void leave_staff_rest_area(StaffWorld&, StaffRuntimeRecord&);
void seek_staff_rest_area(StaffWorld&, StaffRuntimeRecord&);
void arrive_at_research_room(StaffWorld&, StaffRuntimeRecord&);
void update_world_staff_striking_behavior(StaffWorld&, StaffRuntimeRecord&);
void update_staff_training_travel_state(StaffWorld&, StaffRuntimeRecord&);
void update_staff_training_session(StaffWorld&, StaffRuntimeRecord&);
void cancel_staff_training_request(StaffWorld&, StaffRuntimeRecord&, ThingId removed_training_room = 0);
void finish_world_staff_training(StaffWorld&, StaffRuntimeRecord&);
void complete_park_object_mechanic_task(StaffWorld&, ParkObjectRuntimeView&, ThingId);
int handle_park_object_maintenance_request(StaffWorld&, ParkObjectRuntimeView&, bool);
std::int32_t record_world_staff_activity(StaffRuntimeRecord&);
std::int32_t sum_staff_activity_ring_samples(const StaffActivityRing&, double&, std::int32_t max_samples);
std::uint32_t grant_world_staff_experience_from_activity_mix(StaffWorld&, StaffRuntimeRecord&);
void update_world_staff_wage_state(StaffWorld&, StaffRuntimeRecord&);
bool world_staff_first_known_ability_matches(const StaffRuntimeRecord&, ThemeAbility);
bool add_world_staff_ability_learning_progress(StaffWorld&, StaffRuntimeRecord&, std::uint32_t increment);
void complete_world_staff_ability_learning(StaffRuntimeRecord&);
std::size_t find_free_world_staff_candidate_slot(const World&);
std::size_t count_world_staff_candidates(const World&, StaffType);
void recount_world_staff_counts(StaffWorld&, World&);
std::int32_t calculate_staff_salary_from_experience(const StaffQuery&, std::int32_t experience, std::int32_t maximum_wage);
void initialize_world_staff_candidate(StaffWorld&, StaffCandidate&, StaffType, std::size_t slot);
bool generate_opening_staff_pool(StaffWorld&, World&);
void initialize_world_opening_staff_pool(StaffWorld&, World&);
void replenish_world_staff_pool(StaffWorld&, World&);
void maintain_world_staff_pool(StaffWorld&, World&);
int transfer_world_staff_base_state(StaffWorld&, StaffRuntimeRecord&, SaveArchive archive);
int transfer_world_staff_state(StaffWorld&, ThingKind, ThingHeader*, SaveArchive archive);
} //namespace simcoaster
