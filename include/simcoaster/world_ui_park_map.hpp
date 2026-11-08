#pragma once
#include "ui_draw.hpp"
#include "ui_preferences.hpp"
#include "world_event.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
enum class WorldUiCommand : std::int32_t;

struct WorldParkMapUiHost {
    WorldUiPanelHost panels;
    //Native map is a separate common-root UI mode: pause the World, hide its
    //persistent HUD, then restore both on close. No saved World mode is changed.
    std::function<void(bool)> map_mode;
    std::function<void(float world_x, float world_y)> focus_world;
    std::function<void(ThingId)> inspect;
    std::function<void(std::u16string_view)> tooltip;
    std::function<void(std::optional<UiPoint>)> tooltip_offsets;
};

class WorldParkMapUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldParkMapUiController(WorldRuntime&, UiRuntime&, const std::filesystem::path&, WorldParkMapUiHost);
    ~WorldParkMapUiController();
    WorldParkMapUiController(const WorldParkMapUiController&) = delete;
    WorldParkMapUiController& operator=(const WorldParkMapUiController&) = delete;
    bool command(WorldUiCommand);
    void open();
    void close();
    bool is_open() const;
    UiControl* root() const;
    void preferences(const UiPreferences&);
    void handle_event(const WorldEvent&);
    void advance();
    //Draw the actual map raster/markers before this separate root's UI art.
    void append(UiDrawList&, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
