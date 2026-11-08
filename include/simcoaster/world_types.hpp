#pragma once
#include "mesh_runtime_queries.hpp"
#include "world_ids.hpp"

#include "company_email.hpp"
#include "mission_balance.hpp"
#include "rsse_types.hpp"
#include "sam_integer.hpp"
#include "save_schema.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simcoaster {
struct UiControl;
struct ObjectDefinitionBalance;
struct ObjectDefinitionAsset;
enum class SoundRequestKind : std::int32_t {
    positioned = 0
};

struct PositionedTemplateRequest {
    static constexpr auto kind = SoundRequestKind::positioned;
    std::uint32_t catalog_slot, request_key;
    std::array<float, 3> position;
    float parameter;
};

//Runtime domain model. Persisted widths/labels belong to the explicit save
//schema; runtime pointers and object layouts follow the x64 toolchain.
struct WorldCellCoordinates {
    std::int32_t x, y;
};
enum class WorldInputModifierMask : std::uint32_t {
    none = 0
};
enum class WorldInteractionMode : std::int32_t {
    none = 0,
    build_path = 1,
    build_queue = 3,
    place_park_object = 4,
    build_kart_track = 11,
    build_water_track = 16,
    edit_queue = 20,
    build_coaster_support = 21,
    move_coaster_support = 22,
    build_hoarding = 25,
    erase_path_cell = 50,
    remove_park_object = 51,
    erase_path_line = 52,
    erase_track_line = 53,
    hoarding_rectangle = 57,
    demolish_rectangle = 58,
    place_park_object_alternate = 59,
    backtrack_preview = 139
};
enum class RsseScriptCleanupFlags : std::uint32_t {
    none = 0,
    code_1 = 1,
    destroy_effect = 2,
    detach_people = 4
};
inline constexpr std::size_t thing_slot_count = 10250;
inline constexpr std::size_t map_cell_count = 16384;

enum class ThingKind : std::uint8_t {
    unknown_0 = 0,
    person = 1,
    retired = 2,
    park_object = 3,
    mechanic = 4,
    handyman = 5,
    entertainer = 6,
    guard = 7,
    researcher = 8,
    staff_hq = 9,
    mechanic_hq = 10,
    advisor = 11,
    tag_system = 12,
    park_analyser = 13,
    research_lab = 14,
    weather = 15,
    bank_account = 16,
    ui_receiver = 17,
    chat_presence = 18,
    challenge_manager = 19,
    gardener = 20,
    mission_controller = 21,
    animation_manager = 22,
    helper_23 = 23,
};

constexpr bool is_staff_record_kind(ThingKind kind) {
    return kind == ThingKind::mechanic || kind == ThingKind::handyman || kind == ThingKind::entertainer || kind == ThingKind::guard || kind == ThingKind::researcher || kind == ThingKind::gardener;
}

enum class WorldState : std::uint32_t {
    code_0 = 0,
    code_1 = 1,
    clearing = 2,
    transition = 4
};
enum class GameMode : std::int32_t {
    code_0 = 0,
    online = 1,
    code_2 = 2,
    code_3 = 3
};

struct ThingHeader {
    ThingId id;
    ThingKind kind;
    std::uint8_t retirement_pending;
};

struct MapObjectBase {
    ThingHeader header;
    [[=save::Field { "mX", 0 }]] std::uint16_t x;
    [[=save::Field { "mY", 1 }]] std::uint16_t y;
    [[=save::Field { "mMapParent", 3 }]] ThingId map_parent;
    [[=save::Field { "mMapChild", 2 }]] ThingId map_child;
    ThingHeader* panel_next_matching;
    ThingHeader* panel_previous_matching;
};

enum class PersonGeneration : std::int32_t {
    child = 0,
    parent = 1,
    grandparent = 2
};
//Query-facing Person fields; native mGeneration is a DWORD, not a theme ID.
enum class PersonState : std::int32_t {
    initial_route = 0,
    select_entrance = 1,
    walk_to_entrance = 2,
    wait_for_park_open = 3,
    admission_response = 4,
    cross_entrance = 5,
    roaming = 6,
    travel_to_destination = 7,
    temporary_appearance = 8,
    use_litter_bin = 9,
    litter_prank = 10,
    vegetation_fire_prank = 11,
    walk_to_info_desk = 12,
    travel_to_object_queue = 13,
    wait_in_object_queue = 14,
    move_to_queue_position = 15,
    admission_step = 16,
    wait_for_object_script = 17,
    after_info_desk = 18,
    after_object_exit = 19,
    object_interaction = 20,
    caught_by_guard = 21,
    walk_to_exit = 22,
    return_to_roaming = 23,
    whinge_to_staff = 24,
    spread_vicious_rumours = 25,
    listen_to_vicious_rumours = 26,
    first_aid_recovery = 27,
    waiting_to_cross_road = 28,
    crossing_road_to_leave = 29,
    departure_routing = 30
};
enum class MobileThoughtCode : std::int32_t {
    none = 0,
    hungry = 1,
    thirsty = 2,
    hungry_and_thirsty = 3,
    need_toilet = 4,
    sideshow_win = 5,
    sideshow_loss = 6,
    park_mess = 7,
    very_happy = 8,
    happy = 9,
    okay = 10,
    unhappy = 11,
    ride_not_exciting_enough = 12,
    angry = 13,
    sick = 14,
    ride_too_exciting = 15,
    queue_too_long = 16,
    confused = 17,
    whinge_to_staff = 18,
    likes_weather = 19,
    dislikes_weather = 20,
    naughty = 21,
    staff_happy = 22,
    staff_angry = 23,
    staff_tired = 24,
    staff_going_on_strike = 25,
    staff_going_to_training = 26,
    staff_going_to_research = 27,
    staff_going_to_staff_room = 28,
    staff_going_to_job = 29,
    staff_confused = 30
};
enum class ResourceProgramFamily : std::uint32_t {
    sprite_animation,
    thought,
    balloon_discard,
    umbrella,
    litter,
    balloon_idle,
    attached_head,
    upgrade_balloon,
    staff_cursor_preview
};

struct ResourceProgram {
    ResourceProgramFamily family;
    std::uint32_t index;
};

using ResourceArguments = std::array<std::uint32_t, 6>;
enum class LitterSelector : std::uint32_t {
    none = 0,
    vomit = 7,
    stink_bomb = 8
};
enum class PersonAppearanceEffect : std::int32_t {
    none = 0,
    balloon = 1,
    costume = 2,
    umbrella = 3
};

struct MobileEntityActionHistoryEntry {
    [[=save::Field { "event_code", 0 }]] std::uint16_t event_code;
    [[=save::Field { "related_used_thing_id", 1 }]] ThingId related_used_thing_id;
};

struct [[=save::ManualCodec {}]] MobileEntityThoughtHistory {
    [[=save::Field { "mEventHistory[i]", 1 }]] std::array<MobileEntityActionHistoryEntry, 32> event_history;
    [[=save::Field { "mLastThought", 2 }]] MobileThoughtCode last_thought;
    [[=save::Field { "mThoughtScript", 3 }]] std::uint32_t thought_script;
    [[=save::Field { "mActionHistIndex", 0 }]] std::uint16_t action_history_index;
    [[=save::Field { "mTimeBubbleShown", 4 }]] std::uint32_t time_bubble_shown;
};

struct MobileSpriteAnimationState {
    [[=save::Field { "mSpriteScript", 0 }]] std::uint32_t sprite_script;
    std::uint32_t last_animation, current_animation;
    [[=save::Field { "mNextAnim", 1 }]] std::uint32_t next_animation;
    [[=save::Field { "mNextServiceInterval", 2 }]] std::uint32_t next_service_interval;
};

struct FixedPointVector2 {
    [[=save::Field { "x", 0 }]] std::int32_t x;
    [[=save::Field { "y", 1 }]] std::int32_t y;
};
enum class SteeringBehavior {
    seek,
    arrival,
    separation,
    avoid_obstacle,
    repel_obstacle,
    avoid_walls,
    follow_path
};

struct WeightedSteeringBehavior {
    std::int32_t weight;
    SteeringBehavior behavior;
};

inline constexpr std::array default_steering_behaviors { WeightedSteeringBehavior { 58982, SteeringBehavior::avoid_walls }, WeightedSteeringBehavior { 32768, SteeringBehavior::follow_path }, WeightedSteeringBehavior { 6553, SteeringBehavior::separation } };
enum class NavigationResult {
    arrived = 0,
    moving = 1,
    unreachable = 2
};

struct [[=save::ManualCodec {}]] MobileNavigationState {
    [[=save::Field { "mass", 4 }]] std::int32_t mass;
    [[=save::Field { "radius", 21 }]] std::int32_t radius;
    [[=save::Field { "pos", 20 }]] FixedPointVector2 position;
    [[=save::Field { "vel", 22 }]] FixedPointVector2 velocity;
    [[=save::Field { "max_force", 5 }]] std::int32_t max_force;
    [[=save::Field { "max_speed", 6 }]] std::int32_t max_speed;
    [[=save::Field { "local_xaxis", 2 }]] FixedPointVector2 local_xaxis;
    [[=save::Field { "local_yaxis", 3 }]] FixedPointVector2 local_yaxis;
    FixedPointVector2* steering_target_position;
    std::uint32_t unidentified_state; //Native unsaved word: constructor/load
    //clear it; copy preserves it.
    [[=save::Field { "formation_pos", 1 }]] FixedPointVector2 formation_position;
    [[=save::Field { "force", 0 }]] FixedPointVector2 force;
    [[=save::Field { "path_timestamp", 17 }]] std::uint32_t path_timestamp;
    [[=save::Field { "path_target_pos", 16 }]] FixedPointVector2 path_target_position;
    [[=save::Field { "path_total_count", 18 }]] std::uint32_t path_total_count;
    [[=save::Field { "path_buffer_count", 9 }]] std::uint32_t path_buffer_count;
    [[=save::Field { "path_count", 10 }]] std::uint32_t path_count;
    [[=save::Field { "path_finished", 11 }]] std::uint8_t path_finished;
    std::array<FixedPointVector2, 5> subpath_buffer;
    std::array<std::int32_t, 5> subpath_distance;
    [[=save::Field { "path_total_dist", 19 }]] std::int32_t path_total_distance;
    [[=save::Field { "path_subpath_dist", 14 }]] std::int32_t path_subpath_distance;
    [[=save::Field { "path_tail_dist", 15 }]] std::int32_t path_tail_distance;
    [[=save::Field { "path_last_progress", 12 }]] std::int32_t path_last_progress;
    [[=save::Field { "path_stuck_buffer", 13 }]] std::uint32_t path_stuck_buffer;
    [[=save::Field { "nav_mode", 8 }]] std::uint32_t navigation_mode;
    [[=save::Field { "mCantReachDest", 7 }]] std::uint32_t cannot_reach_destination;
};

