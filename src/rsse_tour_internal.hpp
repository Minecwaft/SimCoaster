#pragma once
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/rsse_tour_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_surface.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster::tour_detail {
using State = RsseTourElementState;
using Effect = RsseTourEffect;

inline bool active(const RsseTourEntry& e) {
    return e.element_state != State::inactive;
}

inline RsseTourRecord* find(ScriptWorld& c, std::uint32_t id) {
    return id && id < c.scripts.tours.slots.size() ? c.scripts.tours.slots[id].get() : nullptr;
}

inline const RsseTourRecord* find(const ScriptQuery& c, std::uint32_t id) {
    return id && id < c.scripts.tours.slots.size() ? c.scripts.tours.slots[id].get() : nullptr;
}

inline std::int32_t mul(std::int32_t a, std::int32_t b) {
    return fixed::narrow(std::int64_t { a } * b);
}

inline std::int32_t divide(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "TOUR integer division");
}

inline std::int32_t scale(std::int32_t a, std::int32_t b, int denominator = 1024) {
    return divide(mul(a, b), denominator);
}

inline std::int32_t random(ScriptWorld& c) {
    if (!c.simulation.current) {
        throw std::logic_error("TOUR World is not bound");
    }
    return fixed::absolute(next_world_prng_value(*c.simulation.current));
}

inline std::int32_t sine(const ScriptQuery& c, int i) {
    return c.scripts.tours.sine_table[static_cast<std::uint32_t>(i) & 4095u];
}

inline std::int32_t cosine(const ScriptQuery& c, int i) {
    return sine(c, fixed::add(i, 1024));
}

inline std::array<std::int32_t, 3> sound_position(const RsseTourEntry& e) {
    return { e.current_x / 300, e.current_y / 300, e.current_z / 300 };
}

inline std::int32_t distance(int x, int z, int y = 0) {
    x /= 32;
    z /= 32;
    y /= 32;
    const auto square = fixed::add(fixed::add(mul(x, x), mul(z, z)), mul(y, y));
    return mul(32, fixed::truncate_integer(std::sqrt(static_cast<double>(square))));
}

inline bool inside(const RsseTourBounds& b, int x, int z) {
    return x < b.max_x && z < b.max_z && x >= b.min_x && z >= b.min_z;
}

void animation(ScriptWorld&, RsseTourEntry&, int selector, std::uint32_t variant = 0, int loop = 0);
void release_element(ScriptWorld&, RsseTourRecord&, RsseTourEntry&);
void release_passenger(ScriptWorld&, RsseTourEntry&, int count);
void sound_parameter(ScriptWorld&, const RsseTourRecord&, RsseTourEntry&, int value);
void choose_holding_target(RsseTourRecord&, RsseTourEntry&);
void advance_target(ScriptWorld&, RsseTourEntry&);
int terrain_height(ScriptWorld&, int x, int z);
int obstacle_height(ScriptWorld&, int x, int z);
void random_outside_target(ScriptWorld&, RsseTourEntry&);
void step_element(ScriptWorld&, RsseTourRecord&, std::size_t index);
} //namespace simcoaster::tour_detail
