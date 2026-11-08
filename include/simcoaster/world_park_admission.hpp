#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
//Park Gates VAR_COMMAND values in the installed gate scripts. The transition
//command is a visual departure/transition request, not a change to
//mParkClosed.
enum class ParkGateCommand : std::int32_t {
    close = 0,
    open = 1,
    transition = 2
};
bool is_park_object_open_eligible(AttractionWorld&, ParkObjectRuntimeView&);
//Game.exe 0x64F640. Ordinary repeated requests still broadcast Tag19 3/4;
//gate commands and object open/close only run when the saved state changes.
void handle_world_park_open_close(WorldRuntime&, World&, bool should_open, bool transition_mode = false);
} //namespace simcoaster
