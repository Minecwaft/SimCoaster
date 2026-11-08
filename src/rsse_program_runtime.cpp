#include "simcoaster/rsse_program.hpp"
#include "simcoaster/save_schema.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <ranges>

#include "simcoaster/world.hpp"

namespace simcoaster {
RsseScriptState* create_rsse_program_state_for_owner(ScriptWorld& c, RsseProgram program, ThingId owner) {
    if (!c.scripts.states.active)
        return nullptr;
    auto instance = std::make_unique<RsseProgramInstance>();
    instance->program = std::move(program);
    const auto& h = instance->program.header;
    instance->values.resize(static_cast<std::size_t>(h.value_slot_count));
    instance->stack.resize(static_cast<std::size_t>(h.operand_stack_capacity));
    instance->limbo.resize(static_cast<std::size_t>(h.limbo_capacity));
    instance->bounce.resize(static_cast<std::size_t>(h.bounce_capacity));
    instance->walk.resize(static_cast<std::size_t>(h.walk_capacity));
    auto& state = instance->state;
    state.owner_used_thing_id = owner;
    state.script_source = instance->program.source;
    if (owner) {
        const auto* payload = c.entities.at(owner).payload;
        if (!payload)
            throw std::invalid_argument("RSSE owner is missing");
        state.mesh_instance_handle = reinterpret_cast<const ParkObjectRuntimeView*>(payload)->mesh_instance_id;
        if (state.mesh_instance_handle) {
            std::int32_t node = 1;
            while (c.ports.meshes.find_embedded_node_resource_record(c, state.mesh_instance_handle, 128, node) != -1) {
                instance->attached_people.push_back(0);
                if (node == std::numeric_limits<std::int32_t>::max())
                    throw std::length_error("RSSE attachment count");
                ++node;
            }
        }
    }
    state.attached_person_ids = instance->attached_people;
    state.walk_float_horizontal_scale_milli = state.walk_float_vertical_scale_milli = 1000;
    state.bounce_first_node_id = 1;
    state.value_slots = instance->values;
    state.operand_stack = instance->stack;
    state.instruction_words = instance->program.instructions;
    state.string_table = instance->program.string_table;
    state.stack_high_free_index = h.operand_stack_capacity - 1;
    state.instruction_step_budget = h.instruction_step_budget;
    state.operating_speed = 50;
    state.walk_records = instance->walk;
    state.bounce_records = instance->bounce;
    state.limbo_records = instance->limbo;
    if (instance->program.string_table.empty())
        state.string_table = {};
    //Publish only after host allocations succeed. Native unchecked allocation
    //failures are not meaningful states to preserve in a native-x64 owner.
    c.scripts.states.owned_states.push_back(std::move(instance));
    state.id = c.scripts.states.next_state_id++;
    state.next = c.scripts.states.head;
    if (state.next)
        state.next->previous = &state;
    c.scripts.states.head = &state;
    ++c.scripts.states.state_count;
    return &state;
}

RsseScriptState* create_rsse_program_state(ScriptWorld& c, RsseProgram program) {
    return create_rsse_program_state_for_owner(c, std::move(program), 0);
}
} //namespace simcoaster
