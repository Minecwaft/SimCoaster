#include "simcoaster/world_research.hpp"
#include "simcoaster/world_events.hpp"
#include <bit>
#include <cstring>

namespace simcoaster {
namespace {
bool less_or_unordered(float left, float right) {
    return !(left >= right);
}

float scaled_cost(std::int32_t cost, float scale) {
    return static_cast<float>(static_cast<double>(cost) * scale);
}
} //namespace

int next_crt_random(ResearchWorld& c) {
    c.random.crt_random_state = 214013u * c.random.crt_random_state + 2531011u;
    return static_cast<int>((c.random.crt_random_state >> 16) & 0x7FFFu);
}

int count_linked_staff_by_kind(ResearchWorld& c, std::uint32_t kind) {
    int result = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        const auto* record = row->payload;
        const auto type = static_cast<unsigned>(record->kind);
        if ((type == 4 || type == 5 || type == 6 || type == 8 || type == 7 || type == 20)
            && type == kind
            && !record->retirement_pending)
            ++result;
    }
    return result;
}

bool
prepare_catalog_upgrade_project(ResearchWorld& c, ResearchProject* entry, const ResearchCatalogDefinition* definition) {
    const auto& input = definition->research;
    if (input.category == ResearchCategory::upgrades || input.linked_with)
        return false;
    auto level = std::bit_cast<std::int32_t>(
        world_operations::find_object_control(c, definition->id)->research_option_index + 1);
    if (level >= 3)
        return false;
    while (input.upgrades[level].cost_of_research <= 0) {
        world_operations::find_object_control(c, definition->id)->research_option_index =
            static_cast<std::uint32_t>(level++);
        if (level >= 3)
            return false;
    }
    entry->upgrade_level = static_cast<std::uint8_t>(level);
    entry->research_group = 0;
    entry->category = ResearchCategory::upgrades;
    entry->cost = scaled_cost(input.upgrades[level].cost_of_research, c.research.balance.cost_scale_up);
    entry->cash_per_brain_point = input.upgrades[level].cash_per_brain_point;
    if (!entry->cash_per_brain_point) {
        next_crt_random(c);
        entry->cash_per_brain_point = 1;
    }
    entry->object_definition_id = definition->id;
    entry->research_done = 0;
    entry->staff_required = ResearchStaffRequirement::default_staff;
    entry->staff_ability = ThemeAbility::none;
    return true;
}

bool prepare_catalog_research_project(ResearchWorld& c,
                                      ResearchProject* entry,
                                      const ResearchCatalogDefinition* definition) {
    const auto& input = definition->research;
    if (input.linked_with) {
        world_operations::find_object_control(c, definition->id)->available = 0;
        return false;
    }
    if (input.upgrades[0].cost_of_research) {
        entry->object_definition_id = definition->id;
        entry->research_group = static_cast<std::uint8_t>(input.group);
        entry->cost = scaled_cost(input.upgrades[0].cost_of_research, c.research.balance.cost_scale_up);
        entry->cash_per_brain_point = input.upgrades[0].cash_per_brain_point;
        if (!entry->cash_per_brain_point)
            entry->cash_per_brain_point = next_crt_random(c) % 2 + 1;
        entry->upgrade_level = 0;
        entry->category = input.category;
        entry->research_done = 0;
        entry->staff_required = input.staff[0].staff;
        entry->staff_ability = input.staff[0].ability;
        entry->type = static_cast<std::uint32_t>(input.type);
        return true;
    }
    world_operations::find_object_control(c, definition->id)->available = 1;
    entry->staff_required = ResearchStaffRequirement::default_staff;
    entry->staff_ability = ThemeAbility::none;
    return prepare_catalog_upgrade_project(c, entry, definition);
}

void recalculate_world_research_group_outputs(ResearchLabRecord* lab) {
    std::uint32_t total = 0;
    for (const auto& group : lab->groups)
        total += group.effort;
    for (auto& group : lab->groups)
        group.output = total ? (lab->workload * ((100u * group.effort) / total)) / 100u : 0;
}

