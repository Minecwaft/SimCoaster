#pragma once
#include "application_configuration.hpp"
#include "layout_replay.hpp"
#include "mesh_scene.hpp"
#include "sound_catalog.hpp"
#include "ui_preferences.hpp"
#include "world_camera_input.hpp"
#include "world_mobile_picker.hpp"
#include <chrono>
#include <filesystem>

namespace simcoaster {
enum class ParkScenePass : std::uint8_t {
    world,
    park_map
};
struct WorldRuntime;
struct ParticleRuntime;
class WorldMeshRuntime;
class WorldMeshScene;
class ParkConstructionRuntime;
class ParkAuthoringController;
class NativeCoasterEditor;
class CoasterCatalogRuntime;
class SoundRuntime;
class WorldUiRuntime;
struct CampaignProfile;
struct CampaignAuxiliaryState;
struct CampaignAmbientRecord;
struct ApplicationLobbyView;
struct ApplicationCoasterSourceSelection;

struct ParkRuntimeOpen {
    std::string theme;
    std::optional<std::filesystem::path> saved_park;
    //Borrowed during construction only; the new WorldRuntime owns its copied state.
    const CampaignProfile* campaign {};
    bool coaster_designer {};
    const ApplicationCoasterSourceSelection* coaster_source {};
    std::function<void()> resource_pump {};
    std::optional<ApplicationHardwareProfile> hardware_profile {};
};
enum class CoasterGeometryPointerPhase;

struct ParkRuntimeStatistics {
    std::size_t things {}, mesh_instances {}, scripts {}, event_subscriptions {}, coaster_records {};
    std::size_t restored_bytes {};
    std::size_t train_slots {}, car_slots {}, passenger_references {};
};

//Production owners for a restored park. Assets, script sources and terrain
//remain alive with the World; mesh slots contain real model clones.
class ParkRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    ParkRuntime(const std::filesystem::path& root, const std::filesystem::path& save, std::string theme);
    ParkRuntime(const std::filesystem::path& root, ParkRuntimeOpen);
    ~ParkRuntime();
    ParkRuntime(const ParkRuntime&) = delete;
    ParkRuntime& operator=(const ParkRuntime&) = delete;
    WorldRuntime& context();
    WorldMeshRuntime& meshes();
    ParticleRuntime& particles();
    WorldMeshScene& world_scene();
    ParkConstructionRuntime& construction();
    ParkAuthoringController& authoring();
    NativeCoasterEditor& coaster_editor();
    CoasterCatalogRuntime& coaster_catalog();
    LayoutReplayRuntime& action_recording();
    SoundRuntime& audio();
    WorldUiRuntime& ui();
    bool ui_commands_enabled() const;
    const UiPreferences& preferences() const;
    void apply_preferences(const UiPreferences&, bool apply_clock = true);
    void set_game_speed_percent(std::int16_t);
    const ApplicationHardwareProfile& hardware_profile() const;
    void apply_hardware_profile(const ApplicationHardwareProfile&);
    void set_application_ui_paused(bool);
    CampaignProfile capture_campaign(CampaignAuxiliaryState) const;
    void apply_campaign_for_lobby(const CampaignProfile&);
    std::vector<CampaignAmbientRecord> remap_campaign_ambient(std::span<const CampaignAmbientRecord>) const;
    void set_lobby_audio(bool entering, bool intro);
    void begin_failure_audio_transition();
    void set_debug_music_level(std::int32_t);
    void set_debug_lighting(bool raised);
    //5070A0 Escape: one inspection layer, retaining a previous POI view.
    void exit_special_view_from_escape();
    //The view is copied immediately; nullptr restores ordinary World lighting
    //and the common World/POI/ride camera resolver.
    void set_lobby_view(const ApplicationLobbyView*);
    void stage_coaster_source(const ApplicationCoasterSourceSelection&);
    bool activate_pending_coaster_source();
    std::optional<WorldMobilePick> pick_mobile(std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height);
    bool focus_staff_view(std::uint16_t staff);
    bool focus_person_view(std::uint16_t person, bool follow = false);
    bool focus_mobile_view(std::uint16_t person_or_staff, bool interpolate);
    std::uint16_t pick_ui_world_cursor();
    bool coaster_geometry_pointer(CoasterGeometryPointerPhase, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height);
    std::vector<SoundError> audio_diagnostics() const;
    //Camera inspection owns this slot;0 selects the normal World preset.
    void set_audio_inspected_mesh(std::uint32_t);
    void set_audio_channel_level(std::uint32_t, std::int32_t percent);
    void set_audio_channel_enabled(std::uint32_t, bool);
    void set_audio_quality(std::int32_t percent);
    //Applies the recording to the current World, as41D800 does. Callers must
    //supply the intended source World; this never silently destroys a park.
    void replay_layout(LayoutReplayTiming = LayoutReplayTiming::immediate);
    void cancel_layout_replay();
    const ParkRuntimeStatistics& statistics() const;
    //Capture the live native-format park, including staff, resources, script
    //state and subscriptions. Serialization finishes before the file opens.
    void save(const std::filesystem::path&);
    void build_scene(MeshScene&);
    void build_scene(MeshScene&, const SceneCamera&, float aspect);
    //Empty World surfaces (such as the park selector) retain the current
    //theme's clear colour without rebuilding geometry or ticking simulation.
    void prepare_scene_atmosphere(MeshScene&, const SceneCamera&, ParkScenePass = ParkScenePass::world) const;
    //Authored screen effects have native depth zero and follow the UI pass.
    void build_screen_particles(EffectScene&, float aspect);
    void advance_coasters(std::chrono::milliseconds elapsed);
    //Feed normal focus/heading/zoom before advance_frame. The original World
    //transform derives normal pitch/terrain/bank; special views own their pose.
    //Mobile umbrella offsets and audio use the resolved eye, not this input.
    void set_render_camera(const SceneCamera&);
    //Render, ray picks and the audio listener share this resolved view. Reading
    //it never advances inspection attachment history or per-frame smoothing.
    SceneCamera render_camera();
    void set_camera_input(const WorldCameraInput&);
    WorldCameraInputFeedback camera_input_feedback() const;
    //Actual UI camera actions commit zoom for the native save section. Other
    //presentation callers can change it without replacing the saved value.
    void commit_ui_camera();
    //Complete live-park frame: elapsed is wall time, pause freezes simulation
    //time while unscaled animation continues. World/ride/resource phases share
    //the original fixed-step cursor and bounded catch-up budget.
    void advance_frame(std::chrono::milliseconds elapsed, bool paused = false);
    //Runs every restored RSSE state, resource-script cadence and the coaster/
    //attachment frame consumers, particles and TOUR. Leaves World ticking to
    //diagnostic callers; normal gameplay uses advance_frame.
    void advance_scripted_rides(std::chrono::milliseconds elapsed);
};
} //namespace simcoaster
