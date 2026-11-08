#include "simcoaster/application_world_input.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
bool any_button(const std::array<bool, 2>& buttons) {
    return std::ranges::any_of(buttons, [](bool value) {
        return value;
    });
}
} //namespace

void ApplicationWorldInputRuntime::configure_balance(std::string_view text, bool preserve) {
    normal_camera_.configure_balance(text, preserve);
}

ApplicationWorldInputCleanup ApplicationWorldInputRuntime::cancel(const ApplicationWorldInputCapture& capture) {
    ApplicationWorldInputCleanup result;
    result.clear_world_buttons = capture.world_buttons;
    result.clear_world_buttons[0] = result.clear_world_buttons[0] || capture.coaster_geometry;
    result.clear_world_buttons[1] = result.clear_world_buttons[1] || normal_right_ || special_right_handoff_;
    //Authoring can consume its press before WindowState's generic button bits
    //are written. Its existing capture-loss path must therefore run even when
    //those bits are empty; it cancels only an actually armed construction drag.
    result.cancel_authoring_pointer = true;
    result.cancel_coaster_geometry = capture.coaster_geometry;
    result.clear_camera_keys = true;
    result.release_pointer = true;
    for (std::size_t i = 0; i < capture.world_buttons.size(); ++i) {
        cleanup_release_[i] = cleanup_release_[i] || result.clear_world_buttons[i];
    }
    cleanup_release_[2] = cleanup_release_[2] || normal_middle_;
    //An interrupted authoring/UI handoff may not have populated generic World
    //button bits. No unmatched release after cancellation may create an action;
    //a genuinely new admitted press clears its own suppression below.
    suppressed_release_.fill(true);
    normal_camera_.reset();
    normal_right_ = normal_middle_ = special_right_handoff_ = false;
    return result;
}

ApplicationWorldInputCleanup ApplicationWorldInputRuntime::synchronize(ApplicationWorldInputOwner next, const ApplicationWorldInputCapture& capture) {
    if (next == ApplicationWorldInputOwner::blocked) {
        const bool needs_cleanup = owner_ != next || normal_right_ || normal_middle_ || special_right_handoff_ || any_button(capture.world_buttons) || capture.coaster_geometry;
        owner_ = next;
        if (needs_cleanup) {
            return cancel(capture);
        }
        ApplicationWorldInputCleanup result;
        result.clear_camera_keys = true;
        return result;
    }
    ApplicationWorldInputCleanup result;
    if (capture.coaster_geometry && !capture.coaster_geometry_active) {
        //The tool can finish/cancel synchronously during another input callback.
        //Its old capture and release must not become a new construction click.
        result.cancel_coaster_geometry = result.release_pointer = true;
        result.clear_world_buttons[0] = suppressed_release_[0] = true;
        cleanup_release_[0] = true;
    }
    if (owner_ == ApplicationWorldInputOwner::normal_camera && next == ApplicationWorldInputOwner::special_camera) {
        //Preserve ApplicationWindow's admitted camera-picker handoff. The special
        //input adapter continues to observe the physical RMB already held, while
        //its eventual release is cleanup only, not a new BASE/menu action.
        normal_camera_.reset();
        special_right_handoff_ = normal_right_;
        normal_right_ = false;
        suppressed_release_[2] = suppressed_release_[2] || normal_middle_;
        cleanup_release_[2] = cleanup_release_[2] || normal_middle_;
        result.release_pointer = result.release_pointer || (normal_middle_ && !special_right_handoff_ && !any_button(capture.world_buttons) && !capture.coaster_geometry);
        normal_middle_ = false;
    } else if (owner_ == ApplicationWorldInputOwner::special_camera && next == ApplicationWorldInputOwner::normal_camera) {
        //Only the admitted normal-camera handoff belongs to this owner. Genuine
        //special-view/Staff presses retain the existing current-handler route.
        const bool right = special_right_handoff_;
        suppressed_release_[1] = suppressed_release_[1] || right;
        cleanup_release_[1] = cleanup_release_[1] || right;
        result.clear_world_buttons[1] = right;
        result.release_pointer = result.release_pointer || (right && !capture.world_buttons[0] && !capture.coaster_geometry);
        special_right_handoff_ = false;
        normal_camera_.reset();
    }
    owner_ = next;
    return result;
}

