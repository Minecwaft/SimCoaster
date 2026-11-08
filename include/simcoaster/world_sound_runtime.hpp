#pragma once
#include "sam_record.hpp"
#include "sound_runtime.hpp"
#include <filesystem>
#include <unordered_map>

namespace simcoaster {
struct WorldRuntime;
struct SceneCamera;
struct ObjectDefinitionAsset;

struct RsseSoundOcclusionRegion {
    std::uint32_t handle {};
};

struct SoundInfoBalance {
    [[=sam::Field { "QSOUND", 0, 101 }]] std::int32_t qsound { 0 };
    [[=sam::Field { "REAR", 0, 101 }]] std::int32_t rear { 0 };
    [[=sam::Field { "CROSSOVER", 0, 101 }]] std::int32_t crossover { 0 };
    [[=sam::Field { "EAX", 0, 101 }]] std::int32_t eax { 0 };
    [[=sam::Field { "GEOMETRY", 0, 101 }]] std::int32_t geometry { 0 };
    [[=sam::Field { "MPEGRATE", 0, 101 }]] std::int32_t mpeg_rate { 0 };
    [[=sam::Field { "DUCKINGLEVEL", 0, 101 }]] std::int32_t ducking { 0 };
    [[=sam::Field { "DROPPABLELEVEL", 0, 101 }]] std::int32_t droppable { 0 };
    [[=sam::Field { "DEFAULTQUALITY", 0, 101 }]] std::int32_t default_quality { 0 };
    [[=sam::Field { "FADETOGGLE", 0, 101 }]] std::int32_t fade_toggle { 0 };
    [[=sam::Field { "SOUNDON", 0, 2 }]] std::int32_t sound_on { 0 };
};

struct SoundChannelBalance {
    [[=sam::Field { "CHANNELS", 5, 14 }]] std::int32_t channels { 5 };
    [[=sam::Field { "SWITCH", 0, 101 }]] std::int32_t threshold { 0 };
};

struct SoundRadiusBalance {
    [[=sam::Field { "MINRADIUS", 0.0f, 101.0f }]] float minimum_radius {};
    [[=sam::Field { "SWITCH", 0, 101 }]] std::int32_t threshold { 0 };
};

struct SoundVolumeBalance {
    [[=sam::Field { "SFX", 0, 101 }]] std::int32_t sfx { 0 };
    [[=sam::Field { "MUSIC", 0, 101 }]] std::int32_t music { 0 };
    [[=sam::Field { "SPEECH", 0, 101 }]] std::int32_t speech { 0 };
    [[=sam::Field { "MOVIE", 0, 101 }]] std::int32_t movie { 0 };
};

struct WorldSoundBalance {
    [[=sam::Group { "SoundInfo" }]] SoundInfoBalance info;
    [[=sam::Rows { "ChannelInfo" }]] std::array<SoundChannelBalance, 3> channels {};
    [[=sam::Extent { "ChannelInfo" }]] std::uint32_t channel_extent {};
    [[=sam::Rows { "RadiusInfo" }]] std::array<SoundRadiusBalance, 3> radii {};
    [[=sam::Extent { "RadiusInfo" }]] std::uint32_t radius_extent {};
    [[=sam::Group { "DefaultVolume" }]] SoundVolumeBalance volumes;
};

//Original quality curves with explicit diagnostics for unusable branches.
std::int32_t sound_quality_voice_capacity(const WorldSoundBalance&, std::int32_t, std::string_view file);
std::pair<bool, float> sound_quality_geometry(const WorldSoundBalance&, std::int32_t, bool special_view, std::string_view file);

//Exact84-byte SndDat block atAE0198. The final four DWORDs have no direct
//native xrefs and remain opaque. The two override WORDs share one DWORD.
struct WorldSoundSavedGlobals {
    [[=save::Field { "music_request_handle", 0 }]] std::uint32_t music_handle {};
    [[=save::Field { "kids_sound_request_handle", 1 }]] std::uint32_t kids_handle {};
    [[=save::Field { "kids_sound_request_enabled_value", 2 }]] std::uint32_t kids_enabled = 1;
    [[=save::Field { "rain_intensity_sound_handle", 3 }]] std::uint32_t rain_handle {};
    [[=save::Field { "kids_sound_parameter_cache", 4 }]] std::int32_t kids_parameter {};
    [[=save::Field { "requested_reverb_preset", 5 }]] std::int32_t reverb {};
    [[=save::Field { "channel0_volume_percent", 6 }]] std::int32_t channel0_volume = 100;
    [[=save::Field { "pending_sfx_volume_percent", 7 }]] std::int32_t sfx_volume {};
    [[=save::Field { "pending_music_volume_percent", 8 }]] std::int32_t music_volume {};
    [[=save::Field { "pending_speech_volume_percent", 9 }]] std::int32_t speech_volume {};
    [[=save::Field { "sound_channel_settings_dirty", 10 }]] std::uint32_t dirty = 1;
    [[=save::Field { "pending_speech_ducking_active", 11 }]] std::uint32_t speech_ducking {};
    [[=save::Field { "pending_music_mute_override", 12 }]] std::uint16_t music_mute {};
    [[=save::Field { "subsound_pause_override", 13 }]] std::uint16_t pause_override {};
    [[=save::Field { "opaque_AE01CC", 14 }]] std::uint32_t opaque_AE01CC {};
    [[=save::Field { "pending_sfx_channel_enabled", 15 }]] std::uint32_t sfx_enabled = 1;
    [[=save::Field { "pending_music_channel_enabled", 16 }]] std::uint32_t music_enabled = 1;
    [[=save::Field { "pending_speech_channel_enabled", 17 }]] std::uint32_t speech_enabled = 1;
    [[=save::Field { "opaque_AE01DC_to_AE01E8", 18 }]] std::array<std::uint32_t, 4> opaque_tail {};
};

static_assert(save::value_width<WorldSoundSavedGlobals> == 84);

//World-facing owner for native catalog slots, global channels, music/crowd
//requests and RSSE geometry. The target graph/voices remain in SoundRuntime.
class WorldSoundRuntime {
    WorldRuntime* context_ {};
    SoundAssetReader reader_;
    std::filesystem::path data_root_;
    std::string source_;
    WorldSoundBalance balance_;
    WorldSoundSavedGlobals globals_;
    std::unique_ptr<SoundRuntime> runtime_;
    std::uint32_t music_catalog_ {};
    std::int32_t world_reverb_ {};
    std::int32_t quality_ {};
    bool lobby_mode_ {};
    std::vector<SoundError> diagnostics_;
    std::unordered_map<RsseSoundOcclusionRegion*, std::unique_ptr<RsseSoundOcclusionRegion>> regions_;
    std::uint32_t load_catalog(const std::filesystem::path&, std::string_view, bool optional = false);
    void apply_channels();
    void update_music_and_kids();
    void update_reverb();
    void apply_geometry();

public:
    WorldSoundRuntime(WorldRuntime&, SoundAssetReader, const std::filesystem::path& data_root, std::string source);
    ~WorldSoundRuntime();

