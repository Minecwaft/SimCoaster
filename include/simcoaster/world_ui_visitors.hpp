#pragma once
#include "ui_control.hpp"
#include "ui_draw.hpp"
#include "ui_preferences.hpp"
#include "world_event.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
enum class WorldUiCommand : std::int32_t;

class WorldVisitorsUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldVisitorsUiController(WorldRuntime&, UiRuntime&, UiControl* primary_root, const std::filesystem::path&, WorldUiPanelHost);
    ~WorldVisitorsUiController();
    WorldVisitorsUiController(const WorldVisitorsUiController&) = delete;
    WorldVisitorsUiController& operator=(const WorldVisitorsUiController&) = delete;
    bool command(WorldUiCommand);
    bool open() const;
    void close();
    void close_context();
    void open_statistics();
    void open_list();
    void open_profile(ThingId);
    void open_locator(ThingId = 0);
    void open_context(ThingId, UiPoint);
    bool dismiss_outside(UiPoint);
    void preferences(const UiPreferences&);
    //Event33 must arrive synchronously before physical removal/ThingId reuse.
    void handle_event(const WorldEvent&);
    void advance();
    ThingId locator_target() const;
    void append(UiDrawList&, UiControl&, UiRect, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
