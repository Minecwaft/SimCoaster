#pragma once
#include "mesh_scene.hpp"
#include "park_authoring.hpp"
#include "terrain_grid.hpp"
#include <functional>
#include <windows.h>

namespace simcoaster {
class UiCursorWin32;

//Windows input/menu adapter. Existing camera and staff handlers receive every
//event that this adapter does not consume.
class ParkAuthoringFrontend {
    struct State;
    std::unique_ptr<State> state_;

public:
    ParkAuthoringFrontend(ParkRuntime&, SceneCamera&, TerrainGridView, const std::filesystem::path& original_save, HWND, bool diagnostic_ui = true, std::function<void()> release_pointer = {}, UiCursorWin32* cursors = nullptr);
    ~ParkAuthoringFrontend();
    bool message(HWND, UINT, WPARAM, LPARAM);
    //Root506280 has already installed/admitted this map handler. Preserve
    //that press independently of the previous frame's UI/Staff hover hint.
    void begin_world_map_press(HWND, std::array<std::int32_t, 2> pixel, WorldCellCoordinates, WorldInputModifierMask);
    //Interrupt a held gesture without delivering a click/release to the tool.
    //A construction press can be consumed before generic host button tracking.
    void cancel_pointer_input();
    //The portable owner has already completed the transition. Drop old host
    //gestures and mirror it without cancelling/reselecting the new handler.
    void synchronize_from_world();
    bool pointer_active() const;
    void note_external_change();
    bool select_tool(ParkAuthoringTool);
    bool delete_object(ThingId);
    bool buy_object(ThingId);
    void frame(HWND, std::uint32_t width, std::uint32_t height, bool staff_editing, bool paused, bool path_hover_inspection = false);
};
} //namespace simcoaster
