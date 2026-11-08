#pragma once
#include "world_mobile_picker.hpp"
#include "world_staff_interaction.hpp"
#include "ui_control.hpp"
#include <memory>

namespace simcoaster {
class ParkRuntime;
//Native503FD0/506280 action IDs, independent of construction modes and
//context-menu family IDs. These are transient input state, never save data.
enum class WorldCursorAction : std::uint8_t {
    none = 0,
    extend_exit_path = 1,
    extend_path = 2,
    extend_queue = 3,
    object = 4,
    track = 5,
    coaster = 6,
    visitor = 7,
    handyman = 8,
    mechanic = 9,
    entertainer = 10,
    guard = 11,
    gardener = 12,
    researcher = 13,
    ticket_booth = 14,
    entrance = 15,
    obstacle = 16,
    theme_gate = 17,
    info_desk = 23,
    litter = 33
};
enum class WorldCursorPressResult {
    forward_default,
    forward_map_action,
    consumed
};

class WorldCursorActions {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit WorldCursorActions(ParkRuntime&);
    ~WorldCursorActions();
    void hover(StaffInteractionCursor, std::optional<WorldMobilePick>, std::uint32_t mesh_slot = 0);
    void clear();
    //Keep an approved drawn-body selection through its RMB gesture: freezing
    //the walker can immediately change the frame beneath the physical pixel.
    bool begin_context_press();
    void end_context_press();
    //Distinguish the receiver of native return1. Ctrl-copy/coaster return0.
    WorldCursorPressResult press(WorldInputModifierMask);
    void context_menu(UiPoint);
};
} //namespace simcoaster
