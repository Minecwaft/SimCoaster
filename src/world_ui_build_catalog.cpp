#include "world_ui_build_catalog.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
std::uint32_t owned(WorldRuntime& c, std::uint16_t definition) {
    const auto* control = find_world_object_control(c.simulation.current->object_controls, definition);
    return control ? control->live_instance_count : 0;
}

bool recently_researched(WorldRuntime& c, ObjectUiCategory category, std::uint16_t definition, std::uint16_t linked) {
    const auto index = std::to_underlying(category);
    //Native analyser event17 records only categories0..3. Its facility
    //catalogue query otherwise reads beyond the four genuine rings; no host
    //pointer or adjacent metric bytes are interpreted as research history.
    if (index < 0 || index >= 4)
        return false;
    const auto id = c.simulation.current->saved.park_analyser;
    if (!id || id >= c.entities.size() || !c.entities[id].payload)
        throw std::logic_error("Build catalogue requires the park analyser");
    const auto& analyser = *c.entities.payload<const ParkAnalyserState>(id);
    const auto& history = analyser.short_histories.at(index);
    for (std::int32_t age = 0; age < std::min(history.num_entries, 3); ++age) {
        auto position = history.current_entry - age;
        if (position < 0) {
            if (!history.wrapped)
                continue;
            position += history.num_entries;
        }
        if (position < 0 || position >= 3)
            throw std::logic_error("Invalid catalogue research history cursor");
        const auto value = history.data.at(static_cast<std::size_t>(position));
        if (value == definition || (linked && value == linked))
            return true;
    }
    return false;
}

std::u16string filename_caption(std::string_view filename) {
    //492700 replaces the serialized names with the disk basename split at
    //its first space, hyphen or underscore; the picker rejoins both with space.
    if (filename.size() < 4)
        throw std::invalid_argument("Invalid ready-made coaster filename");
    filename.remove_suffix(4);
    const auto separator = filename.find_first_of(" -_");
    std::u16string output;
    const auto append = [&](std::string_view part) {
        for (const auto unit : part)
            output.push_back(static_cast<unsigned char>(unit));
    };
    append(filename.substr(0, separator));
    output.push_back(u' ');
    if (separator != std::string_view::npos)
        append(filename.substr(separator + 1));
    return output;
}
} //namespace

bool is_build_catalog_definition_available(WorldRuntime& c, const ResearchCatalogDefinition& definition) {
    if (!c.simulation.current || !definition.balance)
        return false;
    const auto& b = *definition.balance;
    if (b.Info.IsSpecial || definition.mesh_load_failed || b.AddOn.UpgradesId || b.AddOn.UpgradesAnyRide)
        return false;
    const auto mission = get_or_create_theme_global_info_manager(c).mission_research.info_number;
    if (b.UsageInfo.ThinkingSpace == 1 && mission < 1)
        return false;
    if (b.UsageInfo.TrainingRoom
        && !(mission > 1
             || (mission == 1 && query_mission_obstacle_research_status(c).status == MissionResearchStatus::complete)))
        return false;
    const auto* control = find_and_cache_world_object_control(c.simulation.current->object_controls, definition.id);
    return control && (control->available || (c.mode.game == GameMode::code_3 && definition.which_track_type == 3));
}

std::vector<BuildCatalogItem> build_catalog_items(WorldRuntime& c, ObjectUiCategory category) {
    std::vector<BuildCatalogItem> result;
    for (const auto* definition : c.resources.object_catalog.entries) {
        if (!definition
            || definition->which_ui_type != category
            || !is_build_catalog_definition_available(c, *definition))
            continue;
        const auto& b = *definition->balance;
        const auto* control = find_world_object_control(c.simulation.current->object_controls, definition->id);
        if (!control)
            throw std::logic_error("Visible Build definition has no object control");
        const auto linked = static_cast<std::uint16_t>(b.Research.LinkedWith);
        const auto count = owned(c, definition->id);
        result.push_back(
            { definition->id,
              get_catalog_object_display_name(c, definition->id),
              fixed::word(control->purchase_cost),
              count || (linked && owned(c, linked)) ? 1 : 0,
              control->first_instance_game_tick == -1 && recently_researched(c, category, definition->id, linked) ? 2
                                                                                                                  : 0,
              b.UsageInfo.SingleInstance == 1 && count > 0 });
    }
    return result;
}

std::vector<ReadyMadeCoasterItem> scan_ready_made_coasters(WorldRuntime& c,
                                                           const std::filesystem::path& root,
                                                           std::uint16_t base,
                                                           std::vector<std::string>& diagnostics) {
    std::vector<ReadyMadeCoasterItem> result;
    for (const auto& filename : c.ports.assets.find_asset_files(c, "data/my coasters/*.cos")) {
        const auto native_path = "data\\my coasters\\" + filename;
        auto stream = c.ports.assets.open_asset_stream(c, native_path);
        if (!stream) {
            diagnostics.push_back("Unable to read ready-made coaster: " + native_path);
            continue;
        }
        const auto design = read_coaster_design(*stream);
        if (!design) {
            //Native short-read success would expose uninitialized scratch. Keep a
            //visible failure instead of creating a plausible, incomplete design.
            diagnostics.push_back("Malformed ready-made coaster: " + native_path);
            continue;
        }
        //In the native base-ride search, this takes precedence over source index.
        if (design->definition_id != base)
            continue;
        result.push_back({ native_path,
                           root / "Data" / "my coasters" / filename,
                           filename_caption(filename),
                           ui_model_asset_hash(native_path),
                           fixed::word(design->build_cost) });
    }
    return result;
}
} //namespace simcoaster
