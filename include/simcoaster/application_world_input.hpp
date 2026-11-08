#pragma once
#include "world_normal_camera_input.hpp"
#include <array>

namespace simcoaster {
//This is host ownership, not an original game mode or a serialized value.
//Blocked includes an inactive application, disabled command families and an
//exclusive frontend/Map/Postcard/Ending/Tip owner. Special retains the existing
//POI/ride camera adapter; it does not disable the native normal-zoom wheel.
enum class ApplicationWorldInputOwner {
    blocked,
    normal_camera,
    special_camera
};
enum class ApplicationWorldPointerEvent {
    move,
    left_down,
    left_up,
    right_down,
    right_up,
    middle_down,
    middle_up,
    wheel
};

//Observe the ACTUAL downstream owners before synchronizing. Each button is
//world_buttons_down OR world_button_gestures, including consumed authoring
//presses and excluding UI-owned buttons.
//A geometry drag retains its existing NativeCoasterEditor cancellation API.
struct ApplicationWorldInputCapture {
    std::array<bool, 2> world_buttons{}; //left, right
    bool coaster_geometry{}, coaster_geometry_active{};
};

struct ApplicationWorldInputCleanup {
    std::array<bool, 2> clear_world_buttons{};
    bool cancel_authoring_pointer{}, cancel_coaster_geometry{};
    bool clear_camera_keys{}, release_pointer{};
};
//Handler delivery follows 4D2260's held-time filter. WorldRuntime-menu admission
//follows the earlier 5070A0 path and is carried independently below. Neither
//may be inferred from a raw release after cancellation/owner handoff.
enum class ApplicationWorldInputDelivery {
    none,
    right_down,
    right_release
};

struct ApplicationWorldPointerFeedback {
    bool consumed{}, capture_pointer{}, release_pointer{};
    std::array<bool, 2> clear_world_buttons{};
    bool cancel_interaction{}, wheel{}, context_menu_release{};
    ApplicationWorldInputDelivery delivery{};
};

struct ApplicationWorldCameraFeedback {
    WorldNormalCameraFeedback camera;
    ApplicationWorldInputCleanup cleanup;
};

//Shared production application boundary. This owns normal RMB/MMB state and
//admission; existing Staff, construction and coaster controllers retain their
//gestures. The Win32 integration must:
//* synchronize BEFORE UI/coaster dispatch and again after controller advance
//(automatic Ending notices can acquire ownership without a pointer event);
//* apply cleanup through the existing authoring capture-loss and coaster
//cancel APIs, clear indicated World/Staff bits, and release platform capture
//only if no current UI or remaining World owner needs it;
//* observe platform presses before UI and route owns_pointer_release to its
//original owner; otherwise let UI take first refusal;
//* use admits_pointer before pending-coaster activation, then call pointer
//before authoring/generic World fallback; consumed stops those routes;
//* apply cancel_interaction through the existing authoring/Staff teardown,
//THEN resolve the current map tool/handler for a right_release delivery.
//This replaces ApplicationWindow's normal_right/normal_middle and its direct
//World/menu admission conditions. It is not an extra policy beside them.
class ApplicationWorldInputRuntime {
    WorldNormalCameraInputRuntime normal_camera_;
    ApplicationWorldInputOwner owner_ = ApplicationWorldInputOwner::blocked;
    bool normal_right_{}, normal_middle_{}, special_right_handoff_{};
    //A restored/replaced World cannot inherit the platform press which loaded
    //it. A fresh UI press owns its release; a new admitted World press clears
    //the later fallback suppression for that button.
    std::array<bool, 3> suppressed_release_{ true, true, true }; //left, right, middle
    std::array<bool, 3> cleanup_release_{ true, true, true };
    bool cleanup_only_release(ApplicationWorldPointerEvent) const;

public:
    void configure_balance(std::string_view text, bool preserve);

    ApplicationWorldInputOwner owner() const {
        return owner_;
    }

    bool normal_pointer_capture() const {
        return normal_right_ || normal_middle_;
    }

    //Pending coaster-source activation precedes normal pointer delivery, but
    //cancelled/inherited releases must never activate it. Use this SAME owner
    //before activating a source, not another platform availability predicate.
    bool admits_pointer(ApplicationWorldPointerEvent) const;
    //Call for physical presses BEFORE UI dispatch. A fresh UI-owned press must
    //retain its release even when an older World gesture was interrupted.
    void observe_platform_press(ApplicationWorldPointerEvent);
    bool owns_pointer_release(ApplicationWorldPointerEvent) const;
    ApplicationWorldInputCleanup synchronize(ApplicationWorldInputOwner, const ApplicationWorldInputCapture&);
    //Capture loss, cancellation, source teardown and explicit modal acquisition
    //use cleanup, never synthetic handler button-up/click delivery.
    ApplicationWorldInputCleanup cancel(const ApplicationWorldInputCapture&);
    ApplicationWorldPointerFeedback
    pointer(ApplicationWorldPointerEvent, const WorldNormalCameraPointerSample&, const UiPreferences&);
    ApplicationWorldCameraFeedback
    advance_normal_camera(WorldRuntime&, const UiPreferences&, WorldNormalCameraInput, std::chrono::milliseconds);
};
} //namespace simcoaster
