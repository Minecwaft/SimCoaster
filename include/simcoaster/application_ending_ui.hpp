#pragma once
#include "world_mail_ui.hpp"
#include "world_ui_panel.hpp"

namespace simcoaster {
struct WorldRuntime;
enum class ApplicationEndingScene {
    bankruptcy,
    failed_challenges,
    congratulations
};

struct ApplicationEndingUiHost {
    WorldUiPanelHost panels;
    std::function<std::array<std::uint32_t, 2>()> viewport;
    std::function<void(bool)> command_families_enabled;
    //Native scene entry dismisses the editor/modal roots and resets the
    //persistent action group before hiding both persistent roots (event6).
    std::function<void()> prepare_scene;
    std::function<void(bool primary, bool secondary)> persistent_visibility;
    std::function<bool()> right_mouse_cancel_enabled;
    //649E60: music level0 and Kids request disabled, after deferred removals.
    std::function<void()> failure_audio;
    std::function<void(const WorldMailUiAction&)> mail_action;
};

//Category25 modal mail (526CD0/526960) and its original failure/victory
//scenes (50AFC0/50B330). Copies the mailbox presentation and retains only
//tag+creation-sequence identity for actions; no mail pointer escapes a call.
class ApplicationEndingUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationEndingUiController(WorldRuntime&,
                                  UiRuntime&,
                                  UiControl* primary,
                                  const std::filesystem::path& game_root,
                                  ApplicationEndingUiHost,
                                  std::string language = "American");
    ~ApplicationEndingUiController();
    ApplicationEndingUiController(const ApplicationEndingUiController&) = delete;
    ApplicationEndingUiController& operator=(const ApplicationEndingUiController&) = delete;
    //Called after World event delivery: consumes genuine WorldRuntime notices and
    //updates overlay bounds when the actual viewport/font group changes.
    void advance();
    void open(WorldMailIdentity);
    void close();
    bool is_open() const;
    bool scene_active() const;
    UiControl* root() const;
};
} //namespace simcoaster