//Shared state of guest and staff records, after their common MapObjectBase.
//Runtime fields use native x64 pointers; fixed-point values retain their game
//units and save widths. This is independent of the original436-byte layout.
struct [[=save::ManualCodec {}]] MobileEntityState {
    MobileSpriteAnimationState animation;
    [[=save::Field { "mAccurateDestX", 0 }]] std::uint16_t accurate_destination_x;
    [[=save::Field { "mAccurateDestY", 1 }]] std::uint16_t accurate_destination_y;
    [[=save::Field { "mSpriteAngle", 13 }]] std::uint32_t sprite_angle;
    [[=save::Field { "mSpriteID", 14 }]] std::uint32_t sprite_id;
    [[=save::Field { "mESPSprite", 5 }]] std::uint32_t sprite_category;
    [[=save::Field { "mSpriteUnderRideCtrl", 15 }]] std::uint32_t sprite_under_ride_control;
    [[=save::Field { "mOnRideSoNoFloatingThingsAllowed", 16 }]] std::uint32_t no_floating_things_on_ride;
    [[=save::Field { "mCount", 4 }]] std::uint8_t count;
    MobileEntityThoughtHistory thought_history;
    [[=save::Field { "mBaseSpeed", 3 }]] std::uint16_t base_speed;
    [[=save::Field { "mPurposeSpeed", 11 }]] std::uint16_t purpose_speed;
    [[=save::Field { "mAdjustorSpeed", 2 }]] std::uint16_t adjustor_speed;
    [[=save::Field { "mPreviousSpeed", 7 }]] float previous_speed;
    [[=save::Field { "mWhingeTimeout", 17 }]] std::uint32_t whinge_timeout;
    [[=save::Field { "mLastRecordedMapId", 6 }]] CellId last_recorded_map_id;
    [[=save::Field { "mSetDestSuccessfully", 12 }]] std::uint32_t destination_set_successfully;
    MobileNavigationState navigation;
    [[=save::Field { "mPreviousX", 8 }]] std::int32_t previous_x;
    [[=save::Field { "mPreviousY", 9 }]] std::int32_t previous_y;
    [[=save::Field { "mStrandedTime", 10 }]] std::uint32_t stranded_time;
};
enum class PersonPrank : std::int32_t {
    none = 0,
    stink_bomb = 1,
    toilet_booby_trap = 2,
    litter_bin = 3,
    vegetation_fire = 4,
    drop_litter = 5,
    balloon = 6,
    vicious_rumour = 7,
    whinge_to_staff = 8
};
enum class PersonUmbrellaState : std::int32_t {
    none = 0,
    removal_pending = 1,
    inactive = 2,
    creation_pending = 3,
    active = 4
};

struct PersonNeeds {
    float thirst, hunger, toilet_need, sickness, litter_meter, fatigue_effect_meter;
};

struct [[=save::ManualCodec {}]] PersonRuntimeView {
    MapObjectBase base;
    MobileEntityState mobile;
    [[=save::Field { "mPriceToPayAtEnd", 41 }]] std::uint32_t price_to_pay_at_end;
    std::uint32_t force_thought_31;
    float happiness;
    [[=save::Field { "mCash", 9 }]] std::int32_t cash;
    [[=save::Field { "mCashSpent", 43 }]] std::int32_t cash_spent;
    PersonNeeds needs;
    [[=save::Field { "mExitLevel", 10 }]] std::int32_t exit_level;
    [[=save::Field { "mPrank", 21 }]] PersonPrank prank;
    [[=save::Field { "mPrankTimer", 22 }]] std::uint32_t prank_timer;
    [[=save::Field { "mNumRides", 14 }]] std::uint32_t number_of_rides;
    [[=save::Field { "mNumShops", 15 }]] std::uint32_t number_of_shops;
    [[=save::Field { "mNumSideshows", 16 }]] std::uint32_t number_of_sideshows;
    [[=save::Field { "mNumSideshowsWon", 17 }]] std::uint32_t number_of_sideshows_won;
    [[=save::Field { "mArrivalDate", 0 }]] std::uint32_t arrival_date;
    [[=save::Field { "mArrivalIndex", 1 }]] std::uint32_t arrival_index;
    [[=save::Field { "mMajorDest", 13 }]] ThingId major_destination;
    [[=save::Field { "mSavedMajorDest", 35 }]] ThingId saved_major_destination;
    [[=save::Field { "mPhotoBooth", 36 }]] ThingId photo_booth;
    std::array<ThingId, 4> previous_rides, previous_temporary_rides;
    [[=save::Field { "mPersonType", 20 }]] std::uint8_t person_type;
    [[=save::Field { "mQueuePos", 26 }]] std::uint8_t queue_position;
    [[=save::Field { "mQueueMoveDelay", 25 }]] std::uint32_t queue_move_delay;
    [[=save::Field { "mBeenAdmitted", 8 }]] std::uint32_t been_admitted;
    [[=save::Field { "mTimeStartedIdling", 40 }]] std::uint32_t time_started_idling;
    [[=save::Field { "mParkOpeningWaitingTime", 19 }]] std::uint32_t park_opening_waiting_time;
    [[=save::Field { "mPaidAdmission", 18 }]] std::uint32_t paid_admission;
    [[=save::Field { "mTimeOfLastSpotAnim", 39 }]] std::uint32_t time_of_last_spot_animation;
    float happiness_at_queue_entry;
    [[=save::Field { "mUmbrellaState", 6 }]] PersonUmbrellaState umbrella_state;
    std::uint32_t appearance_state_deadline_quarter_ticks;
    [[=save::Field { "mUmbrellaScript", 4 }]] std::uint32_t umbrella_script;
    [[=save::Field { "mUmbrellaType", 5 }]] std::uint32_t umbrella_type;
    [[=save::Field { "mExactSpriteTypeIndex", 7 }]] std::uint32_t exact_sprite_type_index;
    std::array<float, 8> sprite_geometry_x, sprite_geometry_y;
    std::uint8_t queue_admission_delay_active;
    std::uint32_t queue_admission_deadline_quarter_ticks;
    [[=save::Field { "mBalloonScript", 2 }]] std::uint32_t balloon_script;
    [[=save::Field { "mRemainingBalloonLife", 27 }]] std::uint32_t remaining_balloon_life;
    [[=save::Field { "mToiletBoobyFlg", 28 }]] std::uint32_t toilet_booby_flag;
    [[=save::Field { "mBalloonColour", 3 }]] std::uint32_t balloon_colour;
    [[=save::Field { "mGeneration", 29 }]] PersonGeneration generation;
    [[=save::Field { "mPreferredExcitement", 30 }]] std::uint32_t preferred_excitement;
    [[=save::Field { "mBoredomThreshold", 31 }]] std::uint32_t boredom_threshold;
    [[=save::Field { "mFoundWhingeVictimFlg", 32 }]] std::uint8_t found_whinge_victim;
    [[=save::Field { "mForcePrankFlg", 33 }]] std::uint8_t force_prank;
    [[=save::Field { "mFirstAidTimeout", 34 }]] std::int32_t first_aid_timeout;
    [[=save::Field { "mLastPosX", 11 }]] float last_position_x;
    [[=save::Field { "mLastPosY", 12 }]] float last_position_y;
    [[=save::Field { "mState", 38 }]] PersonState state;
    [[=save::Field { "mSavedState", 37 }]] PersonState saved_state;
    [[=save::Field { "mInfoDeskTimer", 42 }]] std::uint32_t info_desk_timer;
    [[=save::Field { "mQNext", 23 }]] ThingId queue_next;
    [[=save::Field { "mQPrev", 24 }]] ThingId queue_previous;
    float food_score, drink_score, bought_score, won_score, rides_score;
    [[=save::Field { "mWonLastThang", 44 }]] std::uint8_t won_last_thang;
    [[=save::Field { "mLastDayChecked", 45 }]] std::uint32_t last_day_checked;
};

struct ResourceCategoryVariantRange {
    std::string_view category_name;
    std::int32_t first_loaded_resource_index, loaded_resource_count;
};
enum class ParkObjectCapabilities : std::uint16_t {
    provides_relief = 1,
    chills_you_out = 2,
    is_choosable = 4,
    has_queue = 8,
    provides_security = 16,
    ride_handles_sprite = 32,
    holds_litter = 64,
    is_fireworks = 128,
    runs_continuously = 256,
    thinking_space = 512,
    training_room = 1024,
    is_usable = 2048,
    requires_watering = 4096,
    is_effector_feature = 8192
};
enum class ParkObjectState : std::int32_t {
    normal = 0,
    broken_down = 1,
    upgrade_requested = 2,
    non_usable = 3,
    condemned = 4
};
enum class ObjectUiCategory : std::int32_t {
    rides = 0,
    shops = 1,
    sideshows = 2,
    features = 3,
    code_4 = 4,
    facilities = 5,
    code_6 = 6
};
struct ParkThingHistoryState;

