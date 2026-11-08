#pragma once
#include "simcoaster/sound_environment.hpp"
#include "simcoaster/sound_mixer.hpp"
#include "simcoaster/sound_output_win32.hpp"
#include "simcoaster/sound_runtime.hpp"
#include <list>

namespace simcoaster::sound_runtime_detail {
enum class Flag : std::uint32_t {
    playing = 1,
    persistent = 2,
    loop = 4,
    unpositioned = 8,
    recycle = 16,
    keys_initialized = 32,
    suspended = 64,
    parameter_valid = 128,
    retire_voice = 256,
    descriptor_packet = 512,
    fading = 1024,
    out_of_range = 2048,
    has_bounds = 4096,
    clock_captured = 8192,
    channel_suspended = 16384,
    level_override = 32768,
    frequency_override = 65536,
    output_mode4 = 131072
};

struct Target {
    struct RequestEntry {
        std::shared_ptr<const SoundCatalogAsset> catalog;
        const SoundDescriptor* descriptor {};
        std::uint32_t slot {};
    };

    std::shared_ptr<const SoundCatalogAsset> catalog;
    const SoundDescriptor* descriptor {};
    std::uint32_t catalog_slot {}, id {}, flags {}, scheduled_tick {}, interval {}, priority {}, voice {};
    SoundTargetKind kind = SoundTargetKind::single;
    SoundSelectionCursor cursor;
    SoundPlaylistCursor playlist;
    std::array<float, 3> position {};
    float parameter {};
    std::uint16_t level_override {};
    std::uint8_t fade_parameter {};
    std::uint8_t channel_mask {};
    bool paused {};
    std::uint32_t pause_tick {};
    SoundSavedPayload payload;
    SoundVoiceParameters controls;
    Target* parent {};
    std::list<std::unique_ptr<Target>> children;
    //Native kind5 request-list payload is transient: the save format contains
    //only current graph indices and restores through ordinary kind5 selection.
    std::vector<RequestEntry> request_list;
    std::size_t next_request {};

    bool has(Flag flag) const {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    void set(Flag flag, bool value = true) {
        if (value) {
            flags |= static_cast<std::uint32_t>(flag);
        } else {
            flags &= ~static_cast<std::uint32_t>(flag);
        }
    }

    const SoundSelection* selection() const {
        return descriptor && cursor.selection && *cursor.selection < descriptor->selections.size() ? &descriptor->selections[*cursor.selection] : nullptr;
    }

    const sound_wire::Sample* sample() const {
        const auto* row = selection();
        return row && cursor.sample && *cursor.sample < row->parameters.sample_count && *cursor.sample < row->samples.size() ? &row->samples[*cursor.sample] : nullptr;
    }

    bool sequence() const {
        return kind == SoundTargetKind::conditional_sequence || kind == SoundTargetKind::sequential || kind == SoundTargetKind::weighted_sequence || kind == SoundTargetKind::playlist;
    }

    bool emitter() const {
        return kind == SoundTargetKind::emitter || kind == SoundTargetKind::conditional_emitter;
    }
};

struct Slot {
    std::uint32_t id {};
    std::unique_ptr<Target> target;
};

struct QueuedRequest {
    std::shared_ptr<const SoundCatalogAsset> catalog;
    const SoundDescriptor* descriptor {};
    std::uint32_t catalog_slot {};
    std::array<float, 3> position {};
    float parameter {};
};

struct VoiceOwner {
    Target* target {};
    std::uint32_t priority {};
};
} //namespace simcoaster::sound_runtime_detail

namespace simcoaster {
struct SoundRuntime::State {
    using Target = sound_runtime_detail::Target;
    using Flag = sound_runtime_detail::Flag;
    SoundCatalogLibrary library;
    SoundVoiceMixer mixer;
    SoundEnvironment environment;
    std::unique_ptr<SoundOutputWin32> output;
    SoundRuntimeSettings settings;
    std::vector<sound_runtime_detail::Slot> slots;
    std::vector<std::uint16_t> free_slots, active;
    std::vector<sound_runtime_detail::QueuedRequest> queued;
    std::map<std::uint32_t, sound_runtime_detail::VoiceOwner> voices;
    std::vector<SoundError> errors;
    std::vector<float> wet_buffer;
    std::array<bool, 4> channel_enabled { true, true, true, true };
    std::array<std::uint32_t, 4> channel_level { 100, 100, 100, 100 };
    std::uint32_t generation {}, random {}, now {};
    double fractional_ms {};
    std::optional<std::uint32_t> last_update;
    bool force_update = true, music_muted {};
    bool occlusion_enabled {};
    State(SoundAssetReader reader, std::string global_root, SoundRuntimeSettings config);
    Target* find(std::uint32_t);
    const Target* find(std::uint32_t) const;
    bool grow_pool();
    std::uint32_t create(const sound_runtime_detail::QueuedRequest&, std::vector<Target::RequestEntry> requests = {});
    void retire(std::uint16_t);
    void configure(Target&);
    bool advance_selection(Target&, bool avoid_repeat);
    bool select_sample(Target&, bool avoid_repeat);
    bool bind(Target&);
    bool spawn_child(Target&);
    SoundSelectionCursor value_cursor(const Target&) const;
    float radius(const Target&) const;
    bool channels_allow(std::uint8_t) const;
    bool in_range(Target&);
    bool expired(const Target&) const;
    bool start_voice(Target&, bool continuation = false);
    void update_controls(Target&, bool level = true);
    void stop_voice(Target&, bool detach = false);
    void stop_children(Target&, bool detach = false);
    void collect_completions();
    void update_targets();
    void process_emitter(Target&);
    void render(std::span<float>, std::uint32_t rate);

    void error(SoundError e) {
        errors.push_back(std::move(e));
    }

    void update_channel(std::uint32_t);
    void refresh_environment();
    SoundSavedTarget save_node(const Target&) const;
    std::expected<std::unique_ptr<Target>, SoundError> restore_node(const SoundSavedTarget&, const std::vector<std::uint32_t>&, Target* parent);
};
} //namespace simcoaster
