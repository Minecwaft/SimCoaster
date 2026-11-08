#include "application_host_win32.hpp"
#include "simcoaster/entity_identity.hpp"

namespace simcoaster::win32_host {
bool set_window_pointer_position(WindowState& state, HWND window, std::array<int, 2> position) {
    if (state.local_pointer) {
        state.local_pointer->last_warp = position;
        ++state.local_pointer->warp_count;
        state.mouse_x = position[0];
        state.mouse_y = position[1];
        return true;
    }
    POINT point { position[0], position[1] };
    if (!ClientToScreen(window, &point)) {
        return false;
    }
    (void)SetCursorPos(point.x, point.y);
    return true;
}

void commit_queued_input(WindowState& state, HWND window) {
    if (state.draining_queued_input || !state.ui || !state.park || !state.preferences) {
        return;
    }
    state.draining_queued_input = true;

    struct Finish {
        bool& active;

        ~Finish() {
            active = false;
        }
    } finish { state.draining_queued_input };

    for (;;) {
        //World messages are normally consumed at the end of their own platform
        //callback. Commit any older retained message before later UI actions.
        if (!state.staff_input.empty() && state.drain_world_input) {
            state.drain_world_input(window);
        }
        state.ui->drain_input();
        process_ui_actions(*state.park, state, window, *state.preferences);
        //Camera/selection actions can themselves enqueue control notifications.
        //Resolve the finite callback chain before the next platform input.
        const auto followup = state.ui->drain_input();
        if (!followup && (state.staff_input.empty() || !state.drain_world_input)) {
            break;
        }
    }
    state.ui_commands_enabled = state.park->ui_commands_enabled();
}

void queue_world_input(WindowState& state, HWND window, StaffInput input) {
    state.staff_input.push_back(input);
    try {
        commit_queued_input(state, window);
    } catch (...) {
        //World callbacks now commit at the same platform boundary as UI. Keep
        //exceptions inside WndProc and report them through the existing loop.
        state.ui_error = std::current_exception();
    }
}

bool platform_input_boundary(UINT message) {
    match (message) {
        case WM_MOUSEMOVE or WM_LBUTTONDOWN or WM_LBUTTONUP or WM_RBUTTONDOWN or WM_RBUTTONUP or WM_MBUTTONDOWN or WM_MBUTTONUP or WM_MOUSEWHEEL or WM_KEYDOWN or WM_KEYUP or WM_SYSKEYDOWN or WM_SYSKEYUP or WM_CHAR or WM_SYSCHAR or WM_COMMAND or WM_ACTIVATEAPP or WM_CANCELMODE or WM_CAPTURECHANGED => {
            return true;
        }
        case _ => return false;
    }
}

void cancel_world_button_gestures(WindowState& state) {
    state.world_button_gestures = {};
    state.world_buttons_down = {};
    if (state.ui_context) {
        state.ui_context->staff.interaction.left_button_held = state.ui_context->staff.interaction.right_button_held = false;
    }
}

bool typed_world_pointer_owner(const WindowState& state) {
    if (!state.ui_context) {
        return false;
    }
    const auto kind = world_staff_interaction_kind(*state.ui_context);
    return kind != WorldInteractionHandlerKind::default_ && kind != WorldInteractionHandlerKind::alternate;
}

bool coaster_geometry_active(const WindowState& state) {
    return state.park && state.ui_context && state.ui_context->construction.editor && state.ui_context->construction.editor->geometry_interaction_active();
}

bool world_pointer_message(UINT message) {
    return message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_CAPTURECHANGED;
}

bool host_pointer_capture_required(const WindowState& state) {
    return state.application_pointer_capture
        || state.orbit
        || state.pan
        || state.coaster_geometry_capture
        || (state.authoring && state.authoring->pointer_active())
        || std::ranges::any_of(state.world_buttons_down,
                               [](bool value) {
                                   return value;
                               })
        || std::ranges::any_of(state.world_button_gestures,
                               [](bool value) {
                                   return value;
                               })
        || std::ranges::any_of(state.ui_buttons,
                               [](bool value) {
                                   return value;
                               })
        || (state.ui && state.ui->controls().capture());
}

void release_host_pointer_capture(const WindowState& state, HWND window) {
    if (GetCapture() == window && !host_pointer_capture_required(state)) {
        ReleaseCapture();
    }
}

bool coaster_geometry_message(WindowState& state, HWND window, UINT message, WPARAM wp, LPARAM lp) {
    const auto active = coaster_geometry_active(state);
    const auto cancel = [&] {
        state.coaster_geometry_capture = false;
        if (active) {
            (void)state.park->coaster_geometry_pointer(CoasterGeometryPointerPhase::cancel, { state.mouse_x, state.mouse_y }, state.width, state.height);
        }
        release_host_pointer_capture(state, window);
    };
    if (state.coaster_geometry_capture && (!active || !state.ui_commands_enabled || message == WM_CAPTURECHANGED || message == WM_CANCELMODE || (message == WM_ACTIVATEAPP && !wp))) {
        cancel();
        return false;
    }
    if (!active || !state.ui_commands_enabled) {
        return false;
    }
    if (message != WM_MOUSEMOVE && message != WM_LBUTTONDOWN && message != WM_LBUTTONUP) {
        return false;
    }
    //Right/Alt gestures retain the common camera route. Only a genuine left
    //press arms a geometry drag; neither terrain construction nor Staff
    //inspection may consume the corresponding release, even in live mode0.
    if (!state.coaster_geometry_capture && ((GetKeyState(VK_MENU) < 0) || state.pan || state.orbit)) {
        return false;
    }
    state.mouse_x = static_cast<short>(LOWORD(lp));
    state.mouse_y = static_cast<short>(HIWORD(lp));
    state.park->set_render_camera(state.camera);
    state.camera = state.park->render_camera();
    const auto phase = message == WM_LBUTTONDOWN ? CoasterGeometryPointerPhase::press : message == WM_LBUTTONUP ? CoasterGeometryPointerPhase::release : CoasterGeometryPointerPhase::hover;
    (void)state.park->coaster_geometry_pointer(phase, { state.mouse_x, state.mouse_y }, state.width, state.height);
    if (message == WM_LBUTTONDOWN) {
        state.coaster_geometry_capture = true;
        acquire_window_pointer_capture(window);
    } else if (message == WM_LBUTTONUP) {
        state.coaster_geometry_capture = false;
        release_host_pointer_capture(state, window);
    }
    return true;
}

UiControl* ui_pointer_control(const WindowState& state) {
    if (!state.ui || !state.width || !state.height) {
        return nullptr;
    }
    const auto& runtime = state.ui->controls();
    if (auto* captured = runtime.capture()) {
        return captured;
    }
    return runtime.hit_test({ static_cast<std::int32_t>(static_cast<std::int64_t>(state.mouse_x) * 2048 / state.width), static_cast<std::int32_t>(static_cast<std::int64_t>(state.mouse_y) * 1536 / state.height) });
}

bool ui_owns_pointer(const WindowState& state) {
    const auto* control = ui_pointer_control(state);
    return control && control != state.ui->primary_root() && control != state.ui->secondary_root();
}

bool client_owns_pointer(HWND window) {
    if (GetCapture() == window) {
        return true;
    }
    const auto* state = reinterpret_cast<const WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state && state->local_pointer) {
        RECT client {};
        return GetClientRect(window, &client) && PtInRect(&client, { state->mouse_x, state->mouse_y });
    }
    POINT point {};
    RECT client {};
    if (!GetCursorPos(&point) || WindowFromPoint(point) != window || !ScreenToClient(window, &point) || !GetClientRect(window, &client)) {
        return false;
    }
    return PtInRect(&client, point) != FALSE;
}