ApplicationWorldPointerFeedback ApplicationWorldInputRuntime::pointer(ApplicationWorldPointerEvent event, const WorldNormalCameraPointerSample& sample, const UiPreferences& preferences) {
    ApplicationWorldPointerFeedback result;
    if (event == ApplicationWorldPointerEvent::left_up) {
        cleanup_release_[0] = false;
    } else if (event == ApplicationWorldPointerEvent::right_up) {
        cleanup_release_[1] = false;
    } else if (event == ApplicationWorldPointerEvent::middle_up) {
        cleanup_release_[2] = false;
    }
    //UI-owned input gets first refusal; inherited World releases are cleanup
    //before UI. A blocked World must not fall through
    //to pending-source activation, authoring, Staff inspection or generic menus,
    //including a release inherited from before an automatic Ending notice.
    if (owner_ == ApplicationWorldInputOwner::blocked) {
        result.consumed = true;
        if (event == ApplicationWorldPointerEvent::left_down) {
            suppressed_release_[0] = cleanup_release_[0] = true;
        } else if (event == ApplicationWorldPointerEvent::right_down) {
            suppressed_release_[1] = cleanup_release_[1] = true;
        } else if (event == ApplicationWorldPointerEvent::middle_down) {
            suppressed_release_[2] = cleanup_release_[2] = true;
        } else if (event == ApplicationWorldPointerEvent::left_up) {
            suppressed_release_[0] = false;
        } else if (event == ApplicationWorldPointerEvent::right_up) {
            suppressed_release_[1] = false;
            result.clear_world_buttons[1] = true;
        } else if (event == ApplicationWorldPointerEvent::middle_up) {
            suppressed_release_[2] = false;
        }
        return result;
    }
    if (event == ApplicationWorldPointerEvent::wheel) {
        //466B40 changes the stored normal zoom even during an admitted POI/ride
        //view. Exclusive frontend/UI ownership, above, remains a separate gate.
        result.consumed = result.wheel = true;
        return result;
    }
    std::size_t button {};
    bool down {}, up {};
    match (event) {
        case ApplicationWorldPointerEvent::left_down  => down = true;
        case ApplicationWorldPointerEvent::left_up    => up = true;
        case ApplicationWorldPointerEvent::right_down => {
            button = 1;
            down = true;
        }
        case ApplicationWorldPointerEvent::right_up => {
            button = 1;
            up = true;
        }
        case ApplicationWorldPointerEvent::middle_down => {
            button = 2;
            down = true;
        }
        case ApplicationWorldPointerEvent::middle_up => {
            button = 2;
            up = true;
        }
        case ApplicationWorldPointerEvent::move or ApplicationWorldPointerEvent::wheel => {
        }
        case _ => {
        }
    }
    //A new physical press starts a new gesture even if a cancelled old release
    //was consumed by UI or delivered to another window while deactivated.
    if (down) {
        observe_platform_press(event);
        suppressed_release_[button] = false;
        if (button == 1) {
            special_right_handoff_ = false;
        }
    }
    if (up && suppressed_release_[button]) {
        suppressed_release_[button] = false;
        result.consumed = true;
        if (button < result.clear_world_buttons.size()) {
            result.clear_world_buttons[button] = true;
        }
        return result;
    }
    if (event == ApplicationWorldPointerEvent::right_up && special_right_handoff_) {
        special_right_handoff_ = false;
        result.consumed = result.clear_world_buttons[1] = true;
        result.release_pointer = !normal_middle_;
        return result;
    }
    if (owner_ == ApplicationWorldInputOwner::special_camera) {
        return result; //Existing special camera/Staff/coaster routes retain input.
    }
    if (button == 0 && (down || up)) {
        return result; //Construction, Staff and coaster retain their left owner.
    }
    if (event == ApplicationWorldPointerEvent::middle_down || event == ApplicationWorldPointerEvent::middle_up) {
        if (up && !normal_middle_) {
            return result;
        }
        normal_middle_ = down;
        (void)normal_camera_.pointer(down ? WorldNormalCameraPointerEvent::middle_down : WorldNormalCameraPointerEvent::middle_up, sample, preferences);
        result.consumed = true;
        result.capture_pointer = down;
        result.release_pointer = up && !normal_right_;
        return result;
    }
    if (event == ApplicationWorldPointerEvent::right_up && !normal_right_) {
        return result;
    }
    const auto previous_right = normal_right_;
    const auto native = normal_camera_.pointer(event == ApplicationWorldPointerEvent::right_down ? WorldNormalCameraPointerEvent::right_down : event == ApplicationWorldPointerEvent::right_up ? WorldNormalCameraPointerEvent::right_up : WorldNormalCameraPointerEvent::move, sample, preferences);
    if (event == ApplicationWorldPointerEvent::right_down) {
        normal_right_ = true;
    } else if (event == ApplicationWorldPointerEvent::right_up) {
        normal_right_ = false;
    }
    result.consumed = down || up || previous_right;
    result.capture_pointer = native.capture_pointer;
    result.release_pointer = native.release_pointer && !normal_middle_;
    result.clear_world_buttons[1] = native.release_pointer;
    result.cancel_interaction = native.cancel_interaction;
    result.context_menu_release = native.context_menu_release;
    if (native.handler_right_down) {
        result.delivery = ApplicationWorldInputDelivery::right_down;
    } else if (native.handler_right_up) {
        result.delivery = ApplicationWorldInputDelivery::right_release;
    }
    //The handler's held-time filter does not suppress the preceding menu
    //attempt in5070A0. Both still require this admitted physical gesture.
    return result;
}

