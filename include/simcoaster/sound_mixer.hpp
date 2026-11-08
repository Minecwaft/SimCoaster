#pragma once
#include "sound_catalog.hpp"

namespace simcoaster {
struct SoundVoiceParameters {
    std::array<float, 3> position{};
    bool positioned{};
    float level = 100.0f;
    float frequency_ratio = 1.0f;
    std::uint8_t channel_mask = 1;
    //Total passes, including the first. UINT32_MAX repeats without a limit.
    std::uint32_t loop_count = 1;
    std::uint64_t loop_begin_frame{}, loop_end_frame{}; //End0 means sample end.
    std::uint32_t seek_ms{}, silence_ms{};
    float min_distance = 2.0f, max_distance = 2.1f;
    //Portable spectral occlusion. DC/low band remains unchanged; this scalar
    //attenuates the high band rather than multiplying broadband voice level.
    float high_frequency_gain = 1.0f, high_frequency_crossover_hz = 1500.0f;
    //Unset selects portable routing: positioned sources send1, 2D sources0.
    //The original vendor driver owns the unproven2D default; an explicit
    //linear0..1 send overrides this policy for either source kind.
    std::optional<float> reverb_send = std::nullopt;
};

struct SoundListener {
    std::array<float, 3> position{};
    std::array<float, 3> forward{ 0, 0, 1 }, up{ 0, 1, 0 };
};

struct SoundVoiceStatus {
    SoundVoiceParameters parameters;
    double source_frame{};
    std::uint64_t rendered_frames{};
    std::uint32_t remaining_loops{};
    bool paused{}, trailing_silence{};
};

//Single-owner software mixer. The World thread owns all voice mutations and
//rendering; the Windows output thread only consumes completed float blocks.
//Keys contain a slot and a generation, so retiring/reusing a voice cannot
//redirect an old callback to the replacement.
class SoundVoiceMixer {
    struct State;
    std::unique_ptr<State> state_;

public:
    SoundVoiceMixer();
    ~SoundVoiceMixer();
    SoundVoiceMixer(const SoundVoiceMixer&) = delete;
    SoundVoiceMixer& operator=(const SoundVoiceMixer&) = delete;
    std::uint32_t start(std::shared_ptr<const SoundPcmSample>, const SoundVoiceParameters&);
    bool contains(std::uint32_t key) const;
    std::optional<SoundVoiceStatus> status(std::uint32_t key) const;
    bool stop(std::uint32_t key);
    bool pause(std::uint32_t key);
    bool restart(std::uint32_t key); //Resume: restart is not a rewind operation.
    bool seek(std::uint32_t key, std::uint32_t milliseconds);
    //Replaces controls without implicitly applying seek_ms again. A changed
    //loop configuration starts a new repeat budget at the current source cursor.
    bool update(std::uint32_t key, const SoundVoiceParameters&);
    void stop_all();
    std::size_t active_voices() const;
    //Minimum render frames to the next finite, unpaused voice completion.
    //Zero means a seek left an already-ended voice; render an empty span to
    //collect it without advancing time. Null means no finite active deadline.
    std::optional<std::uint64_t> frames_until_completion(std::uint32_t sample_rate = 48000) const;
    void set_listener(const SoundListener&);
    SoundListener listener() const;
    void set_master_level(float percent);
    void set_channel(std::size_t index, float percent, bool enabled = true);
    //Recovered EAX room-rolloff scalar; only the independent wet bus uses it.
    //Its inverse-distance realization is portable presentation policy.
    void set_reverb_room_rolloff(float);
    //Stereo interleaved output. Native channel levels selected by mask multiply
    //together. Inaudible voices still advance; paused voices do not.
    void render(std::span<float> stereo, std::uint32_t sample_rate = 48000);
    //Disjoint stereo buses of equal size. Dry keeps its existing attenuation
    //and unit clipping. Send remains unclipped for the environment DSP and has
    //independent room-distance falloff. The optional send span may be empty.
    void render(std::span<float> dry, std::span<float> reverb_send, std::uint32_t sample_rate = 48000);
};
} //namespace simcoaster