bool install_ui_cursor(WindowState& state) {
    if (!state.cursor_assets) {
        return false;
    }
    state.cursor_assets->set_visible(!state.camera_mouse_look && !state.ui_transition_cursor_hidden);
    auto* control = ui_pointer_control(state);
    const bool specific = control && control != state.ui->primary_root() && control != state.ui->secondary_root();
    if (control && control->cursor_id >= 0) {
        const auto index = static_cast<std::uint32_t>(control->cursor_id);
        state.cursor_assets->set(index < UiCursorWin32::size() ? static_cast<UiCursorId>(index) : UiCursorId::normal);
        return true;
    }
    if (specific) {
        state.cursor_assets->set(UiCursorId::normal);
        return true;
    }
    //Native4D0960 consumes WM_SETCURSOR instead of allowing DefWindowProc
    //to replace the selected game cursor with the window-class arrow.
    return true;
}

bool ui_message(WindowState& state, HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (!state.ui || !state.width || !state.height) {
        return false;
    }
    auto& runtime = state.ui->controls();
    const auto logical = [&](int x, int y) {
        return UiPoint { static_cast<std::int32_t>(static_cast<std::int64_t>(x) * 2048 / state.width), static_cast<std::int32_t>(static_cast<std::int64_t>(y) * 1536 / state.height) };
    };
    const auto hit = [&](UiPoint p) {
        const auto* control = runtime.hit_test(p);
        return control && control != state.ui->primary_root() && control != state.ui->secondary_root();
    };
    const auto held = [&] {
        return std::ranges::any_of(state.ui_buttons, [](bool b) {
            return b;
        });
    };
    match (message) {
        case WM_SETCURSOR => {
            if (LOWORD(lp) == HTCLIENT && install_ui_cursor(state)) {
                return true;
            }
            return false;
        }
        case WM_ACTIVATEAPP => {
            runtime.activate(wp != 0);
            if (!wp) {
                state.camera_mouse_look = false;
                if (state.cursor_assets) {
                    state.cursor_assets->reset();
                }
                state.ui_buttons = {};
                cancel_world_button_gestures(state);
                state.orbit = state.pan = false;
                state.staff_camera_keys = state.previous_staff_camera_keys = {};
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
            }
            return false;
        }
        case WM_CANCELMODE => {
            runtime.cancel_pointer_input();
            state.ui_buttons = {};
            cancel_world_button_gestures(state);
            state.orbit = state.pan = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            return false;
        }
        case WM_CAPTURECHANGED => {
            //Own ReleaseCapture may precede the queued completed button-up. Only
            //an interrupted gesture is cancelled, preserving that completed click.
            if (held() || runtime.capture()) {
                runtime.cancel_pointer_input();
            }
            state.ui_buttons = {};
            runtime.set_capture(nullptr);
            return false;
        }
        case WM_MOUSEMOVE => {
            const auto x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
            const auto p = logical(x, y);
            runtime.pointer_move(p);
            const bool consumed = held() || runtime.capture() || ((!state.application_pointer_capture && !state.orbit && !state.pan) && hit(p));
            if (consumed) {
                state.mouse_x = x;
                state.mouse_y = y;
            }
            return consumed;
        }
        case WM_LBUTTONDOWN or WM_LBUTTONUP or WM_RBUTTONDOWN or WM_RBUTTONUP or WM_MBUTTONDOWN or WM_MBUTTONUP => {
            const auto button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ? 0u : message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 1u : 2u;
            const auto down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
            const auto p = logical(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
            if (down && !hit(p) && !runtime.capture() && state.ui->dismiss_outside(p)) {
                state.ui_buttons[button] = true;
                acquire_window_pointer_capture(window);
                return true;
            }
            if (!(down ? hit(p) || runtime.capture() : state.ui_buttons[button] || runtime.capture())) {
                return false;
            }
            state.ui_buttons[button] = down;
            runtime.pointer_button(button, down, p);
            if (down) {
                acquire_window_pointer_capture(window);
            } else {
                release_host_pointer_capture(state, window);
            }
            return true;
        }
        case WM_MOUSEWHEEL => {
            POINT pixel { static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
            ScreenToClient(window, &pixel);
            const auto p = logical(pixel.x, pixel.y);
            if (!hit(p) && !runtime.capture()) {
                return false;
            }
            runtime.pointer_move(p);
            runtime.pointer_wheel(static_cast<short>(HIWORD(wp)) / WHEEL_DELTA, p);
            return true;
        }
        case WM_KEYDOWN or WM_KEYUP or WM_SYSKEYDOWN or WM_SYSKEYUP or WM_CHAR => {
            const auto* focus = runtime.focus();
            const bool panel = state.ui_context && state.ui_context->ui.active_root;
            if (!focus && !panel) {
                return false;
            }
            bool camera_handled = false;
            if (message == WM_CHAR) {
                runtime.character(static_cast<char16_t>(wp));
            } else {
                const auto modifiers = (GetKeyState(VK_SHIFT) < 0 ? 3u : 0u) | (GetKeyState(VK_CONTROL) < 0 ? 12u : 0u) | (GetKeyState(VK_MENU) < 0 ? 48u : 0u);
                runtime.key(static_cast<std::uint32_t>(wp) << 8, message == WM_KEYDOWN || message == WM_SYSKEYDOWN, world_ui_platform_key_flags(static_cast<std::uintptr_t>(lp), modifiers));
                camera_handled = world_camera_key_matches(static_cast<std::uint32_t>(wp), modifiers);
            }
            //The persistent root receives native routed keys, while the host's
            //camera/authoring shortcuts remain usable until a panel/edit owns them.
            return camera_handled || panel || (focus && focus != state.ui->primary_root() && focus != state.ui->secondary_root());
        }
        case WM_SYSCHAR => {
            const auto* focus = runtime.focus();
            //TranslateMessage has already queued this after WM_SYSKEYDOWN. A panel
            //that owns those keys also consumes the mnemonic/system-menu followup.
            const auto modifiers = (GetKeyState(VK_SHIFT) < 0 ? 3u : 0u) | (GetKeyState(VK_CONTROL) < 0 ? 12u : 0u) | (GetKeyState(VK_MENU) < 0 ? 48u : 0u);
            return (wp == VK_SPACE && world_camera_key_matches(VK_SPACE, modifiers)) || (state.ui_context && state.ui_context->ui.active_root) || (focus && focus != state.ui->primary_root() && focus != state.ui->secondary_root());
        }
        case _ => return false;
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_CAPTURECHANGED && retained_window_pointer_capture(window, lparam)) {
        return 0; //Same HWND still owns every current UI/World gesture.
    }
    if (state->ui_error && platform_input_boundary(message)) {
        return 0;
    }
    if (state->ui_context) {
        //503970 is sampled at the platform boundary; portable panel/save
        //callbacks consume the same current modifiers without polling Win32.
        const auto modifiers = (GetAsyncKeyState(VK_SHIFT) < 0 ? 3u : 0u) | (GetAsyncKeyState(VK_CONTROL) < 0 ? 12u : 0u) | (GetAsyncKeyState(VK_MENU) < 0 ? 48u : 0u);
        state->ui_context->construction.modifiers = static_cast<WorldInputModifierMask>(modifiers);
    }
    try {
        if (platform_input_boundary(message) && !state->suspend_world_input) {
            commit_queued_input(*state, window);
        }
        if (state->application_input && state->application_input(message, wparam, lparam)) {
            return 0;
        }
        if (state->coaster_geometry_capture && coaster_geometry_message(*state, window, message, wparam, lparam)) {
            return 0;
        }
        if (ui_message(*state, window, message, wparam, lparam)) {
            commit_queued_input(*state, window);
            return message == WM_SETCURSOR ? TRUE : 0;
        }
        if (state->application_world_input && state->application_world_input(message, wparam, lparam)) {
            return 0;
        }
        //4D6160 activates a staged CDK choice only when World input actually
        //resumes. Merely accepting its dialog must not mutate the old tool.
        if (!state->application_world_input && state->park && state->ui_commands_enabled && world_pointer_message(message) && !ui_owns_pointer(*state) && state->park->activate_pending_coaster_source()) {
            state->park->authoring().synchronize_from_world();
            if (state->authoring) {
                state->authoring->note_external_change();
            }
        }
        if (coaster_geometry_message(*state, window, message, wparam, lparam)) {
            return 0;
        }
    } catch (...) {
        state->ui_error = std::current_exception();
        return 0;
    }
    if (!state->ui_commands_enabled && (message == WM_COMMAND || message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MOUSEWHEEL || message == WM_KEYDOWN || message == WM_KEYUP)) {
        return 0;
    }
    try {
        if (message == WM_LBUTTONDOWN && state->ui_commands_enabled && state->prepare_default_world_press && state->prepare_default_world_press(static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)))) {
            state->mouse_x = static_cast<short>(LOWORD(lparam));
            state->mouse_y = static_cast<short>(HIWORD(lparam));
            state->pointer_dragged = false;
            state->world_buttons_down[0] = state->world_button_gestures[0] = true;
            queue_world_input(*state, window, { StaffInputKind::default_press, state->mouse_x, state->mouse_y, state->ui_context->construction.modifiers });
            acquire_window_pointer_capture(window);
            release_host_pointer_capture(*state, window);
            return 0;
        }
    } catch (...) {
        state->ui_error = std::current_exception();
        return 0;
    }
    if (state->authoring
        && !(world_pointer_message(message)
             && (coaster_geometry_active(*state)
                 || typed_world_pointer_owner(*state)
                 || state->path_hover_inspection
                 || std::ranges::any_of(state->world_button_gestures,
                                        [](bool down) {
                                            return down;
                                        })))
        && state->authoring->message(window, message, wparam, lparam)) {
        return 0;
    }
    match (message) {
        case WM_SIZE => {
            state->width = LOWORD(lparam);
            state->height = HIWORD(lparam);
            return 0;
        }
        case WM_CLOSE => {
            DestroyWindow(window);
            return 0;
        }
        case WM_DESTROY => {
            PostQuitMessage(0);
            return 0;
        }
        case WM_KEYDOWN => {
            if (state->application_input) {
                return 0; //Genuine application/UI key routing already ran above.
            }
            if (wparam == VK_ESCAPE) {
                if (state->staff_controls || state->staff_editing) {
                    queue_world_input(*state, window, { StaffInputKind::cancel });
                } else {
                    DestroyWindow(window);
                }
                return 0;
            }
            if (!(lparam & (std::intptr_t { 1 } << 30))) {
                if (wparam == VK_SPACE) {
                    state->paused = !state->paused;
                }
                if (wparam == VK_F12) {
                    state->capture = true;
                }
                if (wparam == 'C') {
                    state->clip = 0;
                }
                if (wparam == 'I') {
                    state->clip = 2;
                }
                if (wparam == 'M') {
                    state->clip = 5;
                }
            }
            return 0;
        }
        case WM_MOUSEWHEEL => {
            if (state->ui_context && (state->ui_context->view.flags & 0x16u)) {
                return 0; //Widget wheel routing ran first; the World view cannot zoom.
            }
            const bool alternate = state->ui_context && state->ui_context->view.camera.alternate_zoom_limits;
            state->camera.distance *= std::exp(-static_cast<float>(static_cast<short>(HIWORD(wparam))) / 120 * 0.15f);
            state->camera.distance = std::clamp(state->camera.distance, alternate ? 40.f : state->preferences ? state->preferences->minimum_zoom : 1.f, alternate ? 300.f : state->preferences ? state->preferences->maximum_zoom : 100000.f);
            state->ui_camera_changed = state->ui != nullptr;
            return 0;
        }
        case WM_LBUTTONDOWN or WM_RBUTTONDOWN => {
            const auto button = message == WM_LBUTTONDOWN ? 0u : 1u;
            state->world_buttons_down[button] = true;
            const bool default_press = button == 0 && state->park && state->ui_context && !typed_world_pointer_owner(*state) && !coaster_geometry_active(*state) && state->park->authoring().tool() == ParkAuthoringTool::inspect && !(state->ui_context->view.flags & 0x16u);
            state->world_button_gestures[button] = default_press || typed_world_pointer_owner(*state);
            state->coaster_camera_gesture = coaster_geometry_active(*state);
            state->mouse_x = static_cast<short>(LOWORD(lparam));
            state->mouse_y = static_cast<short>(HIWORD(lparam));
            state->pointer_dragged = false;
            if (state->world_button_gestures[button]) {
                queue_world_input(*state, window, { default_press ? StaffInputKind::default_press : message == WM_LBUTTONDOWN ? StaffInputKind::left_down : StaffInputKind::right_down, state->mouse_x, state->mouse_y, state->ui_context ? state->ui_context->construction.modifiers : WorldInputModifierMask::none });
            } else {
                if (!state->application_input) {
                    if (button == 0) {
                        state->orbit = true;
                    } else {
                        state->pan = true;
                    }
                }
            }
            acquire_window_pointer_capture(window);
            return 0;
        }
        case WM_LBUTTONUP or WM_RBUTTONUP => {
            const auto button = message == WM_LBUTTONUP ? 0u : 1u;
            const auto was_down = state->world_buttons_down[button];
            state->world_buttons_down[button] = false;
            const auto world_owned = state->world_button_gestures[button];
            state->world_button_gestures[button] = false;
            if (world_owned) {
                queue_world_input(*state, window, { message == WM_LBUTTONUP ? StaffInputKind::left_up : StaffInputKind::right_up, static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)) });
            } else if (button == 1 && was_down && !state->pointer_dragged && !state->coaster_camera_gesture && !coaster_geometry_active(*state)) {
                queue_world_input(*state, window, { StaffInputKind::menu, state->mouse_x, state->mouse_y });
            }
            if (button == 0) {
                state->orbit = false;
            } else {
                state->pan = false;
            }
            state->coaster_camera_gesture = false;
            release_host_pointer_capture(*state, window);
            return 0;
        }
        case WM_CAPTURECHANGED => {
            state->orbit = state->pan = false;
            cancel_world_button_gestures(*state);
            return 0;
        }
        case WM_MOUSEMOVE => {
            const auto x = static_cast<short>(LOWORD(lparam)), y = static_cast<short>(HIWORD(lparam));
            const auto dx = static_cast<float>(x - state->mouse_x), dy = static_cast<float>(y - state->mouse_y);
            state->mouse_x = x;
            state->mouse_y = y;
            state->pointer_dragged |= dx != 0 || dy != 0;
            const bool special_camera = state->ui_context && (state->ui_context->view.flags & 0x16u);
            if (state->orbit && !special_camera) {
                state->camera.yaw -= dx * 0.006f;
                state->camera.pitch = std::clamp(state->camera.pitch + dy * 0.006f, 0.05f, 1.5f);
            }
            if (state->pan && !special_camera) {
                const auto scale = state->camera.distance * 0.0015f;
                state->camera.target[0] += (-dx * std::cos(state->camera.yaw) + dy * std::sin(state->camera.yaw)) * scale;
                state->camera.target[2] += (dx * std::sin(state->camera.yaw) + dy * std::cos(state->camera.yaw)) * scale;
                if (state->ui_context) {
                    state->ui_context->view.flags &= ~4096u;
                    state->ui_context->view.camera.damped_pan_x = state->ui_context->view.camera.damped_pan_z = 0;
                }
            }
            if (!special_camera && (state->orbit || state->pan) && (dx != 0 || dy != 0)) {
                state->ui_camera_changed = state->ui != nullptr;
            }
            return 0;
        }
        case _ => return DefWindowProcW(window, message, wparam, lparam);
    }
}

