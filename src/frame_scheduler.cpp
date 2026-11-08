#include "simcoaster/frame_scheduler.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
FrameAdvance FrameScheduler::advance_clocks(FrameClocks& mesh_clocks, std::chrono::milliseconds elapsed, double scale, bool full_world, bool paused, bool ui_paused) {
    if (elapsed.count() < 0 || mesh_clocks.unscaled.count() > std::numeric_limits<std::int64_t>::max() - elapsed.count()) {
        throw std::invalid_argument("invalid park frame duration");
    }
    std::int64_t simulation_ms = ui_paused ? 0 : elapsed.count();
    if (full_world) {
        const auto rate = paused ? 0. : scale;
        if (!std::isfinite(rate) || rate < 0) {
            throw std::invalid_argument("invalid simulation clock scale");
        }
        const auto scaled = static_cast<double>(elapsed.count()) * rate + fractional_simulation_ms;
        if (!std::isfinite(scaled) || scaled >= std::ldexp(1., 63)) {
            throw std::overflow_error("simulation frame duration");
        }
        simulation_ms = static_cast<std::int64_t>(scaled);
        fractional_simulation_ms = scaled - static_cast<double>(simulation_ms);
    }
    if (mesh_clocks.simulation.count() > std::numeric_limits<std::int64_t>::max() - simulation_ms) {
        throw std::overflow_error("simulation clock");
    }
    if (!script_step_cursor) {
        script_step_cursor = mesh_clocks.simulation.count();
    }
    if (!auxiliary_step_cursor) {
        auxiliary_step_cursor = mesh_clocks.simulation.count();
    }
    if (!world_step_cursor) {
        world_step_cursor = mesh_clocks.simulation.count();
    }
    mesh_clocks.simulation += std::chrono::milliseconds { simulation_ms };
    //Native42A170 pauses both gameplay clock domains for a modal message;
    //the independently owned UI scheduler continues on wall time.
    if (!ui_paused) {
        mesh_clocks.unscaled += elapsed;
    }
    return { std::chrono::milliseconds { simulation_ms }, mesh_clocks.simulation.count() };
}
} //namespace simcoaster
