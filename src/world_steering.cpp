#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_staff.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
using Vector = FixedPointVector2;

World& primary(WorldRuntime& c) {
    return require_bound(c.simulation.reference, "steering needs the primary World");
}

Vector arrive(const MobileNavigationState& state, Vector target) {
    const auto delta = fixed::subtract(target, state.position);
    const auto length = fixed::magnitude(delta);
    if (!length)
        return {};
    const auto desired = std::min(state.max_speed, length / 2);
    const auto scale = fixed::narrow(std::int64_t{ desired } * 65536 / length);
    return fixed::subtract(fixed::scale(delta, scale), state.velocity);
}

Vector follow_path(WorldRuntime& c, MobileNavigationState& state) {
    const auto x = state.position.x >> 20, y = state.position.y >> 20;
    if (x < 0 || y < 0 || x >= 33 || y >= 33)
        throw std::out_of_range("steering change-serial coordinates");
    if (primary(c).grid_change_serials[y][x] > state.path_timestamp)
        if (!build_world_navigation_path(c, state, state.path_target_position, true))
            state.cannot_reach_destination = 1;
    if (state.path_finished != 1) {
        const auto delta = fixed::subtract(state.subpath_buffer.at(state.path_count), state.position);
        const auto remaining = fixed::add(fixed::approximate_length(delta),
                                          fixed::add(state.path_subpath_distance, state.path_tail_distance));
        if (state.path_total_distance
            && std::int64_t{ remaining } * 65536 / state.path_total_distance
            && get_world_navigation_path_progress(state) != 65536) {
            const auto stuck = std::popcount(state.path_stuck_buffer & 0x7FFFu);
            if (std::int64_t{ stuck } * 65536 / 15 >= 26214)
                if (!build_world_navigation_path(c, state, state.path_target_position, true))
                    state.cannot_reach_destination = 1;
        }
    }
    if (!state.path_total_count || state.path_finished == 1)
        return { fixed::negate(state.velocity.x), fixed::negate(state.velocity.y) };
    Vector target;
    if (state.path_count == state.path_total_count - 1) {
        target = state.path_target_position;
        if (fixed::approximate_length(fixed::subtract(target, state.position)) < fixed::multiply(104857, state.radius))
            state.path_finished = 1;
    } else {
        target = state.subpath_buffer.at(state.path_count);
        if (fixed::approximate_length(fixed::subtract(target, state.position))
            < fixed::add(state.radius, state.radius)) {
            state.path_subpath_distance =
                fixed::subtract(state.path_subpath_distance, state.subpath_distance.at(state.path_count));
            if (++state.path_count == state.path_buffer_count)
                build_world_navigation_path(c, state, state.path_target_position);
        }
        target = state.subpath_buffer.at(state.path_count);
    }
    if (std::bit_cast<std::int32_t>(state.path_count) >= std::bit_cast<std::int32_t>(state.path_total_count - 1))
        return arrive(state, target);
    return fixed::subtract(fixed::subtract(target, state.position), state.velocity);
}

MobileEntityState* mobile_state(WorldRuntime& c, ThingHeader* header) {
    if (header->kind == ThingKind::person)
        return &reinterpret_cast<PersonRuntimeView*>(header)->mobile;
    if (is_staff_record_kind(header->kind))
        return &reinterpret_cast<StaffRuntimeRecord*>(header)->mobile;
    if (header->kind == ThingKind::chat_presence)
        return &c.ports.entities.online_presence_mobile_state(c, header);
    return nullptr;
}

Vector separation(WorldRuntime& c, MobileNavigationState& state) {
    if (state.path_count == state.path_total_count - 1 || state.path_finished) {
        const auto delta = fixed::subtract(state.position, state.subpath_buffer.at(state.path_count));
        if (fixed::approximate_length(delta) <= fixed::add(state.radius, 65536))
            return {};
    }
    const auto radius = fixed::add(state.radius, state.radius), extent = fixed::add(radius, 65535) >> 16;
    const auto cx = state.position.x >> 16, cy = state.position.y >> 16, sx = state.position.x >> 8,
               sy = state.position.y >> 8;
    const auto r8 = radius >> 8;
    const auto radius_squared = fixed::word(static_cast<std::uint32_t>(r8) * static_cast<std::uint32_t>(r8));
    unsigned visited = 0;
    Vector sum{};
    for (auto y = cy - extent; y <= cy + extent; ++y)
        for (auto x = cx - extent; x <= cx + extent; ++x) {
            if (!is_world_map_coordinate_in_bounds(x, y))
                continue;
            auto id = primary(c).map_cells.at(static_cast<std::size_t>(y * 128 + x)).who;
            std::size_t links = 0;
            while (id) {
                if (++links > thing_slot_count)
                    throw std::length_error("cyclic mobile neighbor chain");
                auto* record = c.entities.at(id).payload;
                if (!record)
                    throw std::logic_error("missing mobile neighbor");
                auto* mobile = mobile_state(c, record);
                auto* base = reinterpret_cast<MapObjectBase*>(record);
                if (mobile && &mobile->navigation != &state) {
                    const auto dx = fixed::subtract(std::bit_cast<std::int16_t>(base->x), sx),
                               dy = fixed::subtract(std::bit_cast<std::int16_t>(base->y), sy);
                    const auto squared = fixed::word(static_cast<std::uint32_t>(dx) * static_cast<std::uint32_t>(dx)
                                                     + static_cast<std::uint32_t>(dy) * static_cast<std::uint32_t>(dy));
                    if (squared && squared < radius_squared) {
                        const auto& other = mobile->navigation;
                        const auto delta = fixed::subtract(state.position, other.position);
                        const auto distance = fixed::approximate_length(delta);
                        if (distance && distance < fixed::add(fixed::add(state.radius, other.radius), 65536))
                            sum = fixed::add(sum, fixed::divide(delta, distance));
                        if (++visited == 5)
                            return sum;
                    }
                }
                id = base->map_child;
            }
        }
    return sum;
}