std::string theme_from_path(const std::filesystem::path& source) {
    bool next = false;
    for (const auto& part : source) {
        auto text = part.native_encoded_string();
        for (auto& c : text) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c + ('a' - 'A'));
            }
        }
        if (next) {
            return text;
        }
        next = text == "levels";
    }
    return "science";
}

void request(MeshPlaybackInstance& playback, unsigned category, const MeshPlaybackClocks& clocks) {
    if (!playback.assets->animations.find(category)) {
        return;
    }
    const auto selected = request_mesh_playback(playback, 0, { { category, 0 }, 3, 1 }, clocks);
    if (!selected) {
        throw std::runtime_error(describe_mesh_error(selected.error()));
    }
}

Window::Window(WindowState& state, const wchar_t* title) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW type {};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(SIMCOASTER_APPLICATION_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(SIMCOASTER_APPLICATION_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    type.lpszClassName = L"SimCoasterRebuildWindow";
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register renderer window");
    }
    RECT bounds { 0, 0, static_cast<LONG>(state.width), static_cast<LONG>(state.height) };
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    handle = CreateWindowExW(0, type.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, &state);
    if (!handle) {
        throw std::runtime_error("Cannot create renderer window");
    }
}

Window::~Window() {
    if (IsWindow(handle)) {
        DestroyWindow(handle);
    }
}

