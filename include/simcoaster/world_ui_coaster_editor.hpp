#pragma once
#include "native_coaster_editor.hpp"
#include "world_ui_panel.hpp"
#include <memory>

namespace simcoaster {
//Original retained primary-HUD control97. Its artwork is rendered by the
//ordinary UI tree; this owner binds native tools, focus and toolbar lifetime.
class WorldCoasterEditorUiController {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldCoasterEditorUiController(WorldRuntime&,
                                   UiRuntime&,
                                   UiControl* primary,
                                   const std::filesystem::path& game_root,
                                   WorldUiPanelHost);
    ~WorldCoasterEditorUiController();
    WorldCoasterEditorUiController(const WorldCoasterEditorUiController&) = delete;
    WorldCoasterEditorUiController& operator=(const WorldCoasterEditorUiController&) = delete;
    bool open(ThingId, CoasterEditorEntryTool);
    bool open_world_target(ThingId, bool placement_entry);
    void delete_context_geometry(ThingId, std::uint32_t mesh_slot);
    bool open() const;
    //Event29 stores(id==1). Native52F570 checks this automatic-world-entry
    //gate; explicit52F870 open(id,tool) and52F330 restore do not check it.
    bool entry_blocked() const;
    ThingId target() const;
    UiControl* root() const;
    void close();
    void advance();
    void retire(ThingId);
    void handle_event(const WorldEvent&);
};
} //namespace simcoaster
