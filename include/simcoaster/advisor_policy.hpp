#pragma once
#include "sam_record.hpp"
#include <array>
#include <vector>
#include <string>
#include <memory>

namespace simcoaster {
struct MeshPlaybackInstance;

struct AdvisorGeneralPolicy {
    [[=sam::Field { "MinTimeAnyMessage", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeAnyMessage {};
    [[=sam::Field { "MinTimeSameMessage", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessage {};
    [[=sam::Field { "MinScoreForConsideration", sam::Kind::nonnegative_integer }]] std::int32_t MinScoreForConsideration {};
    [[=sam::Field { "UIPanelDelay", sam::Kind::integer }]] std::int32_t UIPanelDelay {};
    [[=sam::Field { "UISelectionModeMessageFilter", sam::Kind::integer }]] std::int32_t UISelectionModeMessageFilter {};
};

struct AdvisorMessageGroupPolicy {
    [[=sam::Field { "MinTimeSameMessage1", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessage1 {};
    [[=sam::Field { "MinTimeSameMessage2", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessage2 {};
    [[=sam::Field { "MinTimeSameMessage3", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessage3 {};
    [[=sam::Field { "MinTimeSameMessageX", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeSameMessageX {};
    [[=sam::Field { "SayOnlyOnce", 0, 2 }]] std::int32_t SayOnlyOnce {};
    [[=sam::Field { "DiscardAfterSlaps", sam::Kind::nonnegative_integer }]] std::int32_t DiscardAfterSlaps {};
};

struct AdvisorQueuePolicy {
    [[=sam::Group { "GeneralAdvisor" }]] AdvisorGeneralPolicy general;
    [[=sam::Rows { "MessageGroups" }]] std::array<AdvisorMessageGroupPolicy, 10> groups;
    [[=sam::Extent { "MessageGroups" }]] std::uint32_t group_count {};
};

//Behavior receiver state. The scan cursor is saved in AdvisorScoring;
//callback outputs and the six spacing-group stamps are runtime-only.
struct AdvisorBehaviorRuntime {
    std::uint32_t descriptor_cursor {};
    std::uint16_t callback_thing_id {};
    std::uint32_t callback_secondary {};
    std::array<std::uint32_t, 6> group_last_played_world_tick {};
};
enum class AdvisorVariantMode : std::int32_t {
    once = 0,
    hold_last = 1,
    cycle = 2,
    random = 3
};

struct AdvisorParticleMotion {
    std::array<float, 2> position_from {}, position_to {}, extent_from {}, extent_to {};
    float start_ms {}, duration_ms {}, phase_scale {};
};

//Presentation owners bind their actual mesh/particle handles here. These are
//host runtime handles, independent of the saved x86 pointer representation.
struct AdvisorPresentationState {
    bool advisor_on = true, tutorial_on = true;
    bool mode_initialized {}, map_source_recreation {}, map_source_view {};
    bool fixed_pose_sequence {}, repositioned_render_transforms {};
    bool speech_ducking_active {}, sound_settings_dirty {};
    std::int32_t attachment_part = -2;
    bool replay_enabled {};
    std::array<std::shared_ptr<MeshPlaybackInstance>, 2> meshes {};
    std::vector<std::uint32_t> ticket_particles;
    AdvisorParticleMotion ticket_motion;
    bool simulation_paused {}, commands_enabled = true;
    std::int32_t response_id = -1, debug_sample_id {}, award_texture_index {};
    std::uint32_t local_speech_catalog_slot {};
    std::array<std::array<float, 16>, 2> render_transforms {};
    std::array<std::array<std::int32_t, 8>, 2> award_texture_slots {};
    //Profile/intro attachment roots precede the persistent toolbar in native
    //lookup order. UI owners bind these alongside their actual control trees.
    bool profile_attachment_visible {}, intro_attachment_visible {};
    bool attachment_controls_present {}, attachment_controls_visible {};
    std::string lobby_selected_source;
};
} //namespace simcoaster