StaffWorldInput::StaffWorldInput(ParkRuntime& owner, WindowState& state, const TerrainGridView& grid) : park(owner), c(owner.context()), window(state), terrain(grid), actions(owner) {
    initialize_world_staff_interaction(c);
}

bool StaffWorldInput::default_owner() const {
    const auto kind = world_staff_interaction_kind(c);
    return (kind == WorldInteractionHandlerKind::default_ || kind == WorldInteractionHandlerKind::alternate) && !park.construction().state.shortcut_rearm_mode && (park.authoring().tool() == ParkAuthoringTool::inspect || (window.path_hover_inspection && idle_path_tool())) && !coaster_geometry_active(window) && !(c.view.flags & 0x16u);
}

bool StaffWorldInput::idle_path_tool() const {
    return c.staff.interaction.pointer_mode_code == 2 && park.authoring().tool() == ParkAuthoringTool::path && c.construction.mode == 1 && (c.construction.path_start_x == -1 || c.construction.path_start_y == -1) && !coaster_geometry_active(window) && !(window.authoring && window.authoring->pointer_active()) && !typed_world_pointer_owner(window) && !(c.view.flags & 0x16u);
}

bool StaffWorldInput::prepare_press(int x, int y) {
    if (park.construction().state.shortcut_rearm_mode) {
        cursor(x, y, false);
        park.construction().resume_shortcut_tool(c);
        park.authoring().synchronize_from_world();
    }
    if (idle_path_tool()) {
        cursor(x, y);
    } else {
        window.path_hover_inspection = false;
    }
    return default_owner();
}

