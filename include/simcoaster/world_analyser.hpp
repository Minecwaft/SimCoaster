#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
enum class AnalyserCellMetric : std::size_t {
    walkable_cells = 5,
    litter_cells = 6,
    developed_cells = 7
};
AnalyserGridRecord& get_park_analyser_grid_record(ParkAnalyserState&, CellId);
std::int32_t count_cached_analyser_litter_cells(EconomyWorld&, AnalyserGridRecord&, std::uint32_t maximum_age);
std::int32_t count_cached_analyser_walkable_cells(EconomyWorld&, AnalyserGridRecord&, std::uint32_t maximum_age);
std::int32_t count_cached_analyser_developed_cells(EconomyWorld&, AnalyserGridRecord&, std::uint32_t maximum_age);
std::int32_t count_park_developed_cells(EconomyWorld&, ParkAnalyserState&, std::uint32_t maximum_age);
enum class StaffCoverageCellFilter : std::int32_t {
    land = 0,
    developed = 1,
    object_entries = 2,
    paths_and_queues = 3
};
void mark_world_staff_patrol_coverage(EconomyWorld&, ThingKind);
std::int32_t calculate_world_staff_patrol_coverage(EconomyWorld&, ThingKind, StaffCoverageCellFilter);
void append_park_analyser_byte_history(ParkAnalyserState&, std::size_t series, std::uint8_t sample);
void record_visitor_admission_in_park_analyser(EconomyWorld&, ParkAnalyserState&, std::int32_t fee);
std::int32_t calculate_park_path_litter_percentage(EconomyWorld&);
double average_eligible_person_happiness(EconomyWorld&, std::int32_t generation = 4);
std::int32_t count_ordinary_park_objects(EconomyWorld&, ObjectUiCategory, std::int32_t minimum_upgrade = 0);
std::int32_t get_cached_park_ride_object_count(EconomyWorld&, ParkAnalyserState&, std::uint32_t maximum_age);
std::int32_t calculate_park_object_and_staff_metric(EconomyWorld&);
void sample_park_analyser_metrics(EconomyWorld&, ParkAnalyserState&);
void handle_park_analyser_world_event(EconomyWorld&, ParkAnalyserState&, const WorldEvent&);
} //namespace simcoaster