//Definition-driven Type3 record, with no per-instance native vtable. Runtime
//pointers/layout follow the host ABI; only labeled fields define save widths.
struct [[=save::ManualCodec {}]] ParkObjectRuntimeView {
    MapObjectBase base;
    [[=save::Field { "mNext", 9 }]] ThingId next_park_object;
    [[=save::Field { "mId", 1 }]] std::uint16_t definition_id;
    [[=save::Field { "mAngle", 0 }]] std::uint32_t angle;
    std::array<std::byte, 4> unknown_after_angle;
    std::uint64_t creation_file_time;
    [[=save::Field { "MeshInstanceID", 2 }]] std::uint32_t mesh_instance_id;
    [[=save::Field { "mRideScriptHandle", 4 }]] std::uint32_t script_id;
    [[=save::Field { "mTrackRideHandle", 5 }]] std::uint32_t track_ride_handle;
    [[=save::Field { "mIsTrackRideValid", 15 }]] std::int32_t is_track_ride_valid;
    [[=save::Field { "mUpgradeParent", 16 }]] ThingId upgrade_parent;
    [[=save::Field { "mPhotoBooth", 35 }]] ThingId photo_booth;
    [[=save::Field { "mBooby", 36 }]] ThingId booby_trap_owner;
    [[=save::Field { "mPrebuiltTrackOpen", 37 }]] std::uint8_t prebuilt_track_open;
    [[=save::Field { "mPrebuiltTrackEdited", 39 }]] std::uint8_t prebuilt_track_edited;
    [[=save::Field { "mBeenAroundAtLeastAMonth", 38 }]] std::uint8_t been_around_at_least_a_month;
    [[=save::Field { "mClosedDueToWeather", 45 }]] std::uint8_t closed_due_to_weather;
    [[=save::Field { "mFlags", 3 }]] ParkObjectCapabilities capabilities;
    [[=save::Field { "mTopLeft", 7 }]] CellId top_left;
    [[=save::Field { "mEntryPos", 8 }]] CellId entry_cell;
    [[=save::Field { "mExitPos", 13 }]] CellId exit_cell;
    [[=save::Field { "mBackOfQueue", 11 }]] CellId back_of_queue;
    [[=save::Field { "mFirstInQ", 14 }]] ThingId first_in_queue;
    std::array<std::byte, 2> unknown_after_first_in_queue;
    [[=save::Field { "mQueueSizeInCells", 30 }]] std::uint32_t queue_size_in_cells;
    float repair_state, working_life;
    [[=save::Field { "mGoneFirstPersonFlg", 29 }]] std::uint8_t gone_first_person;
    std::array<std::byte, 3> unknown_after_gone_first_person;
    float pv; //Literal save label; its gameplay meaning remains unresolved.
    [[=save::Field { "mUpgradeLevel", 34 }]] std::uint8_t current_upgrade;
    std::array<std::byte, 3> unknown_after_upgrade;
    [[=save::Field { "mUpgradeBalloonSprite", 33 }]] std::uint32_t upgrade_balloon_sprite;
    [[=save::Field { "mOperatingSpeed", 21 }]] std::uint32_t operating_speed;
    [[=save::Field { "mOperatingDuration", 20 }]] std::uint8_t operating_duration;
    [[=save::Field { "mOperatingCapacity", 19 }]] std::uint8_t operating_capacity;
    [[=save::Field { "mAssignedStaffMember", 10 }]] ThingId assigned_staff_member;
    [[=save::Field { "mTimeMarkedForMaintenance", 32 }]] std::uint32_t time_marked_for_maintenance;
    [[=save::Field { "mRequestedService", 31 }]] std::int32_t requested_service;
    [[=save::Field { "mCanLoad", 12 }]] std::int32_t can_load;
    [[=save::Field { "mPersonBeingLoaded", 22 }]] ThingId person_being_loaded;
    std::array<std::byte, 2> unknown_before_running_cost;
    [[=save::Field { "m_runningCost", 17 }]] std::uint32_t running_cost;
    [[=save::Field { "mCostOfGoods", 23 }]] std::uint32_t cost_of_goods;
    [[=save::Field { "mQualityOfGoods", 24 }]] std::uint32_t quality_of_goods;
    [[=save::Field { "mChanceOfWinning", 25 }]] std::uint32_t chance_of_winning;
    [[=save::Field { "mPricePerUse", 26 }]] std::uint32_t price_per_use;
    [[=save::Field { "mAmountOfSpecialIngredient", 27 }]] std::uint32_t amount_of_special_ingredient;
    [[=save::Field { "mPortionSize", 28 }]] std::uint32_t portion_size;
    [[=save::Field { "mState", 6 }]] ParkObjectState state;
    [[=save::Field { "mNumWalkAways", 18 }]] std::uint32_t num_walk_aways;
    std::array<char16_t, 33> name_a, name_b;
    [[=save::Field { "mBeenActivated", 42 }]] std::uint8_t activated;
    [[=save::Field { "mHasBeenOpenedAtSomePoint", 43 }]] std::uint8_t has_been_opened;
    std::uint8_t has_mesh_darkening_been_applied;
    [[=save::Field { "m_bFlashUnconnectedExit", 44 }]] std::uint8_t flash_unconnected_exit;
    ParkThingHistoryState* history;
    std::array<std::byte, 4> unknown_after_history;
    [[=save::Field { "mTimeOfLastSurveyReset", 41 }]] std::uint64_t last_survey_reset_file_time;
    [[=save::Field { "mPromotedAttraction", 40 }]] ThingId promoted_attraction;
    std::array<std::byte, 2> unknown_after_promoted_attraction;
    std::uint32_t unknown_324;
};

//Concrete payloads implemented by world_save.cpp. Unreversed byte regions are
//placeholders within these views; host layout is independent of file layout.
struct MechanicHqRecord {
    MapObjectBase base;
    ThingId next_object;
    std::array<std::byte, 2> unknown_tail;
};
enum class MissionCutsceneState : std::uint32_t {
    idle = 0,
    obstacle_advisor_playback = 1,
    removed_obstacle_playback = 2,
    completion_obstacle_playback = 3,
    completion_object_playback = 4
};

struct MissionControllerRecord {
    MapObjectBase base;
    MissionCutsceneState cutscene_state;
    ThingId cutscene_object;
    std::uint8_t research_project_add_attempt_pending;
    std::uint8_t mission_advisor_action_pending;
};
enum class AnimationPlaybackProfile : std::int32_t {
    code_0 = 0,
    code_1 = 1,
    code_2 = 2
};

struct AnimationControlFlags {
    std::uint8_t completion_requested, skip_enabled;
    std::int16_t saved_speed_percent;
    std::uint16_t objectives_open_counter;
};

struct AnimationManagerRecord {
    MapObjectBase base;
    std::uint32_t unknown_020;
    std::int32_t saved_first_person_distance, saved_fog_start, saved_fog_end;
    AnimationPlaybackProfile playback_profile;
    UiControl* playback_window;
    std::int32_t saved_toolbar_button_id;
};

struct RendererCacheValidity {
    std::uint32_t flags, generation;
};

struct Type23Record {
    MapObjectBase base;
    std::uint32_t unknown_020;
};

//These names follow the save labels. A queue entry also carries the same
//bytes as AdvisorActionCandidate: event WORD/DWORD, action ID and score.
struct AdvisorQueueEntry {
    [[=save::Field { "mThingId", 0 }]] std::uint16_t thing_id;
    std::array<std::byte, 2> untouched_002;
    [[=save::Field { "mSecondary", 1 }]] std::uint32_t secondary;
    [[=save::Field { "mResponseNo", 5 }]] std::uint32_t response_no;
    [[=save::Field { "mMessage", 2 }]] std::uint32_t message;
    [[=save::Field { "mScore", 3 }]] std::int32_t score;
    [[=save::Field { "mValid", 4 }]] std::uint8_t valid;
    [[=save::Field { "mOverrideOnlyOnce", 6 }]] std::uint8_t override_only_once;
    [[=save::Field { "mScoreOverridden", 7 }]] std::uint8_t score_overridden;
    std::byte untouched_023;
};

struct [[=save::ManualCodec {}]] AdvisorRecord {
    MapObjectBase base;
    [[=save::Field { "mTimeSig", 0 }]] std::uint32_t time_signature;
    [[=save::Field { "mSoundId", 1 }]] std::uint32_t sound_id;
    std::array<AdvisorQueueEntry, 8> queue;
    [[=save::Field { "mPanelUpFlg", 8 }]] std::uint8_t panel_up;
    std::array<std::byte, 3> untouched_221;
    [[=save::Field { "mNextAvailableQueuePos", 2 }]] std::uint32_t next_available_queue_pos;
    [[=save::Field { "mLastActionStarted", 3 }]] std::uint32_t last_action_started;
    [[=save::Field { "mLastActionDuration", 4 }]] std::uint32_t last_action_duration;
    AdvisorQueueEntry special_candidate;
    [[=save::Field { "mRequestedLastTutorial", 5 }]] std::uint8_t requested_last_tutorial;
    std::array<std::byte, 3> untouched_261;
    [[=save::Field { "mMessagePlaying", 6 }]] std::uint32_t message_playing;
    [[=save::Field { "m_bFromHelpButton", 9 }]] std::uint8_t from_help_button;
    [[=save::Field { "m_bFromUIPanel", 10 }]] std::uint8_t from_ui_panel;
    std::array<std::byte, 2> untouched_270;
    [[=save::Field { "m_lAdvisorDelayTicks", 11 }]] std::uint32_t delay_ticks;
    [[=save::Field { "m_msgDelayedMessage", 7 }]] std::uint32_t delayed_message;
};
enum class ResearchStaffRequirement : std::int32_t {
    default_staff = 0,
    gardener = 1,
    handyman = 2,
    mechanic = 3,
    entertainer = 4,
    guard = 5,
    researcher = 6
};
enum class ResearchCategory : std::int32_t {
    rides = 0,
    shops = 1,
    sideshows = 2,
    features = 3,
    upgrades = 4,
    mission_obstacle = 5,
    facilities = 6
};
enum class ResearchCategoryFilter : std::int32_t {
    rides = 0,
    shops = 1,
    sideshows = 2,
    features = 3,
    upgrades = 4,
    mission_obstacle = 5,
    facilities = 6,
    all = 7
};
enum class ResearchProjectCostBand {
    lower = 0,
    middle = 1,
    higher = 2
};
inline constexpr std::int32_t no_research_project = -1;
inline constexpr std::int32_t initial_research_project = -4;
enum class MissionResearchStatus {
    disabled = 0,
    incomplete = 1,
    complete = 2,
    opened_automatically = 3,
    removed = 4
};

struct MissionResearchQuery {
    std::uint16_t definition_id;
    ThingId placed_thing;
    MissionResearchStatus status;
};
enum class ThemeAbility : std::int32_t {
    none = 0,
    ride_upgrading = 1,
    construction = 2,
    rock_blasting = 3,
    demolition = 4,
    tunnelling = 5,
    stonemasonry = 6,
    firefighting = 7,
    rubble_removal = 8,
    first_aid = 9,
    water_drainage = 10,
    landfill = 11,
    landscaping = 12,
    excavation = 13,
    logging = 14,
    count = 15
};
enum class SpecialRideRequirementType : std::uint32_t {
    handyman = 0,
    mechanic = 1,
    guard = 3,
    gardener = 5,
    end = 7
};

struct SpecialRideRegistryMember {
    [[=save::Field { "AllSpecialRidesType", 2 }]] SpecialRideRequirementType type;
    [[=save::Field { "AllSpecialRidesAbility", 1 }]] ThemeAbility ability;
    [[=save::Field { "AllSpecialRidesThingId", 0 }]] ThingId assigned_staff;
    std::uint16_t reserved;
};

struct SpecialRideRegistryGroup {
    std::array<SpecialRideRegistryMember, 20> members;
    ThingId thing_id;
    std::uint16_t reserved;
};

struct ResearchProject {
    [[=save::Field { "StaffRqd", 8 }]] ResearchStaffRequirement staff_required;
    [[=save::Field { "StaffSA", 9 }]] ThemeAbility staff_ability;
    [[=save::Field { "mCat", 1 }]] ResearchCategory category;
    [[=save::Field { "mType", 7 }]] std::uint32_t type;
    [[=save::Field { "mObjId", 0 }]] std::uint16_t object_definition_id;
    [[=save::Field { "mResearchGroup", 2 }]] std::uint8_t research_group;
    [[=save::Field { "mUpgradeLevel", 3 }]] std::uint8_t upgrade_level;
    [[=save::Field { "mCost", 4 }]] float cost;
    [[=save::Field { "mCashPerBrainPoint", 5 }]] std::int32_t cash_per_brain_point;
    [[=save::Field { "mResearchDone", 6 }]] float research_done;
    std::uint32_t row_index; //Runtime index, omitted from the save schema.
};

struct ResearchGroup {
    [[=save::Field { "effort", 0 }]] std::uint32_t effort;
    [[=save::Field { "output", 1 }]] std::uint32_t output;
    [[=save::Field { "currentProject", 2 }]] std::int32_t current_project;
    [[=save::Field { "highestGroupAvailable", 3 }]] std::uint8_t highest_group_available;
    std::array<std::byte, 3> padding;
};

