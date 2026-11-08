#pragma once
#include "world_domains_fwd.hpp"
#include "world_save.hpp"

namespace simcoaster {
enum class PurposeSpeedMode : std::uint32_t {
    normal = 0,
    boost_25 = 1,
    boost_50 = 2
};
void update_person_roaming(VisitorWorld&, PersonRuntimeView&);
void handle_person_litter_bin_use_arrival(VisitorWorld&, PersonRuntimeView&);
void handle_person_litter_bin_prank_arrival(VisitorWorld&, PersonRuntimeView&);
void handle_person_vegetation_fire_prank_arrival(VisitorWorld&, PersonRuntimeView&);
void spread_rumours_in_cell(VisitorWorld&, PersonRuntimeView&, WorldCellCoordinates);
void whinge_at_staff_in_cell(VisitorWorld&, PersonRuntimeView&, WorldCellCoordinates);
void update_person_whinge_to_staff(VisitorWorld&, PersonRuntimeView&);
void update_person_vicious_rumours(VisitorWorld&, PersonRuntimeView&);
void update_person_after_info_desk_wait(VisitorWorld&, PersonRuntimeView&);
void update_person_after_park_object_exit(VisitorWorld&, PersonRuntimeView&);
bool try_complete_person_park_object_exit(VisitorWorld&, PersonRuntimeView&);
void handle_person_park_object_event(VisitorWorld&, PersonRuntimeView&, const WorldEvent&);
void record_person_park_object_visit(VisitorWorld&, PersonRuntimeView&, ParkObjectRuntimeView&);
void apply_park_object_use_effects(VisitorWorld&, PersonRuntimeView&, ParkObjectRuntimeView&);
std::int32_t apply_person_ride_excitement_effects(VisitorWorld&, PersonRuntimeView&, ParkObjectRuntimeView&);
std::int32_t
compute_person_ride_excitement_difference(AttractionWorld&, const PersonRuntimeView&, ParkObjectRuntimeView&);
bool is_object_price_too_high_for_person(VisitorWorld&, PersonRuntimeView&, ParkObjectRuntimeView&);
bool route_person_to_current_queue_position(VisitorWorld&, PersonRuntimeView&);
void update_person_en_route_to_park_object_queue(VisitorWorld&, PersonRuntimeView&);
void update_person_queue_state(VisitorWorld&, PersonRuntimeView&);
void update_person_waiting_for_park_object_admission(VisitorWorld&, PersonRuntimeView&);
void reselect_person_ride_target_from_nearby_cells(VisitorWorld&, PersonRuntimeView&);
void start_person_temporary_appearance_animation(VisitorWorld&, PersonRuntimeView&, std::uint32_t animation);
std::int32_t score_person_park_object_candidate(VisitorWorld&, PersonRuntimeView&, ParkObjectRuntimeView&);
void select_person_park_object_destination_by_score(VisitorWorld&, PersonRuntimeView&);
ThingId find_nearest_eligible_litter_bin(VisitorWorld&, PersonRuntimeView&, std::int32_t radius);
ThingId find_nearest_vegetation_for_fire_prank(VisitorWorld&, PersonRuntimeView&, std::int32_t radius);
ThingId find_nearby_unstarted_fireworks(VisitorWorld&, const PersonRuntimeView&);
ThingId find_nearest_entertainer_to_entity(VisitorWorld&, const MapObjectBase&);
ThingId find_nearest_active_person_record(VisitorWorld&, const MapObjectBase&);
ThingId select_best_available_info_desk_for_person(VisitorWorld&, const PersonRuntimeView&);
inline constexpr std::array<std::uint16_t, 3> mobile_purpose_speed_values{ 0, 25, 50 };
inline constexpr std::array<std::uint16_t, 5> mobile_base_speed_variants{ 60, 80, 100, 120, 140 };
std::int32_t get_resource_category_variant_count(const VisitorQuery&, std::uint32_t category);
//Future appearance selection is quality-limited independently of the full
//resource range retained for existing people, save restoration and rendering.
std::int32_t get_resource_category_appearance_variant_count(const VisitorQuery&, std::uint32_t category);
void initialize_mobile_sprite_animation(MobileSpriteAnimationState&);
void set_sprite_animation_next(MobileSpriteAnimationState&, std::uint32_t animation_id);
bool
sprite_animation_matches_resource_or_next_anim(VisitorWorld&, MobileSpriteAnimationState&, std::uint32_t animation_id);
void set_world_sprite_animation_resource(VisitorWorld&,
                                         MobileSpriteAnimationState&,
                                         std::uint32_t animation_id,
                                         std::int32_t parameter_a,
                                         std::int32_t parameter_b);
void release_world_sprite_animation_resource_handle(VisitorWorld&, MobileSpriteAnimationState&);
void synchronize_world_sprite_animation_resource_state(VisitorWorld&, MobileSpriteAnimationState&);
std::uint32_t set_sprite_animation_service_interval_from_motion_speed(MobileSpriteAnimationState&, float speed);
void set_navigation_force_and_speed_limits(MobileNavigationState&, float requested_speed);
void update_mobile_entity_speed_and_cached_position(VisitorWorld&, MobileEntityState&);
//Only Person callers supply umbrella state; staff have no enclosing Person.
std::uint16_t set_mobile_entity_purpose_speed(MobileEntityState&,
                                              PurposeSpeedMode,
                                              std::optional<PersonUmbrellaState> person_umbrella = std::nullopt);
void insert_person_previous_ride_history_entry(PersonRuntimeView&, ThingId);
void insert_person_temporary_ride_history_entry(PersonRuntimeView&, ThingId);
void age_person_temporary_ride_history(VisitorWorld&, PersonRuntimeView&);
void
set_mobile_entity_thought_bubble(VisitorWorld&, MobileEntityThoughtHistory&, MobileThoughtCode, bool force_display);
void release_expired_mobile_entity_thought_script(VisitorWorld&, MobileEntityThoughtHistory&);
void update_person_need_and_mood_thought_bubble(VisitorWorld&, PersonRuntimeView&);
void discard_person_balloon(VisitorWorld&, PersonRuntimeView&, bool create_litter);
void start_person_weather_appearance_transition(VisitorWorld&, PersonRuntimeView&, PersonUmbrellaState);
void process_person_weather_appearance_deadline(VisitorWorld&, PersonRuntimeView&);
void update_world_person_weather_effects(VisitorWorld&, PersonRuntimeView&);
void update_world_person_record(VisitorWorld&, PersonRuntimeView&);
void update_person_state_machine(VisitorWorld&, PersonRuntimeView&);
void enter_world_person_state(VisitorWorld&, PersonRuntimeView&, PersonState);
enum class AdmissionFeeBand : std::int32_t {
    much_too_high = 0,
    too_high = 1,
    too_low = 2,
    about_right = 3
};
enum class BusStopSelector : std::int32_t {
    a = 0,
    b = 1,
    c = 2
};
CellId get_bus_stop_cell(const PersonEnvironmentBalance&, BusStopSelector);
CellId select_world_map_cell_id_from_pair(const PersonEnvironmentBalance&, bool second);
std::int32_t get_current_arrival_vehicle_status(VisitorWorld&, World&);
std::uint32_t
get_cached_eligible_person_record_count(VisitorWorld&, ParkAnalyserState&, std::uint32_t refresh_quarters);
std::uint32_t calculate_world_arrival_passenger_count(VisitorWorld&);
bool is_person_departure_routing_at_bus_stop_a(VisitorWorld&, const World&);
void update_bus_arrival_state(VisitorWorld&, ArrivalState&);
bool is_current_arrival_vehicle_not_size1(const World&);
bool can_person_proceed_from_arrival_vehicle(VisitorWorld&, World&);
std::int32_t get_world_park_gates_rsse_slot1(VisitorWorld&, const World&);
bool try_set_mobile_entity_path_to_cell(VisitorWorld&, MapObjectBase&, MobileEntityState&, CellId);
bool try_set_mobile_entity_path_to_park_exit(VisitorWorld&, PersonRuntimeView&);
AdmissionFeeBand classify_park_admission_fee_value(VisitorWorld&);
void update_person_admission_fee_response(VisitorWorld&, PersonRuntimeView&);
void update_person_walking_to_park_entrance(VisitorWorld&, PersonRuntimeView&);
void update_person_waiting_at_park_entrance(VisitorWorld&, PersonRuntimeView&);
void update_person_crossing_park_entrance(VisitorWorld&, PersonRuntimeView&);
void update_person_walking_to_park_exit(VisitorWorld&, PersonRuntimeView&);
void update_person_departure_routing(VisitorWorld&, PersonRuntimeView&);
bool is_first_hour_of_gameplay(const VisitorQuery&);
std::uint32_t increment_world_visitor_count(World&);
void cleanup_world_person_record(VisitorWorld&, PersonRuntimeView&);
void unlink_and_release_world_mobile_entity_base_resources(VisitorWorld&, MapObjectBase&, MobileEntityState&);
void restore_world_person_saved_state(VisitorWorld&, PersonRuntimeView&);
void update_person_first_aid_recovery_timer(VisitorWorld&, PersonRuntimeView&);
void release_world_person_umbrella_resource(VisitorWorld&, PersonRuntimeView&);
void release_and_clear_mobile_entity_thought_script_handle(VisitorWorld&, MobileEntityThoughtHistory&);
void release_mobile_entity_sprite_and_optional_person_effects(VisitorWorld&, MapObjectBase&, MobileEntityState&, bool);
MobileRenderPose interpolate_mobile_entity_render_position(const MobileEntityState&, float factor, bool clamp_to_tick);
void update_mobile_entity_render_pose(VisitorWorld&, MapObjectBase&, MobileEntityState&, float interpolation);
void update_person_balloon_render_pose(VisitorWorld&, PersonRuntimeView&, const WorldVector3&, std::uint32_t angle);
void update_person_umbrella_render_pose(VisitorWorld&, PersonRuntimeView&, const WorldVector3&, std::uint32_t angle);
std::uint32_t create_runtime_template_at_mobile_entity_resource_position(VisitorWorld&,
                                                                         const MobileEntityState&,
                                                                         std::uint32_t particle,
                                                                         std::int32_t vertical_offset,
                                                                         std::int32_t parameter);
CellId get_ticket_booth_cell(const PersonEnvironmentBalance&, std::size_t index);
bool roll_type3_object_chance(VisitorWorld&, ParkObjectRuntimeView&);
void record_person_departure_visit_duration(VisitorWorld&, ParkAnalyserState&, std::uint32_t elapsed_world_ticks);
void add_progress_to_matching_world_challenges(ChallengeManagerRecord&,
                                               ChallengeType,
                                               std::int32_t delta,
                                               ThingId target,
                                               bool secondary);
void initialize_mobile_navigation_state(MobileNavigationState&);
void initialize_world_mobile_entity_base(VisitorWorld&, MapObjectBase&, MobileEntityState&, CellId, ThingKind);
void initialize_world_person_record(VisitorWorld&, PersonRuntimeView&, CellId);
bool create_world_person_record(VisitorWorld&);
ThingId update_world_arrival_vehicle(VisitorWorld&, World&, std::int32_t variant);
void initialize_person_for_restore(PersonRuntimeView&);

int transfer_mobile_thought_history(MobileEntityThoughtHistory&, SaveArchive archive);
int transfer_mobile_navigation_state(MobileNavigationState&, SaveArchive archive);
int transfer_mobile_entity_state(VisitorWorld&, MapObjectBase&, MobileEntityState&, SaveArchive archive);
int transfer_world_person_state(VisitorWorld&, PersonRuntimeView&, SaveArchive archive);
} //namespace simcoaster
