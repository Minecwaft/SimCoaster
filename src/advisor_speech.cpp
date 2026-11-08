#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "advisor_response_catalog.hpp"
#include "advisor_sequence_catalog.hpp"
#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

namespace simcoaster {
namespace {
constexpr std::uint32_t command_mask = 0xFF80FFFFu, flag_mask = 0x7F0000u;

std::uint32_t unscaled(AdvisorWorld& c) {
    if (!c.resources.meshes) {
        throw std::logic_error("advisor playback needs an unscaled clock");
    }
    return static_cast<std::uint32_t>(c.resources.meshes->clocks.unscaled.count());
}

std::int32_t duration(const AdvisorSpeechState& state, std::uint32_t command) {
    //Canned row9 contains command26. Native duration[25] aliases the first
    //command word; express that observed value without an out-of-bounds read.
    if (command == 26) {
        return fixed::word(state.animation_sequence_commands[0]);
    }
    if (!command || command > state.animation_duration_ms.size()) {
        throw std::out_of_range("advisor animation duration");
    }
    return fixed::word(state.animation_duration_ms[command - 1]);
}

std::uint32_t current_word(const AdvisorSpeechState& state) {
    if (state.animation_sequence_cursor_or_generated_terminator < 0) {
        throw std::out_of_range("negative advisor sequence cursor");
    }
    return state.animation_sequence_commands.at(static_cast<std::size_t>(state.animation_sequence_cursor_or_generated_terminator));
}

MeshPlaybackInstance* speech_mesh(AdvisorWorld& c) {
    return c.advisor.presentation.meshes.at(c.advisor.speech.render_slot_index).get();
}

void request_animation(AdvisorWorld& c, std::uint32_t command, std::uint32_t flags) {
    auto* mesh = speech_mesh(c);
    if (!mesh) {
        return;
    }
    if (!c.resources.meshes) {
        throw std::logic_error("advisor animation needs clock binding");
    }
    const auto result = request_mesh_playback(*mesh, 0, { { 5, command - 1 }, flags, 1.f }, c.resources.meshes->clocks);
    if (!result) {
        throw std::runtime_error(describe_mesh_error(result.error()));
    }
}

std::int32_t lip_cue(AdvisorSpeechState& state) {
    if (state.lip_data.empty()) {
        return -1;
    }
    if (state.lip_cue_word_index < 0) {
        throw std::out_of_range("negative advisor lip cursor");
    }
    const auto offset = static_cast<std::size_t>(state.lip_cue_word_index) * sizeof(std::int32_t);
    if (offset > state.lip_data.size() || state.lip_data.size() - offset < sizeof(std::int32_t)) {
        throw std::runtime_error("advisor lip stream has no terminator");
    }
    std::int32_t cue;
    std::memcpy(&cue, state.lip_data.data() + offset, sizeof(cue));
    ++state.lip_cue_word_index;
    return cue == -1 ? -1 : cue / 1000;
}

bool due(std::uint32_t deadline, std::uint32_t now) {
    return fixed::word(deadline - now) <= 0;
}

void node_visibility(AdvisorWorld& c, std::int32_t selector, bool hidden) {
    if (!c.resources.meshes) {
        throw std::logic_error("advisor face needs a mesh owner");
    }
    auto* mesh = speech_mesh(c);
    if (!mesh) {
        return;
    }
    const auto shape = static_cast<std::size_t>(selector - 1);
    const auto index = c.advisor.speech.mouth_shape_node_indices_by_slot.at(c.advisor.speech.render_slot_index).at(shape);
    if (index < 0) {
        throw std::out_of_range("negative advisor mouth node");
    }
    auto& flags = mesh->model.nodes.at(static_cast<std::size_t>(index)).flags;
    if (hidden) {
        flags |= 16u;
    } else {
        flags &= ~16u;
    }
}
} //namespace

std::int32_t advisor_attachment_part_for_sample(std::int32_t sample) {
    return match (sample) -> std::int32_t {
        case auto n if (n >= 998 && n <= 1021)  => 0;
        case auto n if (n >= 1022 && n <= 1041) => 1;
        case auto n if (n >= 1042 && n <= 1065) => 3;
        case auto n if (n >= 1066 && n <= 1080) => 5;
        case auto n if (n >= 1081 && n <= 1097) => 2;
        case auto n if (n >= 1098 && n <= 1108) => 4;
        case _                                  => -1;
    };
}

bool set_advisor_attachment_part(AdvisorWorld& c, std::int32_t part) {
    auto& view = c.advisor.presentation;
    if (!view.profile_attachment_visible && !view.intro_attachment_visible && !c.ui.persistent_root) {
        return false;
    }
    if (!view.attachment_controls_present) {
        return false;
    }
    const bool profile = view.profile_attachment_visible || view.intro_attachment_visible;
    view.attachment_controls_visible = part >= 0 && (profile || !c.ui.active_root);
    if (!profile && part == -2 && view.advisor_on) {
        view.replay_enabled = true;
        return false;
    }
    view.replay_enabled = false;
    if (!view.attachment_controls_visible) {
        return false;
    }
    view.attachment_part = part;
    return true;
}

std::uint32_t advisor_presentation_time_ms(AdvisorWorld& c) {
    auto& state = c.advisor.speech;
    auto now = unscaled(c);
    if (c.advisor.presentation.simulation_paused) {
        if (!state.scaled_clock_pause_active) {
            state.scaled_clock_pause_active = 1;
            state.scaled_clock_pause_start_ms = now;
        }
        now = state.scaled_clock_pause_start_ms;
    } else if (state.scaled_clock_pause_active) {
        state.scaled_clock_pause_active = 0;
        state.scaled_clock_accumulated_pause_ms += now - state.scaled_clock_pause_start_ms;
    }
    return now - state.scaled_clock_accumulated_pause_ms;
}

std::int32_t build_random_advisor_sequence(AdvisorWorld& c, std::int32_t remaining) {
    auto& state = c.advisor.speech;
    std::size_t count = 0;
    std::int32_t accumulated = 0;
    while (remaining > 0 && count < 19) {
        const auto candidate = static_cast<std::size_t>(next_crt_random(c) % 10);
        auto command = static_cast<std::uint32_t>(candidate + 1);
        if (!count) {
            command = 14;
        } else if (remaining < fixed::word(state.animation_duration_ms[candidate])) {
            std::size_t index = 0;
            while (index < 10 && remaining > fixed::word(state.animation_duration_ms[index])) {
                ++index;
            }
            command = static_cast<std::uint32_t>(index < 10 ? index + 1 : 10);
        }
        state.animation_sequence_commands[count++] = command;
        const auto selected = duration(state, command);
        accumulated = fixed::add(accumulated, selected);
        remaining = fixed::subtract(remaining, selected);
    }
    state.animation_sequence_commands[count] = 15;
    if (count + 1 < state.animation_sequence_commands.size()) {
        state.animation_sequence_commands[count + 1] = 0;
    }
    //Native command[20] aliases this cursor; no out-of-bounds host write.
    state.animation_sequence_cursor_or_generated_terminator = 0;
    return fixed::add(fixed::word(state.animation_duration_ms[14]), accumulated);
}

std::int32_t load_advisor_canned_sequence(AdvisorWorld& c, std::int32_t row, std::int32_t fallback) {
    if (row < 0) {
        throw std::out_of_range("negative advisor canned sequence");
    }
    auto& state = c.advisor.speech;
    const auto& source = advisor_catalog::sequences.at(static_cast<std::size_t>(row));
    std::int32_t total = 0;
    std::size_t input = 0, output = 0;
    state.delayed_advisor_ui_event_deadline_ms = 0;
    for (;;) {
        const auto word = source.at(input);
        state.animation_sequence_commands.at(output) = word;
        if (!word) {
            break;
        }
        if (word & 0x400000u) {
            state.delayed_advisor_ui_event_deadline_ms = static_cast<std::uint32_t>(total) + source.at(input + 1);
            input += 2;
        } else {
            const auto command = word & command_mask;
            if (!output && command != 14) {
                state.next_speech_segment_deadline_ms = 0;
            }
            total = fixed::add(total, duration(state, command));
            ++input;
            ++output;
        }
    }
    if (fallback && !total) {
        return build_random_advisor_sequence(c, fallback);
    }
    state.animation_sequence_cursor_or_generated_terminator = 0;
    return total;
}

std::uint32_t AudioHostPort::get_sound_target_sample_time(WorldRuntime& c, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
    return 1;
}

std::uint32_t AudioHostPort::refresh_sound_target_position(WorldRuntime& c, std::uint32_t, const std::array<std::int32_t, 3>&) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
    return 0;
}