struct [[=save::ManualCodec {}]] ResearchLabRecord {
    MapObjectBase base;
    [[=save::Field { "pcurrentp", 4 }]] std::int32_t primary_current_project;
    [[=save::Field { "scurrentp", 5 }]] std::int32_t secondary_current_project;
    std::array<ThingId, 2> consultants;
    std::array<std::byte, 4> unknown_032;
    std::array<ThingId, 20> primary_staff, secondary_staff;
    [[=save::Field { "pavail", 6 }]] std::uint8_t primary_available;
    [[=save::Field { "savail", 7 }]] std::uint8_t secondary_available;
    std::array<std::byte, 2> unknown_118;
    std::array<ResearchProject, 256> projects;
    std::array<ResearchGroup, 7> groups;
    [[=save::Field { "mNumProjects", 0 }]] std::uint32_t num_projects;
    [[=save::Field { "mWorkLoad", 1 }]] std::uint32_t workload;
    [[=save::Field { "mMostExpensiveProject", 2 }]] std::int32_t most_expensive_project;
    [[=save::Field { "mLabActive", 3 }]] std::uint8_t lab_active;
    std::array<std::byte, 3> unknown_9461;
    [[=save::Field { "lPrits", 8 }]] std::uint32_t l_prits;
    [[=save::Field { "lPricat", 9 }]] std::uint32_t l_pricat;
    [[=save::Field { "lPrirt", 10 }]] std::uint32_t l_prirt;
    [[=save::Field { "lSeccat", 11 }]] std::uint32_t l_seccat;
};

//Contiguous observed Balance region: the group initializer scans across the
//five category records, their parser extent, and StartingWorkLoad.
struct ResearchBalanceInput {
    std::array<std::int32_t, 10> percentage_for_tech;
    std::uint32_t tech_parsed_extent;
    std::array<std::int32_t, 5> category_effort;
    std::uint32_t category_parsed_extent;
    std::int32_t starting_workload;
    float speed_scale_down, cost_scale_up;
};

struct CatalogUpgradeResearchRow {
    std::int32_t cost_of_research, cash_per_brain_point;
    std::uint32_t estimated_price_variance;
    std::uint32_t scrap_value_year1, scrap_value_year2, scrap_value_year3, scrap_value_year4;
    std::uint32_t initial_capacity, redline_capacity, initial_duration, redline_duration, initial_speed, redline_speed, wear_rate;
    float queue_wait_time_constant;
    std::uint32_t cost_of_upgrade;
    std::array<std::byte, 8> other_upgrade_tail;
};

struct ResearchStaffRequirements {
    ResearchStaffRequirement staff;
    ThemeAbility ability;
};

struct CatalogAddOnInputs {
    std::uint32_t upgrades_id;
    std::int32_t upgrade_type, upgrades_any_ride;
};

struct CatalogResearchInputs {
    std::uint32_t linked_with;
    std::int32_t group;
    ResearchCategory category;
    std::int32_t type;
    std::array<ResearchStaffRequirements, 2> staff;
    std::uint32_t staff_count;
    std::array<CatalogUpgradeResearchRow, 3> upgrades;
    std::uint32_t upgrade_count;
    std::array<std::byte, 52> unrelated_usage_tail;
    std::array<std::uint32_t, 14> skill_set_counts;
    std::uint32_t skill_set_count;
    std::array<std::byte, 12> unrelated_skill_tail;
    float breakdown_imminent, breakdown_warn_player;
    std::array<std::byte, 16> unrelated_add_on_tail;
};

struct DefinitionGridCell {
    std::int32_t code;
    std::uint8_t attribute;
};

struct DefinitionGrid {
    std::int32_t width, height;
    std::array<DefinitionGridCell, 1600> cells;
};

struct CellMeshTemplate {
    std::int32_t definition_index;
    std::string_view filename; //Empty denotes a native null filename/alias row.
    std::uint32_t metadata;
};

struct CellMeshPlacement {
    std::int32_t orientation_degrees, tile_x, tile_y;
    const DefinitionGrid* geometry;
};

struct CellMeshRegistration {
    std::int32_t definition_index;
    std::uint32_t creation_flags;
    CellMeshPlacement placement;
    std::uint32_t embedded_node_resource_selector, owner_context_or_rsse_state;
    ParkObjectRuntimeView* associated_park_object;
    std::int32_t footprint_width_tiles, footprint_height_tiles, slot_index, callback_context;
};

struct CellMeshRefresh {
    std::uint32_t mesh_instance_id;
    std::int32_t definition_index;
    std::uint32_t creation_flags;
    CellMeshPlacement placement;
    std::int32_t unused_argument_5;
    ParkObjectRuntimeView* owner_park_object;
    const void* resource_context;
    const void* object_identity;
    bool placement_present = true; //TOUR refresh passes a null placement pointer.
};

struct MeshDefinitionTemplateLoad {
    std::string_view directory, filename;
    const DefinitionGrid* placement_geometry;
    //Cell loaders alias the placement grid in native argument4, while option8
    //is clear. Preserve identity without claiming it is a HoardingEdgeLayout.
    const void* hoarding_layout;
    std::uint32_t load_options, object_definition_key, animation_control_count;
    const char16_t *sign_text_override, *sign_source_override;
    std::uint32_t footprint_width_override, footprint_height_override;
};
enum class MeshAnimationRequest : std::int32_t {
    clear = 12,
    hold_first = 13,
    hold_last = 14
};
enum class ParkObjectMeshVariant : std::int32_t {
    closed = 1,
    construction = 2,
    condemned = 4,
    upgrade = 8
};
enum class MeshAnimationTransitionFlags : std::uint32_t {
    loop = 1,
    force = 2,
    bit_4 = 4,
    skip_resource_apply = 8
};

