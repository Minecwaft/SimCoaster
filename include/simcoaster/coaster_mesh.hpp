#pragma once
#include "coaster_profile.hpp"
#include "coaster_collision.hpp"

namespace simcoaster {
struct CoasterCrossSectionPoint {
    float x{}, y{}, u{}, normal_x{}, normal_y{};
};

struct CoasterCrossSectionEdge {
    std::uint32_t texture{}, first{}, second{};
};

struct CoasterCrossSection {
    std::vector<CoasterCrossSectionPoint> points, blend_points;
    std::vector<CoasterCrossSectionEdge> edges;
    float blend_length{};
};

struct CoasterCrossSectionSelector {
    std::array<std::pair<float, float>, 4> ranges; //Speed, heading, pitch, bank.
    std::size_t section{};
    std::optional<std::size_t> blend_from, blend_to;
};

struct CoasterTrackTexture {
    std::string filename;
    bool self_illuminating{};
    float scroll_rate{};
};

struct CoasterMeshDefinition {
    std::vector<CoasterCrossSection> sections;
    std::vector<CoasterCrossSectionSelector> selectors;
    std::vector<CoasterTrackTexture> textures;
    CoasterCollisionProfile collision_profile = default_coaster_collision_profile();
    std::size_t invalid_section{};
    std::optional<std::size_t> winch_section;
};

//The twenty numbered point/edge groups are generated from their explicit SAM
//identities. Runtime indices are native-size values; wire widths stay in SAM.
std::expected<CoasterMeshDefinition, MeshError> parse_coaster_mesh_definition(std::string_view);

struct CoasterTrackMeshVertex {
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
};

struct CoasterTrackMesh {
    std::vector<CoasterTrackMeshVertex> vertices;
    std::vector<std::vector<std::uint32_t>> texture_indices;
    std::size_t cross_sections{}, section_changes{};
    MeshBounds bounds;
};

std::expected<CoasterTrackMesh, MeshError> build_coaster_track_mesh(const CoasterMeshDefinition&,
                                                                    std::span<const CoasterGeometryObjectInput>,
                                                                    const CoasterRideTrackRuntime&,
                                                                    const CoasterCollisionState* peers = nullptr,
                                                                    std::uint32_t selection_texture_bias = 0);
} //namespace simcoaster