void AudioHostPort::submit_sound_target_pause_request(WorldRuntime& c, std::uint32_t) {
    if (c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
        missing_runtime_service();
    }
}

void pause_advisor_speech_for_ui(AdvisorWorld& c) {
    auto& s = c.advisor.speech;
    if (s.response_active && c.advisor.presentation.advisor_on && s.audio_target_id) {
        c.ports.audio.submit_sound_target_pause_request(c, s.audio_target_id);
        s.speech_request_paused_for_ui = 1;
    }
}

void resume_advisor_speech_after_ui(AdvisorWorld& c) {
    auto& s = c.advisor.speech;
    if (s.speech_request_paused_for_ui && c.advisor.presentation.advisor_on && s.audio_target_id) {
        c.ports.audio.submit_sound_target_restart_request(c, s.audio_target_id);
        s.speech_request_paused_for_ui = 0;
    }
}

void reset_advisor_speech(AdvisorWorld& c) {
    c.advisor.speech.response_active = 0;
    (void)set_advisor_attachment_part(c, -2);
    c.advisor.speech.lip_data.clear();
}

std::uint32_t world_operations::builtin::play_advisor_speech_response(WorldRuntime& c, std::int32_t response) {
    const auto found = std::ranges::find(advisor_catalog::responses, response, &advisor_catalog::Response::id);
    if (found == advisor_catalog::responses.end()) {
        return 0;
    }
    auto& state = c.advisor.speech;
    auto& view = c.advisor.presentation;
    const auto& spec = *found;
    const auto part = advisor_attachment_part_for_sample(spec.sample);
    const bool attached = set_advisor_attachment_part(c, part);
    const bool help = c.advisor.advisor_help_override_response_id != 1014 && c.advisor.advisor_help_override_response_id == response;
    if (state.response_active || (!view.advisor_on && !attached && part < 0 && !help)) {
        return 0;
    }
    c.advisor.advisor_help_override_response_id = 1014;
    view.response_id = response;
    view.debug_sample_id = spec.sample;
    state.sample_id = static_cast<std::uint32_t>(spec.sample);
    state.use_selected_resource_source_path = static_cast<std::uint32_t>(spec.render_flags >> 16);
    state.render_slot_index = static_cast<std::uint16_t>(spec.render_flags);
    state.initial_mesh_animation_command = spec.initial_command;
    state.secondary_mesh_animation_command = spec.secondary_command;
    state.response_timeline_origin_ms = advisor_presentation_time_ms(c) - 200u;
    view.repositioned_render_transforms = has_active_world_animation_target(c) || spec.initial_command == 666 || attached || !view.commands_enabled;
    if (view.repositioned_render_transforms) {
        state.initial_mesh_animation_command = 0;
    }
    view.fixed_pose_sequence = spec.sequence_mode == 17;
    if (view.fixed_pose_sequence) {
        view.repositioned_render_transforms = false;
    }
    set_advisor_render_transforms(c, view.repositioned_render_transforms);
    state.lip_cue_frame_toggle = 0;
    state.lip_cue_stream_active = 0;
    state.next_speech_segment_deadline_ms = state.response_timeline_origin_ms + 1000u;
    if (!view.fixed_pose_sequence && !view.repositioned_render_transforms) {
        const auto path = std::format("{}/{}/Speech/lips/sp_{:04}.lip", c.source.data_directory, state.use_selected_resource_source_path ? "levels/" + c.source.selected_resource_source_name : std::string { "Global" }, spec.lip_index);
        if (const auto bytes = c.ports.assets.read_balance_asset(c, path)) {
            const auto payload = std::as_bytes(std::span { bytes->data(), bytes->size() });
            state.lip_data.assign(payload.begin(), payload.end());
            if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw std::length_error("advisor lip file");
            }
            state.lip_file_size = static_cast<std::uint32_t>(payload.size());
            state.lip_cue_word_index = 0;
            state.lip_cue_stream_active = state.lip_cue_frame_toggle = 1;
        }
    }
    view.speech_ducking_active = true;
    view.sound_settings_dirty = true;
    const auto catalog = state.use_selected_resource_source_path ? view.local_speech_catalog_slot : c.resources.global_speech_catalog_slot;
    state.audio_target_id = submit_positioned_template_request(c, 0, catalog, state.sample_id, 0, 0, 0);
    auto length = c.ports.audio.get_sound_target_sample_time(c, state.audio_target_id) + 200u;
    if (!length) {
        return 0;
    }
    if (spec.sequence_mode == -1) {
        if (const auto generated = build_random_advisor_sequence(c, fixed::word(length + 300u))) {
            length = static_cast<std::uint32_t>(generated);
        }
    } else {
        length = static_cast<std::uint32_t>(load_advisor_canned_sequence(c, spec.sequence_mode - 16, fixed::word(length + 300u)));
    }
    resize_advisor_ticket_particles(c, 0, 0);
    const auto first = current_word(state);
    const auto command = first & command_mask;
    request_animation(c, command, 16);
    state.active_animation_duration_index = fixed::word(command - 1);
    state.animation_response_flags = first & flag_mask;
    ++state.animation_sequence_cursor_or_generated_terminator;
    state.initial_animation_started = 1;
    if (state.next_speech_segment_deadline_ms) {
        c.ports.resources.request_target_recycle(c, state.audio_target_id);
        state.audio_target_id = 0;
        state.lip_cue_frame_toggle = 0;
        state.response_timeline_origin_ms += 1000u;
    }
    state.response_end_deadline_ms = state.response_timeline_origin_ms + length;
    state.next_lip_cue_deadline_ms = state.response_timeline_origin_ms + static_cast<std::uint32_t>(lip_cue(state));
    state.response_active = 1;
    if (state.delayed_advisor_ui_event_deadline_ms) {
        state.delayed_advisor_ui_event_deadline_ms += state.response_timeline_origin_ms;
    }
    update_advisor_mesh_state(c);
    return state.animation_duration_ms[14] + length + 1000u;
}