constexpr MeshAnimationTransitionFlags operator|(MeshAnimationTransitionFlags a, MeshAnimationTransitionFlags b) {
    return static_cast<MeshAnimationTransitionFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

//Partial semantic catalog view. Typed grid fields use natural host storage;
//this is neither the native object layout nor a serialized wire image.
struct ObjectDefinitionResourceCache {
    std::array<std::byte, 48> unrelated_prefix;
    std::array<std::uint32_t, 4> particle_template_indices;
    std::uint32_t ride_sound_catalog_slot;
    std::array<std::byte, 20> unrelated_tail;
};

struct ResearchCatalogDefinition {
    std::uint16_t id;
    std::array<std::byte, 146> unrelated_prefix;
    std::uint32_t destroy_particle_effect;
    std::uint32_t repair_particle_effect;
    std::array<std::byte, 48> unrelated_identity_prefix_tail;
    std::int32_t is_photo_booth;
    std::array<std::byte, 124> unrelated_identity_tail;
    std::int32_t which_track_type;
    std::array<std::byte, 72> unrelated_prebuilt_tail;
    std::uint32_t usage_fatigue_effect;
    std::array<std::byte, 72> unrelated_usage_middle;
    std::uint32_t max_capacity;
    std::array<std::byte, 12> unrelated_duration_parameters;
    std::uint32_t max_speed;
    std::array<std::byte, 68> unrelated_usage_effects;
    std::uint32_t usage_rip_off_ok;
    std::array<std::byte, 12> unrelated_usage_prefix_tail;
    CatalogResearchInputs research;
    std::array<std::byte, 544> names_and_resource_cache;
    ObjectUiCategory which_ui_type;
    ObjectDefinitionResourceCache resource_tail;
    std::uint32_t mesh_load_failed;
    std::uint32_t preview_mesh_template_id {};
    bool preview_mesh_template_loaded {};
    std::array<std::uint32_t, 8> additional_mesh_template_ids {};
    std::uint32_t additional_mesh_template_count {};
    CatalogAddOnInputs add_on;
    std::int32_t is_special;
    std::int32_t opened_automatically;
    std::int32_t gate_to;
    DefinitionGrid* after_shape; //Borrowed resource; cache ownership is unported.
    std::uint32_t shape_offset_x, shape_offset_y;
    std::int32_t delete_after_use;
    std::uint32_t replace_with_id;
    std::uint8_t cached_entry_direction;
    std::array<std::int32_t, 2> cached_entry_position {}, cached_exit_position {}, cached_map_offset {};
    std::uint8_t cached_exit_direction {};
    std::int32_t requires_teleport;
    PersonAppearanceEffect appearance_effect;
    std::int32_t attraction_value, new_attraction_decay_time;
    std::array<std::int32_t, 3> attraction_new_bonus;
    //The catalog asset owns the complete reflected SAM model. New simulation
    //consumers borrow it instead of maintaining another field projection.
    const ObjectDefinitionBalance* balance {};
    std::string_view resource_catalog_name; //Borrowed from the owning catalog asset.
    const ObjectDefinitionAsset* source_asset {};
};
struct RideDescriptionBalance;

struct ObjectCatalogManager {
    std::array<ResearchCatalogDefinition*, 330> entries;
    std::uint32_t count;
    std::array<char16_t, 516> display_name_scratch;
    std::uint32_t iterator_index;
    std::uint16_t cached_definition_id, cached_alignment;
    ResearchCatalogDefinition* cached_entry;
    const RideDescriptionBalance* shared_ride_description {}; //Borrowed from catalog owner.
};

struct ObjectControlRecord {
    //These component annotations encode one native mObjectControls[i] field;
    //component labels are not emitted as separate save headers.
    [[=save::Field { "definition_id", 0 }]] std::uint16_t definition_id;
    [[=save::Field { "reserved", 1 }]] std::uint16_t reserved;
    [[=save::Field { "purchase_cost", 2 }]] std::uint32_t purchase_cost;
    [[=save::Field { "copied_fatigue_effect", 3 }]] std::uint32_t copied_fatigue_effect;
    [[=save::Field { "copied_rip_off_ok", 4 }]] std::uint32_t copied_rip_off_ok;
    [[=save::Field { "available", 5 }]] std::uint8_t available;
    [[=save::Field { "padding", 6 }]] std::array<std::byte, 3> padding;
    [[=save::Field { "research_option_index", 7 }]] std::uint32_t research_option_index;
    [[=save::Field { "live_instance_count", 8 }]] std::uint32_t live_instance_count;
    [[=save::Field { "first_instance_game_tick", 9 }]] std::int32_t first_instance_game_tick;
    [[=save::Field { "placement_name_ordinal", 10 }]] std::uint32_t placement_name_ordinal;
};

struct ObjectControlTable {
    std::array<ObjectControlRecord, 150> rows;
    [[=save::Field { "mNumObjectControls", 0 }]] std::uint32_t count;
    [[=save::Field { "mPreviousSearchKey", 1 }]] std::uint16_t cached_definition_id;
    std::array<std::byte, 2> reserved;
    ObjectControlRecord* cached_record;
};

enum class StaffType : std::int32_t {
    handyman = 0,
    mechanic = 1,
    entertainer = 2,
    guard = 3,
    researcher = 4,
    gardener = 5,
    unclassified = 6,
    unsupported_index = 7,
};
enum class StaffState : std::int32_t {
    idle = 0,
    code_0 = idle,
    patrolling = 1,
    walking_to_rest_area = 2,
    resting = 3,
    walking_to_strike = 4,
    striking = 5,
    walking_to_obstacle = 6,
    walking_to_training_room = 7,
    training = 8,
    stunned = 9,
    picked_up = 10,
    frozen = 11,
    frozen_editing_patrol_area = 12,
    walking_to_task = 13,
    being_whinged_at = 14,
    walking_to_litter = 15,
    cleaning_up = 16,
    walking_to_clean_toilet = 17,
    cleaning_toilet = 18,
    walking_to_clean_bin = 19,
    cleaning_bin = 20,
    walking_to_breakdown = 21,
    repairing = 22,
    entertaining = 23,
    researching = 24,
    walking_to_research_room = 25,
    prosecuting = 26,
    catching = 27,
    walking_to_entrance = 28,
    returning_to_patrol = 29,
    first_aiding = 30,
    walking_to_water_plant = 31,
    watering_plant = 32,
    walking_to_help_research = 33,
    helping_research = 34,
    waiting_for_empty_staff_room = 35,
    be_forced_to_destination = 36,
    training_paused = 37,
    walking_to_training_room_paused = 38
};
enum class ResearchWorkSlot : std::int32_t {
    primary = 0,
    secondary = 1
};

template <class Value, std::size_t Capacity>
struct [[=save::ManualCodec {}]] SampleHistory {
    [[=save::Field { "mTemp", 3 }]] Value temporary;
    std::array<Value, Capacity> data;
    [[=save::Field { "mCurrentEntry", 0 }]] std::int32_t current_entry;
    [[=save::Field { "mNumEntries", 1 }]] std::int32_t num_entries;
    [[=save::Field { "mWrappedAround", 2 }]] std::uint8_t wrapped;
};

using StaffActivityRing = SampleHistory<float, 30>;
enum class StaffActivity : std::uint8_t {
    working,
    patrolling,
    resting,
    training,
    researching,
    striking
};
//STAFFSTATES.str row indices, distinct from the six history-channel indices.
enum class StaffActivityStatus : std::int32_t {
    idle = 0,
    looking_for_work = 1,
    working = 2,
    resting = 3,
    on_strike = 4,
    picked_up = 5,
    frozen = 6,
    researching = 7,
    training = 8
};

struct StaffAbilitySlotOrCount {
    std::uint32_t bits;

    ThemeAbility ability() const {
        return std::bit_cast<ThemeAbility>(bits);
    }

    std::uint32_t count() const {
        return bits;
    }
};

struct StaffAreaBounds {
    [[=save::Field { "m_sx", 0 }]] std::int32_t start_x;
    [[=save::Field { "m_sy", 1 }]] std::int32_t start_y;
    [[=save::Field { "m_ex", 2 }]] std::int32_t end_x;
    [[=save::Field { "m_ey", 3 }]] std::int32_t end_y;
};

struct StaffMembershipHeader {
    std::uint32_t bits;

    ThingKind role() const {
        return static_cast<ThingKind>(bits & 255u);
    }
};

struct StaffAreaMembership {
    StaffAreaBounds bounds;
    StaffMembershipHeader header;
    //The area runtime owns this range. Save restoration empties it; only bounds
    //are serialized. The header's upper three bytes are not initialized there.
    CellId *begin, *end, *capacity;
};

//Shared staff runtime view, including the roster link present in every role.
//Guests and staff share MobileEntityState; staff never embed Person state.
struct [[=save::ManualCodec {}]] StaffRuntimeRecord {
    MapObjectBase base;
    MobileEntityState mobile;
    [[=save::Field { "mState", 3 }]] StaffState state;
    [[=save::Field { "mSavedState", 5 }]] StaffState saved_state;
    [[=save::Field { "mForcedState", 4 }]] StaffState forced_state;
    std::array<char16_t, 33> name;
    [[=save::Field { "mTimeHired", 7 }]] std::uint64_t time_hired;
    [[=save::Field { "mExperience", 8 }]] std::uint32_t experience;
    [[=save::Field { "mSalary", 0 }]] std::uint32_t salary;
    ThemeAbility first_known_ability;
    //The original union is transferred twice: second ability, then count. Bits
    //give both interpretations without reading an inactive C++ union member.
    StaffAbilitySlotOrCount second_ability_or_count;
    [[=save::Field { "mCurrentlyLearning", 9 }]] ThemeAbility currently_learning;
    [[=save::Field { "mSAProgress", 11 }]] std::uint32_t ability_progress;
    [[=save::Field { "mSlotLocation", 12 }]] std::int32_t slot_location;
    [[=save::Field { "mGoingToLearn", 10 }]] ThemeAbility going_to_learn;
    float happiness_score, motivation, energy;
    [[=save::Field { "mTimeStartedIdling", 6 }]] std::uint32_t time_started_idling;
    [[=save::Field { "mJobsDone", 1 }]] std::uint32_t jobs_done;
    [[=save::Field { "mAgency", 23 }]] std::uint32_t agency;
    [[=save::Field { "mRestArea", 2 }]] ThingId rest_area;
    StaffAreaMembership area;
    [[=save::Field { "mWageDemandDay", 24 }]] std::uint32_t wage_demand_day;
    [[=save::Field { "mWageDemandPay", 25 }]] std::uint32_t wage_demand_pay;
    [[=save::Field { "mResearching", 26 }]] std::uint8_t researching;
    [[=save::Field { "mTimeStartedResearching", 20 }]] std::uint32_t time_started_researching;
    [[=save::Field { "mRRoom", 21 }]] ThingId research_room;
    [[=save::Field { "mRmode", 22 }]] std::uint8_t research_mode;
    [[=save::Field { "mWorkingTicks", 13 }]] std::uint32_t working_ticks;
    [[=save::Field { "mPatrollingTicks", 14 }]] std::uint32_t patrolling_ticks;
    [[=save::Field { "mRestingTicks", 15 }]] std::uint32_t resting_ticks;
    [[=save::Field { "mIdleTicks", 16 }]] std::uint32_t idle_ticks;
    [[=save::Field { "mTrainingTicks", 17 }]] std::uint32_t training_ticks;
    [[=save::Field { "mResearchingTicks", 18 }]] std::uint32_t researching_ticks;
    [[=save::Field { "mStrikeTicks", 19 }]] std::uint32_t strike_ticks;
    std::array<StaffActivityRing, 6> activity_rings;
    ThingId next_staff;
    //Host-only ownership guard while room-exit notifications run callbacks.
    //The explicit staff save schema deliberately excludes this transient state.
    bool room_occupancy_detaching {};
};

struct [[=save::ManualCodec {}]] MechanicRuntimeRecord {
    StaffRuntimeRecord staff;
    [[=save::Field { "mDurationOfRepair", 0 }]] std::uint32_t repair_duration;
    [[=save::Field { "mObjectToRepair", 1 }]] ThingId object_to_repair;
};

struct [[=save::ManualCodec {}]] HandymanRuntimeRecord {
    StaffRuntimeRecord staff;
    [[=save::Field { "mTimeStartedCleaning", 1 }]] std::uint32_t time_started_cleaning;
    [[=save::Field { "mTargetLitterCell", 0 }]] CellId target_litter_cell;
    [[=save::Field { "mToiletToClean", 2 }]] ThingId toilet_to_clean;
    [[=save::Field { "mBinToClean", 3 }]] ThingId bin_to_clean;
};

struct [[=save::ManualCodec {}]] EntertainerRuntimeRecord {
    StaffRuntimeRecord staff;
    [[=save::Field { "mTimeStartedEntertaining", 0 }]] std::uint32_t time_started_entertaining;
};

struct [[=save::ManualCodec {}]] GuardRuntimeRecord {
    StaffRuntimeRecord staff;
    [[=save::Field { "mPerp", 0 }]] ThingId perpetrator;
    [[=save::Field { "mPatientId", 1 }]] ThingId patient;
    [[=save::Field { "mProsecutionTimestamp", 2 }]] std::uint32_t prosecution_timestamp;
    [[=save::Field { "mFirstAidTimestamp", 3 }]] std::uint32_t first_aid_timestamp;
};

using ResearcherRuntimeRecord = StaffRuntimeRecord;

struct [[=save::ManualCodec {}]] GardenerRuntimeRecord {
    StaffRuntimeRecord staff;
    [[=save::Field { "mTimeStartedWatering", 0 }]] std::uint32_t time_started_watering;
    [[=save::Field { "mPlantToWater", 1 }]] ThingId plant_to_water;
};

struct StaffWorkRate {
    std::int32_t base, trained;
};

struct StaffWorkParameters {
    std::int32_t motivation_factor, max_experience;
    std::array<StaffWorkRate, 6> by_role;
};
enum class StaffStrikeStage : std::uint32_t {
    none = 0,
    warning_issued = 1,
    one_week = 2,
    two_weeks = 3,
    one_month = 4,
};

struct StaffAbilitySlot {
    [[=save::Field { "staff_type", 0 }]] StaffType staff_type;
    [[=save::Field { "required_ability", 1 }]] std::int32_t required_ability;
    [[=save::Field { "assigned_staff", 2 }]] ThingId assigned_staff;
    [[=save::Field { "reserved", 3 }]] std::array<std::byte, 2> padding;
};

struct StaffStrikeState {
    [[=save::Field { "stage", 0 }]] StaffStrikeStage stage;
    [[=save::Field { "strike_active", 1 }]] std::uint32_t strike_active;
    [[=save::Field { "last_processed_game_tick", 2 }]] std::uint32_t last_processed_game_tick;
};

struct StaffTrainingBudget {
    std::array<std::uint32_t, 6> budget;
    std::uint8_t have_ever_trained;
    std::array<std::byte, 3> padding;
};

struct CdkRegistrationRecord {
    std::int32_t object_id = 0, theme_category = 4;
    std::uint32_t exclude_from_coaster_source_picker = 2147483647u;
    std::int32_t source_picker_amount = -1;
};

struct ThemeGlobalBalance {
    [[=sam::IntegerField { "Tickets", "CanEarnTicketsInTheme" }]] std::int32_t can_earn_tickets_in_theme {};
    [[=sam::IntegerField { "Tickets", "CanSpendTicketsInTheme" }]] std::int32_t can_spend_tickets_in_theme {};
    [[=sam::IntegerField { "ParkInfo", "AvailableFromStart" }]] std::int32_t available_from_start {};
    [[=sam::IntegerField { "ParkName", "GateObjectId", true }]] std::int32_t gate_object_id {};
};

struct ThemeGlobalSaveInfo {
    std::uint32_t source_ordinal {};
    std::int32_t current_tech_level {};
    std::optional<ThemeGlobalBalance> global_balance_fields {};
    std::string source_name {};
    std::array<std::uint8_t, 6> opaque_bytes {};
    std::array<std::uint8_t, 4> metadata_valid {};
    std::array<std::int32_t, 4> metadata_values {};
    std::array<std::uint8_t, 8> award_winner_flags {};
    std::array<std::int32_t, 8> award_scores {};
    std::array<char16_t, 33> sign_name_a {}, sign_name_b {};
    std::uint8_t is_open {}, is_available {}, name_changed {}, all_research_completed {}, all_challenges_done {};
    std::int32_t challenges_done {};
};

inline constexpr std::uint32_t theme_source_count = 5;
inline constexpr std::array<std::string_view, theme_source_count> theme_global_source_names { "arabian", "science", "water", "wIntro", "Unknown" };

struct AdvisorActionHistoryEntry {
    std::uint32_t last_played_world_tick {};
    std::int32_t last_response_variant_index = -1;
    std::uint8_t is_active {};
    std::array<std::byte, 3> original_padding {};
    std::uint32_t cancel_count {}, play_count {};
};

struct SharesOwnershipContext {
    std::int32_t shares_owned = -1, ownership_percentage = -1;
    std::u16string current_profile_name {};
};

struct SharePriceContext {
    std::int32_t current_share_price = -1;
    std::u16string company_title {};
};

struct ShareFinanceAnnualReportDelta {
    std::int32_t profit_change {}, visitor_change {}, rating_difference {};
    double income_change {}, park_value_change {};
};

struct ShareFinanceSavedRecord {
    std::uint32_t opaque_000 {}, report_year {};
    std::int32_t history_difference {}, history_sum {}, history_average {};
    std::uint32_t opaque_014 {};
    double history_sum_a {}, history_sum_b {};
    //The rebuilt record owns its comparison by value. Native reset leaked the
    //separately allocated comparison; no external owner needs that allocation.
    ShareFinanceAnnualReportDelta year_over_year {};
    std::uint32_t opaque_02c {};
};

struct SharesFinanceState {
    std::unique_ptr<SharesOwnershipContext> ownership_context {};
    std::unique_ptr<SharePriceContext> share_price_context {};
    std::array<std::unique_ptr<ShareFinanceSavedRecord>, 3> record_slots {};
};

//Natural runtime storage for the compiled Theme state. The larger legacy
//manager save-record schema needs a separate codec.
struct ThemeGlobalInfoManager {
    ThemeGlobalInfoManager() = default;
    ThemeGlobalInfoManager(const ThemeGlobalInfoManager&) = delete;
    ThemeGlobalInfoManager& operator=(const ThemeGlobalInfoManager&) = delete;
    ThemeGlobalInfoManager(ThemeGlobalInfoManager&&) = delete;
    ThemeGlobalInfoManager& operator=(ThemeGlobalInfoManager&&) = delete;
    std::int32_t challenge_failures_remaining {}, mission_number {}, current_promotion_index {}, golden_ticket_balance {};
    std::list<std::u16string> swear_word_list {}, allowed_word_list {};
    std::array<std::uint8_t, 8> global_park_award_flags {}, advisor_eligibility_flags {};
    std::uint32_t reset_zero_reserved_dword {};
    std::uint8_t language_word_filters_enabled {}, manager_startup_action_pending {}, mission_world_event_latch {}, end_of_game_event_processed {};
    ThemeGlobalSaveInfo* selected_theme_info {};
    std::map<std::string, ThemeGlobalSaveInfo, std::less<>> theme_info_records {};
    std::array<CdkRegistrationRecord, 256> cdk_registrations {};
    std::uint32_t cdk_registration_count {}, cdk_registration_cursor {};
    MissionResearchBalance mission_research {};
    std::array<std::uint8_t, 15> theme_ability_activation_flags {};
    std::uint8_t mission_obstacle_research_complete {}, obstacle_removed_message_received {};
    std::uint8_t research_lab_action_532_eligible {};
    std::array<std::int32_t, 15> lobby_area_available_flags {};
    std::array<MissionObjectiveTrackingEntry, 5> objective_entries {};
    std::u16string save_file_basename {};
    std::uint8_t use_gmi_suffix {};
    std::array<AdvisorActionHistoryEntry, 644> history {};
};

struct StaffHqRecord {
    MapObjectBase base;
    std::array<StaffAbilitySlot, 20> staff_slots;
    std::array<std::uint32_t, 15> trained_staff;
    StaffTrainingBudget training;
    std::array<StaffStrikeState, 6> strikes;
    ThingId staff_member_picked_up;
    std::array<std::byte, 2> padding;
    std::uint32_t force_strike;
};

struct BankLoan {
    [[=save::Field { "mLoans[loan].loan_available", 0 }]] std::int32_t loan_available;
    [[=save::Field { "mLoans[loan].amount_available", 1 }]] std::int32_t amount_available;
    [[=save::Field { "mLoans[loan].APR_in_percent", 2 }]] std::int32_t apr_in_percent;
    [[=save::Field { "mLoans[loan].repayment_period_in_months", 3 }]] std::int32_t repayment_period_in_months;
    [[=save::Field { "mLoans[loan].monthly_repayment", 4 }]] std::int32_t monthly_repayment;
    [[=save::Field { "mLoans[loan].loan_bought", 5 }]] std::int32_t loan_bought;
    [[=save::Field { "mLoans[loan].months_repaid", 6 }]] std::int32_t months_repaid;
    [[=save::Field { "mLoans[loan].lenderNameIndex", 7 }]] std::int32_t lender_name_index;
};

//Standard.sam LoanInfo has10 descriptor slots; the bank constructor consumes8.
struct BankLoanOffer {
    std::int32_t amount, apr_in_percent, repayment_period_in_months, lender_name_index;
};

struct BankRecord {
    MapObjectBase base;
    [[=save::Field { "mBalance", 1 }]] std::int32_t balance;
    [[=save::Field { "mBatchBalance", 2 }]] std::int32_t batch_balance;
    std::array<BankLoan, 8> loans;
    [[=save::Field { "mWithdrawalsEnabled", 3 }]] std::int32_t withdrawals_enabled;
    [[=save::Field { "mAdmissionFee", 0 }]] std::int32_t admission_fee;
    [[=save::Field { "mLastBalance", 4 }]] std::int32_t last_balance;
    [[=save::Field { "mTurnEnteredRed", 5 }]] std::int32_t turn_entered_red;
    [[=save::Field { "mProfitThisYear", 6 }]] std::int32_t profit_this_year;
};

using AnalyserDwordHistory144 = SampleHistory<std::uint32_t, 144>;
using ParkThingDwordHistory30 = SampleHistory<std::uint32_t, 30>;
using AnalyserWordHistory3 = SampleHistory<std::uint16_t, 3>;
using AnalyserDwordHistory50 = SampleHistory<std::uint32_t, 50>;
using AnalyserDwordHistory5 = SampleHistory<std::uint32_t, 5>;
using AnalyserByteHistory50 = SampleHistory<std::uint8_t, 50>;

struct ParkThingHistoryState {
    std::array<ParkThingDwordHistory30, 13> dword_history_30;
    std::array<AnalyserDwordHistory144, 6> dword_history_144;
};

struct AnalyserMetric {
    std::uint32_t last_calculation_tick;
    //Different metric slots use different numeric encodings. The wire codec
    //preserves the complete four-byte value without guessing a conversion.
    [[=save::Field { "mVal", 0 }]] std::uint32_t cached_value_bits;
    [[=save::Field { "mWorkedOut", 1 }]] std::uint8_t cache_valid;
};

struct [[=save::ManualCodec {}]] AnalyserGridRecord {
    [[=save::Field { "mStartX", 0 }]] std::int32_t start_x;
    [[=save::Field { "mStartY", 1 }]] std::int32_t start_y;
    [[=save::Field { "mSizeX", 2 }]] std::int32_t size_x;
    [[=save::Field { "mSizeY", 3 }]] std::int32_t size_y;
    std::array<AnalyserMetric, 9> metrics;
};

struct [[=save::ManualCodec {}]] ParkAnalyserState {
    MapObjectBase base;
    [[=save::Field { "mMapSquaresPerCell", 0 }]] std::uint32_t map_squares_per_cell;
    [[=save::Field { "mGridSizeX", 1 }]] std::uint32_t grid_size_x;
    [[=save::Field { "mGridSizeY", 2 }]] std::uint32_t grid_size_y;
    [[=save::Field { "mMapSizeX", 3 }]] std::uint32_t map_size_x;
    [[=save::Field { "mMapSizeY", 4 }]] std::uint32_t map_size_y;
    std::array<AnalyserGridRecord, 1024> grid;
    //Runtime-only scan scratch. The x86 allocation reserved36 entries; a square
    //scan can encounter more litter. Host storage covers the complete map.
    std::array<CellId, map_cell_count> litter_candidate_cells;
    std::array<AnalyserWordHistory3, 4> short_histories;
    std::array<AnalyserMetric, 6> metrics;
    std::array<AnalyserDwordHistory144, 21> long_histories;
    AnalyserDwordHistory50 medium_history;
    AnalyserDwordHistory5 tiny_history;
    std::array<AnalyserByteHistory50, 22> byte_histories;
    [[=save::Field { "mMisbehavingKids", 5 }]] std::uint32_t misbehaving_kids;
    [[=save::Field { "mMisbehavingKidsThatGotAwayWithIt", 6 }]] std::uint32_t misbehaving_kids_that_got_away;
    [[=save::Field { "mMisbehavingKidsEver", 7 }]] std::uint32_t misbehaving_kids_ever;
    [[=save::Field { "mLifetimeVisitors", 8 }]] std::uint32_t lifetime_visitors;
    [[=save::Field { "mVisitorsThisMonth", 9 }]] std::uint32_t visitors_this_month;
    [[=save::Field { "mLastStrike", 10 }]] std::uint64_t last_strike;
    [[=save::Field { "mLastPuke", 11 }]] std::uint64_t last_puke;
    [[=save::Field { "mLastPrank", 12 }]] std::uint64_t last_prank;
    [[=save::Field { "mParkLastOpened", 13 }]] std::uint64_t park_last_opened;
    [[=save::Field { "mLastObjectBuilt", 14 }]] std::uint64_t last_object_built;
    [[=save::Field { "mLastStaffHired", 15 }]] std::uint64_t last_staff_hired;
    [[=save::Field { "mMostPaidForTicket", 16 }]] std::uint32_t most_paid_for_ticket;
    [[=save::Field { "mLargestBusLoad", 17 }]] std::uint32_t largest_bus_load;
    [[=save::Field { "mLongestStay", 18 }]] std::uint32_t longest_stay;
    [[=save::Field { "mNumBreakdownsEver", 19 }]] std::uint32_t breakdowns_ever;
    [[=save::Field { "mNumBalloonsSoldEver", 20 }]] std::uint32_t balloons_sold_ever;
    [[=save::Field { "mNumCostumesSoldEver", 21 }]] std::uint32_t costumes_sold_ever;
    std::uint32_t umbrella_sales_count;
};

using ParkAnalyserFinanceView = ParkAnalyserState;

struct UiMessageReceiverRecord {
    MapObjectBase base;
};

enum class WeatherPrecipitation : std::int32_t {
    rain = 0,
    snow = 1,
    none = 2
};
enum class WeatherCategory : std::int32_t {
    code_0 = 0,
    heatwave = 1,
    rain = 2,
    storm = 3,
    snow = 4
};
using WorldVector3 = std::array<float, 3>;

//Input names come from Standard.sam Weather/WeatherEffects descriptors.
struct WeatherParameters {
    [[=sam::IntegerField { "Weather", "DaysOfWarning", true }]] std::int32_t days_of_warning {};
    [[=sam::IntegerField { "Weather", "DaysBetweenChanges", 1, 1000 }]] std::int32_t days_between_changes {};
    [[=sam::IntegerField { "Weather", "YearsBeforeFirstFreakWeather" }]] std::int32_t years_before_first_freak_weather {};
    [[=sam::IntegerField { "Weather", "SpeedOfChange", 1, 100 }]] std::int32_t speed_of_change {};
    [[=sam::IntegerField { "Weather", "MinDaysForWeatherSpell", true }]] std::int32_t min_days_for_weather_spell {};
    [[=sam::IntegerField { "Weather", "MaxDaysForWeatherSpell", true }]] std::int32_t max_days_for_weather_spell {};
    [[=sam::IntegerField { "Weather", "MinDaysForFreakWeatherSpell", true }]] std::int32_t min_days_for_freak_weather_spell {};
    [[=sam::IntegerField { "Weather", "MaxDaysForFreakWeatherSpell", true }]] std::int32_t max_days_for_freak_weather_spell {};
};

struct WeatherEffectsParameters {
    [[=sam::IntegerField { "WeatherEffects", "QualityForNormalHigh", -1, 101 }]] std::int32_t normal_high {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForNormalLow", -1, 101 }]] std::int32_t normal_low {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForHeatWaveHigh", -1, 101 }]] std::int32_t heatwave_high {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForHeatWaveLow", -1, 101 }]] std::int32_t heatwave_low {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForSnowLow", -1, 101 }]] std::int32_t snow_low {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForSnowHigh", -1, 101 }]] std::int32_t snow_high {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForRainLow", -1, 101 }]] std::int32_t rain_low {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForRainHigh", -1, 101 }]] std::int32_t rain_high {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForLightningLow", -1, 101 }]] std::int32_t lightning_low {};
    [[=sam::IntegerField { "WeatherEffects", "QualityForLightningHigh", -1, 101 }]] std::int32_t lightning_high {};
    [[=sam::IntegerField { "WeatherEffects", "LightningCountdownSpeed", 1, 1000 }]] std::int32_t lightning_countdown_speed {};
    [[=sam::IntegerField { "WeatherEffects", "LightningCountdownFrom", 1, 10000 }]] std::int32_t lightning_countdown_from {};
    [[=sam::IntegerField { "WeatherEffects", "LightningTime", true }]] std::int32_t lightning_time {};
    [[=sam::IntegerField { "WeatherEffects", "ThunderStartsThisFarAway", 0, 15 }]] std::int32_t thunder_starts_this_far_away {};
    [[=sam::IntegerField { "WeatherEffects", "ThunderRandomiser", 1, 4 }]] std::int32_t thunder_randomiser {};
    [[=sam::IntegerField { "WeatherEffects", "MaxRaindrops", 0, 1000 }]] std::int32_t max_raindrops {};
};

