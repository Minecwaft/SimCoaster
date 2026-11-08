#pragma once
#include "simcoaster/sam_record.hpp"
#include "simcoaster/world_types.hpp"

namespace simcoaster {
namespace research_balance_detail {
struct Technology {
    [[=sam::Field{ "PercentageForThisTech", 0, 100 }]] std::int32_t percentage{};
};

struct Category {
    [[=sam::Field{ "Effort", sam::Kind::integer }]] std::int32_t effort{};
};

struct Tuning {
    [[=sam::Field{ "StartingWorkLoad", 70, 101 }]] std::int32_t starting_workload{};
    [[=sam::Field{ "SpeedScaleDown", sam::Kind::nonnegative_scalar }]] float speed_scale_down{};
    [[=sam::Field{ "CostScaleUp", sam::Kind::nonnegative_scalar }]] float cost_scale_up{};
};

struct Balance {
    [[=sam::Rows{ "ResearchTech" }]] std::array<Technology, 10> technology{};
    [[=sam::Extent{ "ResearchTech" }]] std::uint32_t technology_count{};
    [[=sam::Rows{ "ResearchCategories" }]] std::array<Category, 5> categories{};
    [[=sam::Extent{ "ResearchCategories" }]] std::uint32_t category_count{};
    [[=sam::Group{ "Research" }]] Tuning tuning;
};
} //namespace research_balance_detail

//The same World Balance rows feed both a new ResearchLab's project catalog
//and live project work increments. Restoring its saved project array does not
//restore these global tuning values. Import Standard.sam then the selected
//theme overlay, retaining the native descriptor extents used by61BB30.
inline std::expected<void, sam::Error>
load_world_research_balance(ResearchBalanceInput& output, std::string_view text, bool preserve) {
    research_balance_detail::Balance record;
    for (std::size_t i = 0; i < record.technology.size(); ++i)
        record.technology[i].percentage = output.percentage_for_tech[i];
    record.technology_count = output.tech_parsed_extent;
    for (std::size_t i = 0; i < record.categories.size(); ++i)
        record.categories[i].effort = output.category_effort[i];
    record.category_count = output.category_parsed_extent;
    record.tuning = { output.starting_workload, output.speed_scale_down, output.cost_scale_up };
    const auto parsed = sam::parse_record<research_balance_detail::Balance, true>(record, text, preserve);
    if (!parsed)
        return std::unexpected(parsed.error());
    for (std::size_t i = 0; i < record.technology.size(); ++i)
        output.percentage_for_tech[i] = record.technology[i].percentage;
    output.tech_parsed_extent = record.technology_count;
    for (std::size_t i = 0; i < record.categories.size(); ++i)
        output.category_effort[i] = record.categories[i].effort;
    output.category_parsed_extent = record.category_count;
    output.starting_workload = record.tuning.starting_workload;
    output.speed_scale_down = record.tuning.speed_scale_down;
    output.cost_scale_up = record.tuning.cost_scale_up;
    return {};
}
} //namespace simcoaster
