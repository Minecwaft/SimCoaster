#include "simcoaster/require_bound.hpp"
#include "simcoaster/rsse_vm.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace simcoaster {
void step_rsse_coaster_instruction(ScriptWorld& c, RsseScriptState& script) {
    const auto read = [&] {
        return read_rsse_instruction_word(script);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(script, read());
    };
    const auto output = [&](std::uint32_t destination, std::uint32_t value) {
        script.current_value = std::bit_cast<std::int32_t>(value);
        if ((destination & rsse_tag_mask) == std::to_underlying(RsseWordTag::value_slot))
            script.value_slots.at(destination ^ std::to_underlying(RsseWordTag::value_slot)) = script.current_value;
    };
    const auto binding = [&]() -> WorldCoasterBinding& {
        auto* world = c.resources.meshes ? c.resources.meshes->coasters() : nullptr;
        return require_bound<std::invalid_argument>(world ? world->find_map(script.coaster_runtime_map_id) : nullptr, "RSSE COAST has no bound ride runtime");
    };
    match (static_cast<RsseCoastCommand>(read())) {
        case RsseCoastCommand::enqueue_person => {
            const auto person = resolve();
            (void)binding().train_state.queue_a.push(static_cast<std::uint32_t>(person));
            return;
        }
        case RsseCoastCommand::get_space => {
            const auto destination = read();
            output(destination, static_cast<std::uint32_t>(binding().train_state.queue_a.logical_space()));
            return;
        }
        case RsseCoastCommand::dequeue_person => {
            const auto destination = read();
            output(destination, dequeue_completed_ride_exit(binding().train_state));
            return;
        }
        case RsseCoastCommand::set_playback => {
            const auto selector = resolve();
            auto& ride = binding();
            if (selector >= 0 && selector <= 2) {
                constexpr std::array requests{ RideDetailState::run_16,
                                               RideDetailState::paused,
                                               RideDetailState::run_32 };
                transition_coaster_detail_state(c,
                                                *c.resources.meshes,
                                                ride,
                                                requests[static_cast<std::size_t>(selector)]);
            }
            return;
        }
        case RsseCoastCommand::set_passenger_mode => {
            const auto enabled = resolve();
            auto& ride = binding();
            const auto current = ride.train_state.detail_state_flags;
            if (enabled) {
                if (current & 4u)
                    transition_coaster_detail_state(c, *c.resources.meshes, ride, RideDetailState::passenger_mode_8);
            } else if (current & 0xAu)
                transition_coaster_detail_state(c, *c.resources.meshes, ride, RideDetailState::passenger_mode_4);
            return;
        }
        case RsseCoastCommand::set_capacity => {
            const auto capacity = resolve();
            auto& ride = binding();
            (void)c.resources.meshes->set_coaster_capacity(ride.map_id, capacity);
            return;
        }
        case RsseCoastCommand::resolve_discard => {
            (void)resolve();
            return;
        }
        case RsseCoastCommand::bind_map => {
            (void)read();
            script.coaster_runtime_map_id = 0;
            if (auto* world = c.resources.meshes ? c.resources.meshes->coasters() : nullptr)
                for (auto& ride : std::views::reverse(world->bindings))
                    if (ride.script_id == script.id) {
                        script.coaster_runtime_map_id = ride.map_id;
                        (void)c.resources.meshes->set_coaster_speed(c,
                                                                    ride.map_id,
                                                                    static_cast<std::uint32_t>(script.operating_speed));
                        break;
                    }
            return;
        }
        case _ => return;
    }
}
} //namespace simcoaster
