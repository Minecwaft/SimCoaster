#include "rsse_tour_internal.hpp"

namespace simcoaster::tour_detail {
int terrain_height(ScriptWorld& c, int x, int z) {
    if (!inside(c.scripts.tours.bounds, x, z)) {
        return 0;
    }
    if (!c.weather.map_heightfield_width || !c.weather.map_heightfield_height) {
        throw std::logic_error("TOUR terrain dimensions are empty");
    }
    const auto cell_x = std::clamp(x / 300 / 10, 0, static_cast<int>(c.weather.map_heightfield_width) - 1), cell_z = std::clamp(z / 300 / 10, 0, static_cast<int>(c.weather.map_heightfield_height) - 1);
    //The original truncates the sampled height BEFORE multiplication by300.
    return mul(300, fixed::truncate_integer(sample_world_map_cell_surface(c, cell_x, cell_z)));
}

int obstacle_height(ScriptWorld& c, int x, int z) {
    const auto& b = c.scripts.tours.bounds;
    if (x >= fixed::add(b.max_x, 20000) || z >= fixed::add(b.max_z, 20000) || x < fixed::subtract(b.min_x, 20000) || z < fixed::subtract(b.min_z, 20000)) {
        return 100000;
    }
    for (std::size_t i = 1; i < c.scripts.tours.slots.size(); ++i) {
        if (const auto* other = c.scripts.tours.slots[i].get(); other && distance(fixed::subtract(x, other->reference_x), fixed::subtract(z, other->reference_z)) < 10000) {
            return 30000;
        }
    }
    return terrain_height(c, x, z);
}

void choose_holding_target(RsseTourRecord& r, RsseTourEntry& e) {
    e.previous_target_x = e.target_x;
    e.previous_target_y = e.target_y;
    e.previous_target_z = e.target_z;
    e.target_y = std::max(fixed::add(r.reference_y, 5000), 14000);
    e.target_quadrant = (e.target_quadrant + 1u) & 3u;
    e.target_x = fixed::add(r.reference_x, e.target_quadrant < 2 ? 25000 : -25000);
    e.target_z = fixed::add(r.reference_z, (e.target_quadrant == 0 || e.target_quadrant == 3) ? 25000 : -25000);
}

void advance_target(ScriptWorld& c, RsseTourEntry& e) {
    const auto dx = fixed::subtract(e.target_x, e.previous_target_x), dz = fixed::subtract(e.target_z, e.previous_target_z);
    const auto sign = [](int v) {
        return v > 0 ? 1 : v < 0 ? -1 : 0;
    };
    int step_x = 0, step_z = 0;
    if (fixed::absolute(dx) > fixed::absolute(dz)) {
        step_x = 20000 * sign(dx);
    } else {
        step_z = 20000 * sign(dz);
    }
    e.previous_target_x = e.target_x;
    e.previous_target_y = e.target_y;
    e.previous_target_z = e.target_z;
    auto clearance = fixed::add(e.current_y, 5000);
    const auto snap = [](int value) {
        const auto base = mul(value / 20000, 20000);
        return value < 0 ? fixed::subtract(base, 20000) : base;
    };
    if (random(c) % 4 == 0 || obstacle_height(c, fixed::add(e.target_x, step_x), fixed::add(e.target_z, step_z)) >= clearance) {
        const std::array<std::array<int, 2>, 4> alternatives { { { step_z, step_x }, { fixed::negate(step_z), fixed::negate(step_x) }, { step_x, step_z }, { fixed::negate(step_x), fixed::negate(step_z) } } };
        bool selected = false;
        while (!selected) {
            for (const auto& offset : alternatives) {
                const auto x = fixed::add(e.target_x, offset[0]), z = fixed::add(e.target_z, offset[1]);
                if (obstacle_height(c, x, z) < clearance) {
                    e.target_x = x;
                    e.target_z = z;
                    selected = true;
                    break;
                }
            }
            if (!selected) {
                if (clearance > std::numeric_limits<int>::max() - 5000) {
                    throw std::overflow_error("TOUR target clearance");
                }
                clearance += 5000;
            }
        }
        e.target_x = fixed::add(snap(e.target_x), 10000);
        e.target_z = fixed::add(snap(e.target_z), 10000);
    } else {
        e.target_x = fixed::add(fixed::add(snap(e.target_x), 10000), step_x);
        e.target_z = fixed::add(fixed::add(snap(e.target_z), 10000), step_z);
    }
    e.target_y = std::max(fixed::add(std::max(terrain_height(c, e.target_x, e.target_z), terrain_height(c, e.current_x, e.current_z)), 5000), 12000);
}
} //namespace simcoaster::tour_detail
