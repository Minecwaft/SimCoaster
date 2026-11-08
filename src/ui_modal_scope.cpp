#include "simcoaster/ui_modal_scope.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/world.hpp"

namespace simcoaster {
UiModalScope::~UiModalScope() {
    release();
}

void UiModalScope::acquire(bool pause_advisor) {
    saved_commands_ = world_.advisor.presentation.commands_enabled;
    world_.ports.ui.disable_command_families_and_set_disabled_flag(world_);
    commands_ = true;
    advisor_paused_ = pause_advisor;
    if (advisor_paused_) {
        pause_advisor_speech_for_ui(world_);
    }
}

void UiModalScope::acquire_gameplay(bool running, std::function<void(bool)> pause) {
    gameplay_pause_ = std::move(pause);
    gameplay_paused_ = running;
    if (gameplay_paused_) {
        gameplay_pause_(true);
    }
}

void UiModalScope::release() {
    if (std::exchange(commands_, false)) {
        world_.ports.ui.restore_command_families_state(world_, !saved_commands_);
    }
    if (std::exchange(advisor_paused_, false)) {
        resume_advisor_speech_after_ui(world_);
    }
    if (std::exchange(gameplay_paused_, false)) {
        gameplay_pause_(false);
    }
    gameplay_pause_ = {};
}
} //namespace simcoaster