void StaffWorldInput::clear_cursor() {
    actions.clear();
    if (default_owner()) {
        c.staff.interaction.cursor_presentation = 0;
        park.ui().primary_root()->cursor_id = 0;
    }
}

bool StaffWorldInput::cursor(int x, int y, bool refresh_hover) {
    WorldCursorPick details;
    const auto hit = pick_world_interaction_cursor(c, park.render_camera(), terrain, { x, y }, window.width, window.height, &details);
    update_world_staff_interaction_cursor(c, hit.value_or(StaffInteractionCursor {}));
    const bool old_override = window.path_hover_inspection;
    if (!idle_path_tool()) {
        window.path_hover_inspection = false;
    } else if (!c.ui.active_root) {
        const auto cell = hit ? hit->cell : CellId {};
        const auto zero = static_cast<std::uint32_t>(cell) - 1u;
        window.path_hover_inspection = cell && find_world_park_object_at_map_cell(c, static_cast<std::int32_t>(zero & 127u), static_cast<std::int32_t>(zero >> 7));
    }
    if (window.path_hover_inspection) {
        park.construction().state.faces.clear();
        park.construction().invalidate_interaction_cost_quote();
    } else if (old_override && idle_path_tool()) {
        c.staff.interaction.cursor_presentation = 3;
        park.ui().primary_root()->cursor_id = 3;
    }
    if (refresh_hover && default_owner() && !c.ui.active_root) {
        //The user approved selecting the displayed body even when its upper
        //sprite projects beyond the terrain cell beneath the pointer.
        const auto mobile = park.pick_mobile({ x, y }, window.width, window.height);
        actions.hover(hit.value_or(StaffInteractionCursor {}), mobile, details.mesh ? details.mesh->mesh_slot : 0);
    } else if (refresh_hover) {
        clear_cursor();
    }
    return hit.has_value();
}

void StaffWorldInput::open_context(int x, int y) {
    if (c.ui.active_root) {
        //Primary-root5070A0 handles an admitted world RMB release before
        //505DF0: dismiss Staff locator first, then Visitor locator, otherwise
        //close a transient context menu. The same click never picks behind it.
        auto& controls = park.ui().controls();
        if (auto* staff_locator = controls.find_recursive(park.ui().primary_root(), 32000)) {
            controls.dispatch(staff_locator, 4); //58CB70
        } else if (auto* visitor_locator = controls.find_recursive(park.ui().primary_root(), 39000)) {
            controls.dispatch(visitor_locator, 4); //597580
        } else {
            park.ui().close_context_menus();
        }
        return;
    }
    if (!default_owner()) {
        return;
    }
    //Native505DF0 uses the retained hover snapshot BEFORE handler-up
    //updates the map cursor. Never repick a different release-time owner.
    actions.context_menu({ static_cast<std::int32_t>(std::int64_t { x } * 2048 / window.width), static_cast<std::int32_t>(std::int64_t { y } * 1536 / window.height) });
}

void StaffWorldInput::update(HWND handle) {
    const auto old_kind = world_staff_interaction_kind(c);
    cursor_owned |= old_kind != WorldInteractionHandlerKind::default_ && old_kind != WorldInteractionHandlerKind::alternate;
    for (const auto input : std::exchange(window.staff_input, {})) {
        if (input.kind == StaffInputKind::default_press) {
            //Clear any inspect latch from the diagnostic/fallback frontend.
            if (window.authoring) {
                window.authoring->cancel_pointer_input();
            }
            if (!default_owner() || c.ui.active_root) {
                window.world_buttons_down[0] = window.world_button_gestures[0] = false;
                continue;
            }
            cursor(input.x, input.y);
            //Only the explicit map disposition gives a new handler this DOWN.
            const auto result = actions.press(input.modifiers);
            if (window.authoring) {
                window.authoring->note_external_change();
            }
            if (result == WorldCursorPressResult::forward_map_action && window.authoring && park.authoring().tool() != ParkAuthoringTool::inspect && !coaster_geometry_active(window)) {
                //The old root forwards this same DOWN after installing a map
                //handler. Its frontend then owns drag/UP/capture cancellation.
                window.world_buttons_down[0] = window.world_button_gestures[0] = false;
                window.authoring->begin_world_map_press(handle, { input.x, input.y }, WorldCellCoordinates { static_cast<std::int32_t>((c.staff.interaction.cursor.cell - 1u) % 128u), static_cast<std::int32_t>((c.staff.interaction.cursor.cell - 1u) / 128u) }, input.modifiers);
            } else if (result == WorldCursorPressResult::forward_default && default_owner() && !c.ui.active_root) {
                world_staff_interaction_button(c, WorldPointerButton::left, true);
            } else {
                //Ctrl-copy/coaster explicitly swallow DOWN. Their inherited UP
                //must not place a copy or trigger a second inspection action.
                window.world_buttons_down[0] = window.world_button_gestures[0] = false;
                c.staff.interaction.left_button_held = false;
            }
            continue;
        }
        if (input.kind == StaffInputKind::menu) {
            open_context(input.x, input.y);
            continue;
        }
        if (window.authoring) {
            window.authoring->note_external_change();
        }
        if (input.kind == StaffInputKind::cancel) {
            clear_cursor();
            park.ui().staff_controller().close();
            close_staff_area_selection_panel(c, false);
            cancel_world_staff_interaction(c);
            continue;
        }
        if (input.kind == StaffInputKind::right_up) {
            actions.end_context_press();
        }
        const bool on_map = cursor(input.x, input.y);
        const bool mobile_press = input.kind == StaffInputKind::right_down && default_owner() && !c.ui.active_root && actions.begin_context_press();
        if (on_map || mobile_press || input.kind == StaffInputKind::right_up) {
            world_staff_interaction_button(c, input.kind == StaffInputKind::left_down || input.kind == StaffInputKind::left_up ? WorldPointerButton::left : WorldPointerButton::right, input.kind == StaffInputKind::left_down || input.kind == StaffInputKind::right_down);
        } else if (input.kind == StaffInputKind::left_up) {
            c.staff.interaction.left_button_held = false;
        }
        if (const auto* area = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value); area && !area->panel_open) {
            cancel_world_staff_interaction(c);
        }
    }
    //Native4D4450 deletes the patrol modal during right-up. Reconcile its
    //UI owner before another queued platform message can hit that old root.
    if (old_kind == WorldInteractionHandlerKind::staff_area && world_staff_interaction_kind(c) != WorldInteractionHandlerKind::staff_area) {
        park.ui().staff_controller().advance();
    }
    if (!ui_owns_pointer(window)) {
        cursor(window.mouse_x, window.mouse_y);
    } else {
        clear_cursor();
    }
    const auto kind = world_staff_interaction_kind(c);
    const bool owns_cursor = kind != WorldInteractionHandlerKind::default_ && kind != WorldInteractionHandlerKind::alternate;
    const bool geometry_cursor = coaster_geometry_active(window);
    if (!geometry_cursor && (owns_cursor || cursor_owned || default_owner())) {
        park.ui().primary_root()->cursor_id = static_cast<std::int32_t>(c.staff.interaction.cursor_presentation);
    }
    cursor_owned = owns_cursor;
    const auto* area = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value);
    window.staff_editing = area && area->panel_open;
    window.staff_controls = window.staff_editing || kind == WorldInteractionHandlerKind::held_staff || kind == WorldInteractionHandlerKind::hired_staff;
    //Native84D236 applies a control's changed asset only when that control
    //is the mouse target. In special cameras the secondary root owns21;
    //installing the primary tool asset here makes it alternate every frame.
    if (window.cursor_assets && client_owns_pointer(handle) && ui_pointer_control(window) == park.ui().primary_root() && !geometry_cursor) {
        const auto presentation = c.staff.interaction.cursor_presentation;
        window.cursor_assets->set(presentation < UiCursorWin32::size() ? static_cast<UiCursorId>(presentation) : UiCursorId::normal);
    }
}

