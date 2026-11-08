#pragma once
#include "ui_control.hpp"
#include "ui_draw.hpp"
#include "world_types.hpp"
#include "world_ui_action.hpp"
#include <filesystem>

namespace simcoaster {
struct WorldRuntime;
struct ThingHeader;
struct UiPreferences;
class WorldStaffUiController;
class WorldParkObjectUiController;
class WorldBuildUiController;
class WorldCoasterEditorUiController;
enum class CoasterEditorEntryTool : std::uint32_t;
class WorldVisitorsUiController;
class WorldMailUiController;
class WorldManagementUiController;
struct WorldManagementUiHost;
struct WorldUiPanelHost;
struct WorldMailUiAction;
enum class WorldUiShortcutRoute;
class ParkConstructionRuntime;
//Native430730 camera command rows; the platform also uses this to consume
//recognized system keys before DefWindowProc opens a system menu.
bool world_camera_key_matches(std::uint32_t packed_key, std::uint32_t modifiers);
enum class WorldUiCommand : std::int32_t {
    staff_hire = 1,
    staff_list,
    training_room,
    staff_locator,
    staff_finance,
    attraction_build,
    attraction_list,
    research,
    attraction_finance,
    visitor_statistics,
    visitor_list,
    visitor_locator,
    financial_statistics,
    loans,
    shares,
    park_gates,
    park_awards,
    park_statistics,
    challenges,
    park_map,
    overview_map,
    mail,
    game_menu,
    postcard,
    coaster_source,
    help
};

//Owns the original persistent controls and World-facing panel controllers.
//Simulation controls exist headlessly; bitmap/model providers are lazy.
class WorldUiRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldUiRuntime(WorldRuntime&,
                   const std::filesystem::path& game_root,
                   std::function<void()> reset_world_interaction,
                   ParkConstructionRuntime* construction = nullptr);
    ~WorldUiRuntime();
    WorldUiRuntime(const WorldUiRuntime&) = delete;
    WorldUiRuntime& operator=(const WorldUiRuntime&) = delete;
    UiRuntime& controls();
    const UiRuntime& controls() const;
    UiControl* primary_root() const;
    UiControl* secondary_root() const;
    std::array<std::uint32_t, 2> viewport_size() const;
    WorldStaffUiController& staff_controller();
    WorldParkObjectUiController& object_controller();
    WorldBuildUiController& build_controller();
    WorldCoasterEditorUiController& coaster_editor_controller();
    WorldVisitorsUiController& visitor_controller();
    WorldMailUiController& mail_controller();
    WorldManagementUiController& management_controller();
    WorldUiPanelHost panel_host() const;
    void bind_management(WorldManagementUiHost);
    void dispatch_command(WorldUiCommand);
    void dispatch_mail_action(const WorldMailUiAction&);
    void bind_construction_actions(std::function<bool(std::uint16_t)> object,
                                   std::function<bool(const std::filesystem::path&, std::int32_t)> design,
                                   std::function<void(ThingId)> retire = {});
    void bind_object_actions(std::function<bool(ThingId)> queue,
                             std::function<bool(ThingId)> track,
                             std::function<void(ThingId)> ride,
                             std::function<bool(ThingId)> relocate,
                             std::function<bool(ThingId, CoasterEditorEntryTool)> coaster_tool = {});
    void bind_toolbar_actions(std::function<bool(std::uint32_t, bool)>);
    bool select_toolbar_tool(std::uint32_t id);
    void preferences(const UiPreferences&);
    //Shared52C4C0 BODY binding. The direct child is the scrollbar, while the
    //body retains wrapping, clipping, measured overflow and text ownership.
    void bind_scrollable_text(UiControl&, std::optional<std::uint32_t> scrollbar);
    void set_draw_underlay(std::function<void(UiDrawList&, std::uint32_t, std::uint32_t)>);
    void
        set_control_draw_hook(std::function<void(UiDrawList&, const UiControl&, UiRect, std::uint32_t, std::uint32_t)>);
    void set_shortcut_handler(std::function<bool(const UiEvent&, WorldUiShortcutRoute)>);
    void set_tooltip_override(std::optional<std::u16string>);
    void set_world_cursor_help(std::u16string);
    void set_tooltip_offsets(std::optional<UiPoint>);
    bool simulation_paused() const;
    bool dismiss_outside(UiPoint);
    //Commit queued control/application notifications without advancing UI
    //time, repeat timers, World simulation or animation.
    std::size_t drain_input();
    void advance(std::chrono::milliseconds);
    //Live owner state, separate from WorldState and configured simulation speed.
    void bind_gameplay_pause(std::function<bool()>);
    //50AA30 polls deferred application transitions before primary UI delivery.
    void bind_frame_poll(std::function<void()>);
    void begin_frame();
    //The screen-space Advisor inherits the active World/lobby ambient color;
    //its own two registered directional lights replace World directionals.
    void set_advisor_lighting(MeshPoint ambient, std::uint32_t diffuse_mask);
    void build_draw(UiDrawList&, std::uint32_t pixel_width, std::uint32_t pixel_height);
    void set_action_handler(std::function<void(const WorldUiAction&)>);
    std::vector<WorldUiAction> take_actions();
    void set_interaction_cost(std::int32_t);
    void open_objectives();
    void close_panel();
    void close_context_menus();
    void hide_persistent();
    void restore_persistent();
    UiControl* create_playback_window();
    void configure_control_particles(UiControl*);
    void animate_mail(UiControl*, std::int32_t kind, std::chrono::milliseconds interval);
    void request_action(const WorldUiAction&);
    const std::vector<std::string>& diagnostics() const;
};
} //namespace simcoaster
