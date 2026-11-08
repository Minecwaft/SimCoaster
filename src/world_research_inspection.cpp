#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t selected_project(const ResearchLabRecord& lab, ResearchWorkSlot slot) {
    return match (slot) -> std::int32_t {
        case ResearchWorkSlot::primary   => lab.primary_current_project;
        case ResearchWorkSlot::secondary => lab.secondary_current_project;
        case _                           => throw std::out_of_range("research work slot");
    };
}

bool unfinished(const ResearchProject& entry) {
    return !(entry.research_done >= entry.cost);
}
} //namespace

bool research_slot_has_unfinished_project(const ResearchLabRecord* lab, ResearchWorkSlot slot) {
    const auto project = selected_project(*lab, slot);
    return project >= 0 && unfinished(lab->projects.at(static_cast<std::size_t>(project)));
}

bool is_research_project_slot_complete(const ResearchLabRecord* lab, ResearchWorkSlot slot) {
    //The UI panel passes its stored Lab; this does not resolve the global Lab.
    const auto project = selected_project(*lab, slot);
    return project >= 0 && is_research_project_complete(&lab->projects.at(static_cast<std::size_t>(project)));
}

std::int32_t count_unfinished_research_projects(const ResearchLabRecord* lab, ResearchCategoryFilter filter) {
    //Native DEC/JS checks signed(count-1), not signed(count). Preserve its early
    //empty branch, but reject a remaining count that exceeds the actual rows.
    if (std::bit_cast<std::int32_t>(lab->num_projects - 1u) < 0) {
        return 0;
    }
    if (lab->num_projects > lab->projects.size()) {
        throw std::length_error("research project count exceeds capacity");
    }
    std::int32_t count = 0;
    for (auto i = lab->num_projects; i;) {
        const auto& entry = lab->projects[--i];
        if ((filter == ResearchCategoryFilter::all || static_cast<std::int32_t>(entry.category) == static_cast<std::int32_t>(filter)) && unfinished(entry)) {
            ++count;
        }
    }
    return count;
}

bool update_research_lab_active_state_from_researcher_list(ResearchWorld& c, ResearchLabRecord* lab) {
    return c.simulation.current->saved.first_researcher ? set_research_lab_active_and_notify_subscribers(c, lab) : clear_research_lab_active_and_notify_subscribers(c, lab);
}

std::uint8_t get_research_lab_active_flag(const ResearchLabRecord* lab) {
    return lab->lab_active;
}

ResearchStaffRequirements get_research_project_staff_requirements(const ResearchLabRecord* lab, std::int32_t project) {
    const auto& entry = lab->projects.at(static_cast<std::size_t>(project));
    //Replace the two output pointers and incidental EAX ability with one value.
    return { entry.staff_required, entry.staff_ability };
}

std::int32_t find_unfinished_obstacle_research(const ResearchLabRecord* lab) {
    const auto count = std::bit_cast<std::int32_t>(lab->num_projects);
    if (count <= 0) {
        return -1;
    }
    if (lab->num_projects > lab->projects.size()) {
        throw std::length_error("research project count exceeds capacity");
    }
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& entry = lab->projects[static_cast<std::size_t>(i)];
        if (entry.category == ResearchCategory::mission_obstacle && unfinished(entry)) {
            return i;
        }
    }
    return -1;
}
} //namespace simcoaster
