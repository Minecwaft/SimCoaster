#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_research.hpp"
#include "research_forecast.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator)
        throw std::domain_error("research ability divisor is zero");
    if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)
        throw std::overflow_error("research ability quotient is not representable");
    return numerator / denominator;
}

std::int32_t nominal_research_ability(const ResearchQuery& c, const StaffRuntimeRecord& researcher) {
    const auto value = numeric::word(researcher.experience);
    const auto maximum = c.staff.work_balance.max_experience;
    const auto endpoints = c.staff.work_balance.by_role[static_cast<std::size_t>(StaffType::researcher)];
    if (value > maximum)
        return endpoints.trained;
    if (value < 0)
        return endpoints.base;
    const auto factor = divide(numeric::word(numeric::bits(value) << 14u), maximum);
    const auto weighted = numeric::bits(endpoints.trained) * numeric::bits(factor) + numeric::bits(endpoints.base) * (16384u - numeric::bits(factor));
    return divide(numeric::word(weighted), 16384);
}

std::int32_t percent_integer(double value) {
    const auto truncated = std::trunc(value);
    if (!std::isfinite(truncated))
        throw std::domain_error("research percentage is not finite");
    const auto bound = std::ldexp(1.0, 63);
    if (truncated < -bound || truncated >= bound)
        throw std::overflow_error("research percentage is not representable");
    //The public scalar is the signed low DWORD of the converted integer. This
    //is independent of the original register-pair return and x87 conversion.
    return numeric::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(truncated)));
}
} //namespace

std::uint32_t get_research_category_effort(const ResearchLabRecord* lab, ResearchCategory category) {
    return lab->groups.at(static_cast<std::size_t>(category)).effort;
}

std::uint32_t get_total_research_effort(const ResearchLabRecord* lab) {
    std::uint32_t total = 0;
    for (const auto& group : lab->groups)
        total += group.effort;
    return total;
}

std::uint32_t get_active_research_effort(const ResearchLabRecord* lab) {
    std::uint32_t total = 0;
    for (const auto& group : lab->groups)
        if (group.current_project >= 0) {
            const auto& project = lab->projects.at(static_cast<std::size_t>(group.current_project));
            if (!(project.research_done >= project.cost))
                total += group.effort;
        }
    return total;
}

std::uint32_t set_research_category_effort(ResearchLabRecord* lab, ResearchCategory category, std::uint32_t effort) {
    auto& selected = lab->groups.at(static_cast<std::size_t>(category));
    selected.effort = std::min(effort, 100u);
    recalculate_world_research_group_outputs(lab);
    return selected.effort;
}

std::uint32_t set_world_research_lab_work_rate(ResearchLabRecord* lab, std::uint32_t workload) {
    lab->workload = workload;
    return workload; //Output recalculation is a later operation.
}

std::uint32_t get_research_category_output(const ResearchLabRecord* lab, ResearchCategory category) {
    return lab->groups.at(static_cast<std::size_t>(category)).output;
}

std::int32_t sum_researcher_ability_by_experience(const ResearchQuery& c) {
    std::uint32_t total = 0;
    std::size_t visited = 0;
    for (auto id = c.simulation.current->saved.first_researcher; id;) {
        if (++visited >= thing_slot_count)
            throw std::logic_error("researcher chain is cyclic");
        const auto* record = c.entities.at(id).payload;
        if (!record)
            throw std::invalid_argument("researcher chain has no payload");
        const auto* researcher = reinterpret_cast<const StaffRuntimeRecord*>(record);
        total += numeric::bits(nominal_research_ability(c, *researcher));
        id = researcher->next_staff;
    }
    return numeric::word(total);
}

std::uint32_t get_researcher_ability_capacity(const ResearchQuery& c) {
    return numeric::bits(c.research.max_researchers) * numeric::bits(c.staff.work_balance.by_role[static_cast<std::size_t>(StaffType::researcher)].trained);
}

std::uint32_t
estimate_research_category_output(ResearchWorld& c, const ResearchLabRecord* lab, ResearchCategory category) {
    const auto capacity = get_researcher_ability_capacity(c);
    if (!capacity)
        return 0;
    const auto percentage = (100u * numeric::bits(sum_researcher_ability_by_experience(c))) / capacity;
    return (percentage * get_research_category_output(lab, category)) / 100u;
}

bool is_research_project_complete(const ResearchProject* entry) {
    return entry->research_done >= entry->cost;
}

std::int32_t get_research_project_completion_percent(const ResearchProject* entry) {
    //Less, equal and unordered cost all take the native100-percent fallback.
    if (!(entry->cost > 0.1f))
        return 100;
    return percent_integer(static_cast<double>(entry->research_done) * 100.0 / entry->cost);
}

std::int32_t get_world_research_project_progress_percent(const ResearchLabRecord* lab, std::int32_t project) {
    if (project < 0)
        return 0;
    return std::min(get_research_project_completion_percent(&lab->projects.at(static_cast<std::size_t>(project))), 100);
}

std::int32_t get_research_project_cash_per_brain_point(const ResearchLabRecord* lab, std::int32_t project) {
    return lab->projects.at(static_cast<std::size_t>(project)).cash_per_brain_point;
}

bool preferred_research_project_matches_object(const ResearchLabRecord* lab, std::uint16_t definition) {
    const auto selected =
        lab->primary_current_project >= 0 ? lab->primary_current_project : lab->secondary_current_project;
    return selected >= 0 && lab->projects.at(static_cast<std::size_t>(selected)).object_definition_id == definition;
}

std::int32_t estimate_research_project_days_remaining(ResearchWorld& c, ResearchLabRecord* lab, ResearchWorkSlot slot) {
    const auto selected = match (slot) -> std::int32_t {
        case ResearchWorkSlot::primary   => lab->primary_current_project;
        case ResearchWorkSlot::secondary => lab->secondary_current_project;
        case _                           => throw std::out_of_range("research estimate work slot");
    };
    const auto& project = lab->projects.at(static_cast<std::size_t>(selected));
    const auto increment = calculate_research_slot_work_increment(c, lab, slot);
    return research_detail::forecast(project.research_done, project.cost, increment).days;
}
} //namespace simcoaster
