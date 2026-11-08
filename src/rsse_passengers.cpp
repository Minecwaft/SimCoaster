#include "simcoaster/numeric_word.hpp"
#include "simcoaster/rsse_passengers.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t truncate_word(double value) {
    const auto limit = std::ldexp(1.0, 63);
    if (!std::isfinite(value) || value < -limit || value >= limit)
        throw std::domain_error("RSSE unrepresentable walk duration/direction");
    return numeric::word(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}

std::int32_t direction8(double z, double x) {
    return truncate_word((6.28318 - (std::atan2(z, x) - 1.570795)) * 1.2732406201955062 + 0.5) % 8;
}

std::array<float, 3> point(ScriptWorld& c, const RsseScriptState& state, std::int32_t node, std::uint32_t flags) {
    const auto result = resolve_rsse_action_source_transform(c, state, node, flags);
    if (!result)
        throw std::invalid_argument("RSSE unresolved walk endpoint");
    return result->position;
}

std::uint32_t walk_duration(const std::array<float, 3>& from, const std::array<float, 3>& to) {
    //Whole distance units are truncated first. This is not trunc(100*distance).
    const auto duration = 100u * numeric::bits(truncate_word(distance_rsse_points(from, to)));
    return duration ? duration : 100u;
}

void beam(ScriptWorld& c, const std::array<float, 3>& p) {
    std::array<std::int32_t, 3> position{};
    for (std::size_t i = 0; i < 3; ++i)
        position[i] = numeric::word(numeric::bits(truncate_word(p[i])) << 10);
    (void)c.ports.effects.create_particle_effect(c, 19, position, 0);
}
} //namespace

double distance_rsse_points(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const auto y = static_cast<double>(a[1]) - b[1], z = static_cast<double>(a[2]) - b[2],
               x = static_cast<double>(a[0]) - b[0];
    return std::sqrt(z * z + y * y + x * x);
}

std::int32_t quantize_rsse_direction8(float z, float x) {
    return direction8(z, x);
}

void start_rsse_walk_float(ScriptWorld& c,
                           RsseScriptState& state,
                           std::int32_t duration,
                           std::uint16_t horizontal,
                           std::uint16_t vertical) {
    state.walk_float_start_ms = c.ports.clock.get_simulation_clock_time_ms(c);
    state.walk_float_duration_ms = duration;
    state.walk_float_horizontal_scale_milli = horizontal;
    state.walk_float_vertical_scale_milli = vertical;
}

std::uint32_t dequeue_rsse_walk_result(RsseScriptState& state) {
    for (auto& walk : state.walk_records)
        if (walk.phase == RsseWalkPhase::complete) {
            const auto person = walk.person_id;
            walk.phase = RsseWalkPhase::unused;
            walk.person_id = 0;
            return person;
        }
    return 0;
}

void begin_rsse_walk_exit(ScriptWorld& c, RsseScriptState& state, std::uint32_t id) {
    const auto found = std::ranges::find_if(state.walk_records, [&](const auto& walk) {
        return walk.phase != RsseWalkPhase::unused && walk.person_id == id;
    });
    if (found == state.walk_records.end())
        return;
    auto& walk = *found;
    const auto from = point(c, state, walk.exit_start_node, walk.kind == RsseWalkKind::mesh_attach ? 128u : 2048u);
    const auto to = point(c, state, walk.exit_end_node, 2048);
    const auto duration = walk_duration(from, to);
    walk.direction = static_cast<std::int16_t>(
        direction8(static_cast<double>(to[2]) - from[2], static_cast<double>(to[0]) - from[0]));
    //WALKOFF writes end then start, opposite to WALKON. Keep both clock reads.
    walk.phase_end_ms = duration + c.ports.clock.get_simulation_clock_time_ms(c);
    walk.phase_start_ms = c.ports.clock.get_simulation_clock_time_ms(c);
    if (walk.kind == RsseWalkKind::beam)
        beam(c, point(c, state, walk.exit_start_node, 2048));
    if (walk.kind == RsseWalkKind::mesh_attach && walk.phase == RsseWalkPhase::active) {
        const auto index =
            c.ports.meshes.find_embedded_node_resource_record(c, state.mesh_instance_handle, 128, walk.entry_end_node);
        if (index != -1)
            world_operations::release_mesh_model_object_instance(c, state.mesh_instance_handle, index);
    }
    walk.phase = RsseWalkPhase::exiting;
}

