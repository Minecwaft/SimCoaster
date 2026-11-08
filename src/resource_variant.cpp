#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/resource_variant.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
bool float_slot(std::uint32_t slot) {
    return slot >= 1 && slot <= 6;
}

ResourceVariant& active(WorldRuntime& c) {
    return require_bound(c.resources.variants.active, "resource script has no active instance");
}

const ResourceScriptWord& word(WorldRuntime& c, std::uint32_t index) {
    if (index >= c.resources.variants.program.size()) {
        throw std::out_of_range("resource script cursor");
    }
    return c.resources.variants.program[index];
}

std::uint32_t read(WorldRuntime& c) {
    auto& state = active(c);
    const auto result = word(c, state.cursor).value;
    ++state.cursor;
    return result;
}

void push(ResourceVariant& state, std::uint32_t value) {
    if (state.stack_top > 0) {
        state.stack.at(static_cast<std::size_t>(--state.stack_top)) = value;
    }
}

std::uint32_t pop(ResourceVariant& state) {
    if (state.stack_top >= 20) {
        return 0;
    }
    return state.stack.at(static_cast<std::size_t>(state.stack_top++));
}

std::uint32_t insert(WorldRuntime& c, std::unique_ptr<ResourceVariant> state) {
    auto& slots = c.resources.variants.slots;
    for (std::size_t id = 1;; ++id) {
        if (id >= slots.size()) {
            if (slots.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - 50) {
                throw std::length_error("resource handle capacity");
            }
            slots.resize(slots.size() + 50);
        }
        if (!slots[id]) {
            if (c.resources.variants.next_generation == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("resource allocation identity exhausted");
            }
            state->generation = ++c.resources.variants.next_generation;
            state->handle = static_cast<std::uint32_t>(id);
            slots[id] = std::move(state);
            return static_cast<std::uint32_t>(id);
        }
    }
}

const SpriteDescriptor& descriptor(WorldRuntime& c, const ResourceVariant& state) {
    const auto* catalog = c.resources.variants.catalog;
    if (!catalog) {
        throw std::logic_error("resource variant has no sprite catalog");
    }
    const auto first = catalog->variants.at(state.values[10].bits).first_loaded_resource_index;
    return catalog->resources.at(static_cast<std::size_t>(first) + state.values[11].bits).descriptor;
}

std::uint32_t frame_stride(const SpriteDescriptor& sprite, std::uint32_t selector) {
    //Metadata selector byte0 becomes signed index-1. The native byte load then
    //aliases the LOW BYTE of nonzero_frame_stride_entry_count just before the
    //frame table. Represent that sentinel explicitly, without indexing before
    //a C++ array or retaining the original record's byte layout.
    if (selector == std::numeric_limits<std::uint32_t>::max()) {
        return static_cast<std::uint8_t>(sprite.nonzero_frame_stride_entry_count());
    }
    return sprite.frame_index_entries.at(selector).frame_stride;
}

std::uint32_t resolve(WorldRuntime& c, ResourceVariant& state, ResourceScriptEntry entry, bool update_stride) {
    if (!entry.metadata) {
        return entry.index;
    }
    const auto& sprite = descriptor(c, state);
    const auto& component = sprite.component_metadata.at(entry.index);
    state.values[12].bits = std::uint32_t { component.one_based_selector_01 } - 1u;
    if (update_stride) {
        state.values[14].bits = frame_stride(sprite, state.values[12].bits);
    }
    return resource_metadata_program_entry(std::uint32_t { component.one_based_selector_00 } - 1u);
}

void reset_cursor(ResourceVariant& state, std::uint32_t entry) {
    state.cursor = state.entry = entry;
    state.stack_top = 20;
    state.control_depth = state.call_depth = state.loop_depth = 0;
    //Refresh does not reset run state, delay, render enable, or the value bank.
}

void skip_block(WorldRuntime& c, bool stop_at_else) {
    auto& state = active(c);
    while (state.cursor < c.resources.variants.program.size()) {
        const auto opcode = word(c, state.cursor).opcode;
        if (opcode == ResourceOpcode::end_if || (stop_at_else && opcode == ResourceOpcode::otherwise)) {
            break;
        }
        if (opcode == ResourceOpcode::compare) {
            ++state.cursor;
            state.control_depth = fixed::add(state.control_depth, 1);
            skip_block(c, false);
            state.control_depth = fixed::subtract(state.control_depth, 1);
        }
        ++state.cursor;
    }
    if (stop_at_else && state.cursor < c.resources.variants.program.size() && word(c, state.cursor).opcode == ResourceOpcode::otherwise) {
        ++state.cursor;
    }
}

