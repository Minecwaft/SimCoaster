#include "simcoaster/world_events.hpp"
#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
void broadcast(ResearchWorld& c, WorldEvent& event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}

std::size_t project_count(const ResearchLabRecord& lab) {
    if (lab.num_projects > lab.projects.size())
        throw std::out_of_range("research project count");
    return lab.num_projects;
}
} //namespace

StaffType map_research_staff_requirement_to_staff_type_index(ResearchStaffRequirement requirement) {
    return match (requirement) -> StaffType {
        case ResearchStaffRequirement::gardener    => StaffType::gardener;
        case ResearchStaffRequirement::handyman    => StaffType::handyman;
        case ResearchStaffRequirement::mechanic    => StaffType::mechanic;
        case ResearchStaffRequirement::entertainer => StaffType::entertainer;
        case ResearchStaffRequirement::guard       => StaffType::guard;
        case ResearchStaffRequirement::researcher  => StaffType::researcher;
        case _                                     => StaffType::unsupported_index;
    };
}

void mark_all_research_lab_projects_available(ResearchWorld& c, ResearchLabRecord* lab) {
    auto remaining = project_count(*lab);
    while (remaining) {
        auto& project = lab->projects[--remaining];
        if (world_operations::find_object_control(c, project.object_definition_id)->available != 1) {
            world_operations::find_object_control(c, project.object_definition_id)->available = 1;
            WorldEvent event{ ResearchAvailabilityEvent{ project.object_definition_id, project.category } };
            broadcast(c, event);
        }
        //Iterate the live catalog after the event. Appended research rows are not
        //revisited, but listeners may change the current row and linked
        //definitions.
        for (auto* definition = first_object_catalog_entry(&c.resources.object_catalog); definition;
             definition = next_object_catalog_entry(&c.resources.object_catalog))
            if (definition->research.linked_with == project.object_definition_id)
                world_operations::find_object_control(c, definition->id)->available = 1;
    }
}

void synchronize_research_lab_project_object_availability(ResearchWorld& c, ResearchLabRecord* lab) {
    auto remaining = project_count(*lab);
    while (remaining) {
        auto& project = lab->projects[--remaining];
        if (project.research_done >= project.cost) {
            world_operations::find_object_control(c, project.object_definition_id)->available = 1;
            WorldEvent event{ ResearchAvailabilityEvent{ project.object_definition_id, project.category } };
            broadcast(c,
                      event); //Every completed pass broadcasts, even if already set.
        } else
            world_operations::find_object_control(c, project.object_definition_id)->available = 0;
    }
}

bool set_research_lab_active_and_notify_subscribers(ResearchWorld& c, ResearchLabRecord* lab) {
    if (!lab->lab_active) {
        lab->lab_active = 1;
        WorldEvent event{ Tag19Event{ 5 } };
        broadcast(c, event);
    }
    return true;
}

bool clear_research_lab_active_and_notify_subscribers(ResearchWorld& c, ResearchLabRecord* lab) {
    if (!lab->lab_active)
        return false;
    lab->lab_active = 0;
    WorldEvent event{ Tag19Event{ 6 } };
    broadcast(c, event);
    return true;
}
} //namespace simcoaster
