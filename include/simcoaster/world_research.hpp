#pragma once
#include "object_catalog.hpp"
#include "world.hpp"
#include "world_domains_fwd.hpp"
#include <span>

namespace simcoaster {
int initialize_world_research_lab_runtime_state(ResearchWorld&, ResearchLabRecord*);
int count_linked_staff_by_kind(ResearchWorld&, std::uint32_t kind);
bool prepare_catalog_research_project(ResearchWorld&, ResearchProject*, const ResearchCatalogDefinition*);
bool prepare_catalog_upgrade_project(ResearchWorld&, ResearchProject*, const ResearchCatalogDefinition*);
void build_world_research_lab_project_list(ResearchWorld&, ResearchLabRecord*);
void recalculate_world_research_group_outputs(ResearchLabRecord*);
void initialize_world_research_lab_research_groups(ResearchWorld&, ResearchLabRecord*);
void advance_world_research_group_availability(ResearchWorld&, ResearchLabRecord*, std::int32_t category);
int next_crt_random(ResearchWorld&);
StaffType map_research_staff_requirement_to_staff_type_index(ResearchStaffRequirement);
int calculate_staff_work_value(ResearchWorld&, const StaffRuntimeRecord*);
float calculate_research_slot_work_increment(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot);
void update_research_slot_staffing_status(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot);
void clear_world_research_lab_worker_ids(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot);
void advance_world_research_project(ResearchWorld&, ResearchProject*, float increment, ResearchWorkSlot);
void update_world_research_lab_state(ResearchWorld&, ResearchLabRecord*);
void mark_all_research_lab_projects_available(ResearchWorld&, ResearchLabRecord*);
void synchronize_research_lab_project_object_availability(ResearchWorld&, ResearchLabRecord*);
bool set_research_lab_active_and_notify_subscribers(ResearchWorld&, ResearchLabRecord*);
bool clear_research_lab_active_and_notify_subscribers(ResearchWorld&, ResearchLabRecord*);
ThingId find_staff_matching_research_project_requirement(ResearchWorld&, ResearchLabRecord*, std::int32_t project, ThingId excluded = 0);
void remove_world_research_lab_staff_reference(ResearchWorld&, ResearchLabRecord*, const ThingHeader*);
void clear_world_research_lab_staffing(ResearchWorld&, ResearchLabRecord*);
bool set_research_slot_worker_ids(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot, std::span<const ThingId, 20>);
int count_research_project_assigned_staff(ResearchLabRecord*, ResearchWorkSlot);
void copy_research_slot_worker_ids(ResearchLabRecord*, ResearchWorkSlot, std::span<ThingId, 20>);
void handle_world_research_lab_event(ResearchWorld&, ResearchLabRecord*, WorldEvent*);
void try_add_research_project_from_selected_source(ResearchWorld&, ResearchLabRecord*);
std::int32_t select_next_research_category_project(ResearchWorld&, ResearchLabRecord*, ResearchCategory);
std::int32_t select_previous_research_category_project(ResearchWorld&, ResearchLabRecord*, ResearchCategory);
std::int32_t choose_research_project_in_cost_band(ResearchWorld&, const ResearchLabRecord*, ResearchProjectCostBand, std::uint32_t type, ResearchCategory);
std::int32_t count_available_research_projects(ResearchWorld&, const ResearchLabRecord*, ResearchCategory);
std::int32_t find_nth_available_research_project(ResearchWorld&, const ResearchLabRecord*, ResearchCategory, std::int32_t ordinal);
std::uint32_t get_research_category_effort(const ResearchLabRecord*, ResearchCategory);
std::uint32_t get_total_research_effort(const ResearchLabRecord*);
std::uint32_t get_active_research_effort(const ResearchLabRecord*);
std::uint32_t set_research_category_effort(ResearchLabRecord*, ResearchCategory, std::uint32_t effort);
std::uint32_t set_world_research_lab_work_rate(ResearchLabRecord*, std::uint32_t workload);
std::uint32_t get_research_category_output(const ResearchLabRecord*, ResearchCategory);
std::int32_t sum_researcher_ability_by_experience(const ResearchQuery&);
std::uint32_t get_researcher_ability_capacity(const ResearchQuery&);
std::uint32_t estimate_research_category_output(ResearchWorld&, const ResearchLabRecord*, ResearchCategory);
std::int32_t get_world_research_project_progress_percent(const ResearchLabRecord*, std::int32_t project);
std::int32_t get_research_project_cash_per_brain_point(const ResearchLabRecord*, std::int32_t project);
std::int32_t get_research_project_completion_percent(const ResearchProject*);
bool is_research_project_complete(const ResearchProject*);
bool preferred_research_project_matches_object(const ResearchLabRecord*, std::uint16_t definition);
std::int32_t estimate_research_project_days_remaining(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot);
bool set_research_lab_current_project_for_slot(ResearchWorld&, ResearchLabRecord*, std::int32_t project, ResearchWorkSlot);
void clear_research_lab_project_slot(ResearchWorld&, ResearchLabRecord*, ResearchWorkSlot);
std::uint16_t get_research_project_object_id(const ResearchLabRecord*, std::int32_t project);
bool is_research_project_selectable(ResearchWorld&, ResearchWorkSlot, std::int32_t project);
bool research_slot_has_unfinished_project(const ResearchLabRecord*, ResearchWorkSlot);
bool is_research_project_slot_complete(const ResearchLabRecord*, ResearchWorkSlot);
std::int32_t count_unfinished_research_projects(const ResearchLabRecord*, ResearchCategoryFilter);
bool update_research_lab_active_state_from_researcher_list(ResearchWorld&, ResearchLabRecord*);
std::uint8_t get_research_lab_active_flag(const ResearchLabRecord*);
ResearchStaffRequirements get_research_project_staff_requirements(const ResearchLabRecord*, std::int32_t project);
std::int32_t find_unfinished_obstacle_research(const ResearchLabRecord*);
} //namespace simcoaster
