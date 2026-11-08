#pragma once
#include <array>
#include <cstdint>

namespace simcoaster {
struct WorldRuntime;
struct SceneCamera;

//4288F0 copies these ThemeEngine fields directly, without swapping channels.
//Their representation is D3DCOLOR ARGB; display clear/fog use only RGB.
struct WorldAtmosphereColors {
    std::uint32_t background_argb {};
    std::uint32_t fog_argb = 4194432; //6F1D0D, before the theme is loaded.
};

//Renderer startup6F1CD1. World graphics-profile and animation owners already
//replace these values; resolving a frame never reapplies a quality preset.
struct WorldFogRange {
    float start = 100.f, end = 130.f;
};

//6E0980 projection followed by6DF440's adjustment preserves homogeneous Z.
//6D84C0/6D86C0 consume it BEFORE6E16C0 divides/maps GPU depth:
//Z = sin(fov/2) / (1-near/far) * (positive_camera_Z-near).
//This is neither radial distance nor the final normalized depth buffer.
struct WorldFogProjection {
    float scale = 1.f, bias {};
};

//A frame snapshot, not another owner of camera, profile or animation state.
//The chosen portable fog path follows the native explicit vertex-depth
//contract. Original table-fog/alpha-fog/color-fog selection was driver-probed
//by6C46E0; retaining those hardware probes is unnecessary on the new backend.
struct WorldAtmosphere {
    WorldAtmosphereColors colors;
    WorldFogRange range;
    WorldFogProjection projection;
    bool fog_enabled {}, alternate_clear_color {};
};

inline constexpr std::uint32_t world_fog_material_flag = 0x100000u;

//Source routing determines eligibility.6EFC10 meshes,6D0FC7 terrain and
//6C2A95 immediate World quads admit fog;6F6C20's sky material does not.
//Keep source-specific projected-shadow/overlay decisions at their producer.
enum class WorldFogEligibility : std::uint8_t {
    excluded,
    native_geometry
};

WorldFogProjection world_fog_projection(float near_clip, float far_clip, float vertical_fov_radians);
WorldAtmosphere make_world_atmosphere(WorldAtmosphereColors, WorldFogRange, const SceneCamera&, bool fog_enabled, bool alternate_clear_color = false);

//6A5351's special-view latch tracks flags0x16. Distances come from the live
//WorldRuntime, including an active5D0400 animation override and5D0840 restoration.
//Online lobby and temporary secondary passes may instead call make_* with
//their explicit, independently owned overrides; this is not an ATM heuristic.
WorldAtmosphere resolve_world_atmosphere(const WorldRuntime&, WorldAtmosphereColors, const SceneCamera&);

//6D6986 chooses fog RGB when fog OR alternate-clear is enabled, otherwise
//theme BackColour. The modern opaque display surface has alpha1; a theme's
//unused packed alpha byte must not make captures/window clears transparent.
std::array<float, 4> world_atmosphere_clear_color(const WorldAtmosphere&);
std::array<float, 3> world_atmosphere_fog_color(const WorldAtmosphere&);

//Only the fog bit changes. Blend factors, alpha testing/reference, depth,
//texture-source policy, and all other material flags retain their source.
std::uint32_t world_fog_material_flags(const WorldAtmosphere&, std::uint32_t source_flags, WorldFogEligibility);
bool world_fog_material_enabled(const WorldAtmosphere&, std::uint32_t material_flags);

//Modern homogeneous World-geometry W is positive camera Z. Consumers with
//camera-space geometry can supply that Z directly. Explicit screen-space
//effects must not substitute their depth selector or constant W for it.
float world_fog_preprojection_depth(const WorldAtmosphere&, float positive_camera_depth);
float world_fog_factor(const WorldAtmosphere&, float preprojection_depth);

struct WorldFogContribution {
    float diffuse_scale = 1.f;
    std::array<float, 3> additive_rgb {};
};

//6D84C0's two RGB operands, suitable for vertex production: attenuate diffuse
//RGB, leave diffuse alpha unchanged, then add fog after texture combination.
//Keep them separate through interpolation; varying vertex lighting must not
//be multiplied by an independently interpolated fog factor after shading.
//The portable fixed-function-style path uses noperspective colour varyings;
//UV interpolation remains perspective-correct. This replaces the original
//device-dependent colour interpolation/probing with one explicit policy.
WorldFogContribution world_fog_contribution(const WorldAtmosphere&, std::uint32_t material_flags, float preprojection_depth);
} //namespace simcoaster
