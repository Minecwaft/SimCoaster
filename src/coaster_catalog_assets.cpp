#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/layout_replay.hpp"
#include "simcoaster/object_catalog.hpp"
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace simcoaster {
std::size_t PredefinedCoasterBalance::sentinel_count() const {
    for (std::size_t i = 0; i < CoasterPylons.size(); ++i) {
        if (CoasterPylons[i].X == 512) {
            return i;
        }
    }
    throw std::invalid_argument("prebuilt.sam has no X=512 sentinel within 64 rows");
}

std::expected<PredefinedCoasterBalance, CoasterCatalogError> parse_predefined_coaster_balance(std::string_view text) {
    PredefinedCoasterBalance result;
    const auto parsed = sam::parse_record(result, text);
    if (!parsed) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::malformed_predefined_data, "prebuilt.sam", parsed.error() });
    }
    try {
        const auto count = result.sentinel_count();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& row = result.CoasterPylons[i];
            if (row.X < -127 || row.X > 127 || row.Y < -127 || row.Y > 127 || row.Level < 0 || row.Level > 255 || !std::isfinite(row.Loft + row.Rotate + row.Wobble + row.Pitch)) {
                throw std::invalid_argument("invalid predefined coaster row");
            }
        }
    } catch (const std::invalid_argument&) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::malformed_predefined_data, "prebuilt.sam", {} });
    }
    return result;
}

std::expected<PredefinedCoasterBalance, CoasterCatalogError> load_predefined_coaster_balance(const ResearchCatalogDefinition& definition) {
    if (!definition.source_asset) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::missing_predefined_data, std::string { definition.resource_catalog_name }, {} });
    }
    const auto& path = definition.source_asset->source_path;
    auto source = open_mesh_model_library(std::filesystem::path { path });
    if (!source) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::invalid_source, path, {} });
    }
    const auto data = source->read_resource("prebuilt.sam");
    if (!data) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::invalid_source, path, {} });
    }
    if (!*data) {
        return std::unexpected(CoasterCatalogError { CoasterCatalogFailure::missing_predefined_data, path, {} });
    }
    const auto& bytes = **data;
    auto result = parse_predefined_coaster_balance({ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    if (!result) {
        result.error().source = path + "/prebuilt.sam";
    }
    return result;
}

void CoasterCatalogRuntime::select_design_file(AttractionWorld& c, const std::filesystem::path& path, int rotation) {
    struct Input final : Stream {
        std::ifstream source;

        explicit Input(const std::filesystem::path& path) : source(path, std::ios::binary) {}

        std::int32_t read(std::span<std::byte> out) override {
            source.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            return static_cast<std::int32_t>(source.gcount());
        }

        std::int32_t write(std::span<const std::byte>) override {
            return -1;
        }
    } input { path };

    if (!input.source) {
        throw std::runtime_error("cannot open coaster design");
    }
    auto design = read_coaster_design(input);
    if (!design) {
        throw std::invalid_argument("invalid coaster design stream");
    }
    select_design(c, std::move(*design), rotation);
}

bool is_coaster_catalog_definition(const ResearchCatalogDefinition& definition) {
    return definition.balance && (definition.balance->DefaultCoaster.AddDefaultPylons || definition.balance->Prebuilt.IsPrebuiltCoaster || definition.which_track_type == 3);
}

std::uint32_t construction_track_type(const AttractionQuery& c) {
    return c.construction.selected_track_type ? 0u : c.construction.track_type;
}

void set_construction_track_type(AttractionWorld& c, std::uint32_t type, bool recording) {
    if (recording && !c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (!c.construction.action_record_stream) {
            throw std::logic_error("construction recorder is not bound");
        }
        const auto written = write_layout_action(*c.construction.action_record_stream, { LayoutActionCode::set_track, LayoutWord { static_cast<std::uint16_t>(type) } });
        if (!written) {
            throw std::runtime_error("cannot record construction track selector");
        }
    }
    c.construction.track_type = type;
}
} //namespace simcoaster
