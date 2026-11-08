#pragma once
#include "world_staff_interaction.hpp"
#include "world_ui.hpp"
#include <filesystem>
#include <memory>
#include <span>

namespace simcoaster {
struct StaffCandidate;
bool matches_staff_hire_snapshot(const StaffCandidate&, const StaffCandidate&);

//UI identities are World IDs (or candidate-pool slots), never visible row
//numbers. The owner reconciles every borrowed identity before retirement.
struct StaffUiRow {
    ThingId staff {};
    StaffType role = StaffType::unclassified;
    std::u16string name, role_name, ability, activity;
    std::int32_t experience_meter {}, wage {}, happiness_meter {}, energy_meter {};
    ThemeAbility ability_id {};
};

struct StaffHireUiRow {
    std::size_t slot {};
    StaffType role = StaffType::unclassified;
    std::u16string name, agency;
    std::int32_t experience_meter {}, wage {};
    std::uint8_t sprite_variant {};
};

struct StaffHireUiState {
    bool open {};
    StaffType filter = StaffType::unsupported_index;
    std::int32_t selected_slot = -1, sort_key = 1;
    std::array<bool, 6> available {};
    std::uint32_t heading_row {}, message_row = 222;
    std::vector<StaffHireUiRow> rows;
    std::uint64_t revision {};
};

struct StaffListUiState {
    bool open {};
    StaffType filter = StaffType::handyman;
    ThingId selected {};
    std::int32_t sort_key = 1, role_happiness_meter {}, all_happiness_meter {};
    std::array<bool, 6> available {};
    std::u16string status;
    std::vector<StaffUiRow> rows;
    std::uint64_t revision {};
};

struct StaffDetailsUiState {
    bool open {}, can_navigate {};
    ThingId staff {};
    StaffUiRow row;
    std::u16string agency, status, thought, tenure;
    std::int32_t motivation_meter {}, staged_wage {};
    StaffActivityStatus activity {};
    MobileThoughtCode thought_code {};
    std::int32_t activity_icon {}, thought_icon = 999, thought_help = -1;
    std::uint32_t sprite_category {}, sprite_variant {};
    std::uint64_t revision {};
};

struct StaffLocatorUiState {
    bool open {};
    StaffType filter = StaffType::unsupported_index;
    ThingId target {};
    std::array<bool, 6> available {};
    std::u16string caption;
    std::array<float, 3> position {};
    StaffAreaBounds area {};
    std::uint64_t revision {};
};
enum class StaffFinanceAdjustment {
    amount = 0,
    add = 1,
    percent = 2
};

struct StaffFinanceUiState {
    bool open {};
    StaffFinanceAdjustment mode {};
    //Indexed by StaffType, independently of the native widget ordering.
    std::array<std::int32_t, 6> values {};
    std::array<bool, 6> enabled {};
};

struct StaffActivityUiState {
    bool open {};
    ThingId staff {};
    //Idle plus the six native activity-ring channels, in display order.
    std::array<double, 7> percentages {};
    std::uint64_t revision {};
};

struct StaffSelectorUiState {
    bool open {};
    StaffType filter = StaffType::unsupported_index;
    ThingId selected {};
    std::int32_t sort_key = 1;
    std::array<bool, 6> available {};
    std::vector<ThingId> excluded;
    std::array<bool, 6> excluded_roles {};
    std::vector<StaffUiRow> rows;
};

struct StaffTrainingRoomUiState {
    bool open {};
    ThingId room {};
    std::array<StaffTrainingPendingRow, 3> baseline {};
    std::uint32_t message_row {};
    std::optional<std::size_t> ability_picker_slot;
};
enum class StaffWageOutcome {
    rejected,
    demand_met,
    partial_offer_accepted
};
enum class StaffRejectedWageChoice {
    accept_full_demand,
    dismiss,
    leave_unchanged
};

struct StaffWageUiState {
    bool open {}, showing_result {};
    ThingId staff {};
    std::int32_t demanded_raise {}, offered_raise {};
    StaffWageOutcome outcome {};
    std::uint32_t response_row {};
};

struct StaffTrainingBudgetUiState {
    std::array<std::int32_t, 6> budgets {}, experience_meters {};
    bool have_ever_trained {};
    std::int32_t history_6 {}, history_4 {}, history_3_minus_4 {}, total {}, remainder {};
};

struct StaffContextUiState {
    StaffType role {};
    std::u16string name;
    bool on_strike {}, can_set_patrol {}, can_edit_patrol {}, can_clear_patrol {}, can_train {}, can_pickup {}, can_hire {};
};

struct StaffUiState {
    StaffHireUiState hire;
    StaffListUiState list;
    StaffDetailsUiState details;
    StaffLocatorUiState locator;
    StaffFinanceUiState finance;
    StaffActivityUiState activity;
    StaffSelectorUiState selector;
    StaffTrainingRoomUiState training;
    StaffWageUiState wage;
    ThingId staff_room {};
};

//Gameplay/model half of the original Staff UI. Control trees, bitmap fonts,
//preview rendering, and panel Advisor open/help/close requests belong to the
//connected frontend. Mutations delegate to the compiled six-role runtime.
class WorldStaffUiRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldStaffUiRuntime(WorldRuntime&, const std::filesystem::path& game_root);
    ~WorldStaffUiRuntime();
    WorldStaffUiRuntime(const WorldStaffUiRuntime&) = delete;
    WorldStaffUiRuntime& operator=(const WorldStaffUiRuntime&) = delete;
    StaffUiState& state();
    const StaffUiState& state() const;
    void set_retirement_observer(std::function<void(ThingId)>);
    void refresh();
    void close_all();

