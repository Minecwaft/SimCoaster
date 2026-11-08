#pragma once
#include "mesh_terrain.hpp"
#include <memory>

namespace simcoaster {
enum class MeshHoardingFlag : std::uint32_t {
    present = 2,
    enabled = 0x20,
    raising = 0x40,
    lowering = 0x80,
    closed_texture = 0x100,
    construction_texture = 0x200,
    condemned_texture = 0x400,
    upgrade_texture = 0x800,
};

struct MeshHoardingTemplate {
    MeshModel model;
    std::uint32_t width {}, height {};
    //Endpoint cell coordinates, in angle-sorted segment order. Original packed
    //WORD pairs become independent native coordinates, with no host pointers.
    std::vector<std::array<std::array<std::uint32_t, 2>, 2>> samples;
};

std::expected<std::optional<MeshHoardingTemplate>, MeshError> build_mesh_hoarding(const MeshModel&, const MeshProceduralBase&, const DefinitionGrid& layout, std::string_view theme);

struct MeshHoardingRuntime {
    std::shared_ptr<const MeshHoardingTemplate> assets;
    MeshModel model;
    float rate {};
    bool registered {};
};

struct MeshHoardingResult {
    bool registered {}, unregistered {}, pose_changed {};
};

MeshHoardingRuntime create_mesh_hoarding(std::shared_ptr<const MeshHoardingTemplate>);
std::expected<void, MeshError> initialize_mesh_hoarding_terrain(MeshHoardingRuntime&, const TerrainGridView&, const std::array<std::int64_t, 2>& minimum, const std::array<std::int64_t, 2>& maximum, std::int32_t orientation, float root_height);
std::expected<void, MeshError> apply_mesh_hoarding_phase(MeshHoardingRuntime&, float phase);
std::expected<MeshHoardingResult, MeshError> select_mesh_hoarding_variant(MeshHoardingRuntime&, std::uint32_t& flags, const MeshModel& base, std::int32_t orientation, std::int32_t variant);
void reverse_mesh_hoarding(MeshHoardingRuntime&, std::uint32_t& flags);
std::expected<MeshHoardingResult, MeshError> advance_mesh_hoarding(MeshHoardingRuntime&, std::uint32_t& flags, float& phase, float delta_seconds);
std::expected<MeshHoardingResult, MeshError> restore_mesh_hoarding(MeshHoardingRuntime&, std::uint32_t& flags, float phase, const MeshModel& base, std::int32_t orientation);
} //namespace simcoaster
