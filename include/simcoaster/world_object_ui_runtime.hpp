#pragma once
#include "world_object_ui.hpp"
#include "world_ui.hpp"
#include <memory>

namespace simcoaster {
class ParkConstructionRuntime;
//Original503630 routing, including controllers owned by other subsystems.
enum class ParkObjectUiPanel {
    none,
    ride,
    shop,
    sideshow,
    bathroom,
    photo_kiosk,
    info_desk,
    litter_bin,
    feature,
    secondary_ride,
    special_ride,
    staff_room,
    training_room,
    research,
    mode3
};

struct ParkObjectUiRoute {
    ParkObjectUiPanel panel {};
    ThingId target {};
};
enum class ParkObjectDurationUnit : std::int32_t {
    none,
    seconds,
    laps,
    cycles,
    repetitions
};

struct ParkObjectUiSettingRange {
    std::int32_t minimum {}, maximum {}, step = 1;
    bool visible = true, enabled = true;
};

struct ParkObjectUiStagedSettings {
    std::int32_t speed {}, capacity {}, duration {}, price {}, quality {}, ingredient {}, portion {}, chance {}, goods_cost {};
    ThingId promoted_attraction {};
};

struct ParkObjectUiMetrics {
    std::int32_t users {}, walkaways {}, winners {}, monthly_running_cost {}, excitement_meter {}, condition_meter {}, repair_meter {}, life_meter {}, satisfaction_meter {}, value_meter {}, goods_cost {}, contents_meter {}, capacity {}, gender {};
    std::uint32_t scrap_value {}, owned_count {};
};

struct ParkObjectSettingsUiState {
    bool open {}, apply_to_matching {}, can_navigate {}, closed {}, can_toggle_open {};
    ParkObjectUiPanel panel {};
    ThingId object {};
    std::uint16_t definition {};
    std::u16string name, promoted_attraction_name, ingredient_caption, related_ride_name;
    std::array<std::int32_t, 4> definition_modifiers {};
    std::int32_t build_cost {};
    bool build_cost_valid {};
    ParkObjectUiStatus status {};
    ParkObjectDurationUnit duration_unit {};
    std::uint32_t capacity_caption_row = 936;
    ParkObjectUiStagedSettings staged;
    ParkObjectUiSettingRange speed, capacity, duration, price, quality, ingredient, portion, chance, goods_cost;
    std::int32_t speed_redline_meter {}, capacity_redline_meter {};
    std::uint32_t upgrade_level {};
    ParkObjectUiMetrics metrics;
    std::uint64_t revision {};
    //Reload provenance, separate from metric refresh: native widgets own
    //in-flight edit values until initial load/navigation/status reload.
    std::uint64_t input_revision {};
};

struct ParkObjectListUiRow {
    ThingId object {};
    std::optional<ThingId> parent_object;
    std::uint16_t definition {};
    std::u16string name;
    ParkObjectUiStatus status {};
    std::array<std::uint8_t, 4> color {};
    std::int32_t users {}, winners {}, excitement_meter {}, satisfaction_meter {}, repair_meter {}, profit {}, goods_cost {}, value_meter {}, owned_count {}, gender {};
    bool has_usage {}, has_finances {}, has_satisfaction {}, has_cleanliness {}, has_value {};
};

struct ParkObjectListUiState {
    bool open {};
    ObjectUiCategory category = ObjectUiCategory::rides;
    ThingId selected {};
    std::array<bool, 6> available {};
    std::array<std::int32_t, 6> sort_keys { 1, 1, 1, 1, 1, 1 };
    std::vector<ParkObjectListUiRow> rows;
    bool details_visible {};
    std::int32_t monthly_running_cost {}, profit_to_date {};
    std::array<std::int32_t, 4> definition_modifiers {};
    std::uint32_t upgrade_level {};
    std::uint64_t revision {};
};
enum class ParkObjectFinanceMode : std::int32_t {
    amount,
    add,
    percent
};
enum class ParkObjectFinanceCategory : std::size_t {
    admission,
    photo,
    ride,
    shop,
    sideshow,
    bathroom
};

struct ParkObjectFinanceUiState {
    bool open {};
    ParkObjectFinanceMode mode {};
    std::array<std::int32_t, 6> values {};
    std::array<bool, 6> enabled {};
};

struct ParkObjectStatisticsUiState {
    bool open {};
    ThingId object {};
    std::u16string name;
    StatisticsGraphUiState graph;
    std::int32_t takings {}, running_cost {}, goods_cost {}, profit {}, total_profit {};
    bool show_goods_cost {}, show_prizes_cost {};
    std::uint64_t revision {};
};

struct ParkObjectOpinionsUiState {
    bool open {};
    ThingId object {};
    std::u16string name;
    std::int32_t days {}, users {}, walkaways {};
    std::uint32_t days_caption_row = 432;
    std::array<std::uint32_t, 2> queue_caption_rows {};
    bool show_second_queue {};
    std::array<std::int32_t, 5> satisfaction {};
    std::array<std::int32_t, 3> price {};
    std::array<std::int32_t, 2> queue {};
    std::uint64_t revision {};
};

struct ParkObjectPromotionUiRow {
    ThingId object {};
    std::u16string name;
    ParkObjectUiStatus status {};
    std::array<std::uint8_t, 4> color {};
};

struct ParkObjectPromotionUiState {
    bool open {}, return_to_settings {};
    ThingId info_desk {}, selected {};
    ObjectUiCategory category = ObjectUiCategory::rides;
    std::int32_t sort_key = 1;
    std::vector<ParkObjectPromotionUiRow> rows;
};
enum class ParkObjectUpgradeKind {
    attachment,
    level,
    cancel,
    information
};

struct ParkObjectUpgradeUiRow {
    std::int32_t key {};
    ParkObjectUpgradeKind kind {};
    std::u16string name;
    std::int32_t cost {};
    bool affordable {};
};

struct ParkObjectUpgradeUiState {
    bool open {}, trained_mechanic {};
    std::optional<std::uint32_t> message_row;
    ThingId object {};
    std::int32_t selected {}, sort_key = 1;
    std::vector<ParkObjectUpgradeUiRow> rows;
};

struct ParkObjectSpecialRequirementUiRow {
    std::size_t slot {};
    SpecialRideRequirementType type = SpecialRideRequirementType::end;
    ThemeAbility ability {};
    ThingId staff {};
    std::u16string name, role, ability_name;
    bool fulfilled {}, ability_activated {};
};

struct ParkObjectSpecialUiState {
    bool open {}, ready {}, can_activate {}, abilities_activated {};
    ThingId object {};
    std::u16string name;
    std::int32_t ticket_cost {}, ticket_balance {};
    std::vector<ParkObjectSpecialRequirementUiRow> requirements;
    std::uint64_t revision {};
};

struct ParkObjectContextUiState {
    ParkObjectUiRoute route;
    std::int32_t menu_family {};
    std::u16string name;
    bool closed {}, can_open {}, can_close {}, can_request_service {}, can_cancel_service {}, can_request_gardener {}, can_request_janitor {}, unregistered_special {}, can_upgrade {}, can_ride {}, can_edit_track {}, can_delete {}, can_copy {};
};

struct ParkObjectUiState {
    ParkObjectSettingsUiState settings;
    ParkObjectListUiState list;
    ParkObjectFinanceUiState finance;
    ParkObjectStatisticsUiState statistics;
    ParkObjectOpinionsUiState opinions;
    ParkObjectPromotionUiState promotion;
    ParkObjectUpgradeUiState upgrade;
    ParkObjectSpecialUiState special;
};

//Gameplay/model half of the original park-object controllers. Original
//templates, preview owners, Advisor open/help/close and control events belong
//to the connected frontend. No UiControl pointers enter these models.
class WorldParkObjectUiRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldParkObjectUiRuntime(WorldRuntime&, ParkConstructionRuntime&);
    ~WorldParkObjectUiRuntime();
    WorldParkObjectUiRuntime(const WorldParkObjectUiRuntime&) = delete;
    WorldParkObjectUiRuntime& operator=(const WorldParkObjectUiRuntime&) = delete;
    ParkObjectUiState& state();
    const ParkObjectUiState& state() const;
    ParkObjectUiRoute route(ThingId) const;
    ParkObjectContextUiState context(ThingId) const;
    void set_retirement_observer(std::function<void(ThingId)>);
    void retire(const ThingHeader&);
    void handle_event(const WorldEvent&);
    void refresh();
    void close_all();

    ParkObjectUiRoute open_settings(ThingId);
    void refresh_settings();
    void apply_settings();
    void navigate_settings(bool previous);
    void close_settings();
    void set_closed(ThingId, bool);
    void request_service(ThingId, bool);
    void open_list();
    void open_list(ObjectUiCategory, ThingId selected = 0);
    void select_list(ThingId);
    void sort_list(std::int32_t signed_key);
    void refresh_list();
    void open_finance();
    void set_finance_mode(ParkObjectFinanceMode);
    void apply_finance();
    void open_statistics(ThingId);
    void set_statistics_range(StatisticsGraphRange);
    void toggle_statistics_series(std::uint32_t selection);
    void refresh_statistics();
    void open_opinions(ThingId);
    void reset_opinions();
    void refresh_opinions();
    void open_promotion(ThingId info_desk, bool return_to_settings);
    void filter_promotion(ObjectUiCategory);
    void sort_promotion(std::int32_t signed_key);
    void choose_promotion(ThingId attraction);
    void open_upgrade(ThingId);
    void refresh_upgrade();
    bool apply_upgrade(std::int32_t key);
    void open_special(ThingId);
    void refresh_special();
    bool activate_special();
};
} //namespace simcoaster
