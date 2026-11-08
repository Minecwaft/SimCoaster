#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_theme.hpp"
#include <stdexcept>

namespace simcoaster {
namespace {
const ThemeGlobalSaveInfo& resolved_info(MissionWorld& c, ThemeGlobalInfoManager& manager, std::uint32_t ordinal) {
    if (ordinal >= theme_source_count) {
        throw std::out_of_range("theme source ordinal");
    }
    const auto* info = get_or_create_theme_global_info_for_ordinal(c, manager, ordinal);
    return require_bound<std::runtime_error>(info, "theme information could not be loaded");
}
} //namespace

void clear_research_action_532_eligibility(MissionWorld& c) {
    get_or_create_theme_global_info_manager(c).research_lab_action_532_eligible = 0;
}

std::uint32_t selected_research_theme_ordinal(MissionWorld& c) {
    auto& manager = get_or_create_theme_global_info_manager(c);
    if (!manager.selected_theme_info) {
        throw std::logic_error("no selected theme information");
    }
    return resolved_info(c, manager, manager.selected_theme_info->source_ordinal).source_ordinal;
}

std::int32_t research_theme_current_tech_level(MissionWorld& c, std::uint32_t ordinal) {
    auto& manager = get_or_create_theme_global_info_manager(c);
    return resolved_info(c, manager, ordinal).current_tech_level;
}
} //namespace simcoaster
