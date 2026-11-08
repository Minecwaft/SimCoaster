#pragma once
#include "mesh_scene.hpp"
#include "world_mesh_runtime.hpp"

namespace simcoaster {
struct ParkConstructionState;

struct WorldSceneStatistics {
    std::size_t live_slots {}, main_meshes {}, hoardings {}, coaster_tracks {};
};

//Conservative eight-corner clip test of each live drawable's current bounds.
//Animation/visibility scheduling remains independent of geometry submission.
bool world_mesh_intersects_view(const MeshModel&, const MeshMatrix& view_projection);

//Renderer-side asset owners outlive all texture-library references. Geometry
//comes from the live mesh pool, including restored poses and native
//placements.
class WorldMeshScene final : public MeshTextureEnvironment {
    struct Sources;
    std::unique_ptr<Sources> sources_;
    std::filesystem::path root_;
    std::string theme_;
    MeshTextureSet texture_set_ = MeshTextureSet::full;
    bool animated_textures_ = true;
    std::optional<MeshModel> terrain_;
    enum class TextureUse {
        model,
        dynamic_overlay,
        coaster_track
    };
    MeshTextureLibrary& textures(const std::filesystem::path&, TextureUse = TextureUse::model);
    void report_lighting(const MeshScene&, std::size_t first, std::string_view asset, const WorldMeshInstance* = nullptr);
    void append_model(MeshScene&, const MeshModel&, MeshTextureLibrary&, const MeshLighting&, std::string_view asset, const WorldMeshInstance* = nullptr, bool whiten = false);

public:
    WorldMeshScene(std::filesystem::path game_root, std::string theme);
    ~WorldMeshScene() override;
    WorldMeshScene(const WorldMeshScene&) = delete;
    WorldMeshScene& operator=(const WorldMeshScene&) = delete;
    //Apply before the first terrain/model texture load. Native quality changes
    //require recreation; a live scene cannot mix cached full/small namespaces.
    void configure_texture_set(MeshTextureSet);

    MeshTextureSet texture_set() const override {
        return texture_set_;
    }

    void configure_animated_textures(bool);

    bool animated_textures() const override {
        return animated_textures_;
    }

    std::span<const std::string> lighting_warnings() const;

    bool ready() const override {
        return terrain() && procedural_textures();
    }

    void load_terrain();
    void configure_procedural_terrain(bool enabled, std::uint32_t dimension);
    void configure_procedural_bounds(TerrainTileBounds);
    bool procedural_terrain_enabled() const;
    void update_cell_texture(std::int32_t x, std::int32_t y, std::uint32_t group, std::uint32_t member, std::uint32_t rotation);
    const TerrainTextureAtlas* procedural_textures() const;
    //467030/4B0100 temporarily use original terrain textures/UV transforms in
    //special camera views. The original metadata snapshot remains scene-owned.
    void set_special_view_terrain(bool);
    bool special_view_terrain() const;
    //Resolve the per-clone resource-state overrides produced by 6E41F0. The
    //returned cell ownership stays with the mesh, rather than a process-global
    //terrain scratch table; failed creation and arbitrary retirement are safe.
    bool prepare_mesh_base(WorldMeshInstance&, const WorldMeshTemplate&, bool force = false) override;
    std::span<const TerrainCellMetadataRecord> original_terrain_cells() const;

    MeshModel* terrain() {
        return terrain_ ? &*terrain_ : nullptr;
    }

    const MeshModel* terrain() const {
        return terrain_ ? &*terrain_ : nullptr;
    }

    WorldSceneStatistics build(WorldRuntime&, WorldMeshRuntime&, MeshScene&, const MeshLighting&, const MeshMatrix* view_projection = nullptr);
    void append_construction_preview(WorldRuntime&, const ParkConstructionState&, MeshScene&, const MeshLighting&, float animation_phase, float camera_heading, std::array<float, 2> coordinate_offset);
};
} //namespace simcoaster