//The simulation-facing state, independent of the renderer's original layout.
//Texture ownership and the particle renderer remain explicit host services.
struct WindEffectRuntimeState {
    std::uint32_t precipitation_modes = 1;
    bool flash_pending = false;
    std::int32_t precipitation_amount = 0;
    WorldVector3 drift_vector { 0.0f, -1.0f, 0.0f };
    float rain_motion_scale = 5.0f, snow_motion_scale = 0.7f;
};
enum class WeatherSound {
    rain,
    thunder
};

struct WeatherHudState {
    bool visible {};
    std::int32_t attachment_part {}, help_query {};
};

struct WeatherScheduleEntry {
    [[=save::Field { "mMonth", 0 }]] std::int32_t month;
    [[=save::Field { "mType", 4 }]] WeatherCategory type;
    [[=save::Field { "mQuality", 1 }]] std::int32_t quality;
    [[=save::Field { "mLength", 2 }]] std::int32_t length;
    [[=save::Field { "mValid", 3 }]] std::uint8_t valid;
    std::array<std::byte, 3> padding;
};

struct WeatherRecord {
    MapObjectBase base;
    WeatherScheduleEntry pending, active;
    [[=save::Field { "mQuality", 0 }]] std::int32_t quality;
    [[=save::Field { "mLength", 1 }]] std::int32_t length; //Distinct from schedule length; runtime units remain unresolved.
    [[=save::Field { "mCurrentDrops", 2 }]] std::int32_t current_drops;
    [[=save::Field { "mTargetDrops", 3 }]] std::int32_t target_drops;
    [[=save::Field { "mLightning", 4 }]] std::uint8_t lightning;
    std::array<std::byte, 3> padding_077;
    [[=save::Field { "mLightningCountdown", 5 }]] std::int32_t lightning_countdown;
    [[=save::Field { "mLightningNumThisPeriod", 6 }]] std::int32_t lightning_count_this_period;
    [[=save::Field { "mThunderCountdown", 7 }]] std::int32_t thunder_countdown;
    [[=save::Field { "mLastLightningLocation", 8 }]] std::array<float, 3> last_lightning_location;
    [[=save::Field { "mRainSoundHandle", 9 }]] std::int32_t rain_sound_handle;
    [[=save::Field { "mPType", 10 }]] WeatherPrecipitation precipitation;
    [[=save::Field { "mType", 11 }]] WeatherCategory type;
    [[=save::Field { "mWindDir", 12 }]] float wind_direction;
    [[=save::Field { "mWindForce", 13 }]] float wind_force;
    [[=save::Field { "mOverridden", 14 }]] std::uint8_t overridden;
    std::array<std::byte, 3> padding_125;
};

