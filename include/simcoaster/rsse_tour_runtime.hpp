#pragma once
#include "world_domains_fwd.hpp"
#include "rsse_tour.hpp"

namespace simcoaster {
struct RsseScriptState;
enum class RsseTourCommand : std::uint32_t {
    create = 1,
    destroy = 2,
    board = 3,
    unload = 4,
    set_count = 5,
    set_duration = 8,
    set_effect = 9,
    docked_ready = 10,
    start = 11,
    fixed_approach = 12,
    enabled = 14,
    damaged_docked = 15,
    busy = 16,
    separation = 17,
    attitude = 18
};
void initialize_rsse_tours(ScriptWorld&);
void update_rsse_tour_bounds(ScriptWorld&);
std::uint32_t create_rsse_tour(ScriptWorld&, std::uint16_t owner, std::uint32_t script, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t heading, std::int32_t short_approach);
int spawn_rsse_tour_element(ScriptWorld&, std::uint32_t handle);
int board_rsse_tour_passenger(ScriptWorld&, std::uint32_t handle, std::uint32_t person);
std::uint32_t unload_rsse_tour_passenger(ScriptWorld&, std::uint32_t handle, bool force = false);
void set_rsse_tour_effect(ScriptWorld&, std::uint32_t handle, RsseTourEffect);
void set_rsse_tour_enabled(ScriptWorld&, std::uint32_t handle, bool enabled);
void set_rsse_tour_element_count(ScriptWorld&, std::uint32_t handle, std::uint32_t count);
int start_rsse_tour(ScriptWorld&, std::uint32_t handle);
bool rsse_tour_busy(const ScriptQuery&, std::uint32_t handle);
void step_rsse_tours(ScriptWorld&);
void render_rsse_tours(ScriptWorld&, float interpolation);
void step_rsse_tour_instruction(ScriptWorld&, RsseScriptState&);
} //namespace simcoaster