void apply_ui_camera(ParkRuntime& park, WindowState& state) {
    const auto& c = park.context();
    const auto& native = c.view.camera;
    state.camera.target = native.focus_position;
    state.camera.yaw = native.heading_radians;
    state.camera.pitch = native.pitch_radians;
    state.camera.distance = c.view.mobile.camera_zoom;
    state.camera.near_plane = native.near_clip_distance;
    state.camera.far_plane = std::max(native.far_clip_distance, state.camera.near_plane + 1.f);
    park.set_render_camera(state.camera);
    state.camera = park.render_camera();
    park.commit_ui_camera();
}

bool live_action_receiver(WorldRuntime& c, const WorldEntityTarget& action, bool staff) {
    return staff ? action.get<StaffRuntimeRecord>(c.entities) != nullptr : action.get<ParkObjectRuntimeView>(c.entities) != nullptr;
}

void staff_camera_key(ParkRuntime& park, WindowState& state, const WorldStaffCameraAction& action) {
    const auto raw = static_cast<std::uint32_t>(action.key);
    const auto key = raw > 255 ? raw >> 8 : raw;
    const bool down = action.event == 65546;
    std::optional<std::size_t> index;
    match (key) {
        case VK_LEFT     => index = 0;
        case VK_RIGHT    => index = 1;
        case VK_ADD      => index = 2;
        case VK_SUBTRACT => index = 3;
        case VK_NUMPAD8  => index = 4;
        case VK_NUMPAD2  => index = 5;
        case VK_NUMPAD4  => index = 6;
        case VK_NUMPAD6  => index = 7;
        case VK_UP       => index = 8;
        case VK_DOWN     => index = 9;
        case VK_HOME     => {
            if (!down) {
                auto& camera = park.context().view.camera;
                //Original camera-family action24 calls4667A0 on release: focus and
                //fallback heading only. It deliberately preserves the zoom.
                camera.focus_position[0] = 480.f;
                camera.focus_position[2] = 143.f;
                camera.heading_radians = camera.fallback_heading_radians;
                apply_ui_camera(park, state);
            }
        }
        case VK_SPACE => {
            if (!down) {
                auto& override_limits = park.context().view.camera.alternate_zoom_limits;
                override_limits = !override_limits; //Native466870, exact modifiers63.
            }
        }
        case _ => {
        }
    } if (index) {
        state.staff_camera_keys[*index] = down;
    }
}

