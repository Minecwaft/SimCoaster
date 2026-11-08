#pragma once
#include "sound_library.hpp"
#include "sound_save.hpp"
#include <chrono>

namespace simcoaster {
struct SoundRuntimeSettings {
    //Scale applies to incoming request positions; listener is already in audio
    //world units. Bound from the actual World-to-audio setup by ParkRuntime.
    float request_coordinate_scale = 1.f;
    std::uint32_t voice_capacity = 30;
    bool fading_enabled = true;
};

struct SoundOcclusionVolume {
    std::array<float, 3> position{};
    float radius{}, height{};
};

struct SoundRuntimeStatistics {
    std::size_t targets{}, children{}, voices{}, pool_slots{}, failed_requests{};
};

struct SoundRequestSelection {
    std::uint32_t catalog{}, key{};
};

//Native89D1D0 memory/request order; the save stream writes max_z first.
struct SoundTargetXZBounds {
    std::int32_t min_x{}, max_z{}, max_x{}, min_z{};
};

//Single World-owned native runtime. Request calls stage target changes; the
//unscaled audio update retires targets and drives owned voices/output. Native
//encoded IDs are independent of x64 pointers and retain generation checks.
class SoundRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    SoundRuntime(SoundAssetReader, std::string global_root, SoundRuntimeSettings = {});
    ~SoundRuntime();
    SoundRuntime(const SoundRuntime&) = delete;
    SoundRuntime& operator=(const SoundRuntime&) = delete;
    SoundCatalogLibrary& catalogs();
    const SoundCatalogLibrary& catalogs() const;
    std::uint32_t submit(std::uint32_t previous,
                         std::uint32_t catalog,
                         std::uint32_t key,
                         const std::array<float, 3>& position,
                         float parameter = 0.f);
    //Native requestkind1 owns a finite list of catalog/key pairs. Its target
    //reports kind5 but advances the request list instead of repeating one
    //descriptor. Each referenced catalog remains owned for the whole request.
    std::uint32_t submit_sequence(std::uint32_t previous,
                                  std::span<const SoundRequestSelection>,
                                  const std::array<float, 3>& position,
                                  float parameter = 0.f);
    std::uint32_t operation(std::uint32_t target, std::int32_t operation, std::int32_t value);
    std::uint32_t set_position(std::uint32_t target, const std::array<float, 3>&);
    //Stage the listener-clamped emitter rectangle; range evaluation moves the
    //target during its next update. Missing/stale encoded IDs return false.
    bool set_xz_bounds(std::uint32_t target, const SoundTargetXZBounds&);
    void recycle(std::uint32_t target);
    void fade(std::uint32_t target, std::uint8_t parameter = 60);
    void pause(std::uint32_t target);
    void restart(std::uint32_t target);
    void set_level(std::uint32_t target, std::uint32_t level);
    std::uint32_t sample_time(std::uint32_t target) const;
    void set_listener(const std::array<float, 3>& position, const std::array<float, 3>& forward = { 0, 0, 1 });
    void set_channel_enabled(std::uint32_t channel, bool);
    void set_channel_level(std::uint32_t channel, std::uint32_t percent);
    void set_voice_capacity(std::uint32_t);
    void set_request_coordinate_scale(float);
    void set_fading_enabled(bool);
    void set_music_muted(bool);
    void set_reverb_preset(std::uint32_t);
    std::expected<void, SoundError> load_reverb_presets(std::span<const std::byte>);
    void set_occlusion_enabled(bool, float squared_distance_cutoff);
    std::uint32_t add_occlusion(const SoundOcclusionVolume&);
    void remove_occlusion(std::uint32_t);
    //Device creation is separate from simulation: headless callers still run
    //the complete software timeline without an installed playback endpoint.
    bool enable_output(bool = true);
    std::string output_diagnostic() const;
    void advance(std::chrono::milliseconds elapsed);
    //Explicit software render for an offline consumer. Normal live frames use
    //advance to refill a bounded playback lead using endpoint consumption.
    //Without an endpoint, advance retains the exact elapsed software timeline.
    void render(std::span<float> stereo, std::uint32_t sample_rate = 48000);
    std::expected<SoundBackendSnapshot, SoundError> snapshot() const;
    std::expected<void, SoundError> restore(const SoundBackendSnapshot&);
    SoundRuntimeStatistics statistics() const;
    const std::vector<SoundError>& diagnostics() const;
    void clear_diagnostics();
};
} //namespace simcoaster
