#pragma once
#include "ui_draw.hpp"
#include "world_ui_panel.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
enum class WorldUiCommand : std::int32_t;

//Original Build Attraction13000 and ready-made coaster111000 owners. Catalog
//selection remains independent of World construction until activation.
class WorldBuildUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldBuildUiController(WorldRuntime&,
                           UiRuntime&,
                           UiControl* primary_root,
                           const std::filesystem::path&,
                           WorldUiPanelHost);
    ~WorldBuildUiController();
    WorldBuildUiController(const WorldBuildUiController&) = delete;
    WorldBuildUiController& operator=(const WorldBuildUiController&) = delete;
    bool command(WorldUiCommand);
    void open_build(std::int32_t category = -1, std::uint16_t selected_definition = 0);
    void open_ready_made(std::uint16_t base_ride);
    bool open() const;
    void close();
    void advance();
    void append(UiDrawList&, UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