bool comparison(ResourceVariant& state, std::uint32_t left, std::uint32_t operation, std::uint32_t right) {
    if (!operation || operation > 8) {
        return true; //Original diagnostic hook is empty.
    }
    if (float_slot(left)) {
        const double a = state.values.at(left).floating();
        const double b = operation <= 4 ? static_cast<double>(state.values.at(right).floating()) : static_cast<double>(numeric::word(right));
        const bool unordered = std::isunordered(a, b);
        //Recover the numeric unordered contracts without retaining x87 registers
        //or status-word manipulation. Literal comparisons reverse the operands.
        return match (operation) -> bool {
            case 1 or 5 => a == b || unordered;
            case 2 or 6 => a != b && !unordered;
            case 3      => a > b;
            case 4      => a < b || unordered;
            case 7      => a > b || unordered;
            case _      => a < b;
        };
    }
    const auto a = state.values.at(left).integer(), b = operation <= 4 ? state.values.at(right).integer() : numeric::word(right);
    return match ((operation - 1u) % 4u) -> bool {
        case 0 => a == b;
        case 1 => a != b;
        case 2 => a > b;
        case _ => a < b;
    };
}

std::int32_t quotient(std::int32_t a, std::int32_t b, bool remainder) {
    if (!b || (a == std::numeric_limits<std::int32_t>::min() && b == -1)) {
        throw std::domain_error("resource integer division fault");
    }
    return remainder ? a % b : a / b;
}

