#pragma once
#include "world_domains_fwd.hpp"

namespace simcoaster {
struct Stream;
//The section wrapper includes the outer game-save tag. Legacy pointer words
//are presence markers, not host addresses; variable records use explicit wire widths.
bool write_rsse_script_save_section(ScriptWorld&, Stream&);
bool restore_rsse_script_save_section(ScriptWorld&, Stream&);
void restore_pending_rsse_occlusion_regions(ScriptWorld&);
} //namespace simcoaster