    void open_hire(StaffType = StaffType::unsupported_index);
    void close_hire();
    void select_hire(std::size_t candidate_slot);
    bool hire_selected();
    void sort_hire(std::int32_t signed_key);
    void open_list(StaffType = StaffType::unsupported_index, ThingId selected = 0);
    void select_list(ThingId);
    void sort_list(std::int32_t signed_key);
    void open_details(ThingId);
    void navigate_details(bool previous);
    void set_name(ThingId, std::u16string_view);
    void set_salary(ThingId, std::int32_t);
    void apply_details_salary();
    void fire(ThingId);
    void open_locator(ThingId = 0);
    void cycle_locator(StaffType, bool previous = false);
    void close_locator();
    void open_finance();
    void set_finance_mode(StaffFinanceAdjustment);
    void apply_finance();
    void open_activity(ThingId);
    void open_staff_room(ThingId);
    void close_staff_room();
    void open_selector(std::span<const ThingId> excluded = {}, std::span<const StaffType> excluded_roles = {});
    void filter_selector(StaffType);
    void sort_selector(std::int32_t signed_key);

    void open_training_room(ThingId);
    std::vector<StaffTrainingAbilityRow> training_abilities(ThingId) const;
    void begin_training_ability_picker(std::size_t slot);
    void close_training_ability_picker(bool restore_staff_marker = true);
    bool stage_training_staff(ThingId);
    StaffTrainingSelectionResult stage_training_ability(std::size_t slot, ThemeAbility);
    void remove_training_staff(std::size_t slot);
    void commit_training_room();
    void cancel_training_room();
    void begin_pickup(ThingId);
    void begin_patrol(ThingId);
    void clear_patrol(ThingId);
    void begin_wage(ThingId, std::int32_t demanded_raise);
    void set_wage_offer(std::int32_t);
    StaffWageOutcome show_wage_result();
    void resolve_wage_result(StaffRejectedWageChoice);
    StaffTrainingBudgetUiState training_budget() const;
    void set_training_budget(StaffType, std::int32_t);
    StaffContextUiState context(ThingId) const;
};
} //namespace simcoaster
