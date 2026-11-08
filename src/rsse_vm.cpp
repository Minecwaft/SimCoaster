#include "simcoaster/numeric_word.hpp"
#include "simcoaster/rsse_vm.hpp"
#include "simcoaster/rsse_passengers.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/rsse_tour_runtime.hpp"
#include "simcoaster/object_catalog.hpp"
#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr auto stopped = -10000;

bool tagged(std::uint32_t word, RsseWordTag tag) {
    return (word & rsse_tag_mask) == std::to_underlying(tag);
}

std::int32_t immediate(std::uint32_t word) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word));
}

std::int32_t& slot(RsseScriptState& state, std::uint32_t index) {
    if (index >= state.value_slots.size())
        throw std::out_of_range("RSSE value-slot operand");
    return state.value_slots[index];
}

const std::int32_t& slot(const RsseScriptState& state, std::uint32_t index) {
    if (index >= state.value_slots.size())
        throw std::out_of_range("RSSE value-slot operand");
    return state.value_slots[index];
}

void destination(RsseScriptState& state, std::uint32_t word) {
    if (tagged(word, RsseWordTag::value_slot))
        slot(state, word ^ std::to_underlying(RsseWordTag::value_slot)) = state.current_value;
}

std::uint32_t truncated_bits(double value) {
    const auto truncated = std::trunc(value), limit = std::ldexp(1.0, 63);
    return std::isfinite(truncated) && truncated >= -limit && truncated < limit
             ? static_cast<std::uint32_t>(static_cast<std::int64_t>(truncated))
             : 0;
}

std::int32_t divide(std::int32_t numerator, std::int32_t denominator, bool remainder) {
    if (!denominator)
        return 0;
    if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)
        throw std::overflow_error("RSSE integer division overflow");
    return remainder ? numerator % denominator : numerator / denominator;
}

bool push(RsseScriptState& state, std::int32_t value) {
    const auto index = state.stack_high_free_index;
    if (state.operand_stack.data() && index >= 0 && static_cast<std::size_t>(index) < state.operand_stack.size()) {
        state.operand_stack[static_cast<std::size_t>(index)] = value;
        --state.stack_high_free_index;
        return true;
    }
    state.instruction_cursor = stopped;
    return false;
}

std::int32_t pop(RsseScriptState& state) {
    const auto index = state.stack_high_free_index;
    if (state.operand_stack.data()
        && index >= -1
        && static_cast<std::int64_t>(index) < static_cast<std::int64_t>(state.operand_stack.size()) - 1) {
        ++state.stack_high_free_index;
        return state.operand_stack[static_cast<std::size_t>(index + 1)];
    }
    state.instruction_cursor = stopped;
    return 0;
}

std::string_view program_name(const RsseScriptState& state, std::int32_t offset) {
    if (offset < 0 || static_cast<std::size_t>(offset) >= state.string_table.size())
        throw std::out_of_range("RSSE string-table offset");
    const auto tail = state.string_table.substr(static_cast<std::size_t>(offset));
    const auto end = tail.find('\0');
    if (end == tail.npos)
        throw std::out_of_range("unterminated RSSE string");
    return tail.substr(0, end);
}

