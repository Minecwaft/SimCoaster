#pragma once
#include "ui_draw.hpp"
#include "ui_preferences.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
struct WorldEvent;
class WorldParkObjectUiRuntime;
enum class WorldUiCommand : std::int32_t;

//Owns the original park-object control trees and their staged gameplay model,
//including auxiliary pickers, context/name dialogs and detached previews.
class WorldParkObjectUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldParkObjectUiController(WorldRuntime&, UiRuntime&, UiControl* primary_root, const std::filesystem::path&, WorldUiPanelHost);
    ~WorldParkObjectUiController();
    WorldParkObjectUiController(const WorldParkObjectUiController&) = delete;
    WorldParkObjectUiController& operator=(const WorldParkObjectUiController&) = delete;
    bool command(WorldUiCommand);
    bool open() const;
    void close();
    void close_context();
    void open_object(ThingId);
    void open_context(ThingId, UiPoint, bool filtered = false);
    void open_world_context(ThingId, UiPoint, std::int32_t family, std::uint32_t mesh_slot = 0);
    void open_cell_context(CellId, UiPoint);
    void open_statistics(ThingId);
    void open_opinions(ThingId);
    void open_upgrade(ThingId);
    void open_research();
    void open_research_assignment(); //DAVE: primary-slot worker picker, no return.
    void preferences(const UiPreferences&);
    void advance();
    void retire(const ThingHeader&);
    void handle_event(const WorldEvent&);
    bool dismiss_outside(UiPoint);
    bool simulation_paused() const;
    WorldParkObjectUiRuntime& model();
    void append(UiDrawList&, UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
