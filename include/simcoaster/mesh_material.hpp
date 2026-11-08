#pragma once
#include "texture_image.hpp"
#include "world_atmosphere.hpp"

namespace simcoaster {
enum class MeshBlendFactor : std::uint8_t {
    zero = 1,
    one = 2,
    source_color = 3,
    inverse_source_color = 4,
    source_alpha = 5,
    inverse_source_alpha = 6,
    destination_alpha = 7,
    inverse_destination_alpha = 8,
    destination_color = 9,
    inverse_destination_color = 10,
    source_alpha_saturate = 11,
    both_source_alpha = 12,
    both_inverse_source_alpha = 13,
};

struct MeshMaterial {
    std::uint32_t flags { 0x1256 };
    bool generated_uv {}, secondary_texture {};
    //Native705AC0 source window: scale XY followed by origin XY. Generated
    //surfaces with already mapped UVs retain the identity transform.
    std::array<float, 4> texture_uv { 1, 1, 0, 0 };

    bool blended() const {
        return (flags & 0x100u) != 0;
    }

    bool depth_test() const {
        return !(flags & 0x8000u);
    }

    bool depth_write() const {
        return !(flags & 0x800u);
    }

    bool wrap_linear_min() const {
        return (flags & 0x400000u) != 0;
    }

    bool point_mag() const {
        return (flags & 0x20000u) != 0;
    }

    bool mipmaps() const {
        return !(flags & 0x40000u);
    }

    float alpha_reference() const {
        return flags & 0x400u ? static_cast<float>(flags & 0x200u ? 240 : 16) / 255.0f : -1.0f;
    }

    MeshBlendFactor source_blend() const {
        return static_cast<MeshBlendFactor>((flags >> 4) & 15u);
    }

    MeshBlendFactor destination_blend() const {
        return static_cast<MeshBlendFactor>(flags & 15u);
    }
};

//0x706630 +0x6EFD40: preserve source modes, slot overrides, group/node bits.
MeshMaterial derive_mesh_material(std::uint32_t node_flags, std::uint8_t group_flags, const MeshTextureSlot* slot, const TextureImage* texture, bool secondary = false);
std::array<float, 4> mesh_texture_uv_transform(const TextureImage&, std::uint16_t slot_flags = 0);
//0x6EE050 sphere-map kernel. Inputs have already received node.local*view;
//the native singular division is replaced with the texture center.
std::array<float, 2> mesh_reflection_uv(MeshPoint view_position, MeshPoint view_normal);
enum class MeshLightKind : std::uint8_t {
    directional = 2,
    point = 4,
    directional_point = 6
};

struct MeshLight {
    MeshLightKind kind { MeshLightKind::directional };
    MeshPoint color { 1, 1, 1 }, position {}, direction { 0, 0, 1 };
    float radius_squared { 10000 }, attenuation { 0.0001f };
};

struct MeshLighting {
    MeshPoint ambient { 1, 1, 1 }; //Empty/default scene explicitly requests an unlit asset preview.
    std::vector<MeshLight> lights;
    std::uint32_t diffuse_mask { 0xFFFFFFFFu };
};
enum class MeshLightingTransformFailure {
    singular,
    nonfinite
};
std::expected<MeshLighting, MeshLightingTransformFailure> mesh_lighting_in_local_space(const MeshLighting&, const MeshMatrix& model_to_world);
RgbaPixel mesh_vertex_lighting(const MeshLighting& local_lights, const MeshPoint& position, const MeshPoint& normal);
RgbaPixel mesh_argb_color(std::uint32_t);

struct ThemeRenderSettings {
    MeshLighting lighting;
    WorldAtmosphereColors atmosphere;
    TerrainTileBounds procedural_bounds;
};

ThemeRenderSettings load_installed_theme_render_settings(const std::filesystem::path& game_root, std::string_view theme);
//Standard.sam followed by the theme override; missing files leave context
//defaults.
MeshLighting load_installed_mesh_lighting(const std::filesystem::path& game_root, std::string_view theme);
} //namespace simcoaster