Vector avoid_walls(WorldRuntime& c, const MobileNavigationState& state) {
    const auto predicted = fixed::add(state.position, state.velocity);
    const auto length = fixed::magnitude(state.velocity);
    const auto direction = length ? fixed::divide(state.velocity, length) : state.velocity;
    const auto x = static_cast<std::uint32_t>(state.position.x) >> 16,
               y = static_cast<std::uint32_t>(state.position.y) >> 16;
    const auto blocked = [&](std::uint32_t px, std::uint32_t py, unsigned d) {
        return is_world_cell_step_blocked(c,
                                          { static_cast<std::uint8_t>(px), static_cast<std::uint8_t>(py) },
                                          static_cast<GridPathDirection>(d),
                                          state.navigation_mode);
    };
    const auto boundary = [](std::uint32_t coordinate) {
        return fixed::word(coordinate << 16);
    };
    const bool east = direction.x >= 0, west = direction.x < 0, south = direction.y >= 0, north = direction.y < 0;
    if (east && blocked(x, y, 1)) {
        const auto limit = fixed::subtract(boundary(x + 1), state.radius);
        if (predicted.x > limit) {
            const auto push = fixed::subtract(limit, predicted.x);
            return { fixed::add(push, push), 0 };
        }
    }
    if (west && blocked(x, y, 3)) {
        const auto limit = fixed::add(boundary(x), state.radius);
        if (predicted.x < limit) {
            const auto push = fixed::subtract(limit, predicted.x);
            return { fixed::add(push, push), 0 };
        }
    }
    if (south && blocked(x, y, 2)) {
        const auto limit = fixed::subtract(boundary(y + 1), state.radius);
        if (predicted.y > limit) {
            const auto push = fixed::subtract(limit, predicted.y);
            return { 0, fixed::add(push, push) };
        }
    }
    if (north && blocked(x, y, 0)) {
        const auto limit = fixed::add(boundary(y), state.radius);
        if (predicted.y < limit) {
            const auto push = fixed::subtract(limit, predicted.y);
            return { 0, fixed::add(push, push) };
        }
    }
    const auto corner = [&](std::uint32_t nx, std::uint32_t ny, unsigned back_y, unsigned back_x, Vector point)
        -> std::optional<Vector> {
        if (!(blocked(nx, ny, back_y) || blocked(nx, ny, back_x)))
            return std::nullopt;
        if (north) {
            if (blocked(nx, y, back_x) || blocked(x, ny, back_y))
                return std::nullopt;
        } else if (blocked(x, ny, back_y) || blocked(nx, y, back_x))
            return std::nullopt;
        const auto hit = fixed::ray_circle_intersection(state.position, direction, point, state.radius);
        if (hit == std::numeric_limits<std::int32_t>::max() || hit <= 0 || hit > length)
            return std::nullopt;
        return fixed::divide(fixed::subtract(fixed::add(state.position, fixed::scale(direction, hit)), point),
                             state.radius);
    };
    if (south && east)
        if (auto force = corner(x + 1, y + 1, 0, 3, { boundary(x + 1), boundary(y + 1) }))
            return *force;
    if (south && west)
        if (auto force = corner(x - 1, y + 1, 0, 1, { boundary(x), boundary(y + 1) }))
            return *force;
    if (north && west)
        if (auto force = corner(x - 1, y - 1, 2, 1, { boundary(x), boundary(y) }))
            return *force;
    if (north && east)
        if (auto force = corner(x + 1, y - 1, 2, 3, { boundary(x + 1), boundary(y) }))
            return *force;
    return {};
}
} //namespace

