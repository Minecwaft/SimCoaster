#include "simcoaster/world_events.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
bool is_staff(ThingKind kind) {
    return match (kind) -> bool {
        case ThingKind::mechanic
            or ThingKind::handyman
            or ThingKind::entertainer
            or ThingKind::guard
            or ThingKind::researcher
            or ThingKind::gardener => true;
        case _                     => false;
    };
}

StaffRuntimeRecord* staff(ResearchWorld& c, ThingId id) {
    auto* record = c.entities.at(id).payload;
    if (!record || !is_staff(record->kind))
        throw std::invalid_argument("research worker is not a staff record");
    return reinterpret_cast<StaffRuntimeRecord*>(record);
}

ResearchProject& append_row(ResearchLabRecord& lab) {
    if (lab.num_projects >= lab.projects.size())
        throw std::length_error("research project capacity exhausted");
    return lab.projects[lab.num_projects];
}

void notify(ResearchWorld& c, WorldEvent& event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}
} //namespace

ThingId find_staff_matching_research_project_requirement(ResearchWorld& c,
                                                         ResearchLabRecord* lab,
                                                         std::int32_t project,
                                                         ThingId excluded) {
    const auto& requirement = lab->projects.at(static_cast<std::size_t>(project));
    const auto role = map_research_staff_requirement_to_staff_type_index(requirement.staff_required);
    const auto ability = requirement.staff_ability;
    if (role == StaffType::unsupported_index)
        return 0;
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next) {
        const auto* record = entry->payload;
        if (!record || record->id == excluded || record->retirement_pending || !is_staff(record->kind))
            continue;
        if (ability != ThemeAbility::none) {
            if (reinterpret_cast<const StaffRuntimeRecord*>(record)->first_known_ability == ability)
                return record->id;
        } else {
            const bool matches = match (role) -> bool {
                case StaffType::handyman    => record->kind == ThingKind::handyman;
                case StaffType::mechanic    => record->kind == ThingKind::mechanic;
                case StaffType::entertainer => record->kind == ThingKind::entertainer;
                case StaffType::guard       => record->kind == ThingKind::guard;
                case StaffType::gardener    => record->kind == ThingKind::gardener;
                case _                      => false;
            };
            if (matches)
                return record->id;
        }
    }
    return 0;
}

void remove_world_research_lab_staff_reference(ResearchWorld& c, ResearchLabRecord* lab, const ThingHeader* departing) {
    const auto id = departing->id;
    if (departing->kind == ThingKind::researcher) {
        for (std::size_t i = 0; i < lab->primary_staff.size(); ++i) {
            if (lab->primary_staff[i] == id)
                lab->primary_staff[i] = 0;
            if (lab->secondary_staff[i] == id)
                lab->secondary_staff[i] = 0;
        }
    } else {
        for (std::size_t i = 0; i < lab->consultants.size(); ++i)
            if (lab->consultants[i] == id) {
                const auto project = i == 0 ? lab->primary_current_project : lab->secondary_current_project;
                //61DB3E passes the departing ID, but native 61D970 ignores it. Event33
                //precedes unlink/tombstone, so selecting it again leaves a dangling ID
                //and can let a reused record satisfy the next research frame.
                lab->consultants[i] = project < 0 || static_cast<std::size_t>(project) >= lab->num_projects
                                        ? 0
                                        : find_staff_matching_research_project_requirement(c, lab, project, id);
            }
    }
}

void clear_world_research_lab_staffing(ResearchWorld& c, ResearchLabRecord* lab) {
    lab->primary_available = 0;
    lab->secondary_available = 0;
    for (std::size_t i = 0; i < lab->primary_staff.size(); ++i) {
        if (auto id = lab->primary_staff[i])
            world_operations::set_staff_research_mode(c, staff(c, id), 0);
        //Mode transitions can mutate either lane. Read the secondary ID after
        //the primary call, then clear both slots after the second transition.
        if (auto id = lab->secondary_staff[i])
            world_operations::set_staff_research_mode(c, staff(c, id), 0);
        lab->primary_staff[i] = 0;
        lab->secondary_staff[i] = 0;
    }
}

void clear_world_research_lab_worker_ids(ResearchWorld& c, ResearchLabRecord* lab, ResearchWorkSlot slot) {
    auto& ids = slot == ResearchWorkSlot::primary ? lab->primary_staff : lab->secondary_staff;
    for (auto& id : ids) {
        if (id)
            staff(c, id)->researching = 0;
        id = 0;
    }
}

