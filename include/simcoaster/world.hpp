#pragma once
#include "world_domains_fwd.hpp"
#include "world_runtime.hpp"

namespace simcoaster {
//Raw reconstructed operations preserve the game's valid-state preconditions:
//active payloads exist, IDs are in range, retirement/removal happen once, and
//removal count agrees with its nodes. No extra generation or bounds policy is
//inserted into the game algorithms.
void reset_used_thing_record_table(WorldRuntime&);
std::int32_t next_world_prng_value(World&);
StaffCandidate* initialize_world_prefix_record(StaffCandidate*);
World* initialize_world_structure_prefix(World*);
ArrivalState* initialize_world_arrival_state(ArrivalState*);
MapCell* initialize_world_map_cell_record(MapCell*);
TrackCell* initialize_world_track_cell_record(TrackCell*);
RegionEffectCell* initialize_world_re_cell_record(RegionEffectCell*);
MapCell* register_world_map_cell_range(WorldRuntime&, MapCell*, std::uint32_t stride);
TrackCell* register_world_track_cell_range(WorldRuntime&, TrackCell*, std::uint32_t stride);
//Supplied-storage constructor. Leaves all bytes without native stores intact.
//Metadata reset/binding/runtime manager construction happen in later phases.
World* construct_world_structure(WorldRuntime&, World*);
int set_block_save_region(BlockSavePrefix*, int index, void*, std::uint32_t, const char* unused_label);
int initialize_world_structure_globals(WorldRuntime&, World*);
int create_world_structure(WorldRuntime&);
MechanicHqRecord* initialize_world_mechanic_hq_state(WorldRuntime&, MechanicHqRecord*);
UiMessageReceiverRecord* initialize_world_ui_message_receiver(WorldRuntime&, UiMessageReceiverRecord*);
Type23Record* initialize_world_used_thing_type23_record(WorldRuntime&, Type23Record*);
StaffTrainingBudget* initialize_world_staff_training_budget_state(StaffTrainingBudget*);
StaffHqRecord* initialize_world_staff_hq_record(WorldRuntime&, StaffHqRecord*);
void initialize_world_staff_hq_ability_slots(WorldRuntime&, StaffHqRecord*);
AdvisorRecord* initialize_world_advisor_state(WorldRuntime&, AdvisorRecord*);
int initialize_world_advisor_runtime_state(WorldRuntime&, AdvisorRecord*);
ResearchLabRecord* initialize_world_research_lab_state(WorldRuntime&, ResearchLabRecord*);
ResearchGroup* initialize_world_research_group_entry(ResearchGroup*);
EventRegistry* get_world_event_subscription_registry(WorldRuntime&);
bool record_passes_grid_predicates(VisitorWorld&, const MapObjectBase*);
std::uint32_t count_eligible_person_records(VisitorWorld&, std::int32_t generation_selector);
ThingId* allocate_and_link_world_used_thing(WorldRuntime&, ThingId*, ThingHeader*);
void activate_used_thing_metadata_entry(WorldRuntime&, ThingMetadata*, ThingHeader*);
void rebuild_used_thing_free_list(WorldRuntime&);
MapObjectBase* initialize_world_map_object_base(WorldRuntime&, MapObjectBase*, CellId, std::uint32_t kind);
ThingId link_world_used_thing_into_map_cell(WorldRuntime&, MapCell*, ThingId);
MapObjectBase* unlink_world_used_thing_from_map_cell(WorldRuntime&, MapCell*, ThingId);
void unlink_world_used_thing_map_and_event_lists(WorldRuntime&, ThingHeader*);
bool is_world_used_thing_mobile(const ThingHeader*);
std::uint32_t append_pending_world_used_thing_removal(WorldRuntime&, ThingId);
std::uint32_t append_pending_world_used_thing_removal(WorldRuntime&, RemovalQueue&, ThingId);
ThingId* pop_pending_world_used_thing_removal(WorldRuntime&, ThingId*);
ThingId* pop_pending_world_used_thing_removal(WorldRuntime&, RemovalQueue&, ThingId*);
void unlink_and_free_world_used_thing(WorldRuntime&, ThingMetadata*);
void drain_pending_world_used_thing_removals(WorldRuntime&);
void release_world_used_thing_removal_queue_nodes(WorldRuntime&);
RemovalQueue* delete_world_used_thing_removal_queue(WorldRuntime&, RemovalQueue*, std::uint32_t flags);
std::uint32_t retire_world_used_thing(WorldRuntime&, ThingHeader*);
void initialize_game_mode_selector(WorldRuntime&);
void update_world_used_thing(WorldRuntime&, ThingHeader*);
void update_world_managers(WorldRuntime&, World*);
void run_world_simulation_tick(WorldRuntime&, World*);
void clear_world_records_before_save_restore(WorldRuntime&, World*);
} //namespace simcoaster
