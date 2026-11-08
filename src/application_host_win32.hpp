#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/application_configuration.hpp"
#include "simcoaster/application_debug_selector.hpp"
#include "simcoaster/application_ending_ui.hpp"
#include "simcoaster/application_postcard_ui.hpp"
#include "simcoaster/application_session.hpp"
#include "simcoaster/application_startup.hpp"
#include "simcoaster/application_tip_ui.hpp"
#include "simcoaster/application_world_entry.hpp"
#include "simcoaster/application_world_input.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/mesh_playback.hpp"
#include "simcoaster/mesh_renderer.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/park_authoring_win32.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/sam_configuration.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/terrain_texture.hpp"
#include "simcoaster/ui_cursor_win32.hpp"
#include "simcoaster/ui_preferences.hpp"
#include "simcoaster/win32_input_capture.hpp"
#include "simcoaster/windows_resource_ids.h"
#include "simcoaster/world_camera.hpp"
#include "simcoaster/world_camera_picker.hpp"
#include "simcoaster/world_cursor_actions.hpp"
#include "simcoaster/world_cursor_pick.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mail_ui.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_normal_camera_input.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_scene.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_staff_view.hpp"
#include "simcoaster/world_surface.hpp"
#include "simcoaster/world_ui_build.hpp"
#include "simcoaster/world_ui_management.hpp"
#include "simcoaster/world_ui_objects.hpp"
#include "simcoaster/world_ui_park_map.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/world_ui_shortcut_actions.hpp"
#include "simcoaster/world_ui_shortcuts.hpp"
#include "simcoaster/world_ui_staff.hpp"
#include "simcoaster/world_ui_visitors.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace simcoaster {
struct ApplicationInputAcceptance;

