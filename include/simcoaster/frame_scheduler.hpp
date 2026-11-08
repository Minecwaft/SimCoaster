#pragma once
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace simcoaster {
struct FrameClocks {
    std::chrono::milliseconds simulation{}, unscaled{};
};

struct FrameAdvance {
    std::chrono::milliseconds simulation_delta;
    std::int64_t now;
};

//Owns the fixed-step timeline independently of assets, UI and rendering.
//The caller publishes the frame delta before delivering any due steps.
struct FrameScheduler {
    std::optional<std::int64_t> script_step_cursor;
    std::uint64_t script_substeps{};
    std::optional<std::int64_t> auxiliary_step_cursor, world_step_cursor;
    double fractional_simulation_ms{};

    FrameAdvance
    advance_clocks(FrameClocks&, std::chrono::milliseconds, double scale, bool full_world, bool paused, bool ui_paused);

    //Concrete callbacks inline into the owning runtime. No virtual scheduler,
    //function wrapper, per-step allocation or reordered batch is introduced.
    template <class Script, class Auxiliary, class World, class WorldAudio>
    void
    run_due_steps(std::int64_t now, Script&& script, Auxiliary&& auxiliary, World&& world, WorldAudio&& world_audio) {
        if (now - *script_step_cursor > 2000)
            *script_step_cursor = now - 2000;
        unsigned catchup = 0;
        while (now > *script_step_cursor) {
            *script_step_cursor = *script_step_cursor > std::numeric_limits<std::int64_t>::max() - 31
                                    ? std::numeric_limits<std::int64_t>::max()
                                    : *script_step_cursor + 31;
            ++script_substeps;
            if (catchup < 3)
                script();
            if (!(script_substeps & 1u)) {
                auxiliary_step_cursor = *script_step_cursor;
                if (catchup < 3)
                    auxiliary();
            }
            if (!(script_substeps & 7u)) {
                world_step_cursor = *script_step_cursor;
                if (catchup < 3) {
                    ++catchup;
                    world();
                }
                world_audio();
            }
        }
    }
};
} //namespace simcoaster
