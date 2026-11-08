#pragma once
#include "mesh_animation.hpp"
#include "frame_scheduler.hpp"
#include "mesh_restore.hpp"
#include <chrono>
#include <memory>

namespace simcoaster {
using MeshMilliseconds = std::chrono::milliseconds;
using MeshPlaybackClocks = FrameClocks;

struct MeshClipSelection {
    std::uint32_t category { 12 }, variant {};

    bool active() const {
        return category < 12;
    }
};
enum class MeshPlaybackControlFlag : std::uint32_t {
    loop = 1,
    hold_first = 2,
    hold_last = 4,
    completed_primary = 8,
    needs_pose = 16,
    preserve_channels = 32,
    unscaled_clock = 64,
};
enum class MeshPlaybackTransitionFlag : std::uint32_t {
    loop = 1,
    force = 2,
    preserve_channels = 4,
    suppress_binding = 8,
};
enum class MeshPlaybackEvent : std::uint32_t {
    registered = 1,
    unregistered = 2,
    renderer_invalidated = 4
};
enum class MeshInstanceFlag : std::uint32_t {
    template_definition = 1,
    custom_sign_caption = 0x8000000
};
inline constexpr std::uint32_t mesh_request_clear = 12, mesh_request_hold_first = 13, mesh_request_hold_last = 14;

struct MeshPlaybackRequest {
    MeshClipSelection clip;
    std::uint32_t flags {};
    float rate { 1 };
};

struct MeshPlaybackControl {
    std::uint32_t flags {};
    MeshClipSelection clip;
    float rate { 1 }, total_frames {}, frame {};
    MeshMilliseconds start {}, current {}, unpaused {};
    MeshPlaybackRequest deferred { { 12, 0 }, 0, 0 };
    const MeshAnimation* previous_clip {};
};

struct MeshPlaybackAssets {
    MeshModel model;
    MeshAnimationLibrary animations;
};

std::expected<MeshPlaybackAssets, MeshError> read_mesh_playback_assets(const MeshModelLibrary&, std::string_view filename, bool retain_relative_animation = false);
//retain_relative_animation corresponds to the model loader's option 0x20000.
//Ordinary templates strip an incorrectly exported relative-animation flag.
std::expected<std::shared_ptr<const MeshPlaybackAssets>, MeshError> load_mesh_playback_assets(const MeshModelLibrary&, std::string_view filename, bool retain_relative_animation = false);

struct MeshPlaybackInstance {
    std::shared_ptr<const MeshPlaybackAssets> assets;
    MeshModel model;
    std::vector<MeshPlaybackControl> controls;
    std::vector<MeshAnimationState> sampling;
    std::uint32_t runtime_flags {}, transient_state {}, active_control_count {};
    bool registered {};
    MeshAnimationOptions options;
};

std::expected<MeshPlaybackInstance, MeshError> create_mesh_playback(std::shared_ptr<const MeshPlaybackAssets>, std::size_t control_count = 1, std::uint32_t runtime_flags = 0, bool registered = false, bool alternate_clock = false);

struct MeshPlaybackResult {
    MeshMilliseconds nominal_delay {};
    std::uint32_t events {};
};

//Requests 0..11 select clips; 12 clears and 13/14 hold the current clip.
//The nominal delay deliberately follows the native rate-independent query.
std::expected<MeshPlaybackResult, MeshError> request_mesh_playback(MeshPlaybackInstance&, std::size_t control, const MeshPlaybackRequest&, const MeshPlaybackClocks&);
bool clear_deferred_mesh_playback(MeshPlaybackInstance&, std::size_t control);
//Includes the native first-control-only automatic M0 transition, completion,
//deferred requests, one-loop overshoot, retirement and final-pose behavior.
std::expected<MeshPlaybackResult, MeshError> advance_mesh_playback(MeshPlaybackInstance&, const MeshPlaybackClocks&, bool update_pose = true, std::uint32_t automatic_transition_flags = 0);
//Visible activation repairs bindings changed by a background update.
std::expected<void, MeshError> reactivate_mesh_playback(MeshPlaybackInstance&);
std::expected<MeshPlaybackResult, MeshError> evaluate_mesh_playback_pose(MeshPlaybackInstance&, bool force = false);
MeshMilliseconds mesh_playback_nominal_remaining(const MeshPlaybackControl&);

//Only these adapters retain the original 32-bit timestamp wire image. Runtime
//clocks are 64 bit; old timestamps are extended near the supplied clock epoch.
//Restore preserves the original elapsed*0.03/rate reconstruction contract.
std::expected<MeshPlaybackResult, MeshError> restore_mesh_playback_controls(MeshPlaybackInstance&, std::span<const MeshAnimationControl>, const MeshPlaybackClocks&);
std::vector<MeshAnimationControl> capture_mesh_playback_controls(const MeshPlaybackInstance&);
} //namespace simcoaster