namespace win32_host {
enum class StaffInputKind {
    default_press,
    left_down,
    left_up,
    right_down,
    right_up,
    menu,
    cancel
};

struct StaffInput {
    StaffInputKind kind;
    int x {}, y {};
    WorldInputModifierMask modifiers = WorldInputModifierMask::none;
};

struct LocalWindowPointer {
    std::array<int, 2> last_warp {};
    std::uint64_t warp_count {};
};

struct WindowCursorOverride {
    UiControl* root {};
    std::uint64_t lifetime {};
    UiCursorId id = UiCursorId::normal;
};

struct WindowState {
    SceneCamera camera;
    std::uint32_t width { 1100 }, height { 760 };
    bool paused {}, capture {};
    std::optional<unsigned> clip;
    int mouse_x {}, mouse_y {};
    bool orbit {}, pan {};
    bool staff_controls {}, staff_editing {}, pointer_dragged {};
    bool path_hover_inspection {};
    std::function<bool(int, int)> prepare_default_world_press;
    std::vector<StaffInput> staff_input;
    std::function<void(HWND)> drain_world_input;
    bool draining_queued_input {};
    ParkAuthoringFrontend* authoring {};
    WorldUiRuntime* ui {};
    WorldRuntime* ui_context {};
    ParkRuntime* park {};
    bool coaster_geometry_capture {}, coaster_camera_gesture {};
    std::array<bool, 2> world_button_gestures {};
    std::array<bool, 2> world_buttons_down {};
    bool camera_mouse_look {};
    bool ui_transition_cursor_hidden {};
    std::array<bool, 3> ui_buttons {};
    std::exception_ptr ui_error;
    bool ui_commands_enabled = true;
    bool ui_camera_changed {};
    const UiPreferences* preferences {};
    std::array<bool, 10> staff_camera_keys {}, previous_staff_camera_keys {};
    UiCursorWin32* cursor_assets {};
    std::optional<WindowCursorOverride> frontend_cursor;
    std::function<bool(UINT, WPARAM, LPARAM)> application_input;
    std::function<bool(const WorldUiAction&)> application_action;
    std::function<bool(UINT, WPARAM, LPARAM)> application_world_input;
    bool application_pointer_capture {};
    bool suspend_world_input {};
    //Noninteractive hosts still exercise cursor feedback, without moving the
    //Windows desktop cursor. Normal application windows use the OS backend.
    std::optional<LocalWindowPointer> local_pointer;
};

bool set_window_pointer_position(WindowState& state, HWND window, std::array<int, 2> position);
void process_ui_actions(ParkRuntime&, WindowState&, HWND, const UiPreferences&);
void commit_queued_input(WindowState& state, HWND window);
void queue_world_input(WindowState& state, HWND window, StaffInput input);
bool platform_input_boundary(UINT message);
void cancel_world_button_gestures(WindowState& state);
bool typed_world_pointer_owner(const WindowState& state);
bool coaster_geometry_active(const WindowState& state);
bool world_pointer_message(UINT message);
bool host_pointer_capture_required(const WindowState& state);
void release_host_pointer_capture(const WindowState& state, HWND window);
bool coaster_geometry_message(WindowState& state, HWND window, UINT message, WPARAM wp, LPARAM lp);
UiControl* ui_pointer_control(const WindowState& state);
bool ui_owns_pointer(const WindowState& state);
bool client_owns_pointer(HWND window);
bool install_ui_cursor(WindowState& state);
bool ui_message(WindowState& state, HWND window, UINT message, WPARAM wp, LPARAM lp);
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

struct Window {
    HWND handle {};
    explicit Window(WindowState& state, const wchar_t* title = L"SimCoaster rebuild | drag: orbit | right drag: pan | wheel: zoom | " L"Space: pause | C/I/M: animation | F12: capture");
    ~Window();
};

//Only the platform input boundary lives here. All Staff menus, forms, names,
//confirmations, training choices and previews belong to original UI controls.
struct StaffWorldInput {
    ParkRuntime& park;
    WorldRuntime& c;
    WindowState& window;
    TerrainGridView terrain;
    bool cursor_owned {};
    WorldCursorActions actions;
    explicit StaffWorldInput(ParkRuntime& owner, WindowState& state, const TerrainGridView& grid);
    bool default_owner() const;
    bool idle_path_tool() const;
    bool prepare_press(int x, int y);
    void clear_cursor();
    bool cursor(int x, int y, bool refresh_hover = true);
    void open_context(int x, int y);
    void update(HWND handle);
};

std::string theme_from_path(const std::filesystem::path& source);
void request(MeshPlaybackInstance& playback, unsigned category, const MeshPlaybackClocks& clocks);
void apply_ui_camera(ParkRuntime& park, WindowState& state);
bool live_action_receiver(WorldRuntime& c, const WorldUiAction& action, bool staff);
void staff_camera_key(ParkRuntime& park, WindowState& state, const WorldUiAction& action);
void advance_staff_camera(ParkRuntime& park, WindowState& state, HWND window, std::chrono::milliseconds elapsed);
void apply_camera_input_feedback(ParkRuntime& park, WindowState& state, HWND window);
void follow_staff_locator(ParkRuntime& park, WindowState& state);
void append_locator_outline(ParkRuntime& park, MeshScene& scene);
void process_ui_actions(ParkRuntime& park, WindowState& state, HWND window, const UiPreferences& limits);
ApplicationUiError application_error(const ApplicationSessionError& error);

class ApplicationWindow {
    friend struct ::simcoaster::ApplicationInputAcceptance;
    std::filesystem::path root;
    WindowState& window;
    HWND handle;
    std::unique_ptr<MeshRenderer>& renderer;
    ApplicationConfiguration configuration;
    RootSamConfiguration sam_configuration;
    ApplicationPostcards postcards;
    std::unique_ptr<ApplicationSession> session;
    std::unique_ptr<ApplicationFrontendController> frontend;

    struct ViewResumeState {
        std::optional<ApplicationFrontendResumeState> frontend;
        bool primary_visible {}, secondary_visible {}, commands_enabled {};
    };

