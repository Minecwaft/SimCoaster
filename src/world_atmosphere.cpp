#include "simcoaster/world_atmosphere.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/world.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
std::array<float, 3> color_rgb(std::uint32_t argb) {
    constexpr auto channel_scale = 1.f / 255.f;
    return { static_cast<float>((argb >> 16) & 255u) * channel_scale,
             static_cast<float>((argb >> 8) & 255u) * channel_scale,
             static_cast<float>(argb & 255u) * channel_scale };
}

void validate_range(WorldFogRange range) {
    if (!std::isfinite(range.start) || !std::isfinite(range.end))
        throw std::invalid_argument("World fog range is not finite");
    //6C52C0 stores a zero reciprocal for a nonpositive span. The explicit
    //vertex contract still makes it a step immediately after start, rather
    //than silently swapping endpoints or treating this as disabled fog.
}
} //namespace

WorldFogProjection world_fog_projection(float near_clip, float far_clip, float vertical_fov_radians) {
    if (!std::isfinite(near_clip)
        || !std::isfinite(far_clip)
        || !std::isfinite(vertical_fov_radians)
        || near_clip <= 0.f
        || far_clip <= near_clip
        || vertical_fov_radians <= 0.f
        || vertical_fov_radians >= std::numbers::pi_v<float>)
        throw std::invalid_argument("World fog projection is invalid");
    const auto half_fov = static_cast<double>(vertical_fov_radians) * .5;
    const auto scale = std::sin(half_fov) / (1. - static_cast<double>(near_clip) / far_clip);
    return { static_cast<float>(scale), static_cast<float>(-scale * near_clip) };
}

WorldAtmosphere make_world_atmosphere(WorldAtmosphereColors colors,
                                      WorldFogRange range,
                                      const SceneCamera& camera,
                                      bool fog_enabled,
                                      bool alternate_clear_color) {
    validate_range(range);
    return { colors,
             range,
             world_fog_projection(camera.near_plane, camera.far_plane, camera.vertical_fov),
             fog_enabled,
             alternate_clear_color };
}

WorldAtmosphere
resolve_world_atmosphere(const WorldRuntime& c, WorldAtmosphereColors colors, const SceneCamera& camera) {
    return make_world_atmosphere(colors,
                                 { c.view.fog_distance_start, c.view.fog_distance_end },
                                 camera,
                                 (c.view.flags & 0x16u) != 0);
}

std::array<float, 4> world_atmosphere_clear_color(const WorldAtmosphere& atmosphere) {
    const auto rgb =
        color_rgb(atmosphere.fog_enabled || atmosphere.alternate_clear_color ? atmosphere.colors.fog_argb
                                                                             : atmosphere.colors.background_argb);
    return { rgb[0], rgb[1], rgb[2], 1.f };
}

std::array<float, 3> world_atmosphere_fog_color(const WorldAtmosphere& atmosphere) {
    return color_rgb(atmosphere.colors.fog_argb);
}

std::uint32_t world_fog_material_flags(const WorldAtmosphere& atmosphere,
                                       std::uint32_t source_flags,
                                       WorldFogEligibility eligibility) {
    const auto enabled = atmosphere.fog_enabled && eligibility == WorldFogEligibility::native_geometry;
    return (source_flags & ~world_fog_material_flag) | (enabled ? world_fog_material_flag : 0u);
}

bool world_fog_material_enabled(const WorldAtmosphere& atmosphere, std::uint32_t material_flags) {
    return atmosphere.fog_enabled && (material_flags & world_fog_material_flag) != 0;
}

float world_fog_preprojection_depth(const WorldAtmosphere& atmosphere, float positive_camera_depth) {
    return atmosphere.projection.scale * positive_camera_depth + atmosphere.projection.bias;
}

float world_fog_factor(const WorldAtmosphere& atmosphere, float preprojection_depth) {
    if (!atmosphere.fog_enabled)
        return 0.f;
    if (!std::isfinite(preprojection_depth))
        throw std::invalid_argument("World fog vertex depth is not finite");
    if (preprojection_depth <= atmosphere.range.start)
        return 0.f;
    if (preprojection_depth < atmosphere.range.end)
        return (preprojection_depth - atmosphere.range.start) / (atmosphere.range.end - atmosphere.range.start);
    return 1.f;
}

WorldFogContribution
world_fog_contribution(const WorldAtmosphere& atmosphere, std::uint32_t material_flags, float preprojection_depth) {
    if (!world_fog_material_enabled(atmosphere, material_flags))
        return {};
    const auto amount = world_fog_factor(atmosphere, preprojection_depth);
    auto fog = world_atmosphere_fog_color(atmosphere);
    for (auto& channel : fog)
        channel *= amount;
    return { 1.f - amount, fog };
}
} //namespace simcoaster
