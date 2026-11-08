#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_theme.hpp"
#include <stdexcept>

namespace simcoaster {
namespace {
const ResearchCatalogDefinition& definition(ResearchWorld& c, std::uint16_t id) {
    const auto* entry = find_object_definition_by_id(&c.resources.object_catalog, id);
    return require_bound<std::out_of_range>(entry, "research picker definition was not found");
}

ResearchLabRecord& primary_lab(ResearchWorld& c) {
    if (!c.simulation.reference) {
        throw std::logic_error("research picker has no primary world");
    }
    const auto id = c.simulation.reference->saved.research_lab;
    const auto* record = c.entities.at(id).payload;
    if (!record || record->kind != ThingKind::research_lab) {
        throw std::invalid_argument("research picker has no active lab");
    }
    return *c.entities.payload<ResearchLabRecord>(id);
}
} //namespace

bool set_research_lab_current_project_for_slot(ResearchWorld& c, ResearchLabRecord* lab, std::int32_t project, ResearchWorkSlot slot) {
    //Native setter uses secondary only for exactly1. Store before obtaining the
    //Theme manager: failure in that integration leaves the new choice in place.
    (slot == ResearchWorkSlot::secondary ? lab->secondary_current_project : lab->primary_current_project) = project;
    clear_research_action_532_eligibility(c);
    return true;
}

void clear_research_lab_project_slot(ResearchWorld& c, ResearchLabRecord* lab, ResearchWorkSlot slot) {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= lab->consultants.size()) {
        throw std::out_of_range("research work slot");
    }
    (slot == ResearchWorkSlot::primary ? lab->primary_current_project : lab->secondary_current_project) = no_research_project;
    lab->consultants[index] = 0;
    clear_world_research_lab_worker_ids(c, lab, slot);
}

std::uint16_t get_research_project_object_id(const ResearchLabRecord* lab, std::int32_t project) {
    //Native helper admits256 and reads beyond its256-row array. No registered
    //callers establish a purpose for that access; expose only actual rows.
    if (project < 0 || static_cast<std::size_t>(project) >= lab->projects.size()) {
        return 0;
    }
    return lab->projects[static_cast<std::size_t>(project)].object_definition_id;
}

bool is_research_project_selectable(ResearchWorld& c, ResearchWorkSlot slot, std::int32_t project_index) {
    if (slot != ResearchWorkSlot::primary && slot != ResearchWorkSlot::secondary) {
        throw std::out_of_range("research picker slot");
    }
    auto& lab = primary_lab(c);
    const auto& project = lab.projects.at(static_cast<std::size_t>(project_index));
    if (static_cast<std::uint32_t>(get_research_project_completion_percent(&project)) >= 100u) {
        return false;
    }
    if (project_index == (slot == ResearchWorkSlot::primary ? lab.secondary_current_project : lab.primary_current_project)) {
        return false;
    }
    const auto tier = project.research_group;
    const auto ordinal = selected_research_theme_ordinal(c);
    if (tier > research_theme_current_tech_level(c, ordinal)) {
        return false;
    }
    //Theme lookup can mutate the row. Tier is captured above, category and
    //definition ID are read after both calls, matching the UI's live reads.
    if (project.category != ResearchCategory::upgrades) {
        return true;
    }
    auto required_id = project.object_definition_id;
    if (definition(c, required_id).add_on.upgrade_type) {
        const auto parent = definition(c, project.object_definition_id).add_on.upgrades_id;
        if (!parent) {
            return true;
        }
        required_id = static_cast<std::uint16_t>(parent);
    }
    const auto id = find_first_park_object_record_id_by_definition_id(c, required_id);
    if (!id) {
        return false;
    }
    const auto* header = c.entities.at(id).payload;
    if (!header || header->kind != ThingKind::park_object) {
        throw std::invalid_argument("research upgrade parent is not a park object");
    }
    const auto* object = reinterpret_cast<const ParkObjectRuntimeView*>(header);
    return !park_object_definition_is_marked_special(c, object) || get_park_object_activation_flag(object) != 0;
}
} //namespace simcoaster