void step(WorldRuntime& c, ResourceOpcode opcode) {
    auto& state = active(c);
    auto& values = state.values;
    match (opcode) {
        case ResourceOpcode::jump => {
            state.cursor = read(c);
            return;
        }
        case ResourceOpcode::compare or ResourceOpcode::until => {
            const auto left = read(c), operation = read(c), right = read(c);
            if (opcode == ResourceOpcode::compare) {
                state.control_depth = fixed::add(state.control_depth, 1);
                if (!comparison(state, left, operation, right)) {
                    skip_block(c, true);
                }
            } else if (state.loop_depth > 0) {
                const auto body = pop(state);
                if (comparison(state, left, operation, right)) {
                    state.loop_depth = fixed::subtract(state.loop_depth, 1);
                } else {
                    push(state, body);
                    state.cursor = body;
                }
            }
            return;
        }
        case ResourceOpcode::end_if => {
            state.control_depth = fixed::subtract(state.control_depth, 1);
            return;
        }
        case ResourceOpcode::otherwise => {
            skip_block(c, false);
            return;
        }
        case ResourceOpcode::call => {
            const auto entry = read(c);
            push(state, state.cursor);
            state.cursor = entry;
            state.call_depth = fixed::add(state.call_depth, 1);
            return;
        }
        case ResourceOpcode::return_ => {
            state.call_depth = fixed::subtract(state.call_depth, 1);
            if (state.call_depth >= 0) {
                state.cursor = pop(state);
            }
            return;
        }
        case ResourceOpcode::repeat => {
            push(state, state.cursor);
            state.loop_depth = fixed::add(state.loop_depth, 1);
            return;
        }
        case ResourceOpcode::push => {
            push(state, values.at(read(c)).bits);
            return;
        }
        case ResourceOpcode::pop => {
            values.at(read(c)).bits = pop(state);
            return;
        }
        case ResourceOpcode::do_ => {
            const auto count = read(c);
            push(state, state.cursor);
            push(state, count);
            state.loop_depth = fixed::add(state.loop_depth, 1);
            return;
        }
        case ResourceOpcode::loop => {
            if (state.loop_depth > 0) {
                const auto count = fixed::subtract(numeric::word(pop(state)), 1);
                const auto body = pop(state);
                if (count > 0) {
                    push(state, body);
                    push(state, numeric::bits(count));
                    state.cursor = body;
                } else {
                    state.loop_depth = fixed::subtract(state.loop_depth, 1);
                }
            }
            return;
        }
        case ResourceOpcode::end => {
            state.state = ResourceRunState::completed;
            state.render_enabled = false;
            return;
        }
        case ResourceOpcode::yield => {
            state.state = ResourceRunState::yielded;
            return;
        }
        case ResourceOpcode::latch => {
            state.latched_word = read(c);
            return;
        }
        case ResourceOpcode::set or ResourceOpcode::add or ResourceOpcode::subtract or ResourceOpcode::multiply or ResourceOpcode::divide => {
            const auto slot = read(c), operand = read(c);
            auto& destination = values.at(slot);
            if (float_slot(slot)) {
                const double a = destination.floating(), b = numeric::word(operand);
                const auto result = match (opcode) -> double {
                    case ResourceOpcode::set      => b;
                    case ResourceOpcode::add      => a + b;
                    case ResourceOpcode::subtract => a - b;
                    case ResourceOpcode::multiply => a* b;
                    case _                        => a / b;
                };
                destination.floating(static_cast<float>(result));
            } else {
                destination.bits = match (opcode) -> std::uint32_t {
                    case ResourceOpcode::set      => operand;
                    case ResourceOpcode::add      => destination.bits + operand;
                    case ResourceOpcode::subtract => destination.bits - operand;
                    case ResourceOpcode::multiply => destination.bits* operand;
                    case _                        => numeric::bits(quotient(destination.integer(), numeric::word(operand), false));
                };
            }
            return;
        }
        case ResourceOpcode::multiply_position or ResourceOpcode::divide_slot or ResourceOpcode::add_slot or ResourceOpcode::remainder_slot or ResourceOpcode::copy => {
            auto slot = read(c);
            const auto source = read(c);
            if (opcode == ResourceOpcode::multiply_position) {
                slot = slot == 1 ? 1u : 2u;
            }
            auto& destination = values.at(slot);
            const auto from = values.at(source);
            if (opcode == ResourceOpcode::copy) {
                destination = from;
            } else if (opcode == ResourceOpcode::remainder_slot) {
                destination.bits = numeric::bits(quotient(destination.integer(), from.integer(), true));
            } else if (float_slot(slot) || opcode == ResourceOpcode::multiply_position) {
                const double a = destination.floating(), b = from.floating();
                const auto result = match (opcode) -> double {
                    case ResourceOpcode::add_slot          => a + b;
                    case ResourceOpcode::multiply_position => a* b;
                    case _                                 => a / b;
                };
                destination.floating(static_cast<float>(result));
            } else if (opcode == ResourceOpcode::add_slot) {
                destination.bits += from.bits;
            } else {
                destination.bits = numeric::bits(quotient(destination.integer(), from.integer(), false));
            }
            return;
        }
        case ResourceOpcode::remainder => {
            const auto slot = read(c), operand = read(c);
            auto& destination = values.at(slot);
            destination.bits = numeric::bits(quotient(destination.integer(), numeric::word(operand), true));
            return;
        }
        case ResourceOpcode::and_mask => {
            const auto destination = read(c), source = read(c), mask = read(c);
            values.at(destination).bits = values.at(source).bits & mask;
            return;
        }
        case ResourceOpcode::random => {
            const auto destination = read(c);
            if (!c.simulation.reference) {
                throw std::logic_error("resource random requires primary World");
            }
            const auto random = next_world_prng_value(*c.simulation.reference);
            const auto bound = word(c, state.cursor).value + 1u;
            values.at(destination).bits = numeric::bits(quotient(random, numeric::word(bound), true));
            ++state.cursor;
            return;
        }
        case ResourceOpcode::create => {
            const auto entry = read(c);
            (void)create_resource_variant(c, entry, values[11].bits, { values[1].floating(), values[2].floating(), values[3].floating() });
            return;
        }
        case ResourceOpcode::variant => {
            values[11].bits = read(c);
            return;
        }
        case ResourceOpcode::frame_entry => {
            values[12].bits = read(c);
            return;
        }
        case ResourceOpcode::frame_slot or ResourceOpcode::frame or ResourceOpcode::frame_always => {
            const auto operand = read(c);
            values[13].bits = opcode == ResourceOpcode::frame_slot ? values.at(operand).bits : operand;
            state.render_enabled = opcode == ResourceOpcode::frame_always || values[13].bits != std::numeric_limits<std::uint32_t>::max();
            state.state = ResourceRunState::yielded;
            return;
        }
        case ResourceOpcode::hide => {
            state.render_enabled = false;
            return;
        }
        case ResourceOpcode::reset_scale => {
            values[8].floating(1.f);
            return;
        }
        case ResourceOpcode::reset_slot9 => {
            values[9].floating(1.f);
            return;
        }
        case ResourceOpcode::advance_slot9 => {
            values[9].floating(static_cast<float>(static_cast<double>(values[9].floating()) + static_cast<double>(0.01f)));
            return;
        }
        case _ => throw std::invalid_argument("operand executed as resource opcode");
    }
}
} //namespace

