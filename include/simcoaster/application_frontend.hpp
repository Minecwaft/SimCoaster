#pragma once
#include "mesh_model.hpp"
#include "ui_control.hpp"
#include <filesystem>
#include <memory>

namespace simcoaster {
struct WorldRuntime;
//Native campaign/CDK category domain. Resource-source indices are a distinct
//domain: Arabian0, Science1, Water2, Intro3.
enum class ApplicationThemeCategory : std::uint8_t {
    arabian,
    science,
    water
};

struct ApplicationProfileTheme {
    bool available {};
    std::uint16_t completed_challenges {};
};

struct ApplicationProfileView {
    bool present {};
    std::uint64_t generation {};
    std::u16string name;
    std::int32_t promotion {}, share_percentage {}, share_price {}, shares_owned {}, golden_tickets {};
    std::array<ApplicationProfileTheme, 3> themes {};
    std::int32_t last_mission = -1;
    ApplicationThemeCategory last_theme {};
};

struct ApplicationLobbyView {
    MeshMatrix camera_to_world {};
    std::uint32_t directional_rgb {}, ambient_rgb {};
    std::uint16_t selection_mode {};
    std::int32_t selected_area = -1;
};

struct ApplicationFrontendHost {
    //Manager has5 slots; original Profile template exposes only slots0..3.
    std::function<std::array<ApplicationProfileView, 5>()> profiles;
    //Save the previous selected profile before selecting this actual slot, as
    //native56AB40/5690A0/5695D0 do. Persistence belongs to the application
    //owner.
    std::function<bool(std::uint32_t)> activate_profile;
    std::function<bool(std::uint32_t, std::u16string_view)> create_profile;
    std::function<bool(std::uint32_t, std::uint64_t)> delete_profile;
    std::function<int()> selected_resource_source;
    std::function<void(int)> select_resource_source;
    //Cross-World actions must queue a transition, not destroy this controller
    //from inside a UiRuntime callback.
    std::function<void(int, bool)> request_source;
    std::function<void()> open_load_game;
    std::function<void()> open_coaster_source_picker;
    std::function<void()> request_quit;
    std::function<void(std::u16string, bool, std::function<void()>)> message;
    std::function<void(bool)> command_families_enabled;
    std::function<void(bool)> persistent_visible;
    std::function<void()> resume_ui_transition;
    //Apply the shared actual camera/light presentation for rendering, picking
    //and audio. Nullopt restores the host's ordinary camera and theme lighting.
    std::function<void(std::optional<ApplicationLobbyView>)> lobby_view;
    //Native5BD890/5BE810 sound entry/exit: kids0/1; Intro music21;
    //ordinary chooser music0+mute1/0 and current-source retained ambient handles
    //level0/restored configured SFX percent. The session owns ambient records.
    std::function<void(bool entering, bool intro)> lobby_sound;
    std::function<void(std::int32_t)> cursor;
};
enum class ApplicationFrontendPhase {
    none,
    intro,
    profiles,
    profile_transition,
    chooser,
    area_transition
};

struct ApplicationFrontendResumeState {
    ApplicationFrontendPhase phase {};
    std::uint16_t selection_mode {};
    bool hide_close {}, suppress_intro_buttons {};
    struct Detail;
    std::shared_ptr<const Detail> detail;
};

class ApplicationFrontendController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationFrontendController(WorldRuntime&, UiRuntime&, UiControl* routing_root, const std::filesystem::path&, ApplicationFrontendHost, std::string language = "American");
    ~ApplicationFrontendController();
    ApplicationFrontendController(const ApplicationFrontendController&) = delete;
    ApplicationFrontendController& operator=(const ApplicationFrontendController&) = delete;
    //Intro owner4798 and saved lobby103 must already exist in the genuine World.
    //The World animation owner starts4798 with native profile1/selector1; its
    //completion notification calls finish_intro, including the forced route.
    void open_intro(bool suppress_action_buttons = false);
    void finish_intro(bool force_profiles = false);
    void open_profiles();
    void open_chooser(int resource_source = 4, bool hide_close = false);
    void close();
    ApplicationFrontendResumeState capture_resume_state() const;
    //Rebuild controls after a failed application binding. The retained World
    //owns animation/RSSE progress; do not run its entry sequence again.
    void restore_resume_state(const ApplicationFrontendResumeState&);
    void advance();
    void viewport(std::uint32_t width, std::uint32_t height);
    //Key states are supplied by the existing portable UI input route; no global
    //asynchronous Win32 key polling is needed for native arrow feedback.
    bool key(std::uint32_t key, bool down, std::uint32_t modifiers = 0);
    ApplicationFrontendPhase phase() const;
    std::int32_t selected_area() const;
    std::uint16_t selection_mode() const;
    UiControl* root() const;
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
