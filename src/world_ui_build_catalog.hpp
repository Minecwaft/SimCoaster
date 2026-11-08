#pragma once
#include "simcoaster/coaster_design.hpp"
#include "simcoaster/world.hpp"
#include <filesystem>

namespace simcoaster {
//Native57CA20 visibility is deliberately separate from the broad authoring
//diagnostic menu and from the final placement validator.
bool is_build_catalog_definition_available(WorldRuntime&, const ResearchCatalogDefinition&);

struct BuildCatalogItem {
    std::uint16_t definition{};
    std::u16string name;
    std::int32_t cost{}, ownership{}, recent_research{};
    bool unique_owned{};
};

std::vector<BuildCatalogItem> build_catalog_items(WorldRuntime&, ObjectUiCategory);

struct ReadyMadeCoasterItem {
    std::string native_path;
    std::filesystem::path path;
    std::u16string name;
    std::uint32_t path_hash{};
    std::int32_t cost{};
};

std::vector<ReadyMadeCoasterItem> scan_ready_made_coasters(WorldRuntime&,
                                                           const std::filesystem::path&,
                                                           std::uint16_t base_ride,
                                                           std::vector<std::string>& diagnostics);
} //namespace simcoaster
