#pragma once
#include "world_types.hpp"
#include <array>
#include <string>
#include <vector>

namespace simcoaster {
struct WorldRuntime;
enum class VisitorPopulation : std::int32_t {
    children,
    adults,
    seniors,
    all
};

struct VisitorUiMetrics {
    ThingId id{};
    const ThingHeader* identity{};
    std::uint32_t arrival_date{}, arrival_index{};
    std::u16string name, generation, activity, thought;
    std::int32_t happiness_meter{}, preferred_excitement_meter{};
    std::array<std::int32_t, 4> counters{}; //purchases, rides, sideshows, wins
    std::int32_t thought_part = 999, thought_help = -1;
    bool can_context{};
};

struct VisitorUiStatistics {
    std::int32_t current{}, lifetime{}, average_happiness_meter{};
    std::array<std::int32_t, 3> population_meters{}; //happy, undecided, sad
    std::array<std::int32_t, 3> averages{};          //purchases, rides, sideshows
    //Native top-three output slots are deliberately not a sorted array.
    std::array<std::uint8_t, 3> thoughts{ 31, 31, 31 };
};

PersonRuntimeView* find_ui_visitor(WorldRuntime&, ThingId);
std::vector<ThingId> enumerate_ui_visitors(WorldRuntime&, ThingId excluded = 0);
std::u16string visitor_ui_name(WorldRuntime&, const PersonRuntimeView&);
VisitorUiMetrics visitor_ui_metrics(WorldRuntime&, const PersonRuntimeView&);
VisitorUiStatistics visitor_ui_statistics(WorldRuntime&, VisitorPopulation, ThingId excluded = 0);
//Native locator selectors: 1 girl,2 boy,3 man,4 woman,5 grandmother,6
//grandfather.
std::int32_t visitor_ui_demographic(WorldRuntime&, const PersonRuntimeView&);
ThingId next_ui_visitor(WorldRuntime&, ThingId, bool previous, std::int32_t demographic = 0, ThingId excluded = 0);
std::array<bool, 6> visitor_ui_demographics(WorldRuntime&, ThingId excluded = 0);

struct VisitorContextAction {
    std::uint32_t caption{};
    bool enabled{};
};

std::array<VisitorContextAction, 4> visitor_ui_context(WorldRuntime&, const PersonRuntimeView&);
bool execute_visitor_ui_context(WorldRuntime&, PersonRuntimeView&, std::uint32_t caption);
} //namespace simcoaster
