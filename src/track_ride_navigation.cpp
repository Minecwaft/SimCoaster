#include "track_ride_internal.hpp"

namespace simcoaster::track_detail {
std::int32_t magnitude(std::int32_t x, std::int32_t y) {
    auto ax = fixed::absolute(x), ay = fixed::absolute(y);
    unsigned shift = 0;
    while (ax > 32768 || ay > 32767) {
        ax >>= 1;
        ay >>= 1;
        ++shift;
    }
    const auto square = fixed::add(mul(ax, ax), mul(ay, ay));
    return fixed::word(static_cast<std::uint32_t>(fixed::truncate_integer(std::sqrt(static_cast<double>(square)))) << shift);
}

std::int32_t sine(std::int32_t index) {
    static const auto samples = [] {
        std::array<std::int32_t, 512> result {};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = fixed::truncate_integer(std::sin(static_cast<double>(i * 2) * 0.00613591796875) * 256.0);
        }
        return result;
    }();
    return samples[static_cast<std::uint32_t>(index) & 511u];
}

std::int32_t cosine(std::int32_t index) {
    return sine(fixed::add(index, 128));
}

bool contains(const TrackRideGeometryRegion& region, std::int32_t x, std::int32_t y) {
    const auto dx = fixed::subtract(region.origin_x, x), dy = fixed::subtract(region.origin_y, y);
    if (!dx && !dy) {
        return true;
    }
    const auto directions = static_cast<std::uint32_t>((dx >= 0 ? 2 : 4) | (dy >= 0 ? 16 : 8));
    if ((region.flags & directions) != directions) {
        return false;
    }
    if (region.flags & 32u) {
        return fixed::absolute(dx) <= region.extent_x && fixed::absolute(dy) <= region.extent_y;
    }
    return magnitude(dx, dy) <= region.extent_x;
}

bool overlaps(const TrackRideGeometryRegion& a, const TrackRideGeometryRegion& b) {
    if (&a == &b) {
        return true;
    }
    const auto am = a.flags & 30u, bm = b.flags & 30u;
    const auto near = [&] {
        return magnitude(fixed::subtract(a.origin_x, b.origin_x), fixed::subtract(a.origin_y, b.origin_y)) < fixed::add(std::max(a.extent_x, a.extent_y), std::max(b.extent_x, b.extent_y));
    };
    if (am == 30 && bm == 30) {
        return near();
    }
    if (((a.flags | b.flags) & 30u) != 30) {
        return a.origin_x == b.origin_x && a.origin_y == b.origin_y;
    }
    const auto& directional = am == 30 ? b : a;
    const auto& other = am == 30 ? a : b;
    if ((!(directional.flags & 2u) || other.origin_x >= directional.origin_x) && (!(directional.flags & 4u) || other.origin_x <= directional.origin_x) && (!(directional.flags & 8u) || other.origin_y <= directional.origin_y) && (!(directional.flags & 16u) || other.origin_y >= directional.origin_y)) {
        return false;
    }
    if ((other.flags & 30u) == 30) {
        return near();
    }
    return fixed::add(fixed::absolute(fixed::subtract(a.origin_x, b.origin_x)), fixed::absolute(fixed::subtract(a.origin_y, b.origin_y))) == 6144;
}

std::optional<std::size_t> find_region(TrackRideManager& m, std::int32_t x, std::int32_t y) {
    for (std::size_t i = 0; i < m.geometry.size(); ++i) {
        const auto& r = m.geometry[i];
        if ((r.flags & 1u) && !(r.flags & 64u) && contains(r, x, y)) {
            return i;
        }
    }
    return std::nullopt;
}

namespace {
std::pair<std::int32_t, std::int32_t> region_waypoint(const TrackRideGeometryRegion& r) {
    auto x = r.origin_x, y = r.origin_y;
    if (r.flags & 8u) {
        if (!(r.flags & 16u)) {
            y = fixed::add(y, 2172);
        }
    } else {
        y = fixed::subtract(y, 2172);
    }
    if (r.flags & 2u) {
        if (!(r.flags & 4u)) {
            x = fixed::subtract(x, 2172);
        }
    } else {
        x = fixed::add(x, 2172);
    }
    return { x, y };
}

bool water_family(const TrackRide& r) {
    return r.template_tag == -5 || r.template_tag == -8 || r.template_tag == -12 || r.template_tag == -13;
}

void water_impact(TrackRideSimulation& s, std::int32_t old_heading, std::int32_t new_heading, std::int32_t penetration) {
    const auto turn = static_cast<std::uint32_t>(fixed::subtract(new_heading, old_heading)) & 511u;
    const auto delta = mul(turn >= 256u ? 48 : -48, penetration), next = fixed::add(delta, s.desired_heading);
    s.desired_heading = delta >= 0 ? std::min(delta, next) : std::max(delta, next);
}
} //namespace

