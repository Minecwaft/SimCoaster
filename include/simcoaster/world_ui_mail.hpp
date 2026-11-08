#pragma once
#include "ui_draw.hpp"
#include "world_event.hpp"
#include "world_mail_ui.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
enum class WorldUiCommand : std::int32_t;

struct WorldMailUiHost {
    WorldUiPanelHost panels;
    //All navigation uses this typed dispatcher after the native close-first
    //sequence. No MailMessage pointer escapes its World-owned mailbox.
    std::function<void(const WorldMailUiAction&)> dispatch;
    std::function<bool()> command_menu_visible;
};

class WorldMailUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldMailUiController(WorldRuntime&,
                          UiRuntime&,
                          UiControl* primary_root,
                          const std::filesystem::path&,
                          WorldMailUiHost);
    ~WorldMailUiController();
    WorldMailUiController(const WorldMailUiController&) = delete;
    WorldMailUiController& operator=(const WorldMailUiController&) = delete;
    bool command(WorldUiCommand);
    void open();
    bool is_open() const;
    void close();
    void advance();
    void handle_event(const WorldEvent&);
    void append(UiDrawList&, UiControl&, UiRect, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
