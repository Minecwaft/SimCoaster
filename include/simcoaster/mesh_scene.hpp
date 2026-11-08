#pragma once
#include "mesh_material.hpp"
#include "scene_camera.hpp"
#include "world_atmosphere.hpp"
#include <limits>
#include <optional>

namespace simcoaster {
class TerrainTextureAtlas;
struct CoasterTrackMesh;
struct CoasterMeshDefinition;
struct WeatherFrame;

//Homogeneous clip-space geometry shared by sprites and weather. The GPU
//retains W for clipping and perspective-correct texture interpolation.
struct EffectVertex {
    std::array<float, 4> position{};
    RgbaPixel diffuse{};
    std::array<float, 2> uv{};
    //Forward-axis camera-space Z before perspective projection. Arbitrary
    //screen, sky and local-weather W values are not a substitute for this
    //quantity.
    float fog_camera_depth = std::numeric_limits<float>::quiet_NaN();
};

struct EffectDraw {
    std::uint32_t first_index{}, index_count{};
    TextureImagePtr texture;
    MeshMaterial material;
    WorldFogEligibility fog_eligibility = WorldFogEligibility::excluded;
};

struct EffectScene {
    std::vector<EffectVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<EffectDraw> draws;
    //An explicit standalone World pass may carry a snapshot. Public effects
    //rendering never inherits the previous mesh/UI pass's atmosphere.
    std::optional<WorldAtmosphere> atmosphere{};
};

struct SceneVertex {
    MeshPoint position{}, normal{};
    std::array<float, 2> uv{};
    MeshPoint reflection_position{}; //Node-local transform only, before camera view.
    RgbaPixel diffuse{ 255, 255, 255, 255 };
};
//Keep terrain overlays out of the ordinary transparent-object distance sort.
enum class SceneDrawStage : std::uint8_t {
    terrain_surface,
    terrain_sky_shadow,
    world
};

struct SceneDraw {
    std::uint32_t first_index{}, index_count{}, node_flags{}, material_flags{};
    std::size_t source_node{};
    TextureImagePtr texture;
    std::array<float, 3> center{};
    MeshMaterial material;
    TextureImagePtr secondary_texture{};
    bool unlit{};
    //Native generic meshes pass6EF310 before submission. Direct grid fans,
    //screen quads and effect batches use their own emission paths.
    bool cull_back_faces{};
    RgbaPixel diffuse_mask{ 255, 255, 255, 255 };
    WorldFogEligibility fog_eligibility = WorldFogEligibility::native_geometry;
    SceneDrawStage stage = SceneDrawStage::world;
};

struct MeshLightingDiagnostic {
    MeshLightingTransformFailure reason{};
    std::string model, node;
    std::size_t node_index{};
    MeshMatrix transform{};
};

struct MeshScene {
    std::shared_ptr<const WeatherFrame> weather;
    EffectScene sprites;
    std::vector<SceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SceneDraw> draws;
    std::vector<std::string> missing_textures;
    std::vector<MeshLightingDiagnostic> lighting_diagnostics;
    MeshBounds bounds;
    //Absent for generic model diagnostics; an empty World/Map frame still
    //carries this snapshot so its background clear has the native colour.
    std::optional<WorldAtmosphere> atmosphere{};
    void clear();
};

std::vector<std::size_t> scene_draw_order(const MeshScene&, const MeshPoint& eye);
//Keep failed file acquisition diagnosable while submitting the original
//shared NotFound source. A genuinely unbound source remains null.
TextureImagePtr untextured_mesh_texture(); //Neutral GPU sample, not a file fallback.
TextureImagePtr resolve_mesh_texture(MeshScene&, MeshTextureLibrary&, const MeshTextureSlot&);
TextureImagePtr resolve_mesh_texture(MeshScene&, MeshTextureLibrary&, std::string_view);
//0x6E3200 hierarchy/visibility contract: skip emission for8210, prune
//children for20, preserve siblings, compose local*parent. Indices reference
//render corners (UV seams), then position remaps, not raw position numbers.
void append_mesh_scene(MeshScene&,
                       const MeshModel&,
                       MeshTextureLibrary&,
                       const MeshLighting& = {},
                       const TerrainTextureAtlas* = nullptr,
                       std::span<const std::uint8_t> excluded_terrain_cells = {},
                       bool whiten_hover_vertices = false);
//The first library carries the TrackMaster source pair (ride GTexture and
//full Generic Dynamic); diagnostics use the separate generated Dynamic pair.
void append_coaster_track_scene(MeshScene&,
                                const CoasterTrackMesh&,
                                const CoasterMeshDefinition&,
                                MeshTextureLibrary&,
                                const MeshLighting& = {},
                                MeshTextureLibrary* diagnostics = nullptr);
//0x6D0EE0 uses bit2 for render fan order. The height sampler uses bit4;
//these distinct contracts must not be collapsed into one diagonal field.
std::array<std::uint8_t, 4> terrain_cell_uv_selectors(TerrainCellFlags);
MeshPoint terrain_vertex_normal(const TerrainGridView&, std::size_t x, std::size_t y);
MeshBounds terrain_grid_bounds(const TerrainGridView&);
void initialize_terrain_slope_flags(TerrainGridData&);
void append_terrain_scene(MeshScene&,
                          const MeshModel&,
                          MeshTextureLibrary&,
                          const MeshLighting& = {},
                          bool dim_shadow_corners = false,
                          const TerrainTextureAtlas* = nullptr,
                          std::span<const std::uint8_t> excluded_cells = {});
} //namespace simcoaster