void build_world_research_lab_project_list(ResearchWorld& c, ResearchLabRecord* lab) {
    auto* definition = first_object_catalog_entry(&c.resources.object_catalog);
    while (definition) {
        if (!definition->mesh_load_failed && definition->which_ui_type != ObjectUiCategory::code_4) {
            auto& entry = lab->projects[lab->num_projects];
            if (prepare_catalog_research_project(c, &entry, definition)
                || prepare_catalog_upgrade_project(c, &entry, definition)) {
                const auto prior = lab->most_expensive_project;
                if (prior < 0 || less_or_unordered(lab->projects[prior].cost, entry.cost))
                    lab->most_expensive_project = static_cast<std::int32_t>(lab->num_projects);
                entry.row_index = lab->num_projects++;
            }
        }
        definition = next_object_catalog_entry(&c.resources.object_catalog);
    }
    recalculate_world_research_group_outputs(lab);
}

void advance_world_research_group_availability(ResearchWorld& c, ResearchLabRecord* lab, std::int32_t category) {
    auto& group = lab->groups[category];
    bool changed = false;
    for (;;) {
        std::uint32_t completed = 0, eligible = 0;
        if (std::bit_cast<std::int32_t>(lab->num_projects - 1) >= 0)
            for (auto left = lab->num_projects; left; --left) {
                const auto& project = lab->projects[left - 1];
                if (static_cast<std::int32_t>(project.category) == category
                    && project.research_group < group.highest_group_available + 1) {
                    ++eligible;
                    if (!less_or_unordered(project.research_done, project.cost))
                        ++completed;
                }
            }
        const auto percent = eligible ? (100u * completed) / eligible : 0;
        const volatile auto* threshold = &c.research.balance.percentage_for_tech[group.highest_group_available + 1];
        const auto required = static_cast<std::uint32_t>(*threshold);
        if (percent < required || group.highest_group_available >= 7)
            break;
        changed = true;
        ++group.highest_group_available;
    }
    if (changed && std::bit_cast<std::int32_t>(lab->num_projects - 1) >= 0) {
        unsigned unfinished = 0;
        for (auto left = lab->num_projects; left; --left) {
            const auto& project = lab->projects[left - 1];
            if ((category == 7 || static_cast<std::int32_t>(project.category) == category)
                && less_or_unordered(project.research_done, project.cost))
                ++unfinished;
        }
        if (unfinished && category >= 0 && category <= 3)
            world_operations::queue_scored_advisor_action(c, 124 + category);
    }
}

void initialize_world_research_lab_research_groups(ResearchWorld& c, ResearchLabRecord* lab) {
    for (unsigned category = 0; category < 7; ++category) {
        //The last two native group inputs come from the parser extent and
        //StartingWorkLoad immediately following the five category efforts.
        const auto effort = category < 5  ? static_cast<std::uint32_t>(c.research.balance.category_effort[category])
                          : category == 5 ? c.research.balance.category_parsed_extent
                                          : static_cast<std::uint32_t>(c.research.balance.starting_workload);
        lab->groups[category].effort = effort >= 100 ? 100 : effort;
        recalculate_world_research_group_outputs(lab);
        for (std::int32_t i = 0; i < std::bit_cast<std::int32_t>(lab->num_projects); ++i) {
            const auto& project = lab->projects[i];
            if (static_cast<unsigned>(project.category) == category
                && project.research_group <= lab->groups[category].highest_group_available
                && less_or_unordered(project.research_done, project.cost)) {
                lab->groups[category].current_project = i;
                break;
            }
        }
        advance_world_research_group_availability(c, lab, static_cast<std::int32_t>(category));
    }
}

int initialize_world_research_lab_runtime_state(ResearchWorld& c, ResearchLabRecord* lab) {
    lab->num_projects = 0;
    lab->most_expensive_project = -1;
    lab->workload = static_cast<std::uint32_t>(c.research.balance.starting_workload);
    //The original ignored getter still reads this World ID and metadata row.
    auto* volatile analyser = c.entities[c.simulation.current->saved.park_analyser].payload;
    (void)analyser;
    lab->lab_active = count_linked_staff_by_kind(c, 8) > 0;
    build_world_research_lab_project_list(c, lab);
    initialize_world_research_lab_research_groups(c, lab);
    for (int category : { 17, 39, 33 })
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &lab->base.header, category);
    lab->primary_staff.fill(0);
    lab->secondary_staff.fill(0);
    for (unsigned slot = 0; slot < 2; ++slot) {
        (slot ? lab->secondary_available : lab->primary_available) = 0;
        (slot ? lab->secondary_current_project : lab->primary_current_project) = -1;
        lab->consultants[slot] = 0;
    }
    lab->primary_current_project = -4;
    lab->secondary_current_project = -4;
    return 2;
}
} //namespace simcoaster