    std::optional<ViewResumeState> outgoing_views;
    std::unique_ptr<ApplicationGameUiController> game_ui;
    std::unique_ptr<ApplicationCoasterSourceController> coaster_sources;
    std::unique_ptr<ApplicationPostcardUiController> postcard_ui;
    std::unique_ptr<ApplicationEndingUiController> ending;
    std::unique_ptr<ApplicationTipUiController> tips;
    ApplicationDebugState debug_state;
    std::unique_ptr<ApplicationDebugSelectorController> debug_selector;
    std::unique_ptr<WorldUiShortcuts> shortcuts;
    std::unique_ptr<WorldParkMapUiController> park_map;
    std::unique_ptr<StaffWorldInput> staff;
    std::unique_ptr<ParkAuthoringFrontend> authoring;
    std::unique_ptr<ApplicationWorldInputRuntime> world_input_owner;
    bool cleaning_world_input {};
    UiControl* canvas {};
    std::function<void(WorldRuntime&, const WorldEvent&)> previous_event;
    std::array<unsigned, 6> pauses {};
    bool postcard_previous_presentation = true;
    bool map_active {}, loading {}, loading_presentation {}, loading_painted {}, close_after_loading {};
    bool active = true, booted {}, system_quit {};
    bool graphics_recreating {};
    std::string view_recovery_failure;
    std::optional<ManagementGraphicsPreferences> pending_graphics;
    std::optional<ApplicationStartupPresentation> startup;
    ApplicationMoviePlayer movie;
    UiDrawList draw;
    MeshScene scene;
    EffectScene screen_particles;
    std::uint32_t render_width {}, render_height {};
    std::chrono::steady_clock::time_point loading_tick {};
    const char* frame_stage = "startup";

    ParkRuntime& park();
    void require_bound_views() const;
    WorldRuntime& context();
    bool paused() const;
    void pause(std::size_t owner, bool hold);
    bool frontend_active() const;
    void persistent(bool visible);
    void sync_canvas();
    void command_families(bool enabled);
    void resume_transition();
    WorldUiPanelHost panels();
    void report(const PostcardError& error);
    bool result(std::expected<void, ApplicationSessionError> value);
    void inspect(ThingId id);
    void close_auxiliary(bool include_frontend = false);
    void open_coaster_source(bool from_profile);
    void bind_management();
    void bind_game_ui();
    void bind_frontend();
    void bind_map();
    void bind_postcards();
    std::vector<std::uint64_t> pending_send_failures;
    std::u16string load_application_error_text(std::uint32_t row);
    void bind_coaster_source();
    void bind_ending();
    void bind_tips();
    bool shortcut_action(WorldUiShortcutAction selected, std::int32_t argument);
    void bind_shortcuts();
    void bind_world(ParkRuntime* value, ApplicationSessionPage page);
    void release_world(ParkRuntime* old) noexcept;
    bool action(const WorldUiAction& action);
    static std::optional<ApplicationWorldPointerEvent> pointer_event(UINT message);
    ApplicationWorldInputCapture world_input_capture() const;
    ApplicationWorldInputOwner current_world_input_owner() const;
    void release_unused_pointer_capture();
    void apply_world_input_cleanup(const ApplicationWorldInputCleanup& cleanup);
    void synchronize_world_input();
    void cancel_world_pointer_input();
    bool input(UINT message, WPARAM wp, LPARAM lp);
    bool world_input(UINT message, WPARAM wp, LPARAM lp);
    bool world_input_available() const;
    void advance_normal_camera(std::chrono::milliseconds elapsed);
    void resize_renderer();
    void pump_messages();
    void loading_pump();
    void begin_loading(bool presentation = true);
    void end_loading();
    std::expected<void, ApplicationSessionError> save_configuration();
    void apply_graphics();

public:
    ApplicationWindow(std::filesystem::path directory, WindowState& state, HWND target, std::unique_ptr<MeshRenderer>& graphics, ApplicationConfiguration settings, UiPreferences defaults);
    ~ApplicationWindow();
    void boot();
    void report_runtime_failure(std::exception_ptr failure) const noexcept;
    int run(const std::optional<std::filesystem::path>& initial, std::string theme);
};
} //namespace win32_host
} //namespace simcoaster
