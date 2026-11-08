#pragma once
#include "world_domains_fwd.hpp"
#include "world_weather.hpp"
#include "mesh_scene.hpp"

namespace simcoaster {
//Renderer-owned objects use native pointers and containers. None of these
//transient particles/textures belong to WeatherRecord's save wire format.
struct WeatherCubic {
    WorldVector3 a {}, b {}, c {}, d {};
};

struct WeatherLightning {
    WorldVector3 ground {}, sky {}, direction {};
    std::int32_t remaining_ms {};
    std::uint32_t seed {};
};

struct WeatherLightningPath {
    std::vector<WorldVector3> points;
    std::uint32_t color = 0xFFFFFFFFu;
    unsigned branch_depth {};
};

struct WeatherSkyObject {
    WorldVector3 position {};
    float size {};
    bool front {};
    TextureImagePtr texture;
};

struct WeatherFrame {
    std::uint32_t sky_layers = 4;
    bool sky_detail = true;
    bool sky_shadow {};
    std::vector<WeatherSkyObject> sky_objects;
    std::vector<WorldVector3> drops;
    WorldVector3 offset {}, drift {};
    std::uint32_t modes {}, camera_flags {}, diffuse_mask = 0xFFFFFFFFu;
    bool flash {};
    std::vector<WeatherLightningPath> bolts;
    TextureImagePtr rain, snow, lightning, sky, cylinder;
    std::array<WorldVector3, 4> sky_offsets {};
    std::array<WorldVector3, 256> sky_vertices {};
    std::array<std::uint32_t, 256> sky_colors {};
    std::vector<std::uint32_t> sky_indices;
    std::array<WorldVector3, 72> cylinder_vertices {};
};

struct WeatherEffectsRuntime {
    std::uint32_t sky_layers = 4;
    bool sky_detail = true, sky_objects_initialized {};
    bool sky_shadow {};
    std::vector<WeatherSkyObject> sky_objects;
    std::array<WorldVector3, 1024> positions {};
    std::array<float, 128> envelope {};
    std::array<float, 1024> phases {}, reciprocal_phase_factors {};
    std::array<WorldVector3, 1024> perturbation_vectors {};
    std::array<WeatherCubic, 128> cubics {};
    std::array<WorldVector3, 4> sky_velocities {}, sky_offsets {};
    WorldVector3 motion_offset {};
    std::vector<WeatherLightning> lightning;
    TextureImagePtr rain, snow, bolt, sky, cylinder;
    std::array<WorldVector3, 256> sky_vertices {};
    std::array<std::uint32_t, 256> sky_colors {};
    std::vector<std::uint32_t> sky_indices;
    std::array<WorldVector3, 72> cylinder_vertices {};
    std::uint32_t last_tick {}, visual_seed = 1;
    std::uint32_t diffuse_mask = 0xFFFFFFFFu;
    bool particles_initialized {};
    std::shared_ptr<const WeatherFrame> frame;
};

std::expected<void, sam::Error> load_weather_balance_checked(WeatherWorld&, std::string_view, bool preserve = false);
bool load_weather_balance(WeatherWorld&, std::string_view, bool preserve = false);
void initialize_weather_particles(WeatherWorld&, WeatherEffectsRuntime&, std::uint32_t tick);
void initialize_weather_sky(WeatherWorld&, WeatherEffectsRuntime&);
void initialize_weather_sky_objects(WeatherWorld&, WeatherEffectsRuntime&);
void release_weather_sky_objects(WeatherEffectsRuntime&);
void update_weather_effects(WeatherWorld&, WeatherEffectsRuntime&, std::uint32_t tick, bool freeze_motion = false, bool turbulent_snow = false);
void enqueue_weather_lightning(WeatherEffectsRuntime&, WorldVector3 ground, WorldVector3 sky, std::int32_t duration, std::uint32_t seed);
//Clip geometry retains native homogeneous W, which ordinary world vertices
//cannot express. D3D clips triangles; the x86 software clipper is unnecessary.
EffectScene build_weather_scene(const WeatherFrame&, const SceneCamera&, float aspect, bool background);
} //namespace simcoaster
