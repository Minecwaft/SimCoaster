#pragma once
#include "ui_control.hpp"
#include "world_types.hpp"
#include <filesystem>

#include "world_ui_action.hpp"

namespace simcoaster {
class ParkConstructionRuntime;
class NativeCoasterEditor;
enum class CoasterEditorEntryTool : std::uint32_t;

//The persistent UI owns panel focus and external camera/construction actions.
//Concrete controllers retain their own roots, previews and staged gameplay.
struct WorldUiPanelHost {
    ParkConstructionRuntime* construction {};
    NativeCoasterEditor* native_coaster_editor {};
    std::function<void()> dismiss;
    std::function<void(UiControl*)> activate;
    std::function<void(UiControl*)> deactivate;
    //Native554DCD/501490 prepares World input for an auxiliary context menu
    //without replacing or dismissing the currently registered main panel.
    std::function<void()> prepare_context_menu;
    std::function<void()> open_objectives;
    std::function<void(const WorldUiAction&)> request;
    std::function<void(ThingId, bool)> focus_staff;
    std::function<void(ThingId, bool)> focus_person;
    std::function<void(ThingId)> focus_object;
    std::function<bool(const UiEvent&)> camera_key;
    std::function<void(ThingId)> retiring;
    std::function<bool(std::uint16_t)> begin_object_build;
    std::function<bool(const std::filesystem::path&, std::int32_t)> begin_coaster_design;
    std::function<void(ThingId)> open_object;
    std::function<void(ThingId)> open_staff_room;
    std::function<void(ThingId)> open_training_room;
    std::function<bool(ThingId)> edit_object_queue;
    std::function<bool(ThingId)> edit_object_track;
    std::function<void(ThingId, CoasterEditorEntryTool)> open_coaster_tool;
    std::function<void(ThingId, std::uint32_t)> delete_coaster_geometry;
    std::function<void(ThingId)> ride_object;
    std::function<bool(ThingId)> relocate_object;
    std::function<void(bool)> set_editor_toolbar_active;
    std::function<void()> synchronize_focus;
};
} //namespace simcoaster
