#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace simcoaster {
struct Stream;

//Host projection of the advisor's saved speech and lip-animation state.
struct AdvisorSpeechState {
    std::uint32_t animation_response_flags {};
    std::uint32_t response_timeline_origin_ms {};
    std::uint32_t response_end_deadline_ms {};
    std::uint32_t response_active {};
    std::uint32_t lip_cue_frame_toggle {};
    std::uint32_t render_slot_index {};
    std::uint32_t use_selected_resource_source_path {};
    std::uint32_t sample_id {};
    std::uint32_t audio_target_id {};
    std::int32_t initial_mesh_animation_command {};
    std::int32_t secondary_mesh_animation_command {};
    std::uint32_t initial_animation_transition_pending {};
    std::uint32_t initial_animation_started {};
    std::uint32_t next_speech_segment_deadline_ms {};
    std::array<std::int32_t, 2> bug_head_node_index_by_slot {};
    std::array<std::array<std::int32_t, 5>, 2> mouth_shape_node_indices_by_slot {};
    std::array<std::uint32_t, 25> animation_duration_ms {};
    std::array<std::uint32_t, 20> animation_sequence_commands {};
    std::int32_t animation_sequence_cursor_or_generated_terminator {};
    std::int32_t active_animation_duration_index {};
    std::uint32_t mouth_shape_count {};
    std::uint32_t next_lip_cue_deadline_ms {};
    std::uint32_t delayed_advisor_ui_event_deadline_ms {};
    std::uint32_t scaled_clock_pause_active {};
    std::uint32_t scaled_clock_pause_start_ms {};
    std::uint32_t scaled_clock_accumulated_pause_ms {};
    std::int32_t current_mouth_shape_selector {};
    std::int32_t last_applied_mouth_shape_selector {};
    std::uint32_t next_mouth_shape_change_deadline_ms {};
    std::uint32_t lip_file_size {};
    std::int32_t lip_cue_word_index {};
    std::uint32_t lip_cue_stream_active {};
    std::array<std::int32_t, 4> advisor_mesh_node_ids_to_clear {};
    std::array<std::int32_t, 4> advisor_mesh_node_ids_to_set {};
    std::uint32_t speech_request_paused_for_ui {};
    std::vector<std::byte> lip_data;
};

struct CheatCommandState {
    std::array<char, 10> secret_code_buffer {};
    std::uint8_t cheat_commands_enabled {}, activation_event_latched {};
};

bool write_advisor_speech_save_section(Stream&, const AdvisorSpeechState&);
bool restore_advisor_speech_save_section(Stream&, AdvisorSpeechState&);
bool write_cheat_command_state(Stream&, const CheatCommandState&);
bool restore_cheat_command_state(Stream&, CheatCommandState&);
bool write_advisor_scoring_word(Stream&, std::uint32_t);
bool restore_advisor_scoring_word(Stream&, std::uint32_t&);
} //namespace simcoaster
