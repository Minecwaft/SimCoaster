#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_state.hpp"

namespace simcoaster {
std::uint32_t read_rsse_instruction_word(RsseScriptState&);
std::int32_t resolve_rsse_operand(const RsseScriptState&, std::uint32_t);
void step_rsse_instruction(ScriptWorld&, RsseScriptState&);
//The scheduler's instruction-slice portion. Cadence, action attachment updates
//and stopped-state destruction are separate runtime work, not implicit no-ops.
void run_rsse_instruction_slice(ScriptWorld&, RsseScriptState&);
} //namespace simcoaster
