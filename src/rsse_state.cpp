#include "simcoaster/rsse_state.hpp"

namespace simcoaster {
std::uint32_t get_rsse_coaster_runtime_map_id(const ScriptQuery& c, std::uint32_t id) {
    const auto* state = find_rsse_script_state_by_id(c, id);
    return state ? state->coaster_runtime_map_id : 0;
}

RsseScriptState* find_rsse_script_state_by_id(const ScriptQuery& c, std::uint32_t id) {
    if (!c.scripts.states.active)
        return nullptr;
    for (auto* state = c.scripts.states.head; state; state = state->next)
        if (state->id == id)
            return state;
    return nullptr;
}

std::int32_t get_rsse_script_state_slot(const ScriptQuery& c, const RsseScriptState* state, std::int32_t index) {
    return c.scripts.states.active && state && index >= 0 && static_cast<std::size_t>(index) < state->value_slots.size()
             ? state->value_slots[static_cast<std::size_t>(index)]
             : 0;
}

void set_rsse_script_state_slot(ScriptWorld& c, RsseScriptState* state, std::int32_t index, std::int32_t value) {
    if (c.scripts.states.active && state && index >= 0 && static_cast<std::size_t>(index) < state->value_slots.size())
        state->value_slots[static_cast<std::size_t>(index)] = value;
}

std::int16_t get_rsse_script_state_operating_speed(const ScriptQuery& c, const RsseScriptState* state) {
    return c.scripts.states.active && state ? state->operating_speed : 0;
}

void set_rsse_script_state_operating_speed(ScriptWorld& c, RsseScriptState* state, std::int16_t speed) {
    if (c.scripts.states.active && state)
        state->operating_speed = speed;
}

std::int32_t get_rsse_sound_script_slot(const ScriptQuery& c, std::uint32_t id, std::int32_t index) {
    const auto* primary = find_rsse_script_state_by_id(c, id);
    return primary
             ? get_rsse_script_state_slot(c, find_rsse_script_state_by_id(c, primary->sound_script_state_id), index)
             : 0;
}

RsseScriptState* world_operations::builtin::find_script(WorldRuntime& c, std::uint32_t id) {
    return find_rsse_script_state_by_id(c, id);
}

int world_operations::builtin::script_slot(WorldRuntime& c, RsseScriptState* state, int index) {
    return get_rsse_script_state_slot(c, state, index);
}

void world_operations::builtin::set_script_slot(WorldRuntime& c, RsseScriptState* state, int index, int value) {
    set_rsse_script_state_slot(c, state, index, value);
}
} //namespace simcoaster
