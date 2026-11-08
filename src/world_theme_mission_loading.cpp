#include "simcoaster/world_theme.hpp"
#include <stdexcept>
#include <string>

namespace simcoaster {
std::string mission_balance_asset_path(const MissionQuery& c, std::int32_t mission) {
    //Native Miss%02d has a minimum width, including the sign, rather than a
    //mission-number range restriction: -1,00,09,100 are all valid spellings.
    auto number = std::to_string(mission);
    if (number.size() < 2) {
        number.insert(number.begin(), '0');
    }
    return c.missions.missions_directory + "/Miss" + number + ".sam";
}

bool load_mission_balance_data(MissionWorld& c, ThemeGlobalInfoManager* manager) {
    if (!manager) {
        throw std::invalid_argument("null theme manager");
    }
    const auto path = mission_balance_asset_path(c, manager->mission_number);
    const auto text = c.ports.assets.read_balance_asset(c, path);
    if (!text) {
        return false; //Missing files leave existing configuration intact.
    }
    if (!text->empty() && !sam::parse_integer_record(manager->mission_research, *text)) {
        return false;
    }

    //Native0x74C0CA copies all five slots, independently of parsed extent.
    //Failed parsing retains its partial configuration writes and never enters
    //this loop. Empty files skip parsing but still refresh these runtime rows.
    project_mission_objective_configuration(*manager);
    return true;
}

void project_mission_objective_configuration(ThemeGlobalInfoManager& manager) {
    for (std::size_t i = 0; i < manager.objective_entries.size(); ++i) {
        const auto& source = manager.mission_research.exit_statistics[i];
        auto& target = manager.objective_entries[i];
        target.objective_kind = source.objective_kind;
        target.elapsed_days = 0;
        target.deadline_days = source.target_time;
        target.current_value = 0;
        target.target_value = source.target_value;
        target.source_filter_ordinal = source.source_filter_ordinal;
        target.state = MissionObjectiveState::inactive;
        target.percentage_baseline_value = 0;
        //Start FILETIME words and the eight opaque tail bytes remain untouched.
    }
}
} //namespace simcoaster