bool animation_step(ScriptWorld& c, RsseScriptState& state, RsseOpcode opcode, float duration_scale) {
    const auto read = [&] {
        return read_rsse_instruction_word(state);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(state, read());
    };
    const auto clock = [&] {
        return c.ports.clock.get_simulation_clock_time_ms(c);
    };
    const auto request = [&](std::int32_t animation, std::int32_t sub, MeshAnimationTransitionFlags flags, float divisor, std::uint32_t control) {
        return state.mesh_instance_handle ? world_operations::request_mesh_animation_control_transition(c, state.mesh_instance_handle, static_cast<MeshAnimationRequest>(animation), numeric::bits(sub), flags, divisor, control) : 0;
    };
    match (opcode) {
        case RsseOpcode::FLUSHANIM or RsseOpcode::FLUSHANIM_CH => {
            const auto control = opcode == RsseOpcode::FLUSHANIM_CH ? numeric::bits(resolve()) : 0u;
            if (state.mesh_instance_handle)
                world_operations::clear_deferred_mesh_animation(c, state.mesh_instance_handle, control);
            return true;
        }
        case RsseOpcode::GETANIM or RsseOpcode::GETANIM_CH => {
            const auto dest = read(), control = opcode == RsseOpcode::GETANIM_CH ? numeric::bits(resolve()) : 0u;
            if (state.mesh_instance_handle) {
                const auto query = world_operations::get_mesh_animation_control(c, state.mesh_instance_handle, control);
                if (query.animation_id)
                    state.current_value = numeric::word(*query.animation_id);
                if (query.flags & 4u)
                    state.current_value = -1;
            }
            destination(state, dest);
            return true;
        }
        case RsseOpcode::LOOPANIM or RsseOpcode::LOOPANIM_CH => {
            const auto animation = resolve(), sub = resolve();
            const bool channel = opcode == RsseOpcode::LOOPANIM_CH;
            const auto control = channel ? read() : 0u;
            const auto key = (numeric::bits(sub) << 16) + numeric::bits(animation);
            if (channel || key != state.last_loop_animation_key) {
                state.animation_duration_scale_milli = 1000;
                (void)request(animation, sub, MeshAnimationTransitionFlags::loop, duration_scale, control);
                state.animation_deadline_ms = 0;
                if (!channel)
                    state.last_loop_animation_key = key;
            }
            return true;
        }
        case RsseOpcode::WAITANIM or RsseOpcode::WAITANIM_CH => {
            const auto animation = resolve(), sub = resolve();
            const bool channel = opcode == RsseOpcode::WAITANIM_CH;
            const auto control = channel ? read() : 0u;
            if (state.wait_deadline_ms) {
                if (clock() < state.wait_deadline_ms) {
                    state.instruction_cursor -= 3;
                    state.remaining_instruction_steps = 0;
                } else
                    state.wait_deadline_ms = 0;
            } else {
                state.animation_duration_scale_milli = 1000;
                const auto duration = numeric::bits(request(animation, sub, static_cast<MeshAnimationTransitionFlags>(0), duration_scale, control)) - 300u;
                const auto delay = channel ? duration : truncated_bits(static_cast<double>(duration) / duration_scale);
                state.wait_deadline_ms = std::max(delay, 300u) + clock();
                state.instruction_cursor -= 3;
                state.remaining_instruction_steps = 0;
                state.animation_deadline_ms = 0;
                if (!channel)
                    state.last_loop_animation_key = 65535;
            }
            return true;
        }
        case RsseOpcode::TRIGANIM or RsseOpcode::TRIGANIM_CH or RsseOpcode::TRIGWAITANIM or RsseOpcode::TRIGWAITANIM_CH or RsseOpcode::TRIGANIMSPEED => {
            const auto animation = resolve(), sub = resolve();
            const auto dest = read();
            const bool channel = opcode == RsseOpcode::TRIGANIM_CH || opcode == RsseOpcode::TRIGWAITANIM_CH;
            const bool wait = opcode == RsseOpcode::TRIGWAITANIM || opcode == RsseOpcode::TRIGWAITANIM_CH,
                       speed = opcode == RsseOpcode::TRIGANIMSPEED;
            const auto control = channel ? numeric::bits(resolve()) : 0u;
            const auto rate = speed ? resolve() : 1000;
            if (wait && state.awaited_animation_id_plus_one) {
                auto current = dest;
                if (state.mesh_instance_handle) {
                    const auto query =
                        world_operations::get_mesh_animation_control(c, state.mesh_instance_handle, control);
                    if (query.animation_id)
                        current = *query.animation_id;
                }
                if (current + 1u == state.awaited_animation_id_plus_one)
                    state.awaited_animation_id_plus_one = 0;
                else {
                    state.instruction_cursor -= 4;
                    state.remaining_instruction_steps = 0;
                }
                return true;
            }
            state.animation_duration_scale_milli = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(rate));
            const auto divisor =
                speed ? static_cast<float>(static_cast<double>(rate) * duration_scale * static_cast<double>(0.001f))
                      : duration_scale;
            state.current_value = std::max(numeric::word(numeric::bits(request(animation, sub, static_cast<MeshAnimationTransitionFlags>(0), divisor, control)) - 300u), 300);
            destination(state, dest);
            const auto now = clock();
            if (speed) {
                if (!rate)
                    throw std::domain_error("RSSE animation zero rate");
                state.animation_deadline_ms = now + numeric::bits(divide(numeric::word(1000u * numeric::bits(state.current_value)), rate, false));
            } else
                state.animation_deadline_ms =
                    now + truncated_bits(static_cast<double>(state.current_value) / duration_scale);
            if (!channel)
                state.last_loop_animation_key = 65535;
            if (wait) {
                state.awaited_animation_id_plus_one = numeric::bits(animation) + 1u;
                state.instruction_cursor -= 4;
            }
            return true;
        }
        case _ => return false;
    }
}
} //namespace