std::optional<std::size_t> nearest_region(TrackRideManager& m, std::size_t source, std::int32_t x, std::int32_t y, bool include_source) {
    const auto& from = m.geometry.at(source);
    std::optional<std::size_t> best;
    auto distance = std::numeric_limits<std::int32_t>::max();
    for (std::size_t i = 0; i < m.geometry.size(); ++i) {
        const auto& region = m.geometry[i];
        if (!(region.flags & 1u) || (region.flags & 64u) || (!include_source && i == source) || !overlaps(from, region)) {
            continue;
        }
        const auto [wx, wy] = region_waypoint(region);
        if (std::ranges::any_of(m.geometry, [&](const auto& solid) {
                return (solid.flags & 65u) == 65u && contains(solid, wx, wy);
            })) {
            continue;
        }
        const auto candidate = magnitude(fixed::subtract(region.origin_x, x), fixed::subtract(region.origin_y, y));
        if (candidate < distance) {
            distance = candidate;
            best = i;
        }
    }
    return best;
}

std::int32_t steer(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s, std::int32_t x, std::int32_t y) {
    auto& m = manager(c);
    if (has(s, Flag::rebuild_target) && s.active_geometry) {
        const auto target = find_region(m, x, y);
        if (!target) {
            s.target_x = x;
            s.target_y = y;
            return -1;
        }
        if (overlaps(m.geometry.at(*s.active_geometry), m.geometry.at(*target))) {
            s.target_x = fixed::add(x, s.target_offset_x);
            s.target_y = fixed::add(y, s.target_offset_y);
        } else {
            const auto nearest = nearest_region(m, *s.active_geometry, x, y, false);
            if (!nearest) {
                s.velocity_x = s.velocity_y = 0;
                return -1;
            }
            const auto [wx, wy] = region_waypoint(m.geometry[*nearest]);
            s.target_x = wx;
            s.target_y = wy;
            s.flags |= bit(Flag::cross_region_waypoint);
        }
        s.flags &= ~bit(Flag::rebuild_target);
    }
    const auto dx = fixed::subtract(s.target_x, s.position_x), dy = fixed::subtract(s.target_y, s.position_y);
    const auto direction = fixed::truncate_integer(std::atan2(static_cast<double>(dx), static_cast<double>(fixed::negate(dy))) * 0.3183101550488766 * -256.0 + 256.0);
    auto difference = static_cast<std::int32_t>(static_cast<std::uint32_t>(fixed::subtract(direction, s.steering_heading)) & 511u);
    const auto distance = magnitude(dx, dy);
    auto turn = r.current_values[2];
    if (difference > 256) {
        turn = fixed::negate(turn);
        difference = 512 - difference;
    }
    if (difference > r.current_values[2] / 2 && r.fault != TrackRideFault::broken && has(s, Flag::drive_enabled)) {
        s.steering_heading = fixed::add(s.steering_heading, turn) & 511;
    }
    const auto tolerance = std::clamp(distance / 512, 22, 48);
    if (difference < tolerance && (s.flags & (bit(Flag::drive_enabled) | bit(Flag::approach_damping))) == bit(Flag::drive_enabled) && r.fault != TrackRideFault::broken) {
        auto thrust = fixed::add(s.thrust_variation, r.current_values[0]);
        if (!s.active_geometry) {
            throw std::logic_error("driving track ride has no region");
        }
        if ((m.geometry.at(*s.active_geometry).flags & 30u) != 30) {
            thrust = mul(3, thrust) / 4;
        }
        s.velocity_x = fixed::add(s.velocity_x, mul(mul(4, thrust), sine(s.steering_heading)) / 1024);
        s.velocity_y = fixed::add(s.velocity_y, mul(mul(4, thrust), cosine(s.steering_heading)) / 1024);
    }
    return distance;
}

