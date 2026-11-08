#pragma once
#include "ui_control.hpp"

namespace simcoaster {
struct WorldRuntime;

//The panel still chooses whether a message owns either pause. This owner
//pairs each acquired effect with one release, including construction failure.
class UiModalScope {
    WorldRuntime& world_;
    bool commands_{}, saved_commands_{}, advisor_paused_{}, gameplay_paused_{};
    std::function<void(bool)> gameplay_pause_;

public:
    explicit UiModalScope(WorldRuntime& world) : world_(world) {}

    ~UiModalScope();
    UiModalScope(const UiModalScope&) = delete;
    UiModalScope& operator=(const UiModalScope&) = delete;
    void acquire(bool pause_advisor);
    void acquire_gameplay(bool running, std::function<void(bool)>);
    void release();

    bool advisor_paused() const {
        return advisor_paused_;
    }
};

struct UiFocusBookmark {
    UiControl* control{};
    std::uint64_t serial{};

    void capture(const UiRuntime& ui, UiControl* target) {
        control = target;
        serial = ui.lifetime_serial(target);
    }

    UiControl* resolve(const UiRuntime& ui, UiControl* fallback, bool check_serial = true) const {
        return (check_serial ? serial && ui.lifetime_serial(control) == serial : ui.owns(control)) ? control : fallback;
    }
};
} //namespace simcoaster
