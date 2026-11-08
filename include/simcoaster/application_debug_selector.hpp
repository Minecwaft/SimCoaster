#pragma once
#include "world_ui_panel.hpp"
#include <expected>

namespace simcoaster {
enum class ApplicationDebugPage : std::uint8_t {
    bullfrog,
    nick,
    ben,
    john,
    jansuz,
    ed,
    dave,
    chris,
    richard,
    leigh,
    will_b,
    old_panel
};
std::u16string_view application_debug_page_name(ApplicationDebugPage);
enum class ApplicationDebugCommand : std::uint8_t {
    none,
    skip_mission,
    promote,
    raise_light,
    lower_light,
    award,
    advisor95,
    offer_challenge,
    open_drinks,
    close_drinks,
    game_over_mail,
    reload_challenges,
    open_research,
    rebuild_staff_slots,
    assign_research,
    sound,
    music_up,
    music_down,
    promotion_mail,
    button_window,
    inspection,
    spawn_guard,
    spawn_researcher,
    outbox,
    dialup,
    refresh_lighting,
    toggle_special,
    toggle_challenges,
    tip,
    unlock_areas,
    training_mail,
    wage_mail,
    win_game,
    shares10,
    silence_advisor,
    focus_atm,
    share_price5,
    annual_report,
    intro_mail,
    objective_mail,
    spam_test,
    ticket,
    obstacle_message,
    open_obstacle,
    send_entertainer,
    source_water,
    source_arabian,
    source_science,
    build,
    statistics,
    finance
};

//Original process globals, independent of park/profile serialization.
struct ApplicationDebugState {
    std::int32_t annual_report_year = 2000, objective_mail_index{}, music_level = 5, sort_key = 1;
    bool force_special{}, always_win{};
};

void bind_application_debug_state(WorldRuntime&, const ApplicationDebugState&);
//Executes World commands; false identifies a presentation/session command
//owned by the application host. Failures throw to the controller boundary.
bool execute_application_debug_command(WorldRuntime&, ApplicationDebugState&, ApplicationDebugCommand);

struct ApplicationDebugSelectorHost {
    WorldUiPanelHost panels;
    ApplicationDebugState* persistent{};
    UiControl* inspection_parent{}; //Native persistent primary root.
    std::function<std::expected<void, std::string>(ApplicationDebugCommand)> execute;
    std::function<void(std::string_view)> report_error;
    std::function<bool(const UiEvent&)> key;
};

//Developer-only54AB20/531BC0/532240, with original page-specific commands.
class ApplicationDebugSelectorController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationDebugSelectorController(WorldRuntime&, UiRuntime&, UiControl* primary, ApplicationDebugSelectorHost);
    ~ApplicationDebugSelectorController();
    ApplicationDebugSelectorController(const ApplicationDebugSelectorController&) = delete;
    ApplicationDebugSelectorController& operator=(const ApplicationDebugSelectorController&) = delete;
    bool open();
    void close();
    bool is_open() const;
    UiControl* root() const;
    const std::optional<std::string>& last_error() const;
};
} //namespace simcoaster
