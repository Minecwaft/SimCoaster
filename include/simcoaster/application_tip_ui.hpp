#pragma once
#include "campaign_profile.hpp"
#include "world_ui_panel.hpp"

namespace simcoaster {
struct ApplicationTipUiHost {
    WorldUiPanelHost panels;
    //The Session owns these values across source Worlds and profile changes.
    std::function<CampaignTipHistory&()> history;
    std::function<bool()> enabled;
    std::function<void(bool)> set_enabled;
    std::function<void(bool)> pause;
    std::function<void(bool)> command_families_enabled;
    std::function<void()> resume_ui_transition;
};

//Original5922C0/592040/592560. Source activation calls show(false) only on
//State10's restore/reset path, excluding Intro, validation/rebuild and a
//pending source chooser. The keyboard/Leigh developer page call show(true).
class ApplicationTipUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationTipUiController(WorldRuntime&, UiRuntime&, UiControl* primary, const std::filesystem::path& game_root, ApplicationTipUiHost, std::string language = "American");
    ~ApplicationTipUiController();
    ApplicationTipUiController(const ApplicationTipUiController&) = delete;
    ApplicationTipUiController& operator=(const ApplicationTipUiController&) = delete;
    bool show(bool force = false);
    void next();
    //Destruction/replacement/Escape close without the mission continuation;
    //the original close/okay command and event5 dispatch that continuation.
    void close();
    bool is_open() const;
    UiControl* root() const;
    std::optional<std::uint32_t> current_tip() const;
};
} //namespace simcoaster
