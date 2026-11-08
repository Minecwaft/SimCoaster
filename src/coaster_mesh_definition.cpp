#include "simcoaster/sam_schema.hpp"
#include "simcoaster/coaster_mesh.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"
#include <cmath>
#include <limits>
#include <ranges>

namespace simcoaster {
namespace {
struct TextureRow {
    [[=sam::Field { "pcTextureFilename", sam::Kind::string }]] std::optional<std::string> filename;
    [[=sam::Field { "bIsSelfIlluminating", 0, 2 }]] std::uint32_t self_illuminating {};
    [[=sam::Field { "fScrollRate", sam::Kind::scalar }]] float scroll_rate {};
};

struct PointRow {
    [[=sam::Field { "fX", sam::Kind::scalar }]] float x {};
    [[=sam::Field { "fY", sam::Kind::scalar }]] float y {};
    [[=sam::Field { "fU", sam::Kind::scalar }]] float u {};
    [[=sam::Field { "fNx", sam::Kind::scalar }]] float nx {};
    [[=sam::Field { "fNy", sam::Kind::scalar }]] float ny {};
};

struct EdgeRow {
    [[=sam::Field { "usTexture", 0, 20 }]] std::uint32_t texture {};
    [[=sam::Field { "usCSPoint1", 0, 20 }]] std::uint32_t first {};
    [[=sam::Field { "usCSPoint2", 0, 20 }]] std::uint32_t second {};
};

struct SectionRow {
    [[=sam::Field { "usCSPointBlockIndex", 0, 21 }]] std::uint32_t points {};
    [[=sam::Field { "usCSEdgeBlockIndex", 0, 21 }]] std::uint32_t edges {};
    [[=sam::Field { "usTrackEdge", 0, 20 }]] std::uint32_t track_edge {};
    [[=sam::Field { "usBlendToCSPointBlockIndex", 0, 21 }]] std::uint32_t blend {};
    [[=sam::Field { "fBlendLength", sam::Kind::scalar }]] float blend_length {};
};

struct SelectorRow {
    [[=sam::Field { "fSpeedRangeMin", sam::Kind::scalar }]] float speed_min {};
    [[=sam::Field { "fSpeedRangeMax", sam::Kind::scalar }]] float speed_max {};
    [[=sam::Field { "fDirectionRangeMin", sam::Kind::scalar }]] float heading_min {};
    [[=sam::Field { "fDirectionRangeMax", sam::Kind::scalar }]] float heading_max {};
    [[=sam::Field { "fPitchRangeMin", sam::Kind::scalar }]] float pitch_min {};
    [[=sam::Field { "fPitchRangeMax", sam::Kind::scalar }]] float pitch_max {};
    [[=sam::Field { "fBankRangeMin", sam::Kind::scalar }]] float bank_min {};
    [[=sam::Field { "fBankRangeMax", sam::Kind::scalar }]] float bank_max {};
    [[=sam::Field { "usCrossSectionIndex", 0, 21 }]] std::uint32_t section {};
    [[=sam::Field { "usBlendFromCrossSectionIndex", 0, 21 }]] std::uint32_t from {};
    [[=sam::Field { "usBlendToCrossSectionIndex", 0, 21 }]] std::uint32_t to {};
};
struct TrackSam;
constexpr std::array<std::string_view, 20> block_suffixes { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20" };
consteval {
    std::vector<std::meta::info> fields;
    const auto rows = [&](std::meta::info type, std::string name) {
        fields.push_back(std::meta::data_member_spec(type, { .name = name, .annotations = { std::meta::reflect_constant(sam::Rows { name }) } }));
        fields.push_back(std::meta::data_member_spec(^^typename std::uint32_t, { .name = name + "_count", .annotations = { std::meta::reflect_constant(sam::Extent { name }) } }));
    };
    for (const auto suffix : block_suffixes) {
        rows(^^std::array<PointRow, 20>, std::string { "asCrossSectionPoints" } + std::string { suffix });
        rows(^^std::array<EdgeRow, 20>, std::string { "asCrossSectionEdges" } + std::string { suffix });
    }
    rows(^^std::array<TextureRow, 20>, "asTextureData");
    rows(^^std::array<SectionRow, 20>, "asCrossSections");
    rows(^^std::array<SelectorRow, 20>, "asCrossSectionSelects");
    fields.push_back(std::meta::data_member_spec(^^DynamicTextureCoasterBalance, { .name = "coaster", .annotations = { std::meta::reflect_constant(sam::Group { "sCoasterType" }) } }));
    std::meta::define_aggregate(^^TrackSam, fields);
}

[[noreturn]] void invalid(std::size_t index) {
    throw MeshError { MeshFailure::configuration, index, "Coaster.sam track cross-sections", {} };
}

std::size_t index_of(std::uint32_t index, std::size_t size) {
    if (!index || index > size) {
        invalid(index);
    }
    return index - 1;
}
} //namespace

std::expected<CoasterMeshDefinition, MeshError> parse_coaster_mesh_definition(std::string_view text) {
    try {
        auto parsed = std::make_unique<TrackSam>();
        const auto read = sam::parse_record<TrackSam, true>(*parsed, text);
        if (!read) {
            return std::unexpected(MeshError { MeshFailure::configuration, read.error().offset, "Coaster.sam track schema", {} });
        }
        std::array<std::vector<CoasterCrossSectionPoint>, 20> point_blocks;
        std::array<std::vector<CoasterCrossSectionEdge>, 20> edge_blocks;
        constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^TrackSam, std::meta::access_context::current()));
        template for (constexpr auto block : std::views::iota(std::size_t { 0 }, std::size_t { 20 })) {
            for (std::size_t j = 0; j < parsed->[:members[block * 4 + 1]:]; ++j) {
                const auto& p = parsed->[:members[block * 4]:][j];
                if (!std::isfinite(p.x + p.y + p.u + p.nx + p.ny) || !(std::hypot(p.nx, p.ny) > 0)) {
                    invalid(block);
                }
                point_blocks[block].push_back({ p.x, p.y + parsed->coaster.fTrackHeightAdjust, p.u, p.nx, p.ny });
            }
            for (std::size_t j = 0; j < parsed->[:members[block * 4 + 3]:]; ++j) {
                const auto& e = parsed->[:members[block * 4 + 2]:][j];
                edge_blocks[block].push_back({ e.texture, e.first, e.second });
            }
        }
        CoasterMeshDefinition result;
        auto collision = parse_coaster_collision_profile(text);
        if (!collision) {
            return std::unexpected(collision.error());
        }
        result.collision_profile = std::move(*collision);
        for (std::size_t i = 0; i < parsed->asTextureData_count; ++i) {
            const auto& t = parsed->asTextureData[i];
            if (!t.filename || t.filename->empty()) {
                invalid(i);
            }
            result.textures.push_back({ *t.filename, t.self_illuminating != 0, t.scroll_rate });
        }
        for (std::size_t i = 0; i < parsed->asCrossSections_count; ++i) {
            const auto& source = parsed->asCrossSections[i];
            CoasterCrossSection s;
            s.points = point_blocks[index_of(source.points, point_blocks.size())];
            s.edges = edge_blocks[index_of(source.edges, edge_blocks.size())];
            s.blend_length = source.blend_length;
            if (s.points.empty() || s.edges.empty()) {
                invalid(i);
            }
            if (source.blend) {
                s.blend_points = point_blocks[index_of(source.blend, point_blocks.size())];
                if (s.blend_points.size() != s.points.size()) {
                    invalid(i);
                }
            }
            for (const auto& e : s.edges) {
                if (e.texture >= result.textures.size() || e.first >= s.points.size() || e.second >= s.points.size()) {
                    invalid(i);
                }
            }
            result.sections.push_back(std::move(s));
        }
        result.invalid_section = parsed->coaster.uiInvalidCrossSection ? index_of(static_cast<std::uint32_t>(parsed->coaster.uiInvalidCrossSection), result.sections.size()) : 0;
        if (parsed->coaster.uiWinchCrossSection) {
            result.winch_section = index_of(static_cast<std::uint32_t>(parsed->coaster.uiWinchCrossSection), result.sections.size());
        }
        for (std::size_t i = 0; i < parsed->asCrossSectionSelects_count; ++i) {
            const auto& source = parsed->asCrossSectionSelects[i];
            CoasterCrossSectionSelector select;
            select.section = index_of(source.section, result.sections.size());
            if (source.from) {
                select.blend_from = index_of(source.from, result.sections.size());
            }
            if (source.to) {
                select.blend_to = index_of(source.to, result.sections.size());
            }
            const std::array ranges { std::pair { source.speed_min, source.speed_max }, std::pair { source.heading_min, source.heading_max }, std::pair { source.pitch_min, source.pitch_max }, std::pair { source.bank_min, source.bank_max } };
            for (std::size_t j = 0; j < ranges.size(); ++j) {
                const auto [low, high] = ranges[j];
                select.ranges[j] = low != 0 || high != 0 ? std::pair { low * 0.017453292f, high * 0.017453292f } : std::pair { -std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            }
            result.selectors.push_back(std::move(select));
        }
        if (result.selectors.empty() || result.textures.empty()) {
            invalid(0);
        }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::span<const SamFieldContract> coaster_mesh_sam_contracts() {
    static constexpr auto fields = std::define_static_array(compiled_sam_contracts<TrackSam>());
    return fields;
}
} //namespace simcoaster
