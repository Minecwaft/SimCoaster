#pragma once
#include "world_domains_fwd.hpp"
#include "mesh_scene.hpp"
#include "sprite_geometry.hpp"
#include "sprite_projection.hpp"
#include "world_staff_interaction.hpp"

namespace simcoaster {
std::optional<StaffInteractionCursor> pick_staff_interaction_cursor(const SceneCamera&,
                                                                    const TerrainGridView&,
                                                                    float pixel_x,
                                                                    float pixel_y,
                                                                    std::uint32_t width,
                                                                    std::uint32_t height);
ThingId find_staff_at_interaction_cursor(StaffWorld&);
void append_staff_interaction_overlays(StaffWorld&, MeshScene&);
void append_world_runtime_overlays(StaffWorld&, MeshScene&, std::span<const StaffInteractionOverlay>);

struct WorldSpriteSceneSettings {
    SpriteShadowQuality shadow_quality{ SpriteShadowQuality::off };
    std::span<const MeshLight> lights;
    //Native6DC720 uses .60000002 normally and .25 in special camera views.
    //An explicit override is reserved for a proven alternative World pass.
    std::optional<float> height_bias;
};

class WorldSpriteScene {
    SpritePixelCatalog pixels_;
    ResourceSpritePass pass_;
    std::map<std::pair<std::size_t, std::size_t>, TextureImagePtr> textures_;
    std::filesystem::path shadow_data_directory_;
    std::unique_ptr<MeshTextureLibrary> shadow_library_;
    TextureImagePtr shadow_texture_;
    bool shadow_texture_attempted_{};
    std::vector<TextureError> shadow_texture_diagnostics_;
    std::vector<SpriteProjectionError> shadow_geometry_diagnostics_;
    std::size_t legacy_height_bias_fallback_count_{};
    void load_shadow_texture(const std::filesystem::path& data_directory);

public:
    void append(StaffWorld&,
                const SceneCamera&,
                float aspect,
                MeshScene&,
                bool prepare_staff_poses = true,
                const WorldSpriteSceneSettings& settings = {});

    const auto& shadow_texture_diagnostics() const {
        return shadow_texture_diagnostics_;
    }

    const auto& shadow_geometry_diagnostics() const {
        return shadow_geometry_diagnostics_;
    }

    std::size_t legacy_height_bias_fallback_count() const {
        return legacy_height_bias_fallback_count_;
    }
};
} //namespace simcoaster