std::int32_t get_world_navigation_path_progress(const MobileNavigationState& state) {
    if (state.path_finished == 1)
        return 65536;
    const auto delta = fixed::subtract(state.subpath_buffer.at(state.path_count), state.position);
    if (!state.path_total_distance)
        return 65536;
    const auto remaining =
        fixed::add(fixed::approximate_length(delta), fixed::add(state.path_subpath_distance, state.path_tail_distance));
    return fixed::subtract(65536, fixed::narrow(std::int64_t{ remaining } * 65536 / state.path_total_distance));
}

bool
is_world_navigation_step_unblocked(WorldRuntime& c, std::int32_t source, std::int32_t candidate, std::uint32_t mode) {
    const auto index = [](std::int32_t id) {
        return static_cast<std::uint32_t>(static_cast<CellId>(id)) - 1u;
    };
    const auto si = index(source), di = index(candidate);
    const auto x = static_cast<int>(si & 127u), y = static_cast<int>(si >> 7);
    const auto dx = static_cast<int>(di & 127u) - x, dy = static_cast<int>(di >> 7) - y;
    if (std::abs(dx) > 1 || std::abs(dy) > 1) {
        c.visitors.navigation_step_rejection_seen = true;
        return false;
    }
    if (candidate == -1)
        return false;
    const auto blocked = [&](int px, int py, unsigned d) {
        return is_world_cell_step_blocked(c,
                                          { static_cast<std::uint8_t>(px), static_cast<std::uint8_t>(py) },
                                          static_cast<GridPathDirection>(d),
                                          mode);
    };
    const auto horizontal = dx > 0 ? 1u : 3u, vertical = dy > 0 ? 2u : 0u;
    if (dx && dy) {
        //Two routes around a diagonal corner: either clear pair accepts movement.
        if (dx == 1 && dy == 1)
            return (!blocked(x, y, horizontal) && !blocked(x + dx, y, vertical))
                || (!blocked(x, y, vertical) && !blocked(x, y + dy, horizontal));
        return (!blocked(x, y, vertical) && !blocked(x, y + dy, horizontal))
            || (!blocked(x, y, horizontal) && !blocked(x + dx, y, vertical));
    }
    if (dx)
        return !blocked(x, y, horizontal);
    if (dy)
        return !blocked(x, y, vertical);
    return true;
}

FixedPointVector2
compute_world_navigation_steering(WorldRuntime& c, SteeringBehavior behavior, MobileNavigationState& state) {
    return match (behavior) -> Vector {
        case SteeringBehavior::seek => do {
            if (!state.steering_target_position)
                throw std::logic_error("seek needs a target");
            do_return fixed::subtract(fixed::subtract(*state.steering_target_position, state.position), state.velocity);
        };
        case SteeringBehavior::arrival => do {
            if (!state.steering_target_position)
                throw std::logic_error("arrival needs a target");
            do_return arrive(state, *state.steering_target_position);
        };
        case SteeringBehavior::separation     => separation(c, state);
        //These native callbacks enumerate an empty obstacle source. They really
        //return zero in Game.exe; no unported backend is being substituted here.
        case SteeringBehavior::avoid_obstacle => Vector{};
        case SteeringBehavior::repel_obstacle => Vector{};
        case SteeringBehavior::avoid_walls    => avoid_walls(c, state);
        case SteeringBehavior::follow_path    => follow_path(c, state);
        case _                                => throw std::invalid_argument("unknown steering behavior");
    };
}

std::int32_t step_world_navigation_state(WorldRuntime& c, MobileNavigationState& state) {
    Vector force{};
    for (std::size_t i = 0; i < c.visitors.steering_behaviors.size(); ++i) {
        const auto produced = compute_world_navigation_steering(c, c.visitors.steering_behaviors[i].behavior, state);
        const auto limited = fixed::clamp(produced, state.max_force);
        force = fixed::add(force, fixed::scale(limited, c.visitors.steering_behaviors.at(i).weight));
    }
    state.velocity = fixed::clamp(fixed::add(state.velocity, fixed::clamp(force, state.max_force)), state.max_speed);
    const auto candidate = fixed::add(state.position, state.velocity);
    const auto cell = [](Vector p) -> std::int32_t {
        const auto x = static_cast<std::uint32_t>(p.x) >> 16, y = static_cast<std::uint32_t>(p.y) >> 16;
        return x < 128 && y < 128 ? static_cast<std::int32_t>(y * 128 + x + 1) : -1;
    };
    if (is_world_navigation_step_unblocked(c, cell(state.position), cell(candidate), state.navigation_mode))
        state.position = candidate;
    else
        state.path_stuck_buffer = state.path_stuck_buffer * 2u | 1u;
    state.local_xaxis = fixed::normalize(state.velocity);
    state.local_yaxis = { fixed::negate(state.local_xaxis.y), state.local_xaxis.x };
    const auto progress = get_world_navigation_path_progress(state);
    state.path_stuck_buffer =
        state.path_stuck_buffer * 2u | static_cast<unsigned>(progress <= state.path_last_progress);
    state.path_last_progress = progress;
    return progress;
}
} //namespace simcoaster
