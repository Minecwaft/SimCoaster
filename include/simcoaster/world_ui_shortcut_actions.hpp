#pragma once
#include "world_ui_shortcuts.hpp"

namespace simcoaster {
class ParkRuntime;
//Native gameplay effects shared by original keyboard callbacks. Presentation
//actions (dialogs, HUD, special-view exit) remain with their actual UI owner.
//Unsupported presentation actions return false; genuine engine errors surface.
bool apply_world_ui_shortcut_action(ParkRuntime&, WorldUiShortcutAction, std::int32_t argument = 0);
} //namespace simcoaster