void initialize_resource_variant_runtime(WorldRuntime& c, const LoadedSpriteCatalog& catalog) {
    auto& runtime = c.resources.variants;
    runtime.catalog = &catalog;
    c.resources.resource_category_variants = catalog.variants;
    runtime.appearance_variant_counts.clear();
    for (const auto& range : catalog.variants) {
        runtime.appearance_variant_counts.push_back(range.loaded_resource_count);
    }
    if (runtime.initialized) {
        return;
    }
    runtime.initialized = true;
    runtime.step_budget = 50;
    runtime.slots.resize(100);
}

void release_resource_variant_runtime(WorldRuntime& c) {
    //Own allocations on x64; the original shutdown loses its record pointers
    //after clearing the initialized flag. That leak is not a runtime contract.
    auto& runtime = c.resources.variants;
    runtime.slots.clear();
    runtime.active = nullptr;
    runtime.initialized = false;
    runtime.appearance_variant_counts.clear();
}

ResourceVariant* find_resource_variant(WorldRuntime& c, std::uint32_t handle) {
    auto& runtime = c.resources.variants;
    return runtime.initialized && numeric::word(handle) > 0 && handle < runtime.slots.size() ? runtime.slots[handle].get() : nullptr;
}

std::uint32_t allocate_resource_variant(WorldRuntime& c, ResourceScriptEntry entry, ResourceArguments arguments) {
    if (!c.resources.variants.initialized) {
        return 0;
    }
    auto state = std::make_unique<ResourceVariant>();
    auto& values = state->values;
    values[10].bits = arguments[0];
    values[11].bits = arguments[1];
    values[12].bits = arguments[2];
    values[1].bits = arguments[3];
    values[2].bits = arguments[4];
    values[3].bits = arguments[5];
    values[7].bits = 255;
    values[8].floating(1.f);
    values[9].floating(1.f);
    if (entry.metadata) {
        const auto& metadata = descriptor(c, *state).component_metadata.at(entry.index);
        values[17].bits = metadata.value_02;
        values[18].bits = metadata.value_03;
    }
    const auto resolved = resolve(c, *state, entry, true);
    reset_cursor(*state, resolved);
    if (!entry.metadata) {
        values[14].bits = frame_stride(descriptor(c, *state), values[12].bits);
    }
    state->deadline = c.ports.clock.get_simulation_clock_time_ms(c) + state->delay;
    return insert(c, std::move(state));
}

std::uint32_t create_resource_variant(WorldRuntime& c, std::uint32_t entry, std::uint32_t variant, const WorldVector3& position) {
    if (!c.resources.variants.initialized) {
        return 0;
    }
    auto state = std::make_unique<ResourceVariant>();
    reset_cursor(*state, entry);
    state->values[11].bits = variant;
    for (std::size_t i = 0; i < 3; ++i) {
        state->values[i + 1].floating(position[i]);
    }
    state->values[7].bits = 255;
    state->values[8].floating(1.f);
    state->values[9].floating(1.f);
    state->deadline = c.ports.clock.get_simulation_clock_time_ms(c) + state->delay;
    return insert(c, std::move(state));
}

void refresh_resource_variant(WorldRuntime& c, std::uint32_t handle, ResourceScriptEntry entry) {
    if (auto* state = find_resource_variant(c, handle)) {
        reset_cursor(*state, resolve(c, *state, entry, true));
    }
}

bool resource_variant_matches(WorldRuntime& c, std::uint32_t handle, ResourceScriptEntry entry) {
    if (auto* state = find_resource_variant(c, handle)) {
        return state->entry == resolve(c, *state, entry, false);
    }
    return false;
}

void release_resource_variant(WorldRuntime& c, std::uint32_t handle) {
    if (auto* state = find_resource_variant(c, handle)) {
        if (c.resources.variants.active == state) {
            c.resources.variants.active = nullptr;
        }
        c.resources.variants.slots[handle].reset();
    }
}

std::uint32_t run_resource_variant_script(WorldRuntime& c, ResourceVariant& state) {
    c.resources.variants.active = &state;
    if (state.state == ResourceRunState::suspended) {
        return 0;
    }
    state.state = ResourceRunState::running;
    std::uint32_t executed = 0;
    do {
        state.last_cursor = state.cursor;
        const auto opcode = word(c, state.cursor++).opcode;
        step(c, opcode);
        ++executed;
    } while (state.state == ResourceRunState::running && executed < c.resources.variants.step_budget);
    return executed;
}

