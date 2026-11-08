#pragma once
#include "ui_control.hpp"
#include "ui_draw.hpp"
#include "ui_preferences.hpp"
#include "world_types.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
enum class WorldUiCommand : std::int32_t;
using StaffUiPanelHost = WorldUiPanelHost;

//Original Staff frontend across all six roles. The gameplay model owner,
//control roots, auxiliary dialogs and animated previews share one lifetime.
class WorldStaffUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldStaffUiController(WorldRuntime&, UiRuntime&, UiControl* primary_root, const std::filesystem::path&, StaffUiPanelHost);
    ~WorldStaffUiController();
    WorldStaffUiController(const WorldStaffUiController&) = delete;
    WorldStaffUiController& operator=(const WorldStaffUiController&) = delete;
    bool command(WorldUiCommand);
    bool open() const;
    void close();
    void close_context();
    void advance();
    void open_details(std::uint16_t staff);
    void open_hire(StaffType = StaffType::unsupported_index);
    void open_locator(ThingId = 0);
    void open_room(std::uint16_t room);
    void open_training(std::uint16_t room);
    void open_training_budget();
    void open_context(std::uint16_t staff, UiPoint);
    void open_wage(std::uint16_t staff, std::int32_t demanded_increment);
    void preferences(const UiPreferences&);
    ThingId locator_target() const;
    StaffAreaBounds locator_area() const;
    bool dismiss_outside(UiPoint);
    bool simulation_paused() const;
    void append(UiDrawList&, UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
