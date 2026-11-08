#pragma once
#include "world_domains_fwd.hpp"

#include "world.hpp"

namespace simcoaster {
struct TrackRideManager;
TrackRideLayoutMetrics query_track_ride_layout_metrics(const TrackRideManager&, std::uint32_t handle);
//First constructor phase: register a newly allocated instance and publish its
//creation event. Definition-derived setup runs after subscribers have returned.
void begin_park_object_instance(AttractionWorld&, ParkObjectRuntimeView&, CellId, std::uint16_t definition_id, std::int32_t rotation, std::uint32_t track_ride_handle);
//Definition-dependent constructor phase, called after the creation event and
//before state transition, mesh creation and script loading. The caller owns
//the original constructor's earlier and later phases.
void initialize_park_object_definition_settings(AttractionWorld&, ParkObjectRuntimeView&, const ResearchCatalogDefinition&, CellId, std::int32_t rotation);
//Called only after the object's RSSE state has been installed.
void initialize_park_object_script_settings(AttractionWorld&, ParkObjectRuntimeView&, const ResearchCatalogDefinition&);
//Records creation time and updates the definition's first/live instance data.
//Sign localization and the remaining finalization follow this phase.
void record_park_object_creation_in_world(AttractionWorld&, ParkObjectRuntimeView&, const ResearchCatalogDefinition&);
void initialize_or_refresh_park_object_mesh(AttractionWorld&, ParkObjectRuntimeView&, bool refresh);
void refresh_type3_meshes_after_restore(AttractionWorld&, World&);
//Borrowed catalog lookup; its existing last-ID cache remains authoritative.
ResearchCatalogDefinition* get_park_object_definition(AttractionWorld&, const ParkObjectRuntimeView*);
const ObjectDefinitionBalance& get_park_object_balance(AttractionWorld&, const ParkObjectRuntimeView&);
std::uint8_t calculate_park_object_excitement_rating(AttractionWorld&, ParkObjectRuntimeView&, std::int32_t first, std::int32_t second, std::int32_t third);
std::uint8_t get_park_object_excitement(AttractionWorld&, ParkObjectRuntimeView&, bool use_base_rating);
std::array<std::uint16_t, 2> compute_park_object_service_target_coordinates(AttractionWorld&, const ParkObjectRuntimeView&);
std::array<std::uint16_t, 2> get_park_object_entry_exit_position(AttractionWorld&, const ParkObjectRuntimeView&, bool use_exit);
CellId find_nearest_path_cell_around_object(AttractionWorld&, const ParkObjectRuntimeView&, CellId reference);
ThingId get_info_desk_promoted_attraction_id(AttractionWorld&, const ParkObjectRuntimeView&);
bool has_clear_entry_exit_path(AttractionWorld&, const ParkObjectRuntimeView&);
bool set_park_object_promoted_attraction(AttractionWorld&, ParkObjectRuntimeView&, ThingId);
void refresh_info_desk_signs_for_renamed_object(AttractionWorld&, ThingId);
void handle_park_object_event(AttractionWorld&, ParkObjectRuntimeView&, const WorldEvent&);
std::uint32_t query_world_used_thing_type_in_square_radius(VisitorWorld&, std::int32_t kind, CellId center, std::int32_t radius, bool first_only);
void increment_park_object_use_history_counters(ParkObjectRuntimeView&);
void record_park_object_queue_walkaway(ParkObjectRuntimeView&);
std::int32_t calculate_shop_goods_cost(AttractionWorld&, const ParkObjectRuntimeView&, float quality, float ingredient, float portion);
std::int32_t calculate_current_shop_goods_cost(AttractionWorld&, const ParkObjectRuntimeView&);
void record_park_object_payment_and_challenge_progress(AttractionWorld&, ParkObjectRuntimeView&, std::uint32_t);
void record_park_object_use_expense(AttractionWorld&, ParkObjectRuntimeView&, std::int32_t);
void close_park_object_after_blocked_person_exit(AttractionWorld&, ParkObjectRuntimeView&);
void apply_toilet_use_wear(AttractionWorld&, ParkObjectRuntimeView&, std::uint32_t);
ThingId find_first_park_object_record_id_by_definition_id(AttractionWorld&, std::uint16_t definition_id);
bool park_object_definition_is_marked_special(AttractionWorld&, const ParkObjectRuntimeView*);
std::uint8_t get_park_object_activation_flag(const ParkObjectRuntimeView*);
std::uint8_t truncate_park_object_condition(float);
bool park_object_requires_staff_service(AttractionWorld&, const ParkObjectRuntimeView&);
bool is_track_type_3_park_object_runtime_openable(WorldRuntime&, const ParkObjectRuntimeView*);
bool is_park_object_restricted_by_hot_dog_scare_challenge(AttractionWorld&, const ParkObjectRuntimeView&);
bool is_ride_restricted_by_safety_review_challenge(AttractionWorld&, const ParkObjectRuntimeView&);
void open_park_object(AttractionWorld&, ParkObjectRuntimeView&);
void close_park_object(AttractionWorld&, ParkObjectRuntimeView&);
void set_special_ingredient_objects_open(AttractionWorld&, std::int32_t ingredient, bool open);
bool is_choosable_park_object_with_eligible_queue(WorldRuntime&, ParkObjectRuntimeView&);
std::int64_t get_park_object_age_in_days(WorldRuntime&, const ParkObjectRuntimeView&);
std::int32_t calculate_park_attraction_value(WorldRuntime&);
void unlink_park_object_from_world_list(AttractionWorld&, World&, const ParkObjectRuntimeView&);
std::uint32_t cleanup_world_park_object_instance(AttractionWorld&, ParkObjectRuntimeView&);
double calculate_park_object_condition_decay(AttractionWorld&, ParkObjectRuntimeView&, float speed, float capacity, float unused_duration, bool use_requested_capacity);
void update_park_object_condition(AttractionWorld&, ParkObjectRuntimeView&);
void tick_world_park_object(AttractionWorld&, ParkObjectRuntimeView&);
void update_world_park_object_state(AttractionWorld&, ParkObjectRuntimeView&);
void transition_park_object_state(AttractionWorld&, ParkObjectRuntimeView&, ParkObjectState);
void tick_open_park_object_state(AttractionWorld&, ParkObjectRuntimeView&);
void park_object_let_person_on(AttractionWorld&, ParkObjectRuntimeView&);
void reconcile_park_object_queue_head_with_script_admission(AttractionWorld&, ParkObjectRuntimeView&);
} //namespace simcoaster