std::uint32_t process_due_resource_variant_scripts(WorldRuntime& c) {
    auto& runtime = c.resources.variants;
    runtime.steps_this_update = 0;
    if (!runtime.initialized) {
        return 0;
    }
    runtime.time_snapshot = c.ports.clock.get_simulation_clock_time_ms(c);
    for (std::size_t id = 1; id < runtime.slots.size(); ++id) {
        if (auto* state = runtime.slots[id].get()) {
            if (numeric::word(runtime.time_snapshot) <= numeric::word(state->deadline)) {
                continue;
            }
            if (state->state == ResourceRunState::completed) {
                release_resource_variant(c, static_cast<std::uint32_t>(id));
                continue;
            }
            runtime.steps_this_update += run_resource_variant_script(c, *state);
            state->deadline = runtime.time_snapshot + state->delay;
        }
    }
    return runtime.steps_this_update;
}

void set_resource_variant_integer(WorldRuntime& c, std::uint32_t handle, std::uint32_t slot, std::uint32_t value) {
    if (auto* state = find_resource_variant(c, handle); state && slot < state->values.size()) {
        state->values[slot].bits = value;
    }
}

void set_resource_variant_float(WorldRuntime& c, std::uint32_t handle, std::uint32_t slot, float value) {
    set_resource_variant_integer(c, handle, slot, std::bit_cast<std::uint32_t>(value));
}

void set_resource_variant_extended_frame_metadata(WorldRuntime& c, std::uint32_t handle, bool enabled) {
    if (auto* state = find_resource_variant(c, handle)) {
        state->values[16].bits = enabled ? state->values[16].bits | 512u : state->values[16].bits & ~512u;
    }
}

void suspend_active_resource_variant(WorldRuntime& c, std::uint32_t validation_handle) {
    if (find_resource_variant(c, validation_handle)) {
        active(c).state = ResourceRunState::suspended;
    }
}

void resume_active_resource_variant(WorldRuntime& c, std::uint32_t validation_handle) {
    if (find_resource_variant(c, validation_handle) && active(c).state == ResourceRunState::suspended) {
        active(c).state = ResourceRunState::running;
    }
}

void call_resource_variant(WorldRuntime& c, std::uint32_t handle, std::uint32_t entry) {
    if (auto* state = find_resource_variant(c, handle)) {
        push(active(c), state->cursor);
        state->cursor = entry;
        state->call_depth = fixed::add(state->call_depth, 1);
    }
}

void return_resource_variant(WorldRuntime& c, std::uint32_t handle) {
    if (auto* state = find_resource_variant(c, handle)) {
        state->call_depth = fixed::subtract(state->call_depth, 1);
        if (state->call_depth >= 0) {
            state->cursor = pop(active(c));
        }
    }
}

std::uint32_t clone_resource_variant_for_preview(WorldRuntime& c, std::uint32_t handle, float x, float y, float scale, float adjustment) {
    if (!c.resources.variants.initialized) {
        return 0;
    }
    auto* source = find_resource_variant(c, handle);
    if (!source) {
        throw std::invalid_argument("preview source resource handle");
    }
    auto state = std::make_unique<ResourceVariant>(*source);
    state->values[1].floating(x);
    state->values[2].floating(y);
    state->values[3].floating(scale);
    state->values[8].floating(adjustment);
    state->values[16].bits = (state->values[16].bits & ~512u) | 327680u;
    return insert(c, std::move(state));
}

void copy_resource_variant_state(WorldRuntime& c, std::uint32_t destination, std::uint32_t source) {
    auto* to = find_resource_variant(c, destination);
    const auto* from = find_resource_variant(c, source);
    if (!to || !from) {
        throw std::invalid_argument("resource state copy handle");
    }
    to->cursor = from->cursor;
    for (auto slot : { 10u, 11u, 12u, 17u, 18u, 7u, 9u, 15u, 13u, 14u }) {
        to->values[slot] = from->values[slot];
    }
    to->render_enabled = from->render_enabled;
    to->values[16].bits = (to->values[16].bits & ~512u) | 327680u;
}

void normalize_resource_variant_indices(WorldRuntime& c) {
    if (!c.resources.variants.initialized) {
        return;
    }
    if (!c.resources.variants.catalog) {
        throw std::logic_error("resource normalization has no catalog");
    }
    for (auto& state : c.resources.variants.slots) {
        if (state) {
            const auto count = c.resources.variants.catalog->variants.at(state->values[10].bits).loaded_resource_count;
            state->values[11].bits = numeric::bits(quotient(state->values[11].integer(), count, true));
        }
    }
}
} //namespace simcoaster