void update_advisor_mesh_transition(AdvisorWorld& c) {
    auto& s = c.advisor.speech;
    const auto* mesh = speech_mesh(c);
    const auto deferred = mesh ? mesh->controls.at(0).deferred.clip.category : 12u;
    if (s.initial_animation_transition_pending && deferred == 12) {
        update_advisor_mesh_state(c);
        s.initial_animation_transition_pending = s.initial_animation_started = 0;
    } else if (s.initial_animation_started) {
        update_advisor_mesh_state(c);
        s.initial_animation_started = 0;
    }
    update_advisor_ticket_particles(c);
}

void update_advisor_speech(AdvisorWorld& c) {
    auto& s = c.advisor.speech;
    auto& view = c.advisor.presentation;
    const auto now = advisor_presentation_time_ms(c);
    if (!s.response_active) {
        if (!s.lip_data.empty()) {
            s.lip_data.clear();
            s.lip_file_size = 0;
            s.lip_cue_stream_active = 0;
        }
        return;
    }
    if (s.audio_target_id) {
        s.audio_target_id = c.ports.audio.refresh_sound_target_position(c, s.audio_target_id, { 0, 0, 0 });
    }
    if (!s.audio_target_id && due(s.response_end_deadline_ms, now)) {
        (void)set_advisor_attachment_part(c, -2);
        s.response_active = 0;
        view.speech_ducking_active = false;
        view.sound_settings_dirty = true;
        resize_advisor_ticket_particles(c, 0, 0);
    }
    if (s.next_speech_segment_deadline_ms && due(s.next_speech_segment_deadline_ms, now)) {
        s.audio_target_id = submit_positioned_template_request(c, 0, s.use_selected_resource_source_path ? view.local_speech_catalog_slot : c.resources.global_speech_catalog_slot, s.sample_id, 0, 0, 0);
        s.next_speech_segment_deadline_ms = 0;
        s.lip_cue_frame_toggle = 1;
    }
    if (s.delayed_advisor_ui_event_deadline_ms && due(s.delayed_advisor_ui_event_deadline_ms, now)) {
        (void)c.ports.effects.create_particle_effect(c, 87, { 90000, 0, 7000 }, 0);
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 198, 0, 0, 0);
        if (!view.lobby_selected_source.empty()) {
            (void)get_or_create_theme_global_info_for_source(c, get_or_create_theme_global_info_manager(c), view.lobby_selected_source);
        }
        s.delayed_advisor_ui_event_deadline_ms = 0;
    }
    if (!s.initial_animation_started && (current_word(s) & command_mask)) {
        const auto* mesh = speech_mesh(c);
        if (mesh && mesh->controls.at(0).deferred.clip.category == 12) {
            const auto word = current_word(s), command = word & command_mask;
            request_animation(c, command, 0);
            s.animation_response_flags = word & flag_mask;
            s.initial_animation_transition_pending = 1;
            s.active_animation_duration_index = fixed::word(command - 1);
            ++s.animation_sequence_cursor_or_generated_terminator;
        }
    }
    if (s.lip_cue_stream_active && fixed::word(now) > fixed::word(s.next_lip_cue_deadline_ms)) {
        s.lip_cue_frame_toggle = !s.lip_cue_frame_toggle;
        const auto cue = lip_cue(s);
        s.next_lip_cue_deadline_ms = static_cast<std::uint32_t>(cue);
        if (cue == -1) {
            s.lip_cue_stream_active = s.lip_cue_frame_toggle = 0;
        } else {
            s.next_lip_cue_deadline_ms += s.response_timeline_origin_ms;
        }
    }
    if (s.lip_cue_frame_toggle && s.lip_cue_stream_active) {
        if (fixed::word(now) > fixed::word(s.next_mouth_shape_change_deadline_ms)) {
            const auto count = fixed::word(s.mouth_shape_count);
            if (count <= 0) {
                throw std::domain_error("advisor mouth-shape count");
            }
            s.current_mouth_shape_selector = next_crt_random(c) % count + 1;
            s.next_mouth_shape_change_deadline_ms = now + 100u;
        }
    } else {
        s.current_mouth_shape_selector = 1;
    }
    if (s.last_applied_mouth_shape_selector != s.current_mouth_shape_selector) {
        if (s.last_applied_mouth_shape_selector > 0) {
            node_visibility(c, s.last_applied_mouth_shape_selector, true);
        }
        if (s.current_mouth_shape_selector && !(s.animation_response_flags & 0x10000u) && !view.repositioned_render_transforms) {
            node_visibility(c, s.current_mouth_shape_selector, false);
        }
        s.last_applied_mouth_shape_selector = s.current_mouth_shape_selector;
    }
}
} //namespace simcoaster
