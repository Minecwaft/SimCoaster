#include "simcoaster/schema_projection.hpp"
#include "simcoaster/save_stream.hpp"
#include "simcoaster/later_save.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>

namespace simcoaster {
namespace {
using save::read;
using save::write;

struct SavedAdvisor {
    [[=save::Field{ "animation_response_flags", 0 }]] std::uint32_t animation_response_flags{};
    [[=save::Field{ "response_timeline_origin_ms", 1 }]] std::uint32_t response_timeline_origin_ms{};
    [[=save::Field{ "response_end_deadline_ms", 2 }]] std::uint32_t response_end_deadline_ms{};
    [[=save::Field{ "response_active", 3 }]] std::uint32_t response_active{};
    [[=save::Field{ "lip_cue_frame_toggle", 4 }]] std::uint32_t lip_cue_frame_toggle{};
    [[=save::Field{ "render_slot_index", 5 }]] std::uint32_t render_slot_index{};
    [[=save::Field{ "use_selected_resource_source_path", 6 }]] std::uint32_t use_selected_resource_source_path{};
    [[=save::Field{ "sample_id", 7 }]] std::uint32_t sample_id{};
    [[=save::Field{ "audio_target_id", 8 }]] std::uint32_t audio_target_id{};
    [[=save::Field{ "initial_mesh_animation_command", 9 }]] std::int32_t initial_mesh_animation_command{};
    [[=save::Field{ "secondary_mesh_animation_command", 10 }]] std::int32_t secondary_mesh_animation_command{};
    [[=save::Field{ "initial_animation_transition_pending", 11 }]] std::uint32_t initial_animation_transition_pending{};
    [[=save::Field{ "initial_animation_started", 12 }]] std::uint32_t initial_animation_started{};
    [[=save::Field{ "next_speech_segment_deadline_ms", 13 }]] std::uint32_t next_speech_segment_deadline_ms{};
    [[=save::Field{ "bug_head_node_index_by_slot", 14 }]] std::array<std::int32_t, 2> bug_head_node_index_by_slot{};
    [[=save::Field{ "mouth_shape_node_indices_by_slot",
                    15 }]] std::array<std::array<std::int32_t, 5>, 2> mouth_shape_node_indices_by_slot{};
    [[=save::Field{ "animation_duration_ms", 16 }]] std::array<std::uint32_t, 25> animation_duration_ms{};
    [[=save::Field{ "animation_sequence_commands", 17 }]] std::array<std::uint32_t, 20> animation_sequence_commands{};
    [[=save::Field{ "animation_sequence_cursor_or_generated_terminator",
                    18 }]] std::int32_t animation_sequence_cursor_or_generated_terminator{};
    [[=save::Field{ "active_animation_duration_index", 19 }]] std::int32_t active_animation_duration_index{};
    [[=save::Field{ "mouth_shape_count", 20 }]] std::uint32_t mouth_shape_count{};
    [[=save::Field{ "next_lip_cue_deadline_ms", 21 }]] std::uint32_t next_lip_cue_deadline_ms{};
    [[=save::Field{ "delayed_advisor_ui_event_deadline_ms", 22 }]] std::uint32_t delayed_advisor_ui_event_deadline_ms{};
    [[=save::Field{ "scaled_clock_pause_active", 23 }]] std::uint32_t scaled_clock_pause_active{};
    [[=save::Field{ "scaled_clock_pause_start_ms", 24 }]] std::uint32_t scaled_clock_pause_start_ms{};
    [[=save::Field{ "scaled_clock_accumulated_pause_ms", 25 }]] std::uint32_t scaled_clock_accumulated_pause_ms{};
    [[=save::Field{ "current_mouth_shape_selector", 26 }]] std::int32_t current_mouth_shape_selector{};
    [[=save::Field{ "last_applied_mouth_shape_selector", 27 }]] std::int32_t last_applied_mouth_shape_selector{};
    [[=save::Field{ "next_mouth_shape_change_deadline_ms", 28 }]] std::uint32_t next_mouth_shape_change_deadline_ms{};
    [[=save::Field{ "lip_file_size", 29 }]] std::uint32_t lip_file_size{};
    [[=save::Field{ "lip_cue_word_index", 30 }]] std::int32_t lip_cue_word_index{};
    [[=save::Field{ "lip_cue_stream_active", 31 }]] std::uint32_t lip_cue_stream_active{};
    [[=save::Field{ "ignored_lip_pointer", 32 }]] std::uint32_t ignored_lip_pointer{};
    [[=save::Field{ "advisor_mesh_node_ids_to_clear",
                    33 }]] std::array<std::int32_t, 4> advisor_mesh_node_ids_to_clear{};
    [[=save::Field{ "advisor_mesh_node_ids_to_set", 34 }]] std::array<std::int32_t, 4> advisor_mesh_node_ids_to_set{};
    [[=save::Field{ "speech_request_paused_for_ui", 35 }]] std::uint32_t speech_request_paused_for_ui{};
};

static_assert(save::record_width<SavedAdvisor> == 380);

using schema::copy_matching;

static_assert(schema::matching_fields<AdvisorSpeechState, SavedAdvisor>.size() == 35);
static_assert(schema::matching_fields<SavedAdvisor, AdvisorSpeechState>.size() == 35);

constexpr std::uint32_t advisor_tag = 1599489089u;
} //namespace

bool write_advisor_speech_save_section(Stream& stream, const AdvisorSpeechState& state) {
    if (state.lip_data.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    if (!state.lip_data.empty() && state.lip_file_size != state.lip_data.size())
        return false;
    SavedAdvisor saved{};
    copy_matching(saved, state); //Legacy process address is intentionally zero.
    if (!write(stream, advisor_tag)
        || !write(stream, 380u)
        || !write(stream, saved)
        || !write(stream, static_cast<std::uint32_t>(state.lip_data.size())))
        return false;
    return state.lip_data.empty() || stream.write(state.lip_data) == static_cast<std::int32_t>(state.lip_data.size());
}

bool restore_advisor_speech_save_section(Stream& stream, AdvisorSpeechState& state) {
    std::uint32_t tag{}, size{}, lip_size{};
    SavedAdvisor saved{};
    if (!read(stream, tag)
        || tag != advisor_tag
        || !read(stream, size)
        || size != 380
        || !read(stream, saved)
        || !read(stream, lip_size)
        || lip_size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    std::vector<std::byte> lip(lip_size);
    if (!lip.empty() && stream.read(lip) != static_cast<std::int32_t>(lip.size()))
        return false;
    copy_matching(state, saved);
    state.lip_file_size = lip_size;
    state.lip_data = std::move(lip);
    return true;
}

bool write_cheat_command_state(Stream& stream, const CheatCommandState& state) {
    return write(stream, state.cheat_commands_enabled) && write(stream, state.activation_event_latched);
}

bool restore_cheat_command_state(Stream& stream, CheatCommandState& state) {
    return read(stream, state.cheat_commands_enabled) && read(stream, state.activation_event_latched);
}

bool write_advisor_scoring_word(Stream& stream, std::uint32_t word) {
    return write(stream, word);
}

bool restore_advisor_scoring_word(Stream& stream, std::uint32_t& word) {
    return read(stream, word);
}
} //namespace simcoaster