bool set_research_slot_worker_ids(ResearchWorld& c,
                                  ResearchLabRecord* lab,
                                  ResearchWorkSlot slot,
                                  std::span<const ThingId, 20> ids) {
    auto* selected = match (slot) -> std::array<ThingId, 20>* {
        case ResearchWorkSlot::primary   => &lab->primary_staff;
        case ResearchWorkSlot::secondary => &lab->secondary_staff;
        case _                           => nullptr;
    };
    if (!selected)
        return true;
    for (auto id : *selected)
        if (id)
            staff(c, id)->researching = 0;
    //Preserve forward copying and per-ID flag stores; exact self-aliasing is
    //valid and no snapshot or cross-lane ownership rule is introduced.
    for (std::size_t i = 0; i < selected->size(); ++i) {
        const auto id = ids[i];
        (*selected)[i] = id;
        if (id)
            staff(c, id)->researching = 1;
    }
    return true;
}

int count_research_project_assigned_staff(ResearchLabRecord* lab, ResearchWorkSlot slot) {
    const auto& ids = slot == ResearchWorkSlot::primary ? lab->primary_staff : lab->secondary_staff;
    return static_cast<int>(std::count_if(ids.begin(), ids.end(), [](ThingId id) {
        return id != 0;
    }));
}

void copy_research_slot_worker_ids(ResearchLabRecord* lab, ResearchWorkSlot slot, std::span<ThingId, 20> output) {
    const auto& ids = slot == ResearchWorkSlot::secondary ? lab->secondary_staff : lab->primary_staff;
    for (std::size_t i = 0; i < ids.size(); ++i)
        output[i] = ids[i];
}

void try_add_research_project_from_selected_source(ResearchWorld& c, ResearchLabRecord* lab) {
    if (!is_obstacle_mission_research_enabled(c))
        return;
    const auto query = query_mission_obstacle_research_status(c);
    if (!query.placed_thing)
        return;
    const auto id = query.definition_id;
    const bool belongs = match (c.ports.assets.selected_resource_source(c)) -> bool {
        case 0 => id >= 3700;
        case 1 => id <= 1800;
        case 2 => id >= 2700 && id <= 2800;
        case _ => false;
    };
    if (!belongs || query.status != MissionResearchStatus::incomplete)
        return;
    if (lab->num_projects > lab->projects.size())
        throw std::length_error("invalid research project count");
    for (std::size_t i = 0; i < lab->num_projects; ++i)
        if (lab->projects[i].object_definition_id == id)
            return;
    auto& entry = append_row(*lab);
    entry.object_definition_id = id;
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition)
        throw std::out_of_range("mission research object definition was not found");
    entry.research_group = static_cast<std::uint8_t>(definition->research.group);
    entry.cost =
        static_cast<float>(get_or_create_theme_global_info_manager(c).mission_research.obstacle_research_effort);
    entry.cash_per_brain_point = 1;
    entry.upgrade_level = 0;
    entry.research_done = 0;
    entry.category = ResearchCategory::mission_obstacle;
    entry.staff_required = ResearchStaffRequirement::default_staff;
    entry.staff_ability = ThemeAbility::none;
    ++lab->num_projects;
}

void handle_world_research_lab_event(ResearchWorld& c, ResearchLabRecord* lab, WorldEvent* event) {
    match (event->type()) {
        case 17 => {
            const auto id = event->get<ResearchAvailabilityEvent>().definition_id;
            (void)find_object_definition_by_id(&c.resources.object_catalog, id);
            clear_cdk_registration_exclusion_for_researched_object(&get_or_create_theme_global_info_manager(c), id);
            if (event->get<ResearchAvailabilityEvent>().category == ResearchCategory::mission_obstacle) {
                WorldEvent obstacle{ CurrentObstacleEvent{ c.simulation.current->saved.current_obstacle } };
                notify(c, obstacle);
            }
            auto& entry = append_row(*lab);
            const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
            if (!definition)
                throw std::out_of_range("upgrade research object definition was not found");
            if (prepare_catalog_upgrade_project(c, &entry, definition))
                ++lab->num_projects;
        }
        case 33 => {
            const auto id = event->get<RetirementEvent>().retired_id;
            const auto* record = c.entities.at(id).payload;
            if (!record)
                throw std::invalid_argument("retiring research reference has no record");
            if (is_staff(record->kind))
                remove_world_research_lab_staff_reference(c, lab, record);
            else if (record->kind == ThingKind::park_object
                     && (static_cast<std::uint16_t>(
                             reinterpret_cast<const ParkObjectRuntimeView*>(record)->capabilities)
                         & static_cast<std::uint16_t>(ParkObjectCapabilities::thinking_space)))
                clear_world_research_lab_staffing(c, lab);
        }
        case 39 => try_add_research_project_from_selected_source(c, lab);
        case _  => ;
    }
}
} //namespace simcoaster