    SoundRuntime& runtime() {
        return *runtime_;
    }

    const SoundRuntime& runtime() const {
        return *runtime_;
    }

    void load_object_catalog(ObjectDefinitionAsset&);
    void configure_world_balance(std::string_view text, bool preserve = true);
    void set_quality(std::int32_t quality);
    void set_channel_level(std::uint32_t channel, std::int32_t percent);
    void set_channel_enabled(std::uint32_t channel, bool);
    void set_inspected_mesh(std::uint32_t slot);
    void set_camera(const SceneCamera&, bool paused = false);
    void stage_music_mute(std::uint16_t);
    void set_lobby_mode(bool entering, bool intro);
    void set_music_level(std::int32_t);
    //649E60: select music level0; disable/recycle the Kids request only.
    void begin_failure_transition();
    RsseSoundOcclusionRegion* add_occlusion(float x, float unused_y, float z, float radius, float height);
    void remove_occlusion(RsseSoundOcclusionRegion*);
    void world_step();
    //Native channel staging precedes Advisor speech playback in the frame.
    //Render advancement follows the frame's World/RSSE producers.
    void prepare_frame(bool paused);
    void advance(std::chrono::milliseconds elapsed);
    void restore_globals(std::span<const std::byte>);
    std::array<std::byte, 84> capture_globals();
    void restore_backend(std::span<const std::byte>);
    std::vector<std::byte> capture_backend() const;
    //Backendless saves need fresh targets; a restored target graph must keep
    //its exact encoded IDs because World/RSSE records already reference them.
    void finish_restore(bool restored_backend);
    std::vector<SoundError> diagnostics() const;
};
} //namespace simcoaster