void advance_staff_camera(ParkRuntime& park, WindowState& state, HWND window, std::chrono::milliseconds elapsed) {
    auto& c = park.context();
    auto& camera = c.view.camera;
    const auto* active = park.ui().controls().focus();
    const bool keyboard_owner = active && (active == park.ui().primary_root() || active == park.ui().secondary_root() || active->id == 32000 || active->id == 97);
    WorldCameraInput input;
    input.active = GetActiveWindow() == window && park.ui_commands_enabled();
    input.pointer = { state.mouse_x, state.mouse_y };
    input.viewport = { state.width, state.height };
    input.left_button = state.world_buttons_down[0];
    input.right_button = state.world_buttons_down[1];
    if (keyboard_owner) {
        input.rotate_left = state.staff_camera_keys[0];
        input.rotate_right = state.staff_camera_keys[1];
        input.forward = state.staff_camera_keys[2] || state.staff_camera_keys[8];
        input.backward = state.staff_camera_keys[3] || state.staff_camera_keys[9];
    }
    park.set_camera_input(input);
    if (c.view.flags & 0x16u) {
        state.previous_staff_camera_keys = state.staff_camera_keys;
        if (!keyboard_owner) {
            state.staff_camera_keys = state.previous_staff_camera_keys = {};
        }
        return; //Portable native input owns POI walking and attachment look.
    }
    if (!keyboard_owner) {
        state.staff_camera_keys = {};
        state.previous_staff_camera_keys = {};
        camera.damped_pan_x = camera.damped_pan_z = 0;
        return;
    }
    //Native467930 rejects a nonpositive delta and replaces stalls >200ms
    //with100ms; camera movement consumes unscaled wall time even when paused.
    const float frame = elapsed.count() >= 200 ? 100.f : static_cast<float>(elapsed.count() + 1);
    const auto& keys = state.staff_camera_keys;
    const auto before = camera.focus_position;
    const auto heading = camera.heading_radians;
    const auto zoom = c.view.mobile.camera_zoom;
    const bool isometric = park.preferences().isometric_on != 0;
    if (isometric) {
        if (keys[0] && !state.previous_staff_camera_keys[0]) {
            camera.heading_radians += 1.5707964f;
        }
        if (keys[1] && !state.previous_staff_camera_keys[1]) {
            camera.heading_radians -= 1.5707964f;
        }
    } else {
        camera.heading_radians += (static_cast<float>(keys[0]) - keys[1]) * frame * .0020000001f;
    }
    c.view.mobile.camera_zoom = std::clamp(zoom + (static_cast<float>(keys[3] || keys[9]) - (keys[2] || keys[8])) * frame * .050000001f, camera.alternate_zoom_limits ? 40.f : park.preferences().minimum_zoom, camera.alternate_zoom_limits ? 300.f : park.preferences().maximum_zoom);
    const bool panning = keys[4] || keys[5] || keys[6] || keys[7];
    const auto blend = std::min(frame / (isometric ? 200.f : 140.f), 1.f);
    //Forecast this frame's heading for the existing pan calculation without
    //writing it twice. ParkRuntime's portable input phase commits the same
    //interpolation once before resolving the displayed native pose.
    auto pan_heading = (1.f - blend) * camera.smoothed_heading_radians + blend * camera.heading_radians;
    if (std::fabs(pan_heading - camera.heading_radians) < .0005f) {
        pan_heading = camera.heading_radians;
    }
    const auto previous_x = camera.damped_pan_x, previous_z = camera.damped_pan_z;
    if (panning) {
        c.view.flags &= ~4096u;
        //Native edge/key input is +/-1, the.95 dead zone becomes speed.5.
        const auto local_x = (static_cast<float>(keys[7]) - keys[6]) * .5f;
        const auto local_z = (static_cast<float>(keys[4]) - keys[5]) * .5f;
        const auto sine = std::sin(pan_heading);
        const auto cosine = std::cos(pan_heading);
        const auto scale = std::sqrt(c.view.mobile.camera_zoom) * .02f;
        camera.damped_pan_x = (scale * (cosine * local_x + sine * local_z) + previous_x) * .5f;
        camera.damped_pan_z = (scale * (cosine * local_z - sine * local_x) + previous_z) * .5f;
        const auto& grid = park.world_scene().terrain()->terrain->view();
        if ((before[0] < 0 && camera.damped_pan_x < 0) || (before[0] > static_cast<float>(grid.width_cells) * grid.cell_scale_x && camera.damped_pan_x > 0)) {
            camera.damped_pan_x = previous_x / (frame * .01f + 1.f);
        }
        if ((before[2] < 0 && camera.damped_pan_z < 0) || (before[2] > static_cast<float>(grid.height_cells) * grid.cell_scale_y && camera.damped_pan_z > 0)) {
            camera.damped_pan_z = previous_z / (frame * .01f + 1.f);
        }
    } else {
        camera.damped_pan_x /= frame * .01f + 1.f;
        camera.damped_pan_z /= frame * .01f + 1.f;
    }
    if (std::fabs(camera.damped_pan_x) < .0001f && std::fabs(camera.damped_pan_z) < .0001f) {
        camera.damped_pan_x = camera.damped_pan_z = 0;
    }
    camera.focus_position[0] += camera.damped_pan_x * frame;
    camera.focus_position[2] += camera.damped_pan_z * frame;
    state.previous_staff_camera_keys = keys;
    if (camera.focus_position != before || camera.heading_radians != heading || c.view.mobile.camera_zoom != zoom) {
        apply_ui_camera(park, state);
    }
}

void apply_camera_input_feedback(ParkRuntime& park, WindowState& state, HWND window) {
    const auto feedback = park.camera_input_feedback();
    state.camera_mouse_look = feedback.mouse_look_active && GetActiveWindow() == window;
    if (GetActiveWindow() != window) {
        if (state.cursor_assets) {
            state.cursor_assets->reset();
        }
        return;
    }
    if (state.cursor_assets) {
        state.cursor_assets->set_visible(!state.camera_mouse_look && !state.ui_transition_cursor_hidden);
    }
    auto position = feedback.restore_pointer;
    if (feedback.recenter_pointer) {
        position = std::array { static_cast<std::int32_t>(state.width / 2u), static_cast<std::int32_t>(state.height / 2u) };
    }
    if (position) {
        if (set_window_pointer_position(state, window, *position)) {
            state.mouse_x = (*position)[0];
            state.mouse_y = (*position)[1];
            park.ui().controls().pointer_move({ static_cast<std::int32_t>(static_cast<std::int64_t>(state.mouse_x) * 2048 / state.width), static_cast<std::int32_t>(static_cast<std::int64_t>(state.mouse_y) * 1536 / state.height) });
        }
    }
}

void follow_staff_locator(ParkRuntime& park, WindowState& state) {
    auto& c = park.context();
    const auto target = park.ui().staff_controller().locator_target();
    if ((c.view.flags & 4096u) && target && park.focus_staff_view(target)) {
        apply_ui_camera(park, state);
    } else if (const auto person = park.ui().visitor_controller().locator_target(); (c.view.flags & 4096u) && person && park.focus_person_view(person, true)) {
        apply_ui_camera(park, state);
    }
}

void append_locator_outline(ParkRuntime& park, MeshScene& scene) {
    auto& c = park.context();
    auto& controller = park.ui().staff_controller();
    if (!(c.view.flags & 4096u) || !controller.locator_target()) {
        return;
    }
    const auto bounds = controller.locator_area();
    const auto first = static_cast<std::uint16_t>(static_cast<std::uint32_t>(bounds.start_y) * 128u + static_cast<std::uint32_t>(bounds.start_x) + 1u);
    const auto last = static_cast<std::uint16_t>(static_cast<std::uint32_t>(bounds.end_y) * 128u + static_cast<std::uint32_t>(bounds.end_x) + 1u);
    if (first == 1 && last == map_cell_count) {
        return;
    }
    if (!first || first > map_cell_count || !last || last > map_cell_count) {
        throw std::out_of_range("Staff locator patrol bounds");
    }
    const int sx = (first - 1) % 128, sy = (first - 1) / 128, ex = (last - 1) % 128, ey = (last - 1) / 128;
    std::vector<StaffInteractionOverlay> outline;
    const auto line = [&](int x, int y, int end_x, int end_y) {
        const auto dx = x == end_x ? 0 : x < end_x ? 1 : -1;
        const auto dy = y == end_y ? 0 : y < end_y ? 1 : -1;
        for (;;) {
            if (is_world_map_coordinate_in_bounds(x, y)) {
                outline.push_back({ static_cast<CellId>(y * 128 + x + 1), 8 });
            }
            if (x == end_x && y == end_y) {
                break;
            }
            x += dx;
            y += dy;
        }
    };
    //Exact58BB00 four major-axis commands. Horizontal passes omit the
    //vertical corner columns whenever the stored X endpoints differ.
    line(sx == ex ? sx : sx + 1, sy, sx == ex ? ex : ex - 1, sy);
    line(ex, sy, ex, ey);
    line(sx == ex ? sx : sx + 1, ey, sx == ex ? ex : ex - 1, ey);
    line(sx, ey, sx, sy);
    append_world_runtime_overlays(c, scene, outline);
}