void ApplicationWorldInputRuntime::observe_platform_press(ApplicationWorldPointerEvent event) {
    match (event) {
        case ApplicationWorldPointerEvent::left_down   => cleanup_release_[0] = false;
        case ApplicationWorldPointerEvent::right_down  => cleanup_release_[1] = false;
        case ApplicationWorldPointerEvent::middle_down => cleanup_release_[2] = false;
        case _                                         => {
        }
    }
}

bool ApplicationWorldInputRuntime::cleanup_only_release(ApplicationWorldPointerEvent event) const {
    match (event) {
        case ApplicationWorldPointerEvent::left_up   => return cleanup_release_[0];
        case ApplicationWorldPointerEvent::right_up  => return cleanup_release_[1];
        case ApplicationWorldPointerEvent::middle_up => return cleanup_release_[2];
        case _                                       => return false;
    }
}

bool ApplicationWorldInputRuntime::owns_pointer_release(ApplicationWorldPointerEvent event) const {
    if (cleanup_only_release(event)) {
        return true;
    }
    match (event) {
        case ApplicationWorldPointerEvent::right_up  => return normal_right_ || special_right_handoff_;
        case ApplicationWorldPointerEvent::middle_up => return normal_middle_;
        case _                                       => return false;
    }
}

bool ApplicationWorldInputRuntime::admits_pointer(ApplicationWorldPointerEvent event) const {
    if (owner_ == ApplicationWorldInputOwner::blocked) {
        return false;
    }
    match (event) {
        case ApplicationWorldPointerEvent::left_up   => return !suppressed_release_[0];
        case ApplicationWorldPointerEvent::right_up  => return !suppressed_release_[1] && !special_right_handoff_;
        case ApplicationWorldPointerEvent::middle_up => return !suppressed_release_[2];
        case _                                       => return true;
    }
}

ApplicationWorldCameraFeedback ApplicationWorldInputRuntime::advance_normal_camera(WorldRuntime& context, const UiPreferences& preferences, WorldNormalCameraInput input, std::chrono::milliseconds elapsed) {
    if (owner_ != ApplicationWorldInputOwner::normal_camera) {
        throw std::logic_error("Normal camera advanced without World input ownership");
    }
    input.middle_button = normal_middle_;
    ApplicationWorldCameraFeedback result;
    result.camera = normal_camera_.advance(context, preferences, input, elapsed);
    if (!input.active) {
        ApplicationWorldInputCapture capture;
        capture.world_buttons[1] = normal_right_;
        result.cleanup = cancel(capture);
    } else if (result.camera.release_pointer) {
        suppressed_release_[1] = suppressed_release_[1] || normal_right_;
        result.cleanup.clear_world_buttons[1] = normal_right_;
        result.cleanup.release_pointer = normal_right_ && !normal_middle_;
        normal_right_ = false;
    }
    return result;
}
} //namespace simcoaster