std::uint32_t read_rsse_instruction_word(RsseScriptState& state) {
    const auto cursor = state.instruction_cursor;
    if (cursor < 0 || static_cast<std::size_t>(cursor) >= state.instruction_words.size()) {
        state.instruction_cursor = stopped;
        return 0;
    }
    ++state.instruction_cursor;
    return state.instruction_words[static_cast<std::size_t>(cursor)];
}

std::int32_t resolve_rsse_operand(const RsseScriptState& state, std::uint32_t word) {
    return tagged(word, RsseWordTag::value_slot) ? slot(state, word ^ std::to_underlying(RsseWordTag::value_slot))
                                                 : immediate(word);
}

void step_rsse_instruction(ScriptWorld& c, RsseScriptState& state) {
    const auto duration_scale =
        static_cast<float>(static_cast<double>(state.operating_speed) * static_cast<double>(0.01f) + 0.5);
    const auto word = read_rsse_instruction_word(state);
    if (state.instruction_cursor < 0)
        return;
    if (!tagged(word, RsseWordTag::opcode)) {
        state.instruction_cursor = stopped;
        return;
    }
    const auto opcode = static_cast<RsseOpcode>(word ^ std::to_underlying(RsseWordTag::opcode));
    const auto read = [&] {
        return read_rsse_instruction_word(state);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(state, read());
    };
    const auto clock = [&] {
        return c.ports.clock.get_simulation_clock_time_ms(c);
    };
    match (opcode) {
        case RsseOpcode::NOP => return;
        case RsseOpcode::CRIT_LOCK => {
            c.scripts.states.step_control = 1;
            return;
        }
        case RsseOpcode::CRIT_UNLOCK => {
            c.scripts.states.step_control = 0;
            state.remaining_instruction_steps = 0;
            return;
        }
        case RsseOpcode::ENDSLICE => {
            state.remaining_instruction_steps = 0;
            return;
        }
        case RsseOpcode::SETLV => {
            state.current_value = numeric::word(read());
            return;
        }
        case RsseOpcode::COPY => {
            const auto dest = read();
            if (tagged(dest, RsseWordTag::value_slot)) {
                state.current_value = resolve();
                destination(state, dest);
            }
            return;
        }
        case RsseOpcode::ADD => {
            const auto dest = read(), source = read();
            if (tagged(dest, RsseWordTag::value_slot)) {
                auto& value = slot(state, dest ^ std::to_underlying(RsseWordTag::value_slot));
                value = numeric::word(numeric::bits(value) + numeric::bits(resolve_rsse_operand(state, source)));
                state.current_value = value;
            }
            return;
        }
        case RsseOpcode::SUB or RsseOpcode::MULT or RsseOpcode::DIV or RsseOpcode::MOD => {
            const auto dest = read();
            const auto a = resolve(), b = resolve();
            state.current_value = match (opcode) -> std::int32_t {
                case RsseOpcode::SUB  => numeric::word(numeric::bits(a) - numeric::bits(b));
                case RsseOpcode::MULT => numeric::word(numeric::bits(a) * numeric::bits(b));
                case RsseOpcode::DIV  => divide(a, b, false);
                case _                => divide(a, b, true);
            };
            destination(state, dest);
            return;
        }
        case RsseOpcode::TEST => {
            const auto source = read();
            if (tagged(source, RsseWordTag::value_slot))
                state.current_value = resolve_rsse_operand(state, source);
            return;
        }
        case RsseOpcode::CMP => {
            const auto a = read();
            const auto b = resolve();
            if (tagged(a, RsseWordTag::value_slot))
                state.current_value = numeric::word(numeric::bits(resolve_rsse_operand(state, a)) - numeric::bits(b));
            return;
        }
        case RsseOpcode::GETTIME => {
            const auto dest = read();
            state.current_value = numeric::word(clock());
            destination(state, dest);
            return;
        }
        case RsseOpcode::RAND => {
            const auto dest = read();
            const auto bound = immediate(read()) + 1;
            if (!bound)
                throw std::domain_error("RSSE RAND zero modulus");
            if (!c.simulation.reference)
                throw std::invalid_argument("RSSE random primary World");
            const auto value = static_cast<std::int32_t>(numeric::bits(next_world_prng_value(*c.simulation.reference)) >> 1) % bound;
            state.current_value = value < 0 ? numeric::word(0u - numeric::bits(value)) : value;
            destination(state, dest);
            return;
        }
        case RsseOpcode::PUSH => {
            const auto value = resolve();
            (void)push(state, value);
            state.current_value = value;
            return;
        }
        case RsseOpcode::POP => {
            const auto dest = read();
            state.current_value = pop(state);
            destination(state, dest);
            return;
        }
        case RsseOpcode::HUSH => {
            const auto source = read();
            const auto index = state.stack_low_count;
            if (state.operand_stack.data()
                && index >= 0
                && static_cast<std::size_t>(index) < state.operand_stack.size()) {
                const auto value = resolve_rsse_operand(state, source);
                state.operand_stack[static_cast<std::size_t>(index)] = value;
                state.current_value = value;
                ++state.stack_low_count;
            }
            return;
        }
        case RsseOpcode::HOP => {
            const auto dest = read();
            const auto index = state.stack_low_count;
            if (state.operand_stack.data()
                && index > 0
                && static_cast<std::int64_t>(state.stack_high_free_index)
                       <= static_cast<std::int64_t>(state.operand_stack.size())) {
                if (static_cast<std::size_t>(index) > state.operand_stack.size())
                    throw std::out_of_range("RSSE low-stack pop");
                --state.stack_low_count;
                state.current_value = state.operand_stack[static_cast<std::size_t>(index - 1)];
                destination(state, dest);
            }
            return;
        }
        case RsseOpcode::JSR => {
            const auto address = read();
            (void)push(state, numeric::word(numeric::bits(state.instruction_cursor) | std::to_underlying(RsseWordTag::address)));
            if (tagged(address, RsseWordTag::address))
                state.instruction_cursor = numeric::word(address ^ std::to_underlying(RsseWordTag::address));
            return;
        }
        case RsseOpcode::RETURN => {
            const auto address = numeric::bits(pop(state));
            if (tagged(address, RsseWordTag::address))
                state.instruction_cursor = numeric::word(address ^ std::to_underlying(RsseWordTag::address));
            return;
        }
        case RsseOpcode::BRANCH or RsseOpcode::BRANCH_Z or RsseOpcode::BRANCH_NZ or RsseOpcode::BRANCH_NV or RsseOpcode::BRANCH_PV => {
            const auto address = read();
            const auto take = match (opcode) -> bool {
                case RsseOpcode::BRANCH    => true;
                case RsseOpcode::BRANCH_Z  => state.current_value == 0;
                case RsseOpcode::BRANCH_NZ => state.current_value != 0;
                case RsseOpcode::BRANCH_NV => state.current_value < 0;
                case _                     => state.current_value > 0;
            };
            if (take)
                state.instruction_cursor = tagged(address, RsseWordTag::address) ? numeric::word(address ^ std::to_underlying(RsseWordTag::address)) : stopped;
            return;
        }
        case RsseOpcode::DBGMSG => {
            (void)read();
            return;
        }
        case RsseOpcode::NAME => {
            const auto name = read();
            if (tagged(name, RsseWordTag::string))
                state.program_name_string_offset = numeric::word(name ^ std::to_underlying(RsseWordTag::string));
            return;
        }
        case RsseOpcode::TURBO => {
            state.turbo_enabled = static_cast<std::uint8_t>(read());
            return;
        }
        case RsseOpcode::WAIT or RsseOpcode::WAITABS => {
            if (state.wait_deadline_ms) {
                (void)read();
                if (clock() < state.wait_deadline_ms) {
                    state.instruction_cursor -= 2;
                    state.remaining_instruction_steps = 0;
                } else
                    state.wait_deadline_ms = 0;
            } else {
                const auto duration = resolve();
                const auto delay = opcode == RsseOpcode::WAIT ? truncated_bits(static_cast<double>(duration) / duration_scale) : numeric::bits(duration);
                state.wait_deadline_ms = delay + clock();
                state.instruction_cursor -= 2;
                state.remaining_instruction_steps = 0;
            }
            return;
        }
        case RsseOpcode::WAIT4ANIM => {
            if (state.animation_deadline_ms) {
                if (clock() < state.animation_deadline_ms) {
                    --state.instruction_cursor;
                    state.remaining_instruction_steps = 0;
                } else
                    state.animation_deadline_ms = 0;
            }
            return;
        }
        case RsseOpcode::SETTIMER => {
            const auto delay = resolve();
            state.timer_deadline_ms = numeric::bits(delay) + clock();
            return;
        }
        case RsseOpcode::GETTIMER => {
            const auto dest = read();
            const auto now = clock();
            state.current_value = std::max(numeric::word(state.timer_deadline_ms - now), 0);
            destination(state, dest);
            return;
        }
        case RsseOpcode::YEAR or RsseOpcode::MONTH or RsseOpcode::DAY or RsseOpcode::HOUR or RsseOpcode::MIN or RsseOpcode::SEC => {
            SystemTimeFields time{};
            c.ports.clock.get_local_time(&time);
            state.current_value = match (opcode) -> std::int32_t {
                case RsseOpcode::YEAR  => static_cast<std::int32_t>(time.year) - 1900;
                case RsseOpcode::MONTH => time.month;
                case RsseOpcode::DAY   => time.day;
                case RsseOpcode::HOUR  => time.hour;
                case RsseOpcode::MIN   => time.minute;
                case _                 => time.second;
            };
            destination(state, read());
            return;
        }
        case RsseOpcode::SPAWNCHILD or RsseOpcode::SPAWNSOUND => {
            const auto operand = read();
            if (!tagged(operand, RsseWordTag::string))
                return;
            const auto name = program_name(state, numeric::word(operand ^ std::to_underlying(RsseWordTag::string)));
            RsseScriptState* spawned = nullptr;
            if (state.script_source && c.scripts.states.active) {
                auto program = load_relative_rsse_script(*state.script_source, name);
                if (program)
                    spawned = create_rsse_program_state(c, std::move(*program));
            }
            if (opcode == RsseOpcode::SPAWNSOUND)
                state.sound_script_state_id = spawned ? spawned->id : 0;
            else {
                state.child_script_state_id = spawned ? spawned->id : 0;
                if (spawned) {
                    spawned->parent_script_state_id = state.id;
                    spawned->owner_used_thing_id = state.owner_used_thing_id;
                    spawned->auxiliary_runtime_object_handle = state.auxiliary_runtime_object_handle;
                    spawned->inherited_context_word = state.inherited_context_word;
                    spawned->mesh_instance_handle = state.mesh_instance_handle;
                }
            }
            return;
        }
        case RsseOpcode::REMOVECHILD => {
            if (state.child_script_state_id)
                (void)destroy_rsse_script_state_by_id(c, state.child_script_state_id, RsseScriptCleanupFlags::none);
            state.child_script_state_id = 0;
            return;
        }
        case RsseOpcode::SETVARINCHILD or RsseOpcode::SETVARINPARENT => {
            const auto index = resolve(), value = resolve();
            const auto id =
                opcode == RsseOpcode::SETVARINCHILD ? state.child_script_state_id : state.parent_script_state_id;
            if (!id)
                return;
            auto* target = find_rsse_script_state_by_id(c, id);
            if (!target)
                throw std::invalid_argument("RSSE missing related state");
            if (index < 0)
                throw std::out_of_range("RSSE related state slot");
            if (static_cast<std::size_t>(index) < target->value_slots.size()) {
                target->value_slots[static_cast<std::size_t>(index)] = value;
                state.current_value = value;
            }
            return;
        }
        case RsseOpcode::GETVARINCHILD or RsseOpcode::GETVARINPARENT => {
            const auto dest = read();
            const auto index = resolve();
            if (!tagged(dest, RsseWordTag::value_slot))
                return;
            const auto id =
                opcode == RsseOpcode::GETVARINCHILD ? state.child_script_state_id : state.parent_script_state_id;
            if (!id)
                return;
            const auto* target = find_rsse_script_state_by_id(c, id);
            if (!target)
                throw std::invalid_argument("RSSE missing related state");
            if (index < 0)
                throw std::out_of_range("RSSE related state slot");
            if (static_cast<std::size_t>(index) < target->value_slots.size()) {
                state.current_value = target->value_slots[static_cast<std::size_t>(index)];
                destination(state, dest);
            }
            return;
        }
        case RsseOpcode::GETREMOTEVAR => {
            const auto dest = read();
            const auto id = resolve(), index = resolve();
            state.current_value = 0;
            if (const auto* other = find_rsse_script_state_by_id(c, numeric::bits(id)); other && index >= 0 && static_cast<std::size_t>(index) < other->value_slots.size()) {
                state.current_value = other->value_slots[static_cast<std::size_t>(index)];
            }
            destination(state, dest);
            return;
        }
        case RsseOpcode::SETREMOTEVAR => {
            const auto id = resolve(), index = resolve(), value = resolve();
            if (auto* other = find_rsse_script_state_by_id(c, numeric::bits(id)); other && index >= 0 && static_cast<std::size_t>(index) < other->value_slots.size()) {
                other->value_slots[static_cast<std::size_t>(index)] = value;
                state.current_value = value;
            }
            return;
        }
        case RsseOpcode::FINDSCRIPTRAND => {
            const auto name = read(), dest = read();
            state.current_value = 0;
            if (!tagged(name, RsseWordTag::string) || !state.string_table.data())
                return;
            const auto wanted = program_name(state, numeric::word(name ^ std::to_underlying(RsseWordTag::string)));
            const auto matches = [&](const RsseScriptState& other) {
                return other.string_table.data()
                    && other.program_name_string_offset >= 0
                    && program_name(other, other.program_name_string_offset) == wanted;
            };
            std::uint32_t count = 0;
            for (auto* other = c.scripts.states.head; other; other = other->next)
                if (matches(*other))
                    ++count;
            if (!count)
                return;
            if (!c.simulation.reference)
                throw std::invalid_argument("RSSE random primary World");
            auto selected = (numeric::bits(next_world_prng_value(*c.simulation.reference)) >> 1) % count;
            for (auto* other = c.scripts.states.head; other; other = other->next)
                if (matches(*other) && selected-- == 0) {
                    state.current_value = numeric::word(other->id);
                    if (dest & std::to_underlying(RsseWordTag::value_slot))
                        slot(state, dest ^ std::to_underlying(RsseWordTag::value_slot)) = state.current_value;
                    return;
                }
            return;
        }
        case RsseOpcode::COAST => {
            step_rsse_coaster_instruction(c, state);
            return;
        }
        case RsseOpcode::BUMP => {
            step_rsse_bump_instruction(c, state);
            return;
        }
        case RsseOpcode::TOUR => {
            step_rsse_tour_instruction(c, state);
            return;
        }
        case RsseOpcode::GETCUSTPTCLCODE => {
            const auto dest = read();
            (void)resolve();
            state.current_value = 0;
            destination(state, dest);
            return;
        }
        case RsseOpcode::SETREVERB => {
            state.reverb_preset_override = resolve();
            return;
        }
        case RsseOpcode::END => {
            state.instruction_cursor = stopped;
            return;
        }
        case _ => {
            if (animation_step(c, state, opcode, duration_scale))
                return;
            if (step_rsse_passenger_instruction(c, state, opcode))
                return;
            if (step_rsse_action_instruction(c, state, opcode))
                return;
            if (std::to_underlying(opcode) >= rsse_opcode_descriptors.size()) {
                state.instruction_cursor = stopped;
                return;
            }
            throw std::logic_error("RSSE opcode is not yet reconstructed: "
                                   + std::string{ rsse_opcode_descriptors[std::to_underlying(opcode)].mnemonic });
        }
    }
}

void run_rsse_instruction_slice(ScriptWorld& c, RsseScriptState& state) {
    c.scripts.states.step_control = 0;
    state.remaining_instruction_steps = state.instruction_step_budget;
    while (state.remaining_instruction_steps > 0 && state.instruction_cursor >= 0) {
        const auto instruction = state.instruction_cursor;
        try {
            step_rsse_instruction(c, state);
        } catch (const std::exception& error) {
            const auto source =
                state.script_source
                    ? state.script_source->path.native_encoded_string() + ":" + state.script_source->archive_entry
                    : std::string{};
            throw std::runtime_error("RSSE state "
                                     + std::to_string(state.id)
                                     + " at word "
                                     + std::to_string(instruction)
                                     + " ("
                                     + source
                                     + "): "
                                     + error.what());
        }
        if (!c.scripts.states.step_control)
            --state.remaining_instruction_steps;
    }
}
} //namespace simcoaster
