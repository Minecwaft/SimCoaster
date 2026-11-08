#pragma once

#include "sam_integer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace simcoaster {
enum class StaffType : std::int32_t;

//Codes verified against the metric switch, tracking transitions and source
//resolver in Game.exe. Unrestricted SAM fields can also contain unnamed codes.
enum class MissionObjectiveMetric : std::int32_t {
    none = 0,
    adjusted_bank_positive = 1,
    adjusted_bank_balance = 2,
    total_challenges_completed = 3,
    //The native metric sums analyser history series7..11. Mission-file comments
    //describe profit growth; the individual series have not all been identified.
    park_analyser_growth_percent = 4,
    eligible_person_count_growth_percent = 5,
    global_park_award_count = 6,
    share_ownership_percent = 7,
    eligible_person_count = 8
};
enum class MissionObjectiveState : std::int32_t {
    inactive = 0,
    tracking = 1,
    target_met = 2,
    deadline_expired = 3
};
enum class MissionObjectiveSource : std::int32_t {
    arabian = 0,
    science = 1,
    water = 2,
    intro = 3,
    //This selector is distinct from the global ThemeInfo record Unknown at4.
    all_sources = 4
};

struct MissionBoundaryClick {
    [[=sam::IntegerField { "BoundaryClicks", "X" }]] std::int32_t x {};
    [[=sam::IntegerField { "BoundaryClicks", "Y" }]] std::int32_t y {};
};

struct MissionExitStatistic {
    [[=sam::IntegerField { "ExitStatistic", "TextId" }]] std::int32_t text_id {};
    [[=sam::IntegerField { "ExitStatistic", "Type" }]] MissionObjectiveMetric objective_kind {};
    [[=sam::IntegerField { "ExitStatistic", "InTheme" }]] MissionObjectiveSource source_filter_ordinal {};
    [[=sam::IntegerField { "ExitStatistic", "TargetValue" }]] std::int32_t target_value {};
    [[=sam::IntegerField { "ExitStatistic", "TargetTime" }]] std::int32_t target_time {};
    [[=sam::IntegerField { "ExitStatistic", "TargetObject" }]] std::int32_t target_object {};
    [[=sam::IntegerField { "ExitStatistic", "TargetStaffType" }]] std::int32_t target_staff_type {};
};

struct ObstacleStaffRequirement {
    [[=sam::IntegerField { "ObstacleStaff", "StaffType" }]] StaffType staff_type {};
    [[=sam::IntegerField { "ObstacleStaff", "Ability" }]] std::int32_t ability {};
    [[=sam::IntegerField { "ObstacleStaff", "Number" }]] std::int32_t number {};
};

//Mission schema at Game.exe0x9A0C68:50 descriptors before the terminator.
//Declaration order follows the descriptor-backed integer values, independent
//of the original receiver header, pointers or runtime layout.
struct MissionBalance {
    [[=sam::IntegerField { "Info", "Number" }]] std::int32_t info_number {};
    [[=sam::IntegerField { "Info", "NoResearch", 0, 2 }]] std::int32_t no_research {};
    [[=sam::IntegerField { "Completion", "ThemeOpened", 0, 2 }]] std::int32_t completion_theme_opened {};
    [[=sam::IntegerField { "Completion", "ThemeNumber" }]] std::int32_t completion_theme_number {};
    [[=sam::IntegerField { "Completion", "CutsceneObject" }]] std::int32_t completion_cutscene_object {};
    [[=sam::IntegerField { "Completion", "ObstacleOpened" }]] std::int32_t completion_obstacle_opened {};
    [[=sam::IntegerField { "Completion", "EndOfGame", 0, 2 }]] std::int32_t completion_end_of_game {};
    [[=sam::IntegerField { "Completion", "LoadNewHoardings", 0, 2 }]] std::int32_t completion_load_new_hoardings {};
    [[=sam::IntegerField { "Completion", "EndingTheme" }]] std::int32_t ending_theme {};
    [[=sam::IntegerField { "TechLevel", "Theme", 0, 3 }]] std::int32_t tech_level_theme {};
    [[=sam::IntegerField { "TechLevel", "NewMaxGroup" }]] std::int32_t tech_level_new_max_group {};
    [[=sam::Rows { "BoundaryClicks" }]] std::array<MissionBoundaryClick, 64> boundary_clicks {};
    [[=sam::Extent { "BoundaryClicks" }]] std::uint32_t boundary_clicks_parsed_extent {};
    [[=sam::Rows { "ExitStatistic" }]] std::array<MissionExitStatistic, 5> exit_statistics {};
    [[=sam::Extent { "ExitStatistic" }]] std::uint32_t exit_statistics_parsed_extent {};
    [[=sam::IntegerField { "Obstacle", "Type" }]] std::int32_t obstacle_type {};
    [[=sam::IntegerField { "Obstacle", "InTheme" }]] std::int32_t obstacle_in_theme {};
    [[=sam::IntegerField { "Obstacle", "ResearchEffort" }]] std::int32_t obstacle_research_effort {};
    [[=sam::IntegerField { "Obstacle", "RemoveEffort" }]] std::int32_t obstacle_remove_effort {};
    [[=sam::IntegerField { "Obstacle", "GoldenTickets" }]] std::int32_t obstacle_golden_tickets {};
    [[=sam::Rows { "ObstacleStaff" }]] std::array<ObstacleStaffRequirement, 5> obstacle_staff {};
    [[=sam::Extent { "ObstacleStaff" }]] std::uint32_t obstacle_staff_parsed_extent {};
    [[=sam::IntegerField { "Promotion", "Number" }]] std::int32_t promotion_number {};
    [[=sam::IntegerField { "Promotion", "StockAward" }]] std::int32_t promotion_stock_award {};
    [[=sam::IntegerField { "RequiredChallenges", "Theme" }]] std::int32_t required_challenges_theme {};
    [[=sam::IntegerField { "RequiredChallenges", "Number" }]] std::int32_t required_challenges_number {};
};

using MissionResearchBalance = MissionBalance;

//The loader copies unrestricted signed SAM codes without validating a metric
//enum. Runtime tracking semantics are separate from these configuration rows.
struct MissionObjectiveTrackingEntry {
    MissionObjectiveState state {};
    MissionObjectiveMetric objective_kind {};
    std::int32_t target_value {}, deadline_days {};
    std::uint32_t start_filetime_low {}, start_filetime_high {};
    std::int32_t current_value {}, elapsed_days {};
    MissionObjectiveSource source_filter_ordinal {};
    std::int32_t percentage_baseline_value {};
    std::array<std::byte, 8> opaque_tail {};
};
} //namespace simcoaster
