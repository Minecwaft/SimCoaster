#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_people.hpp"

namespace simcoaster {
double distance_rsse_points(const std::array<float, 3>&, const std::array<float, 3>&);
std::int32_t quantize_rsse_direction8(float axis_z, float axis_x);
void begin_rsse_walk_exit(ScriptWorld&, RsseScriptState&, std::uint32_t person_id);
std::uint32_t dequeue_rsse_walk_result(RsseScriptState&);
void start_rsse_walk_float(ScriptWorld&,
                           RsseScriptState&,
                           std::int32_t duration_ms,
                           std::uint16_t horizontal_scale,
                           std::uint16_t vertical_scale);
//Factored out of the original single-instruction interpreter.
bool step_rsse_passenger_instruction(ScriptWorld&, RsseScriptState&, RsseOpcode);
} //namespace simcoaster
