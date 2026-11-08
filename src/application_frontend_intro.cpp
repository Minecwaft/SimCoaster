#include "application_frontend_internal.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_objects.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char intro_template[]{
#embed "ui_templates/application_intro.bin"
};
static_assert(sizeof(intro_template) == 196);
constexpr std::array<std::int32_t, 3> speeches{ 565, 566, 567 }; //Native9BA2CC.
} //namespace

void ApplicationFrontendController::State::open_intro(bool suppress) {
    close();
    suppress_buttons = suppress;
    suppress_template = true;
    skip_requested = false;
    speech_index = -1;
    speech_status = 0;
    final_speech = false;
    pulse_enabled = !suppress;
    pulse_phase = false;
    pulse_deadline = 0;
    intro_pulse_button = suppress ? 0u : 111004u;
    const auto camera = find_first_park_object_record_id_by_definition_id(c, 4798);
    auto* header = camera ? c.entities.at(camera).payload : nullptr;
    if (!header || header->kind != ThingKind::park_object)
        throw std::runtime_error("Original Intro requires the real4798 Camera Path owner");
    intro_script_id = reinterpret_cast<ParkObjectRuntimeView*>(header)->script_id;
    auto* script = find_rsse_script_state_by_id(c, intro_script_id);
    if (!script)
        throw std::runtime_error("Original Intro Camera Path has no RSSE program");
    if (!restoring)
        set_rsse_script_state_slot(c, script, 3, 0);
    intro = parse(intro_template, 111000);
    phase = ApplicationFrontendPhase::intro;
    ui.set_visible(required(intro, 111002), false);
    ui.set_visible(required(intro, 111001), false);
    ui.set_visible(required(intro, 111004), !suppress);
    required(intro, 111001).help_index = 352;
    required(intro, 111004).help_index = 353;
    commands(false);
    host.persistent_visible(false);
    //Hiding the HUD closes its panels and releases their input routes. Claim
    //Intro input afterwards, or the full-screen playback window intercepts
    //pointer hits before they can reach the native Skip button (542C60).
    ui.set_focus(suppress ? nullptr : intro);
    ui.set_input_root(intro);
    host.resume_ui_transition();
    host.lobby_sound(true, true);
    intro_sound_active = true;
}

void ApplicationFrontendController::State::skip_intro() {
    if (skip_requested || !intro)
        return;
    auto& skip = required(intro, 111004);
    if (!skip.visible())
        return;
    skip_requested = true;
    ui.set_visible(skip, false);
    ui.set_visible(required(intro, 111001), true);
    latch_world_animation_completion_if_target_active(c);
}

void ApplicationFrontendController::State::play_intro() {
    if (!intro)
        return;
    if (mode < 5)
        mode = 5;
    for (const auto action : speeches)
        cancel_advisor(action);
    ui.set_visible(required(intro, 111002), false);
    remove(intro);
    suppress_buttons = false;
    open_profiles();
}

void ApplicationFrontendController::State::finish_intro(bool force) {
    //Native543390 hides the primary HUD again after animation completion
    //restores it. The globe/Play view still belongs to the frontend.
    host.persistent_visible(false);
    map_view_active = true;
    select_mode(4, false);
    if (force) {
        suppress_template = true;
        host.resume_ui_transition();
    }
    if (skip_requested || force) {
        if (mode < 5)
            mode = 5;
        if (!force)
            for (const auto action : speeches)
                cancel_advisor(action);
        remove(intro);
        open_profiles();
        return;
    }
    if (intro && !suppress_buttons) {
        ui.set_visible(required(intro, 111004), false);
        ui.set_visible(required(intro, 111001), true);
    }
    //Native state10 enters state19/20 only once source-view-active is set.
    if (!lobby_bound)
        bind_lobby(true);
    //The native tree keeps this frontend above the re-hidden HUD. Restore
    //the modern confined input route after animation teardown restored HUD
    //focus; the still-visible Intro controls retain pointer/key ownership.
    if (intro && ui.owns(intro)) {
        ui.set_input_root(intro);
        ui.set_focus(suppress_buttons ? nullptr : intro);
    }
}

void ApplicationFrontendController::State::update_intro() {
    if (!intro)
        return;
    if (!skip_requested) {
        auto* script = intro_script_id ? find_rsse_script_state_by_id(c, intro_script_id) : nullptr;
        if (script && get_rsse_script_state_slot(c, script, 3) != speech_status && !c.advisor.speech.response_active) {
            if (speech_status == 0) {
                intro_pulse_button = 0;
                if (pulse_enabled) {
                    pulse_enabled = false;
                    pulse_phase = false;
                    pulse_deadline = 0;
                    ui.set_button_auxiliary_state(required(intro, 111004), false);
                }
            }
            ++speech_status;
            ++speech_index;
            if (speech_index < 0 || speech_index >= static_cast<int>(speeches.size()))
                throw std::runtime_error("Intro RSSE speech phase exceeds original three actions");
            queue_advisor(speeches[static_cast<std::size_t>(speech_index)]);
        }
        if (speech_index == 2) {
            if (c.advisor.speech.response_active) {
                final_speech = true;
                intro_script_id = 0;
            } else if (final_speech && map_view_active) {
                speech_index = -1;
                ui.set_visible(required(intro, 111004), false);
                ui.set_visible(required(intro, 111001), true);
                pulse_enabled = true;
                intro_pulse_button = 111001;
            }
        }
    }
}
} //namespace simcoaster
