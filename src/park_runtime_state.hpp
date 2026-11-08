#pragma once
#include "park_runtime_bindings.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/application_coaster_source.hpp"
#include "simcoaster/application_frontend.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/campaign_profile.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/layout_world_runtime.hpp"
#include "simcoaster/mesh_surface.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/park_authoring.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/park_construction_refresh.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/particle_runtime.hpp"
#include "simcoaster/resource_key.hpp"
#include "simcoaster/resource_variant_io.hpp"
#include "simcoaster/rsse_people.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/rsse_save.hpp"
#include "simcoaster/rsse_tour_runtime.hpp"
#include "simcoaster/save_container.hpp"
#include "simcoaster/scape_runtime.hpp"
#include "simcoaster/sprite_catalog.hpp"
#include "simcoaster/terrain_sky_shadow.hpp"
#include "simcoaster/terrain_texture.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/weather_runtime.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_calendar_ui.hpp"
#include "simcoaster/world_camera_render.hpp"
#include "simcoaster/world_coaster_guest_restore.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_cursor_pick.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_graphics_profile.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_particle_scene.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_scene.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include "simcoaster/world_source_startup.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_staff_view.hpp"
#include "simcoaster/world_startup.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "world_research_balance.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>

namespace simcoaster {
namespace park_detail {
struct SoundSnapshot {
    std::uint32_t backend_present {}, feature_flags {};
    std::vector<std::byte> global_state, backend_state;
};

struct RuntimeContent {
    LoadedObjectCatalog catalog;
    LoadedSpriteCatalog sprites;
    std::optional<DwfbArchive> sprite_texture_archive;
    bool sprite_f_family = false;
};

struct RuntimeRendering {
    explicit RuntimeRendering(const std::filesystem::path& root, const std::string& theme) : scene(root, theme) {}

    WorldMeshRuntime meshes;
    WorldMeshScene scene;
    TerrainSkyShadowCache terrain_sky_shadow;
    WorldSpriteScene sprite_scene;
    WorldParticleScene particle_scene;
    WorldMobilePicker mobile_picker;
    TerrainGridView terrain;
    ActiveTerrainModelDataView active { &terrain };
    MeshLighting lighting;
    WorldAtmosphereColors atmosphere_colors;
    SpriteShadowQuality sprite_shadow_quality { SpriteShadowQuality::off };
};

struct RuntimeTools {
    ParkConstructionRuntime construction;
    NativeCoasterEditor coaster_editor { construction };
    CoasterCatalogRuntime coaster_catalog { construction };
    LayoutWorldBackend layout_backend { construction, &coaster_editor };
    LayoutReplayRuntime layout_replay;
};

struct RuntimeSimulation {
    explicit RuntimeSimulation(ParkRuntimeBindings& bindings) : world(std::make_unique<WorldRuntime>(bindings, EntityOwnership::store)) {}

    std::unique_ptr<WorldRuntime> world;
    std::unique_ptr<ParticleRuntime> particles = std::make_unique<ParticleRuntime>();
    TrackRideManager rides;
    std::unique_ptr<WeatherEffectsRuntime> weather = std::make_unique<WeatherEffectsRuntime>();
    RestoredClockState clocks;
    SoundSnapshot sound;
};

struct RuntimePresentation {
    std::unique_ptr<WorldSoundRuntime> audio;
    std::unique_ptr<ScapeOmpRuntime> scape;
    std::optional<ApplicationLobbyView> lobby_view;
    std::optional<MeshLighting> lobby_lighting;
    std::optional<ApplicationCoasterSourceSelection> pending_coaster_source;
    std::unique_ptr<WorldUiRuntime> ui;
    std::unique_ptr<ParkAuthoringController> authoring;
    std::optional<SceneCamera> audio_camera;
    WorldCameraInputRuntime camera_input_runtime;
    WorldCameraInput camera_input;
    WorldCameraInputFeedback camera_input_result;
};

struct RuntimeFrame {
    ParkRuntimeStatistics statistics;
    FrameScheduler scheduler;
    bool mobile_poses_ready {};
    bool application_ui_paused {};
    bool frame_external_paused {};
    std::optional<float> native_camera_zoom;
    float preview_phase {}, preview_camera_heading {};
    std::array<float, 2> preview_reference {};
    std::uint32_t preview_blink_counter {};
    std::vector<StaffInteractionOverlay> runtime_overlays;
};

struct RuntimeConfiguration {
    ParkSaveHeader header;
    UiPreferencesLoad preferences;
    ApplicationHardwareProfile hardware_profile;
    ParticleLibrarySettings particle_library;
    WorldSourceStartupBalance source_startup;
};
} //namespace park_detail

struct ParkRuntime::State {
    ParkRuntimeBindings bindings;
    park_detail::RuntimeContent content;
    park_detail::RuntimeRendering rendering;
    park_detail::RuntimeTools tools;
    park_detail::RuntimeSimulation simulation;
    park_detail::RuntimePresentation presentation;
    park_detail::RuntimeFrame frame;
    park_detail::RuntimeConfiguration configuration;
    SceneCamera render_camera();
    void publish_render_camera(bool paused);
    State(const std::filesystem::path& root, const std::string& theme, bool coaster_designer, std::function<void()> pump, std::optional<ApplicationHardwareProfile> requested_profile);
    void apply_hardware_profile(const ApplicationHardwareProfile& profile);
    ~State();
    void initialize_assets(const std::filesystem::path& root, const std::string& theme);
    void advance(std::chrono::milliseconds elapsed, bool full_world, bool paused);
    void build_scene_geometry(MeshScene&, const MeshMatrix* view_projection = nullptr);
    void prepare_world_poses(float interpolation);
    void open(ParkRuntime&, const std::filesystem::path&, ParkRuntimeOpen&);
};
} //namespace simcoaster
