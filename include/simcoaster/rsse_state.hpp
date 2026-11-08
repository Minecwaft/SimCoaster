#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
RsseScriptState* find_rsse_script_state_by_id(const ScriptQuery&, std::uint32_t);
std::uint32_t get_rsse_coaster_runtime_map_id(const ScriptQuery&, std::uint32_t script_id);
std::int32_t get_rsse_script_state_slot(const ScriptQuery&, const RsseScriptState*, std::int32_t);
void set_rsse_script_state_slot(ScriptWorld&, RsseScriptState*, std::int32_t, std::int32_t);
std::int16_t get_rsse_script_state_operating_speed(const ScriptQuery&, const RsseScriptState*);
void set_rsse_script_state_operating_speed(ScriptWorld&, RsseScriptState*, std::int16_t);
std::int32_t get_rsse_sound_script_slot(const ScriptQuery&, std::uint32_t, std::int32_t);
} //namespace simcoaster
