#pragma once
#include "ui_control.hpp"
#include "ui_preferences.hpp"

namespace simcoaster {
struct WorldRuntime;
//466B40 event65549: one signed wheel event changes zoom by10, regardless
//of its magnitude; the displayed normal frame applies the native limits.
bool apply_world_normal_camera_wheel(WorldRuntime&, std::int32_t amount);
enum class WorldNormalCameraPointerEvent {
    move,
    right_down,
    right_up,
    cancel,
    middle_down,
    middle_up
};

struct WorldNormalCameraPointerSample {
    //Native5070A0's movement threshold is in logical2048x1536 coordinates.
    UiPoint logical_position;
    std::chrono::milliseconds ui_time {}, platform_time {};
    //Only original interaction handlerkind10 bypasses the RMB cancel delay.
    bool staff_area_selection {};
};

struct WorldNormalCameraPointerFeedback {
    bool capture_pointer {}, release_pointer {};
    //On a short RMB release, replace the handler with BASE, set mode0 with
    //recording, and deactivate the coaster editor BEFORE forwarding release.
    bool cancel_interaction {};
    //5070A0 attempts the menu before 4D2260 applies its handler-up deadline.
    bool context_menu_release {};
    bool handler_right_down {}, handler_right_up {};
};

struct WorldNormalCameraInput {
    bool active {}, pointer_inside_world {};
    std::array<std::int32_t, 2> pointer {}; //pixels
    std::array<std::uint32_t, 2> viewport { 1100, 760 };
    bool middle_button {}, rotate_left {}, rotate_right {}, zoom_in {}, zoom_out {};
    bool pan_forward {}, pan_backward {}, pan_left {}, pan_right {};
    //Native input bit128 defaults enabled in466DE0. Bit512's alternate
    //limits remain in WorldRuntime.world_view_camera.alternate_zoom_limits.
    bool zoom_limits_enabled = true;
    std::chrono::milliseconds ui_time {};
};

struct WorldNormalCameraFeedback {
    bool changed {}, camera_right_held {}, release_pointer {};
    //Native500A40/597580 closes the Visitor Locator on an explicit pan.
    bool close_visitor_locator {};
};

//466B40/467930 normal-view movement and5070A0/4D2260 RMB routing. The
//platform supplies separate UI/monotonic clock samples; no Win32 polling,
//diagnostic orbit gestures or synthetic map clicks occur in this owner.
class WorldNormalCameraInputRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldNormalCameraInputRuntime();
    ~WorldNormalCameraInputRuntime();
    WorldNormalCameraInputRuntime(const WorldNormalCameraInputRuntime&) = delete;
    WorldNormalCameraInputRuntime& operator=(const WorldNormalCameraInputRuntime&) = delete;
    void configure_balance(std::string_view, bool preserve);
    WorldNormalCameraPointerFeedback pointer(WorldNormalCameraPointerEvent, const WorldNormalCameraPointerSample&, const UiPreferences&);
    WorldNormalCameraPointerFeedback reset();
    WorldNormalCameraFeedback advance(WorldRuntime&, const UiPreferences&, const WorldNormalCameraInput&, std::chrono::milliseconds);
};
} //namespace simcoaster