enum class ChallengeStatus : std::int32_t {
    na = 0,
    available = 1,
    preparing = 2,
    active = 3,
    complete = 4
};
enum class ChallengeType : std::int32_t {
    code_0 = 0,
    salt_item_sales = 1,
    fat_item_sales = 2,
    ice_item_sales = 3,
    sugar_item_sales = 4,
    gift_sales = 5,
    balloon_sales = 6,
    costume_sales = 7,
    restaurant_uses = 8,
    code_9 = 9,
    code_10 = 10,
    visitor_admissions = 11,
    shop_use_profit = 12,
    sideshow_use_profit = 13,
    ride_research_events = 16,
    code_17 = 17,
    code_19 = 19,
    code_20 = 20,
    code_22 = 22,
    code_24 = 24,
    code_25 = 25,
    code_27 = 27,
    code_28 = 28,
    code_34 = 34,
    code_35 = 35,
    code_36 = 36,
    code_37 = 37,
    code_38 = 38,
    code_39 = 39,
    code_40 = 40,
    code_41 = 41,
    code_42 = 42,
    code_43 = 43,
    ice_or_sugar_revenue = 45,
    code_46 = 46,
    code_48 = 48,
    code_50 = 50,
    code_51 = 51,
    code_52 = 52,
    code_53 = 53,
    code_55 = 55,
    code_56 = 56,
    code_57 = 57,
    code_58 = 58,
};
//The serializer transfers this script selector without validation. RSSE owns
//its interpretation; keeping the distinct type avoids confusing it with IDs.
enum class ChallengeScriptIndex : std::int32_t;

struct ChallengeRecord {
    [[=save::Field { "mType", 0 }]] ChallengeType type;
    [[=save::Field { "mTargetTime", 1 }]] std::int32_t target_time;
    [[=save::Field { "mTargetVal", 2 }]] std::int32_t target_value;
    [[=save::Field { "mTargetVal2", 3 }]] std::int32_t target_value2;
    [[=save::Field { "mTargetObj", 4 }]] ThingId target_object;
    [[=save::Field { "mTargetObj2", 5 }]] ThingId target_object2;
    [[=save::Field { "mTargetStaffType", 6 }]] std::int32_t target_staff_type;
    [[=save::Field { "mCashPrize", 7 }]] std::int32_t cash_prize;
    [[=save::Field { "mGTPrize", 24 }]] std::int32_t gt_prize;
    [[=save::Field { "mSharePrize", 25 }]] std::int32_t share_prize;
    [[=save::Field { "mSharePriceEffectIfWon", 26 }]] std::int32_t share_price_effect_if_won;
    [[=save::Field { "mSharePriceEffectIfLost", 27 }]] std::int32_t share_price_effect_if_lost;
    [[=save::Field { "mDoubleOrNothingFollowup", 8 }]] std::int32_t double_or_nothing_followup;
    [[=save::Field { "mCheckAtEndOnly", 9 }]] std::uint8_t check_at_end_only;
    std::array<std::byte, 3> padding_049;
    [[=save::Field { "mGracePeriodLimit", 16 }]] std::int32_t grace_period_limit;
    [[=save::Field { "mTriggerData", 23 }]] std::int32_t trigger_data;
    [[=save::Field { "mGraceTargetVal", 17 }]] std::int32_t grace_target_value;
    [[=save::Field { "mMinDaysTilTrigger", 14 }]] std::int32_t min_days_til_trigger;
    [[=save::Field { "mMaxDaysTilTrigger", 15 }]] std::int32_t max_days_til_trigger;
    [[=save::Field { "mDelay", 13 }]] std::int32_t delay;
    [[=save::Field { "mSender", 28 }]] std::int32_t sender;
    [[=save::Field { "mScriptIndex", 29 }]] ChallengeScriptIndex script_index;
    [[=save::Field { "mMissonNeeded", 30 }]] std::int32_t mission_needed;
    [[=save::Field { "mPromoGiven", 31 }]] std::int32_t promo_given;
    [[=save::Field { "mChallengeLost", 10 }]] std::int32_t challenge_lost;
    [[=save::Field { "mOfferTimeSet", 18 }]] std::uint8_t offer_time_set;
    std::array<std::byte, 7> padding_097;
    [[=save::Field { "mOfferTime", 19 }]] std::uint64_t offer_time;
    [[=save::Field { "mValue", 21 }]] std::int32_t value;
    [[=save::Field { "mValue2", 22 }]] std::int32_t value2;
    std::uint64_t started_at_world_time; //Initialized on load; omitted from the stream.
    [[=save::Field { "mFinishTIme", 20 }]] std::uint64_t finish_time;
    [[=save::Field { "mCurrentStatus", 11 }]] ChallengeStatus status;
    [[=save::Field { "mThingInFocus", 12 }]] ThingId thing_in_focus;
    [[=save::Field { "mSentTimeWarning", 32 }]] std::uint8_t sent_time_warning;
    std::byte padding_143;
};

struct ChallengeManagerRecord {
    MapObjectBase base;
    std::array<std::byte, 4> unknown_020;
    std::array<ChallengeRecord, 10> records;
    std::uint64_t next_challenge_delay_time;
    std::uint8_t system_switched_on;
    std::uint8_t offer_latch;
    std::array<ThingId, 10> actual_thing;
    std::array<std::byte, 2> unknown_tail;
};

//Both kinds18/19 use this saved Challenge layout. The distinct chat-presence
//constructor and its online state remain an unported integration dependency.

struct ThingMetadata {
    ThingHeader* payload;
    std::uint32_t id_storage;
    ThingMetadata* active_next;
    ThingMetadata* active_previous;
    ThingMetadata* free_next;
    ThingKind allocation_kind {}; //Stable identity survives the live retirement tag.
};

struct RemovalNode {
    ThingId id;
    std::uint16_t unknown_02; //Append/pop never initialize or consume this WORD.
    RemovalNode* previous;
    RemovalNode* next;
};
struct RemovalQueue;

struct RemovalQueue {
    RemovalNode* tail;
    RemovalNode* head;
    std::uint32_t count;
};

