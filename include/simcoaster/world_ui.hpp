#pragma once
#include "world_event.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace simcoaster {
struct WorldRuntime;
//Host presentation models for the native World UI controllers. These own
//copied row values; World retains ownership of the records used as identities.
//They are process UI state, not part of the World save schema or x86 layouts.
enum class UiListSortDirection : std::int16_t {
    ascending = 0,
    descending = 1
};

template <class Row>
struct WorldUiList {
    bool open{};
    std::vector<Row> rows;
    std::int16_t sort_column{};
    UiListSortDirection sort_direction{};
    std::uint64_t revision{};
};

struct StaffRoomRosterRow {
    const ThingHeader* record{};
    ThingId staff{};
    std::u16string name, role;
    std::int32_t energy_meter{};
};

struct VisitorListRow {
    const ThingHeader* record{};
    ThingId person{};
    std::uint32_t arrival_index{};
    std::optional<std::u16string> generation_caption;
    std::int32_t happiness_meter{}, thought_index{};
};

struct TrainingRoomDisplayRow {
    bool visible{};
    std::uint8_t pending_index{};
    std::u16string name, role, ability;
    //Native UITEXT933 denotes inactive;934 formats an integer percentage.
    std::uint32_t progress_text_row = 933;
    std::int32_t progress_percent{};
};

struct TrainingRoomUiState {
    bool open{}, empty_notice_visible{}, add_staff_enabled{};
    std::int32_t active_row_count{};
    std::array<TrainingRoomDisplayRow, 3> rows;
    std::uint64_t revision{};
};

struct VisitorProfileUiState {
    bool open{};
    //Corresponds to the concrete controller's close-panel virtual operation.
    //An open binding must supply its event4 dispatcher; it is not a no-op leaf.
    std::function<void(WorldRuntime&)> close_panel;
};

struct VisitorLocatorUiState {
    bool open{};
    const ThingHeader* selected_person{};
    std::int32_t selected_filter = -1;
    std::array<bool, 6> filter_enabled{};
    std::uint64_t revision{};
};

struct RideSettingsUiState {
    bool open{};
    //Native concrete vtable slot1C; receives only its owning controller.
    std::function<void(WorldRuntime&)> refresh_statistics_and_status;
};

struct CalendarHeaderUiState {
    bool present{}, debug_counts{}, centered{}, vertically_centered{};
    std::u16string text;
    std::array<std::uint8_t, 4> color{};
    std::uint64_t revision{};
};
enum class StatisticsGraphRange : std::int32_t {
    one_year = 0,
    three_years = 1,
    twelve_years = 2
};

struct StatisticsGraphSeries {
    std::uint32_t history_index{};
    std::vector<std::int32_t> samples; //Resampled oldest-to-newest, including fallback endpoint.
};

struct StatisticsGraphUiState {
    bool open{};
    std::uint32_t selected_series_mask{};
    StatisticsGraphRange range{};
    std::int32_t lower{}, upper{}, divisions = 10;
    std::vector<StatisticsGraphSeries> series;
    std::uint64_t revision{};
};

struct FinancialStatisticsUiState {
    StatisticsGraphUiState graph;
    std::int32_t income{}, expenditure{}, balance{};
    std::array<std::uint8_t, 4> balance_color{};
};
enum class LoanView : std::int32_t {
    offers = 0,
    outstanding = 1
};

struct LoanManagementRow {
    std::int32_t key{};
    std::u16string lender;
    std::int32_t amount{}, months{}, annual_rate{}, monthly_payment{}, total_repayment{};
};

struct LoanManagementUiState {
    bool open{};
    LoanView view{};
    std::vector<LoanManagementRow> rows;
    std::uint64_t revision{};
};

struct ParkAwardsUiState {
    bool open{}, highlight_visible{};
    std::int32_t highlighted_type = -1;
    std::uint32_t highlight_started_ms{};
    std::uint64_t revision{};
};

struct WorldUiState {
    const ThingHeader* selected_record{}; //Native shared A5960C selection.
    //The connected Staff controller releases its identities while the retiring
    //payload is still alive, before the World can recycle its ThingId.
    std::function<void(WorldRuntime&, const ThingHeader&)> staff_retiring;
    //The park-object owner receives the actual event while its payload is
    //still alive. It owns row insertion/removal and invalidates staged values
    //and detached previews before retirement can recycle a World identity.
    std::function<void(WorldRuntime&, const WorldEvent&)> park_object_event;
    WorldUiList<StaffRoomRosterRow> staff_room;
    TrainingRoomUiState training_room;
    WorldUiList<VisitorListRow> visitor_list;
    //Native removal bookkeeping wraps as a full DWORD; insertion does not add1.
    std::uint32_t visitor_list_removal_balance{};
    VisitorProfileUiState visitor_profile;
    VisitorLocatorUiState visitor_locator;
    RideSettingsUiState ride_settings;
    CalendarHeaderUiState calendar_header;
    StatisticsGraphUiState park_statistics;
    FinancialStatisticsUiState financial_statistics;
    LoanManagementUiState loans;
    ParkAwardsUiState park_awards;
    bool mode_selector_initialized{}, objectives_refresh_pending{};
};

void append_matching_staff_room_roster_member(WorldRuntime&, StaffRuntimeRecord&);
void remove_staff_room_roster_member(WorldRuntime&, const ThingHeader&);
void refresh_training_room_rows_and_availability(WorldRuntime&);
void remove_training_room_staff_record_if_panel_open(WorldRuntime&, ThingId);
void insert_visitor_list_person(WorldRuntime&, const PersonRuntimeView&);
void remove_visitor_list_person(WorldRuntime&, const ThingHeader&);
void refresh_visitor_locator_filter_buttons(WorldRuntime&);
void handle_world_ui_message(WorldRuntime&, const WorldEvent&);
void rebuild_loan_management_rows(WorldRuntime&);
} //namespace simcoaster
