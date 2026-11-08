#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
bool is_obstacle_mission_research_enabled(MissionWorld& c) {
    const auto& balance = get_or_create_theme_global_info_manager(c).mission_research;
    //Native0x5F0190 tests both full DWORDs, before any definition-ID narrowing.
    return balance.obstacle_type != 0 && balance.no_research == 0;
}

MissionResearchQuery query_mission_obstacle_research_status(MissionWorld& c) {
    if (!is_obstacle_mission_research_enabled(c)) {
        return {};
    }
    MissionResearchQuery result {};
    result.definition_id = static_cast<std::uint16_t>(get_or_create_theme_global_info_manager(c).mission_research.obstacle_type);
    if (!c.simulation.current) {
        throw std::invalid_argument("mission obstacle base World");
    }
    const auto* obstacle = resolve_mission_obstacle(c);
    result.placed_thing = obstacle ? obstacle->base.header.id : 0;
    result.status = get_or_create_theme_global_info_manager(c).mission_obstacle_research_complete ? MissionResearchStatus::complete : MissionResearchStatus::incomplete;

    //Native0x5F01D0 deliberately performs separate singleton accesses. Capture
    //EndingTheme before acquiring the selected manager and resolving its ordinal.
    const auto ending_theme = get_or_create_theme_global_info_manager(c).mission_research.ending_theme;
    auto& selected_manager = get_or_create_theme_global_info_manager(c);
    if (!selected_manager.selected_theme_info) {
        throw std::invalid_argument("selected theme info");
    }
    const auto ordinal = selected_manager.selected_theme_info->source_ordinal;
    if (ordinal >= theme_source_count) {
        throw std::out_of_range("theme source ordinal");
    }
    auto* resolved = get_or_create_theme_global_info_for_ordinal(c, selected_manager, ordinal);
    if (!resolved) {
        throw std::invalid_argument("resolved theme info");
    }
    if (resolved->source_ordinal == static_cast<std::uint32_t>(ending_theme)) {
        auto* definition = find_object_definition_by_id(&c.resources.object_catalog, result.definition_id);
        if (!definition) {
            throw std::out_of_range("mission obstacle definition");
        }
        if (definition->opened_automatically == 1) {
            result.status = MissionResearchStatus::opened_automatically;
        }
    }
    if (get_or_create_theme_global_info_manager(c).obstacle_removed_message_received) {
        result.status = MissionResearchStatus::removed;
    }
    return result;
}

void refresh_theme_ability_activation_flags(ThemeGlobalInfoManager* manager) {
    if (!manager) {
        throw std::invalid_argument("null theme manager");
    }
    const auto& balance = manager->mission_research;
    const auto count = std::bit_cast<std::int32_t>(balance.obstacle_staff_parsed_extent);
    if (count > static_cast<std::int32_t>(balance.obstacle_staff.size())) {
        throw std::length_error("mission obstacle staff count");
    }
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& row = balance.obstacle_staff[static_cast<std::size_t>(i)];
        if (row.number != 0) {
            manager->theme_ability_activation_flags.at(static_cast<std::size_t>(row.ability)) = 1;
        }
    }
    //Native0x74C3C0 retains existing flags and sets this latch even for a
    //nonpositive parsed extent. It does not set the removal-message latch.
    manager->mission_obstacle_research_complete = 1;
}

void mark_obstacle_removed_message_received(ThemeGlobalInfoManager* manager) {
    if (!manager) {
        throw std::invalid_argument("null theme manager");
    }
    manager->obstacle_removed_message_received = 1;
}
} //namespace simcoaster