struct CellRenderData {
    //Component names only select bytes within the single native mTileData field.
    [[=save::Field { "texture_group", 0 }]] std::uint32_t texture_group;
    [[=save::Field { "texture_member", 1 }]] std::uint32_t texture_member;
    [[=save::Field { "orientation_degrees", 2 }]] std::uint32_t orientation_degrees;
};

struct CellBaseFields {
    [[=save::Field { "mMeshInstance", 2 }]] std::uint32_t mesh_instance;
    [[=save::Field { "mType", 7 }]] std::int32_t type;
    [[=save::Field { "mNeighbours", 3 }]] std::uint8_t neighbors;
    [[=save::Field { "mDirection", 0 }]] std::uint8_t direction;
    [[=save::Field { "mFlags", 1 }]] std::uint16_t flags;
    [[=save::Field { "mParentID", 5 }]] CellId parent_id;
    std::array<std::byte, 2> padding_014;
    [[=save::Field { "mTileData", 6 }]] CellRenderData render_data;
    [[=save::Field { "mOverlapCounter", 4 }]] std::int16_t overlap_counter;
    [[=save::Field { "mHoardingNeighbours", 8 }]] std::uint8_t hoarding_neighbors;
    std::uint8_t padding_031;
};
enum class CellStorageKind : std::uint8_t {
    map = 1,
    track = 2
};

struct CellBase {
    CellId index;
    CellStorageKind record_kind;
    std::uint8_t unknown_003;
    CellBaseFields common;
};

struct MapCell {
    CellBase base;
    [[=save::Field { "mIsObstacle", 8 }]] std::uint32_t is_obstacle;
    [[=save::Field { "mWho", 7 }]] ThingId who;
    [[=save::Field { "mStatusFlags", 5 }]] std::uint8_t status_flags;
    std::uint8_t unknown_043;
    [[=save::Field { "mLitterScript", 2 }]] std::uint32_t litter_request;
    [[=save::Field { "mLitter", 0 }]] std::uint32_t litter_selector;
    [[=save::Field { "mLitterCollector", 1 }]] ThingId litter_collector;
    std::array<std::byte, 2> unknown_054;
    [[=save::Field { "mTimeMarkedForLitterCollection", 6 }]] std::uint32_t litter_collection_tick;
    [[=save::Field { "mLitterScript", 3 }]] std::uint32_t litter_auxiliary_request;
    //Process-only scratch at native +40h. Coverage queries clear the entire
    //base map then mark the union of custom patrol rectangles for one role.
    std::int32_t staff_patrol_coverage;
    [[=save::Field { "mPylonIndex", 4 }]] std::uint16_t pylon_index;
    std::array<std::byte, 2> unknown_070;
};

struct StaffCandidate {
    [[=save::Field { "mType", 0 }]] StaffType type;
    [[=save::Field { "mName", 1 }]] std::uint32_t name_index;
    [[=save::Field { "mSalary", 2 }]] std::uint32_t salary;
    [[=save::Field { "mAgency", 3 }]] std::uint32_t agency_index;
    [[=save::Field { "mSubType", 4 }]] std::uint8_t resource_variant;
    [[=save::Field { "mValid", 5 }]] std::uint8_t valid;
    [[=save::Field { "mOnPointer", 6 }]] std::uint8_t on_pointer;
    std::uint8_t unknown_019;
    [[=save::Field { "mTimeSig", 7 }]] std::uint32_t creation_tick;
    [[=save::Field { "mTimeoutTime", 8 }]] std::uint32_t timeout;
    [[=save::Field { "mExperience", 9 }]] std::uint32_t experience;
};

struct PendingStaffPlacement {
    StaffCandidate candidate;
    std::size_t source_slot;
    std::uint32_t preview_resource {};
};

struct StaffPoolState {
    std::array<std::uint32_t, 6> linked_count;
    std::uint32_t last_update_tick;
    std::uint8_t opening_pool_generated;
    std::array<std::uint8_t, 6> stop_producing;
    std::array<std::byte, 5> unknown_tail;
};

struct FileTime {
    [[=save::Field { "low", 0 }]] std::uint32_t low;
    [[=save::Field { "high", 1 }]] std::uint32_t high;
};

struct SystemTimeFields {
    std::uint16_t year, month, day_of_week, day, hour, minute, second, millisecond;
};

//The shared nine-DWORD day-report block, not the persistent calendar markers.
//Its last two fields are copied but never written by the calendar routine.
struct CalendarReportFields {
    std::int32_t second, minute, hour, day_of_month, zero_based_month, year_since_1900;
    std::int32_t day_of_week, untouched_1c, untouched_20;
};

struct CalendarState {
    [[=save::Field { "mFunnyTimeStart", 0 }]] FileTime funny_time_start;
    [[=save::Field { "mSessionStart", 1 }]] FileTime session_start;
    [[=save::Field { "mMonthAtLastUpdate", 3 }]] std::uint32_t month_at_last_update;
    [[=save::Field { "mDayAtLastUpdate", 4 }]] std::uint32_t day_at_last_update;
    [[=save::Field { "mYearAtLastUpdate", 2 }]] std::uint32_t year_at_last_update;
    [[=save::Field { "mFunnySecsPerRealSec", 5 }]] std::uint32_t funny_seconds_per_real_second;
};

struct ArrivalState {
    [[=save::Field { "mArrivalRate", 0 }]] std::uint32_t arrival_rate;
    [[=save::Field { "mTimeSig", 1 }]] std::uint32_t time_signature;
    [[=save::Field { "mTargetVehicleCapacity", 2 }]] std::uint32_t target_capacity;
    [[=save::Field { "mPeopleOnBus", 3 }]] std::uint32_t people_on_bus;
    [[=save::Field { "mOffloading", 4 }]] std::uint8_t offloading;
    [[=save::Field { "mGatesOpen", 5 }]] std::uint8_t gates_open;
    std::array<std::byte, 2> unknown_tail;
};

struct WorldTail {
    [[=save::Field { "mRandomSeed", 14 }]] std::uint32_t random_seed;
    [[=save::Field { "mGameTick ", 5 }]] std::uint32_t game_tick;
    [[=save::Field { "mParkClosed ", 8 }]] std::uint32_t park_closed;
    [[=save::Field { "mNumberOfVisitorsToDate ", 9 }]] std::uint32_t number_of_visitors_to_date;
    [[=save::Field { "mStaffHQ ", 16 }]] ThingId staff_hq;
    [[=save::Field { "mMechanicHQ ", 6 }]] ThingId mechanic_hq;
    ThingId advisor;
    [[=save::Field { "mTagSystem ", 17 }]] ThingId tag_system;
    [[=save::Field { "mParkAnalyser ", 7 }]] ThingId park_analyser;
    [[=save::Field { "mResearchLab ", 15 }]] ThingId research_lab;
    [[=save::Field { "mWeather ", 19 }]] ThingId weather;
    [[=save::Field { "mBankAccount ", 3 }]] ThingId bank_account;
    [[=save::Field { "mUIMsgReceiver ", 18 }]] ThingId ui_receiver;
    [[=save::Field { "mCurrentArrivalVehicle ", 4 }]] ThingId current_arrival_vehicle;
    [[=save::Field { "mArrivalVehicle_Size1 ", 0 }]] ThingId arrival_vehicle_size_1;
    [[=save::Field { "mArrivalVehicle_Size2 ", 1 }]] ThingId arrival_vehicle_size_2;
    [[=save::Field { "mArrivalVehicle_Size3 ", 2 }]] ThingId arrival_vehicle_size_3;
    [[=save::Field { "mParkGates ", 10 }]] ThingId park_gates;
    [[=save::Field { "mTrafficLights ", 12 }]] ThingId traffic_lights;
    ThingId challenge_manager, mission_controller;
    [[=save::Field { "mCurrentObstacle ", 13 }]] ThingId current_obstacle;
    [[=save::Field { "mLobby ", 11 }]] ThingId lobby;
    ThingId animation_manager, helper_23;
    std::array<std::byte, 2> unknown_058;
    [[=save::Field { "mWorldState ", 20 }]] WorldState state;
    [[=save::Field { "mFirstHandyman", 21 }]] ThingId first_handyman;
    [[=save::Field { "mFirstGardener", 27 }]] ThingId first_gardener;
    [[=save::Field { "mFirstMechanic", 22 }]] ThingId first_mechanic;
    [[=save::Field { "mFirstEntertainer", 23 }]] ThingId first_entertainer;
    [[=save::Field { "mFirstResearcher", 25 }]] ThingId first_researcher;
    [[=save::Field { "mFirstGuard", 24 }]] ThingId first_guard;
    [[=save::Field { "mFirstObject", 26 }]] ThingId first_park_object;
    std::array<std::byte, 6> unknown_tail;
};

//Cell and object-control save fields use explicit metadata. The remaining
//region-profile storage retains its measured extent without an invented
//schema.
struct TrackCell {
    CellBase base;
    [[=save::Field { "mSegmentNumber", 0 }]] std::uint16_t segment_number;
    std::array<std::byte, 2> unknown_tail;
};

//Separate10-byte effect layout; not a subtype of the map/track common header.
struct RegionEffectCell {
    [[=save::Field { "mpEffect[i]", 0 }]] std::int16_t happiness;
    [[=save::Field { "mpEffect[i]", 1 }]] std::int16_t illness;
    [[=save::Field { "mpEffect[i]", 2 }]] std::int16_t hunger;
    [[=save::Field { "mpEffect[i]", 3 }]] std::int16_t security;
    [[=save::Field { "mpEffect[i]", 4 }]] std::int16_t attraction;
};

struct BlockSaveRegion {
    void* base;
    std::uint32_t size_bytes;
};

//Verified two-region prefix; the original complete class size is not claimed.
struct BlockSavePrefix {
    std::array<BlockSaveRegion, 2> regions;
};
enum class RegionEffectProfileId : std::int32_t {
    entertainer = 0,
    clean_toilet = 1,
    vomit_puddle = 2,
    guard = 3,
    security_camera = 4,
    stink_bomb = 5,
    dirty_toilet = 6,
    fireworks = 7,
    clean_bin = 8,
    dirty_bin = 9,
    fresh_plant = 10,
    dry_plant = 11,
    very_dry_plant = 12,
    dead_plant = 13,
    other_feature_effector = 14
};

struct RegionEffectProfile {
    RegionEffectCell effects;
    std::uint8_t radius;
    std::byte unknown_0b;
};

struct WorldRegionEffectsView {
    RegionEffectCell* cells {};
    RegionEffectProfile* profiles {};
};

struct VegetationWeatherParameters {
    float heatwave_wither {}, rain_wither {}, storm_wither {}, snow_wither {};
};

struct World {
    std::array<StaffCandidate, 32> staff_candidates;
    StaffPoolState staff_pool;
    CalendarState calendar;
    std::array<std::byte, 4> award_receiver;
    ArrivalState arrival;
    std::array<MapCell, map_cell_count> map_cells;
    std::array<TrackCell, map_cell_count> track_cells;
    std::uint32_t grid_change_serials[33][33];
    std::array<RegionEffectCell, map_cell_count> region_cells;
    std::array<RegionEffectProfile, 15> region_profiles;
    ObjectControlTable object_controls;
    WorldTail saved;
};
} //namespace simcoaster