bool step_rsse_passenger_instruction(ScriptWorld& c, RsseScriptState& state, RsseOpcode opcode) {
    const auto read = [&] {
        return read_rsse_instruction_word(state);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(state, read());
    };
    const auto clock = [&] {
        return c.ports.clock.get_simulation_clock_time_ms(c);
    };
    const auto is_destination = [](std::uint32_t word) {
        return (word & rsse_tag_mask) == std::to_underlying(RsseWordTag::value_slot);
    };
    const auto output = [&](std::uint32_t word) {
        if (is_destination(word)) {
            const auto index = word ^ std::to_underlying(RsseWordTag::value_slot);
            if (index >= state.value_slots.size())
                throw std::out_of_range("RSSE passenger destination");
            state.value_slots[index] = state.current_value;
        }
    };
    match (opcode) {
        case RsseOpcode::ADDHEAD => {
            const auto id = numeric::bits(resolve());
            if (std::ranges::find(state.attached_person_ids, 0u) == state.attached_person_ids.end())
                return true;
            if (!c.simulation.reference)
                throw std::invalid_argument("RSSE attachment random primary World");
            std::size_t index;
            do {
                index = (numeric::bits(next_world_prng_value(*c.simulation.reference)) >> 1) % state.attached_person_ids.size();
            } while (state.attached_person_ids[index]);
            state.attached_person_ids[index] = id;
            const auto model = c.ports.meshes.find_embedded_node_resource_record(c,
                                                                                 state.mesh_instance_handle,
                                                                                 128,
                                                                                 static_cast<std::int32_t>(index + 1));
            if (model != -1)
                world_operations::bind_world_person_sprite_to_mesh_model_object(c,
                                                                                state.mesh_instance_handle,
                                                                                model,
                                                                                id);
            return true;
        }
        case RsseOpcode::DELHEAD => {
            const auto id = numeric::bits(resolve());
            for (std::size_t i = 0; i < state.attached_person_ids.size(); ++i)
                if (state.attached_person_ids[i] == id) {
                    const auto index =
                        c.ports.meshes.find_embedded_node_resource_record(c,
                                                                          state.mesh_instance_handle,
                                                                          128,
                                                                          static_cast<std::int32_t>(i + 1));
                    if (index != -1)
                        world_operations::release_mesh_model_object_instance(c, state.mesh_instance_handle, index);
                    state.attached_person_ids[i] = 0;
                }
            return true;
        }
        case RsseOpcode::LIMBO => {
            const auto id = numeric::bits(resolve()), duration = numeric::bits(resolve());
            for (auto& record : state.limbo_records)
                if (!record.person_id) {
                    record.person_id = id;
                    record.release_after_ms = clock() + 1000u * duration;
                    state.limbo_count = numeric::word(numeric::bits(state.limbo_count) + 1u);
                    state.current_value = 1;
                    return true;
                }
            state.current_value = 0;
            return true;
        }
        case RsseOpcode::UNLIMBO or RsseOpcode::FORCEUNLIMBO => {
            const auto dest = read();
            const bool force = opcode == RsseOpcode::FORCEUNLIMBO;
            if (force && !is_destination(dest))
                return true;
            const auto now = force ? 0u : clock();
            state.current_value = 0;
            for (auto& record : state.limbo_records)
                if (record.person_id && (force || numeric::word(record.release_after_ms) < numeric::word(now))) {
                    state.current_value = numeric::word(record.person_id);
                    record.person_id = 0;
                    state.limbo_count = numeric::word(numeric::bits(state.limbo_count) - 1u);
                    break;
                }
            output(dest);
            return true;
        }
        case RsseOpcode::INLIMBO or RsseOpcode::LIMBOSPACE => {
            const auto dest = read();
            state.current_value = opcode == RsseOpcode::INLIMBO ? state.limbo_count : numeric::word(static_cast<std::uint32_t>(state.limbo_records.size()) - numeric::bits(state.limbo_count));
            output(dest);
            return true;
        }
        case RsseOpcode::BOUNCESETNODE => {
            state.bounce_first_node_id = numeric::word(read());
            return true;
        }
        case RsseOpcode::BOUNCESETBASE => {
            state.bounce_base_height = numeric::low_short(resolve());
            return true;
        }
        case RsseOpcode::BOUNCE => {
            const auto id = numeric::bits(resolve()), duration = numeric::bits(resolve());
            for (std::size_t i = 0; i < state.bounce_records.size(); ++i)
                if (!state.bounce_records[i].person_id) {
                    auto& record = state.bounce_records[i];
                    record.person_id = id;
                    record.attachment_node_id = numeric::word(static_cast<std::uint32_t>(i) + numeric::bits(state.bounce_first_node_id));
                    record.release_after_ms = clock() + 1000u * duration;
                    record.start_ms = clock();
                    ++state.bounce_count;
                    state.current_value = 1;
                    return true;
                }
            state.current_value = 0;
            return true;
        }
        case RsseOpcode::UNBOUNCE or RsseOpcode::FORCEUNBOUNCE => {
            const auto dest = read();
            const auto deadline_clock = clock();
            for (auto& record : state.bounce_records)
                if (record.person_id) {
                    const auto elapsed = numeric::word(clock() - record.start_ms);
                    if ((opcode == RsseOpcode::FORCEUNBOUNCE || numeric::word(record.release_after_ms) < numeric::word(deadline_clock)) && elapsed % 1000 / 200 == 0) {
                        --state.bounce_count;
                        state.current_value = numeric::word(record.person_id);
                        record.person_id = 0;
                        output(dest);
                        return true;
                    }
                }
            state.current_value = 0;
            output(dest);
            return true;
        }
        case RsseOpcode::BOUNCING => {
            const auto dest = read();
            state.current_value = std::bit_cast<std::int16_t>(state.bounce_count);
            output(dest);
            return true;
        }
        case RsseOpcode::WALKON => {
            const auto id = numeric::bits(resolve());
            const auto entry_start = resolve(), entry_end = resolve(), exit_start = resolve(), exit_end = resolve(),
                       kind = resolve(), flags = resolve();
            const auto from = point(c, state, entry_start, 2048),
                       to = point(c, state, entry_end, kind == 4 ? 128u : 2048u);
            const auto duration = walk_duration(from, to);
            const auto found = std::ranges::find_if(state.walk_records, [](const auto& walk) {
                return walk.phase == RsseWalkPhase::unused;
            });
            if (found == state.walk_records.end())
                return true;
            auto& walk = *found;
            walk.entry_start_node = numeric::low_short(entry_start);
            walk.entry_end_node = numeric::low_short(entry_end);
            walk.exit_start_node = numeric::low_short(exit_start);
            walk.exit_end_node = numeric::low_short(exit_end);
            walk.person_id = id;
            walk.kind = static_cast<RsseWalkKind>(numeric::low_short(kind));
            walk.phase_start_ms = clock();
            walk.phase_end_ms = duration + clock();
            walk.direction = static_cast<std::int16_t>(
                quantize_rsse_direction8(static_cast<float>(static_cast<double>(to[2]) - from[2]),
                                         static_cast<float>(static_cast<double>(to[0]) - from[0])));
            walk.phase = RsseWalkPhase::entering;
            walk.flags = static_cast<RsseWalkFlags>(static_cast<std::uint16_t>(flags));
            return true;
        }
        case RsseOpcode::WALKOFF => {
            const auto id = numeric::bits(resolve());
            begin_rsse_walk_exit(c, state, id);
            return true;
        }
        case RsseOpcode::WALKGET => {
            const auto dest = read();
            state.current_value = numeric::word(dequeue_rsse_walk_result(state));
            output(dest);
            return true;
        }
        case RsseOpcode::WALKST_FLOAT => {
            const auto duration = resolve(), horizontal = resolve(), vertical = resolve();
            start_rsse_walk_float(c, state, numeric::word(1000u * numeric::bits(duration)), static_cast<std::uint16_t>(horizontal), static_cast<std::uint16_t>(vertical));
            return true;
        }
        case RsseOpcode::WALKFLOATSTAT => {
            const auto dest = read();
            state.current_value = state.walk_float_duration_ms;
            output(dest);
            return true;
        }
        case RsseOpcode::WALKFLOATSTOP => {
            if (state.walk_float_duration_ms) {
                const auto first = clock(), second = clock();
                const auto normalized = (1000u * first - 1000u * state.walk_float_start_ms) / numeric::bits(state.walk_float_duration_ms);
                state.walk_float_duration_ms = 1000;
                state.walk_float_start_ms = second - normalized;
            }
            return true;
        }
        case _ => return false;
    }
}
} //namespace simcoaster
