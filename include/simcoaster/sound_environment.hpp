#pragma once
#include "sound_catalog.hpp"

namespace simcoaster {
//52-byte game record, reordered by8AC920 when sending EAX2 listener data.
//These are actual SndReverb.map parameters, not EAX built-in preset IDs.
struct SoundReverbParameters {
    [[=save::Field { "dwEnvironment", 0 }]] std::uint32_t environment {};
    [[=save::Field { "flEnvironmentSize", 1 }]] float size {};
    [[=save::Field { "flEnvironmentDiffusion", 2 }]] float diffusion {};
    [[=save::Field { "lRoom", 3 }]] std::int32_t room_millibels {};
    [[=save::Field { "lRoomHF", 4 }]] std::int32_t room_hf_millibels {};
    [[=save::Field { "flDecayTime", 5 }]] float decay_seconds {};
    [[=save::Field { "flDecayHFRatio", 6 }]] float decay_hf_ratio {};
    [[=save::Field { "lReflections", 7 }]] std::int32_t reflections_millibels {};
    [[=save::Field { "flReflectionsDelay", 8 }]] float reflections_delay_seconds {};
    [[=save::Field { "lReverb", 9 }]] std::int32_t reverb_millibels {};
    [[=save::Field { "flReverbDelay", 10 }]] float reverb_delay_seconds {};
    [[=save::Field { "flRoomRolloffFactor", 11 }]] float room_rolloff {};
    [[=save::Field { "flAirAbsorptionHF", 12 }]] float air_absorption_hf {};
};

static_assert(save::value_width<SoundReverbParameters> == 52);

struct SoundEnvironmentRegion {
    float x {}, z {};
    std::uint32_t opaque_value {}; //RSSE passes raw height bits; query ignores it.
    float radius {};
};

struct SoundOcclusionParameters {
    float scalar {}, eq = 1.f;
    std::int32_t obstruction_millibels {}, occlusion_millibels {};
    float occlusion_lf_ratio = .25f, occlusion_room_ratio = .5f;
    float air_absorption_factor = 1.f;
};

//Original reverb is performed by the EAX/A3D driver. This owned software
//implementation preserves its data and uses a deterministic eight-delay
//feedback network; it does not claim to reproduce a proprietary driver DSP.
//Occlusion returns the recovered controls, without inventing broadband gain.
class SoundEnvironment {
    struct State;
    std::unique_ptr<State> state_;

public:
    SoundEnvironment();
    ~SoundEnvironment();
    SoundEnvironment(const SoundEnvironment&) = delete;
    SoundEnvironment& operator=(const SoundEnvironment&) = delete;
    //Version2000: countDWORD followed by presenceBYTE + optional52-byte row.
    std::expected<void, SoundError> load_presets(std::span<const std::byte>);
    const SoundReverbParameters* preset(std::uint32_t) const;
    std::optional<std::uint32_t> active_preset() const;
    bool set_reverb_preset(std::uint32_t);
    float room_rolloff() const;
    void reset_reverb();
    void process(std::span<float> stereo, std::uint32_t sample_rate);
    //Separate send allows source-side room rolloff without changing dry level.
    void process(std::span<float> dry, std::span<const float> reverb_send, std::uint32_t sample_rate);
    std::uint32_t add_occlusion(const SoundEnvironmentRegion&);
    std::uint32_t add_occlusion(const std::array<float, 3>& position, float radius, float height);
    void remove_occlusion(std::uint32_t);
    void clear_occlusion();
    void set_occlusion_enabled(bool, float radius_cutoff = 0.f);
    SoundOcclusionParameters occlusion(const std::array<float, 3>& listener, const std::array<float, 3>& source) const;
};
} //namespace simcoaster