void process_ui_actions(ParkRuntime& park, WindowState& state, HWND window, const UiPreferences& limits) {
    auto& owner = park.ui();
    std::vector<WorldUiAction> deferred;
    auto actions = owner.take_actions();
    while (!actions.empty()) {
        for (const auto& action : actions) {
            match (action) {
                case { const WorldObjectFocusAction& payload } => {
                    auto& c = park.context();
                    if (live_action_receiver(c, payload, false)) {
                        auto* object = c.entities.payload<ParkObjectRuntimeView>(static_cast<std::size_t>(payload.id));
                        //Native466050 changes only horizontal focus. Unlike503530's
                        //mobile-camera setter, this path does not clear damped pan.
                        c.ports.camera.focus_world_view_camera_on_park_object_center(c, object);
                        apply_ui_camera(park, state);
                    }
                    continue;
                }
                case { const WorldStaffFocusAction& payload } => {
                    auto& c = park.context();
                    if (live_action_receiver(c, payload, true) && (!payload.follow || ((c.view.flags & 4096u) && owner.staff_controller().locator_target() == payload.id)) && park.focus_mobile_view(static_cast<ThingId>(payload.id), payload.follow != 0)) {
                        apply_ui_camera(park, state);
                    }
                    continue;
                }
                case { const WorldPersonFocusAction& payload } => {
                    auto& c = park.context();
                    if (const auto* target = payload.template live_payload<PersonRuntimeView>(c.entities)) {
                        const auto& person = *target;
                        const auto identity = (std::uint64_t { person.arrival_date } << 32) | person.arrival_index;
                        const bool follow = payload.follow != 0;
                        if (identity == payload.identity_generation && (!follow || ((c.view.flags & 4096u) && owner.visitor_controller().locator_target() == payload.id)) && park.focus_person_view(static_cast<ThingId>(payload.id), follow)) {
                            apply_ui_camera(park, state);
                        }
                    }
                    continue;
                }
                case { const WorldPositionFocusAction& payload } => {
                    set_world_view_camera_focus(park.context(), { static_cast<float>(payload.x), static_cast<float>(payload.y) });
                    apply_ui_camera(park, state);
                    continue;
                }
                case { const WorldStaffCameraAction& payload } => {
                    staff_camera_key(park, state, payload);
                    continue;
                }
                case { const WorldDeleteParkObjectAction& payload } => {
                    if (state.authoring && live_action_receiver(park.context(), payload, false)) {
                        (void)state.authoring->delete_object(static_cast<ThingId>(payload.id));
                    }
                    continue;
                }
                case { const WorldBuyParkObjectAction& payload } => {
                    if (state.authoring && live_action_receiver(park.context(), payload, false)) {
                        (void)state.authoring->buy_object(static_cast<ThingId>(payload.id));
                    }
                    continue;
                }
                case { WorldResetInteractionAction } => {
                    if (state.authoring) {
                        //501490 already completed the portable reset. Reselecting here
                        //used to arm the old Path/Bulldozer intent a second time.
                        state.path_hover_inspection = false;
                        state.authoring->synchronize_from_world();
                        continue;
                    }
                }
                case { WorldCenterPointerAction } => {
                    if (window) {
                        (void)set_window_pointer_position(state, window, { static_cast<int>(state.width / 2u), static_cast<int>(state.height / 2u) });
                        continue;
                    }
                }
                case { const WorldCameraAction& payload } => {
                    auto& c = park.context();
                    auto& native = c.view.camera;
                    const bool repeat = payload.event == 65544 || payload.event == 65545;
                    if (payload.control == 84) {
                        set_world_view_camera_default_scene_pose(c);
                    } else if (payload.control == 87 || payload.control == 88) {
                        const auto amount = native.isometric_mode && !repeat ? 1.5707964f : .12f;
                        native.heading_radians += payload.control == 87 ? amount : -amount;
                        if (native.isometric_mode && !repeat) {
                            native.heading_radians = std::round(native.heading_radians / 1.5707964f) * 1.5707964f;
                        }
                    } else if (payload.control == 89 || payload.control == 90) {
                        if (payload.event == 258) {
                            c.view.mobile.camera_zoom = static_cast<float>(payload.control == 89 ? limits.minimum_zoom : limits.maximum_zoom);
                        } else {
                            const auto amount = repeat ? 1.5f : 20.f;
                            c.view.mobile.camera_zoom += payload.control == 89 ? -amount : amount;
                        }
                        c.view.mobile.camera_zoom = std::clamp(c.view.mobile.camera_zoom, static_cast<float>(limits.minimum_zoom), static_cast<float>(limits.maximum_zoom));
                    } else if (payload.control != 0) {
                        deferred.push_back(action);
                        continue;
                    }
                    apply_ui_camera(park, state);
                    continue;
                }
                case { const WorldToolbarAction& payload } => {
                    if (state.authoring) {
                        //Native toolbar D/C/B select the already compiled construction
                        //modes.
                        const auto tool = payload.control == 68 ? std::optional { ParkAuthoringTool::path } : payload.control == 67 ? std::optional { ParkAuthoringTool::inspect } : payload.control == 66 ? std::optional { ParkAuthoringTool::demolish_rectangle } : std::nullopt;
                        if (tool && state.authoring->select_tool(*tool)) {
                            continue;
                        }
                    }
                }
                case _ => {
                }
            } if (state.application_action && state.application_action(action)) {
                continue;
            }
            //Remaining panel/source requests stay owned and observable until their
            //corresponding frontend controller consumes them; never discard them.
            deferred.push_back(action);
        }
        //Applying an object action can restore a camera and enqueue its native
        //view notification. Commit those followups before another World pick.
        actions = owner.take_actions();
    }
    for (const auto& action : deferred) {
        owner.request_action(action);
    }
}
} //namespace simcoaster::win32_host