void resolve_region(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    auto& m = manager(c);
    if (!s.active_geometry) {
        throw std::logic_error("track ride simulation has no navigable region");
    }
    if (contains(m.geometry.at(*s.active_geometry), s.position_x, s.position_y)) {
        return;
    }
    if (const auto region = find_region(m, s.position_x, s.position_y)) {
        s.active_geometry = region;
        s.flags |= bit(Flag::rebuild_target);
        return;
    }
    const auto& region = m.geometry.at(*s.active_geometry);
    if (s.boundary_correction_count >= 0) {
        s.boundary_correction_count = fixed::add(s.boundary_correction_count, 1);
    }
    const auto dx = fixed::subtract(s.position_x, region.origin_x), dy = fixed::subtract(s.position_y, region.origin_y);
    const auto old_heading = heading(s.velocity_x_secondary, s.velocity_y_secondary);
    std::int32_t penetration {};
    if (region.flags & 32u) {
        const auto ax = fixed::absolute(dx), ay = fixed::absolute(dy);
        penetration = dx;
        if (ax > region.extent_x) {
            s.velocity_x = scale(s.velocity_x, r.current_values[3], -1024);
            s.position_x = fixed::add(region.origin_x, dx < 0 ? fixed::negate(region.extent_x) : region.extent_x);
            penetration = fixed::absolute(fixed::subtract(region.extent_x, ax));
        }
        if (ay > region.extent_y) {
            s.velocity_y = scale(s.velocity_y, r.current_values[3], -1024);
            s.position_y = fixed::add(region.origin_y, dy < 0 ? fixed::negate(region.extent_y) : region.extent_y);
            penetration = fixed::absolute(fixed::subtract(region.extent_y, ay));
        }
    } else {
        const auto radial = fixed::truncate_integer(std::atan2(static_cast<double>(dx), static_cast<double>(fixed::negate(dy))) * 0.3183101550488766 * 256.0 - 256.0);
        const auto rotation = (2 * ((-128 - radial) & 511) - 256 - ((old_heading - 256) & 511) - old_heading) & 511;
        const auto vx = scale(s.velocity_x, r.current_values[3]), vy = scale(s.velocity_y, r.current_values[3]);
        s.velocity_x = mul(4, fixed::add(mul(vx, cosine(rotation)), mul(vy, sine(rotation)))) / 1024;
        s.velocity_y = mul(4, fixed::subtract(mul(vy, cosine(rotation)), mul(vx, sine(rotation)))) / 1024;
        const auto distance = magnitude(dx, dy);
        penetration = fixed::absolute(fixed::subtract(region.extent_x, distance));
        s.position_x = fixed::add(region.origin_x, div(mul(dx, region.extent_x), distance));
        s.position_y = fixed::add(region.origin_y, div(mul(dy, region.extent_x), distance));
    }
    if (water_family(r)) {
        water_impact(s, old_heading, heading(s.velocity_x, s.velocity_y), penetration);
    }
}

void collide_solids(WorldRuntime& c, TrackRideSimulation& s) {
    auto& m = manager(c);
    TrackRide* r = nullptr;
    for (auto& owner : m.rides) {
        if (owner) {
            for (auto& row : owner->simulations) {
                if (&row == &s) {
                    r = owner.get();
                }
            }
        }
    }
    if (!r) {
        throw std::logic_error("track ride collision owner");
    }
    for (const auto& solid : m.geometry) {
        if ((solid.flags & 65u) == 65u && contains(solid, s.position_x, s.position_y)) {
            const auto dx = fixed::subtract(s.position_x, solid.origin_x), dy = fixed::subtract(s.position_y, solid.origin_y);
            const auto old_heading = heading(fixed::negate(s.velocity_x_secondary), fixed::negate(s.velocity_y_secondary)), normal = heading(dx, dy);
            const auto difference = static_cast<std::uint32_t>(fixed::subtract(old_heading, normal)) & 511u;
            if (difference >= 128u && difference <= 384u) {
                continue;
            }
            const auto square = fixed::add(mul(s.velocity_x_secondary, s.velocity_x_secondary), mul(s.velocity_y_secondary, s.velocity_y_secondary));
            const auto speed = fixed::truncate_integer(std::sqrt(static_cast<double>(square)));
            const auto projected = mul(mul(4, cosine(static_cast<std::int32_t>(difference))), speed) / 1024;
            const auto impulse = scale(fixed::add(r->current_values[3], 1024), projected);
            s.velocity_x = fixed::add(s.velocity_x, mul(mul(4, impulse), sine(normal)) / 1024);
            s.velocity_y = fixed::add(s.velocity_y, mul(mul(4, impulse), cosine(normal)) / 1024);
            s.flags |= bit(Flag::rebuild_target) | bit(Flag::collision_contact);
            const auto distance = magnitude(dx, dy);
            s.position_x = fixed::add(solid.origin_x, div(mul(dx, solid.extent_x), distance));
            s.position_y = fixed::add(solid.origin_y, div(mul(dy, solid.extent_x), distance));
            if (water_family(*r)) {
                const auto new_heading = fixed::truncate_integer(std::atan2(static_cast<double>(fixed::negate(s.velocity_x)), static_cast<double>(s.velocity_y)) * 0.3183101550488766 * -256.0 + 256.0);
                water_impact(s, old_heading, new_heading, fixed::absolute(fixed::subtract(distance, solid.extent_x)));
            }
        }
    }
}
} //namespace simcoaster::track_detail
