#pragma once
#include "ui_graph.hpp"
#include "ui_preferences.hpp"
#include "world_ui_panel.hpp"
#include <memory>

namespace simcoaster {
struct WorldRuntime;
struct WorldEvent;
enum class WorldUiCommand : std::int32_t;

//The original Options controls retain their three profile/resolution/device
//choices. The application maps these semantic choices to its actual renderer;
//the UI never pretends that changing a caption changed a device.
struct ManagementGraphicsPreferences {
    bool hardware = true;
    std::int32_t quality{}, resolution{}, video_device{};
};

struct ManagementGraphicsCapabilities {
    bool show_renderer_choices{};
    std::vector<std::u16string> devices;
};
enum class ManagementOptionsChange {
    live_audio,
    live_controls,
    commit,
    rollback
};

struct WorldManagementUiHost {
    WorldUiPanelHost panels;
    std::string language;
    std::function<UiPreferences()> read_options;
    std::function<void(const UiPreferences&, ManagementOptionsChange)> write_options;
    std::function<ManagementGraphicsPreferences()> read_graphics;
    std::function<ManagementGraphicsCapabilities()> graphics_capabilities;
    std::function<void(const ManagementGraphicsPreferences&, bool commit)> write_graphics;
    std::function<void(bool)> pause_options;
    std::function<void()> open_objectives;
    //Shared52C4C0 body presentation. A scrollbar ID registers its direct child;
    //nullopt unregisters before the body/control lifetime ends.
    std::function<void(UiControl&, std::optional<std::uint32_t>)> bind_scrollable_text;
};

//Persistent management pages, their original child controls and transactional
//dialogs. World and profile owners retain gameplay data; this owner retains
//only UI choices, staged edits and copied row identities.
class WorldManagementUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldManagementUiController(WorldRuntime&,
                                UiRuntime&,
                                UiControl* primary,
                                const std::filesystem::path& game_root,
                                WorldManagementUiHost);
    ~WorldManagementUiController();
    WorldManagementUiController(const WorldManagementUiController&) = delete;
    WorldManagementUiController& operator=(const WorldManagementUiController&) = delete;
    bool command(WorldUiCommand);
    void open_options();
    void open_challenges(std::optional<ChallengeScriptIndex> initial_script = {});
    void open_awards(std::optional<std::int32_t> highlighted_type = {});
    void close();
    bool open() const;
    bool simulation_paused() const;
    UiControl* root() const;
    void advance(std::chrono::milliseconds);
    void handle_event(const WorldEvent&);
    void append(UiDrawList&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
