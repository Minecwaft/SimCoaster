#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include <numeric>
#include <optional>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr std::array<int, 4> dx { 0, 1, 0, -1 }, dy { -1, 0, 1, 0 };

GridPathCell offset(GridPathCell cell, int x, int y) {
    return { static_cast<std::uint8_t>(cell.x + x), static_cast<std::uint8_t>(cell.y + y) };
}

GridPathCell step(GridPathCell cell, int direction) {
    return offset(cell, dx.at(static_cast<std::size_t>(direction)), dy.at(static_cast<std::size_t>(direction)));
}

bool queue(std::int32_t type) {
    return type == 3 || type == 9;
}

World& primary(VisitorWorld& c) {
    return require_bound<std::invalid_argument>(c.simulation.reference, "routing has no primary World");
}

std::size_t index(CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("routing cell ID");
    }
    return id - 1u;
}

bool root(std::int32_t type) {
    return type == 11 || type == 13 || type == 16 || type == 18 || type == 25;
}

bool child(std::int32_t type) {
    return type == 12 || type == 17;
}

struct LineStepper {
    int delta_x, delta_y, error, x_direction, y_direction;

    LineStepper(GridPathCell start, GridPathCell target) : delta_x(std::abs(int(target.x) - start.x)), delta_y(std::abs(int(target.y) - start.y)), error(delta_x / 2 + delta_y / 2), x_direction(target.x > start.x ? 1 : 3), y_direction(target.y > start.y ? 2 : 0) {
        if (start.x == target.x) {
            x_direction = y_direction;
        }
        if (start.y == target.y) {
            y_direction = x_direction;
        }
    }

    int next() {
        if (error < delta_x) {
            error += delta_y;
            return x_direction;
        }
        error -= delta_x;
        return y_direction;
    }
};

struct DetourFrontier {
    std::vector<GridPathCell> waypoints;
    GridPathCell target, current, anchor;
    int direction, turn;
};

class RouteSearch {
    VisitorWorld& context;
    std::uint32_t mode;
    GridRouteDiagnostics* diagnostics;
    LineStepper line { {}, {} };
    std::uint32_t steps {};

    bool blocked(GridPathCell cell, int direction) {
        return is_world_cell_step_blocked(context, cell, static_cast<GridPathDirection>(direction), mode);
    }

    bool direct(GridPathCell start, GridPathCell target) {
        line = LineStepper { start, target };
        GridPathCell previous = start;
        int direction = -1;
        bool first = true;
        while (start != target) {
            const auto old = direction;
            direction = line.next();
            if (blocked(start, direction)) {
                return false;
            }
            if (first) {
                first = false;
            } else if (direction != old) {
                if (!blocked(start, old) && blocked(step(start, old), direction)) {
                    return false;
                }
                if (!blocked(previous, direction) && blocked(step(previous, direction), old)) {
                    return false;
                }
            }
            previous = start;
            start = step(start, direction);
        }
        return true;
    }

    bool compact(WorldGridRoute& route, std::size_t first) {
        bool any = false, changed;
        do {
            std::size_t read = first, write = first;
            auto start = first ? route.waypoints.at(first - 1) : route.start;
            changed = false;
            while (read < route.waypoints.size()) {
                unsigned skip = 4;
                while (skip && (read + skip >= route.waypoints.size() || !direct(start, route.waypoints[read + skip]))) {
                    --skip;
                }
                if (skip) {
                    read += skip;
                    changed = true;
                    any = true;
                } else {
                    start = route.waypoints[read++];
                    route.waypoints[write++] = start;
                }
            }
            route.waypoints.resize(write);
        } while (changed);
        return any;
    }

    void advance(DetourFrontier& frontier) {
        if (frontier.direction == 51) {
            return;
        }
        if (blocked(frontier.current, frontier.direction)) {
            frontier.direction = (frontier.direction - frontier.turn) & 3;
            frontier.waypoints.push_back(frontier.current);
            if (frontier.waypoints.size() > 251) {
                frontier.direction = 51;
                return;
            }
        } else {
            frontier.current = step(frontier.current, frontier.direction);
            const auto side = (frontier.direction + frontier.turn) & 3;
            if (!blocked(frontier.current, side)) {
                frontier.direction = side;
                frontier.waypoints.push_back(frontier.current);
                if (frontier.waypoints.size() > 251) {
                    frontier.direction = 51;
                    return;
                }
            }
        }
        const auto& target = frontier.target;
        const auto& current = frontier.current;
        const auto passed = [&](int direction) {
            return match (direction) -> bool {
                case 0 => target.y >= current.y;
                case 1 => target.x <= current.x;
                case 2 => target.y <= current.y;
                case 3 => target.x >= current.x;
                case _ => throw std::logic_error("invalid detour direction");
            };
        };
        const auto approaching = [&](int direction) {
            return match (direction) -> bool {
                case 0 => target.y <= current.y;
                case 1 => target.x >= current.x;
                case 2 => target.y >= current.y;
                case 3 => target.x <= current.x;
                case _ => throw std::logic_error("invalid detour direction");
            };
        };
        if (passed((frontier.turn + frontier.direction) & 3) && approaching(frontier.direction) && current.x >= std::min(frontier.anchor.x, target.x) && current.x <= std::max(frontier.anchor.x, target.x) && current.y >= std::min(frontier.anchor.y, target.y) && current.y <= std::max(frontier.anchor.y, target.y) && current != frontier.anchor) {
            frontier.direction = 50;
        }
    }

    static bool meet(const DetourFrontier& left, const DetourFrontier& right) {
        return left.direction != 50 && left.direction != 51 && step(left.current, left.direction) == right.current && ((left.direction - 2) & 3) == right.direction;
    }

    std::int32_t trace(WorldGridRoute& route, std::uint32_t limit) {
        auto current = route.start, last = current;
        int direction = 0;
        bool first = true;
        line = LineStepper { current, route.target };
        const auto fail = [&] {
            route.waypoints.push_back(last);
            return grid_route_failure;
        };
        while (steps < limit) {
            if (current == route.target) {
                break;
            }
            const auto old = direction;
            direction = line.next();
            if (blocked(current, direction)) {
                if (diagnostics) {
                    diagnostics->captured_waypoint_count = static_cast<std::uint8_t>(route.waypoints.size());
                    diagnostics->captured_step_index = steps;
                    diagnostics->blocked_direction = static_cast<GridPathDirection>(direction);
                }
                route.waypoints.push_back(last);
                DetourFrontier left { {}, route.target, last, last, (direction + 1) & 3, -1 }, right { {}, route.target, last, last, (direction - 1) & 3, 1 };
                bool found = false;
                while (steps < limit) {
                    advance(left);
                    if (meet(left, right)) {
                        return fail();
                    }
                    advance(right);
                    if (meet(left, right)) {
                        return fail();
                    }
                    const auto use = [&](const DetourFrontier& frontier, int side) {
                        if (route.waypoints.size() + frontier.waypoints.size() + 2 > 252) {
                            return false;
                        }
                        route.waypoints.insert(route.waypoints.end(), frontier.waypoints.begin(), frontier.waypoints.end());
                        route.waypoints.push_back(frontier.current);
                        if (diagnostics) {
                            diagnostics->detour_side = static_cast<std::int8_t>(side);
                        }
                        return true;
                    };
                    if (left.direction == 50) {
                        if (!use(left, -1)) {
                            return fail();
                        }
                        found = true;
                        break;
                    }
                    if (right.direction == 50) {
                        if (!use(right, 1)) {
                            return fail();
                        }
                        found = true;
                        break;
                    }
                    if (left.direction == 51 && right.direction == 51) {
                        return fail();
                    }
                    ++steps;
                }
                if (!found || steps == limit) {
                    return fail();
                }
                compact(route, 0);
                current = last = route.waypoints.back();
                line = LineStepper { current, route.target };
                first = true;
            } else {
                current = step(current, direction);
                if (first) {
                    first = false;
                } else if (direction != old) {
                    bool corner = false;
                    if (blocked(current, old)) {
                        if (!blocked(last, direction)) {
                            corner = blocked(step(last, direction), old);
                        }
                    } else {
                        corner = blocked(step(current, old), direction);
                    }
                    if (corner) {
                        route.waypoints.push_back(current);
                        if (route.waypoints.size() > 250) {
                            last = current;
                            return fail();
                        }
                    }
                }
                last = current;
            }
            ++steps;
        }
        if (route.waypoints.size() >= 250 || current != route.target) {
            return grid_route_failure;
        }
        route.waypoints.push_back(last);
        return static_cast<std::int32_t>(steps);
    }

    void simplify(WorldGridRoute& route) {
        compact(route, 0);
        unsigned passes = 9;
        bool restart;
        do {
            restart = false;
            for (std::size_t i = 0; i + 1 < route.waypoints.size(); ++i) {
                WorldGridRoute trial { i ? route.waypoints[i - 1] : route.start, route.waypoints[i + 1], {} };
                steps = 0;
                if (diagnostics) {
                    diagnostics->captured_waypoint_count = 255;
                }
                if (trace(trial, 60000) == grid_route_failure) {
                    continue;
                }
                if (trial.waypoints.size() > 1 && std::find(trial.waypoints.begin(), trial.waypoints.end() - 1, route.waypoints[i]) != trial.waypoints.end() - 1) {
                    continue;
                }
                if (route.waypoints.size() + trial.waypoints.size() - 2 > 252) {
                    continue;
                }
                route.waypoints.erase(route.waypoints.begin() + static_cast<std::ptrdiff_t>(i), route.waypoints.begin() + static_cast<std::ptrdiff_t>(i + 2));
                route.waypoints.insert(route.waypoints.begin() + static_cast<std::ptrdiff_t>(i), trial.waypoints.begin(), trial.waypoints.end());
                compact(route, i >= 4 ? i - 4 : 0);
                if (!--passes) {
                    return;
                }
                restart = true;
                break;
            }
        } while (restart);
        for (std::size_t i = 0; i < route.waypoints.size(); ++i) {
            const auto start = i ? route.waypoints[i - 1] : route.start;
            const auto finish = route.waypoints[i];
            const int x = int(finish.x) - start.x, y = int(finish.y) - start.y;
            if (!x || !y) {
                continue;
            }
            const auto ax = std::abs(x), ay = std::abs(y), divisor = std::gcd(ax, ay);
            if (!(ax / divisor & 1) || !(ay / divisor & 1)) {
                continue;
            }
            const int sx = x > 0 ? 1 : -1, sy = y > 0 ? 1 : -1, vertical = sy > 0 ? 2 : 0, horizontal = sx > 0 ? 1 : 3;
            const auto diagonal = offset(start, sx, sy);
            std::optional<GridPathCell> inserted;
            if (blocked(diagonal, (vertical + 2) & 3) && !blocked(diagonal, (horizontal + 2) & 3)) {
                inserted = offset(finish, -sx, 0);
            } else if (blocked(diagonal, (horizontal + 2) & 3) && !blocked(diagonal, (vertical + 2) & 3)) {
                inserted = offset(finish, 0, -sy);
            } else if (blocked(start, vertical) && !blocked(start, horizontal)) {
                inserted = offset(finish, 0, -sy);
            } else if (blocked(start, horizontal) && !blocked(start, vertical)) {
                inserted = offset(finish, -sx, 0);
            }
            if (inserted && ax == 1 && ay == 1 && route.waypoints.size() + 1 < 252) {
                route.waypoints.insert(route.waypoints.begin() + static_cast<std::ptrdiff_t>(i), *inserted);
                ++i;
            }
        }
    }

public:
    RouteSearch(VisitorWorld& input, std::uint32_t navigation_mode, GridRouteDiagnostics* capture) : context(input), mode(navigation_mode), diagnostics(capture) {}

    std::int32_t build(WorldGridRoute& route) {
        route.waypoints.clear();
        steps = 0;
        if (diagnostics) {
            diagnostics->captured_waypoint_count = 255;
        }
        const auto result = trace(route, 60000);
        simplify(route);
        return result;
    }
};

std::int32_t distance(FixedPointVector2 from, FixedPointVector2 to) {
    const auto absolute = [](std::int32_t value) {
        return value < 0 ? numeric::subtract(0, value) : value;
    };
    const auto x = absolute(numeric::subtract(to.x, from.x)), y = absolute(numeric::subtract(to.y, from.y));
    return numeric::subtract(numeric::add(x, y), std::min(x, y) >> 1);
}

FixedPointVector2 center(GridPathCell cell) {
    return { int(cell.x) * 65536 + 32768, int(cell.y) * 65536 + 32768 };
}

GridPathCell grid(FixedPointVector2 position) {
    return { static_cast<std::uint8_t>(std::bit_cast<std::uint32_t>(position.x) >> 16), static_cast<std::uint8_t>(std::bit_cast<std::uint32_t>(position.y) >> 16) };
}
} //namespace

bool is_world_cell_step_blocked(VisitorWorld& c, GridPathCell cell, GridPathDirection requested, std::uint32_t mode) {
    const auto direction = std::to_underlying(requested);
    if (direction >= 4) {
        throw std::invalid_argument("routing direction");
    }
    if (cell.x >= 128 || cell.y >= 128 || (!cell.x && direction == 3) || (!cell.y && direction == 0) || (cell.x == 127 && direction == 1) || (cell.y == 127 && direction == 2)) {
        return true;
    }
    const auto next = step(cell, direction);
    const auto source_index = static_cast<std::size_t>(cell.y) * 128 + cell.x, destination_index = static_cast<std::size_t>(next.y) * 128 + next.x;
    auto& world = primary(c);
    const auto& source = world.map_cells[source_index].base;
    const auto& destination = world.map_cells[destination_index];
    const auto st = source.common.type, dt = destination.base.common.type;
    if (dt == 1 && !queue(st)) {
        return false;
    }
    if (st == 4 && dt == 4) {
        return false;
    }
    if (dt == 4) {
        return true;
    }
    if (dt == 30 && !destination.is_obstacle && mode != 2) {
        return false;
    }
    if (mode == 3 && (dt == 30 || dt == 32)) {
        return false;
    }
    if (mode == 2) {
        if ((destination.base.common.flags & 0x400) && !destination.is_obstacle) {
            return false;
        }
        if (dt == 9) {
            const auto& owner = world.map_cells.at(index(source.common.parent_id));
            const auto id = find_first_park_object_id_in_map_cell(c, &owner);
            if (id) {
                const auto* object = c.entities.at_payload<const ParkObjectRuntimeView>(id);
                return c.ports.meshes.find_embedded_node_resource_record(c, object->mesh_instance_id, 0x1000, 1) == 0;
            }
        }
    }
    if ((destination.base.common.flags & 0x40) || dt == 2 || dt == 5 || dt == 7 || dt == 8) {
        return true;
    }
    if (queue(dt) || (queue(st) && dt == 1)) {
        if (!(st == 1 || queue(st))) {
            return true;
        }
        constexpr std::array<std::uint8_t, 4> required { 16, 64, 1, 4 };
        return !(get_world_cell_neighbor_mask(c, &destination.base) & required[direction]);
    }
    if (!c.simulation.track_cell_records) {
        throw std::invalid_argument("routing has no track array");
    }
    const auto& track = c.simulation.track_cell_records[index(destination.base.index)];
    bool is_root = root(track.base.common.type);
    if (child(track.base.common.type)) {
        is_root = track.base.common.parent_id && root(world.track_cells.at(index(track.base.common.parent_id)).base.common.type);
    }
    if (is_root) {
        const auto track_id = child(track.base.common.type) ? track.base.common.parent_id : destination.base.index;
        if (!world_cell_has_track_segment_flags(&world.track_cells.at(index(track_id)).base)) {
            return true;
        }
    }
    if ((st == 1 || queue(st)) && !(mode == 1 && st == 1 && dt != 1 && !queue(dt) && dt != 4)) {
        if ((st != 9 && st != 10) || dt != 0) {
            return true;
        }
        constexpr std::array<std::uint8_t, 4> exit_direction { 16, 64, 1, 4 };
        return get_world_cell_direction(&source) != exit_direction[direction];
    }
    return false;
}

std::int32_t build_smoothed_world_grid_route(VisitorWorld& c, WorldGridRoute& route, std::uint32_t mode, GridRouteDiagnostics* diagnostics) {
    return RouteSearch { c, mode, diagnostics }.build(route);
}

std::int32_t calculate_world_grid_path_length_between_cells(VisitorWorld& c, CellId start, CellId target, std::uint32_t mode) {
    const auto unpack = [](CellId id) {
        const auto value = static_cast<std::uint32_t>(id) - 1u;
        return GridPathCell { static_cast<std::uint8_t>(value & 127u), static_cast<std::uint8_t>(value >> 7) };
    };
    WorldGridRoute route { unpack(start), unpack(target), {} };
    if (build_smoothed_world_grid_route(c, route, mode) == grid_route_failure) {
        return -1;
    }
    std::int32_t length = 0;
    for (std::size_t i = 1; i < route.waypoints.size(); ++i) {
        const auto a = route.waypoints[i - 1], b = route.waypoints[i];
        length += std::abs(int(a.x) - int(b.x)) + std::abs(int(a.y) - int(b.y));
    }
    return length;
}

bool build_world_navigation_path(VisitorWorld& c, MobileNavigationState& state, FixedPointVector2 target, bool rebuild) {
    WorldGridRoute route { grid(state.position), grid(target), {} };
    const auto result = build_smoothed_world_grid_route(c, route, state.navigation_mode);
    state.path_target_position = target;
    if (result == grid_route_failure) {
        state.path_total_count = state.path_buffer_count = 0;
        state.path_total_distance = state.path_subpath_distance = state.path_tail_distance = state.path_last_progress = 0;
        state.path_stuck_buffer = state.path_count = 0;
        state.path_finished = 1;
        state.cannot_reach_destination = 1;
        return false;
    }
    if (rebuild) {
        route.waypoints.insert(route.waypoints.begin(), grid(state.position));
        if (route.waypoints.size() > 252) {
            route.waypoints.resize(252);
        }
    }
    state.path_total_count = static_cast<std::uint32_t>(route.waypoints.size());
    state.path_buffer_count = std::min(state.path_total_count, 5u);
    state.path_total_distance = distance(state.position, center(route.waypoints.front()));
    for (std::size_t i = 1; i < route.waypoints.size(); ++i) {
        state.path_total_distance = numeric::add(state.path_total_distance, distance(center(route.waypoints[i - 1]), center(route.waypoints[i])));
    }
    for (std::size_t i = 0; i < state.path_buffer_count; ++i) {
        state.subpath_buffer[i] = center(route.waypoints[i]);
    }
    state.path_subpath_distance = 0;
    for (std::size_t i = 1; i < state.path_buffer_count; ++i) {
        const auto length = distance(state.subpath_buffer[i - 1], state.subpath_buffer[i]);
        state.subpath_distance[i - 1] = length;
        state.path_subpath_distance = numeric::add(state.path_subpath_distance, length);
    }
    state.path_tail_distance = 0;
    for (std::size_t i = state.path_buffer_count; i < route.waypoints.size(); ++i) {
        state.path_tail_distance = numeric::add(state.path_tail_distance, distance(center(route.waypoints[i - 1]), center(route.waypoints[i])));
    }
    state.path_last_progress = 0;
    state.path_stuck_buffer = state.path_count = 0;
    state.path_finished = 0;
    state.cannot_reach_destination = 0;
    state.path_timestamp = next_world_grid_change_serial(c);
    return true;
}

bool local_world_grid_unchanged_since_serial(VisitorWorld& c, const MapObjectBase& base, const MobileEntityState& mobile) {
    if (!c.simulation.current) {
        throw std::invalid_argument("routing has no base World");
    }
    const auto cell = static_cast<CellId>((base.y >> 8) * 128 + (base.x >> 8) + 1);
    auto last = cell;
    if (queue(get_world_map_cell_record_by_id(c, cell)->base.common.type)) {
        std::size_t count = 0;
        for (auto next = cell; next; next = find_next_queue_item_id(c, next)) {
            if (count++ >= map_cell_count) {
                throw std::length_error("cyclic routing queue");
            }
            last = next;
        }
    }
    std::uint32_t greatest = 0;
    for (int y : { 1, 0, -1 }) {
        for (int x : { -1, 0, 1 }) {
            if (greatest > mobile.stranded_time) {
                return false;
            }
            const auto id = static_cast<CellId>(int(last) + x + y * 128);
            if (!id) {
                throw std::out_of_range("zero routing change-serial reference");
            }
            const auto value = static_cast<std::uint32_t>(id) - 1;
            greatest = std::max(greatest, c.simulation.current->grid_change_serials[value >> 11][(value >> 4) & 7]);
        }
    }
    return greatest < mobile.stranded_time;
}

bool request_mobile_entity_path_to_destination(VisitorWorld& c, MapObjectBase& base, MobileEntityState& mobile, std::uint16_t x, std::uint16_t y) {
    if (next_world_grid_change_serial(c) < mobile.stranded_time) {
        mobile.stranded_time = 0;
    }
    if (mobile.stranded_time && local_world_grid_unchanged_since_serial(c, base, mobile)) {
        return false;
    }
    mobile.accurate_destination_x = x;
    mobile.accurate_destination_y = y;
    mobile.stranded_time = 0;
    return build_world_navigation_path(c, mobile.navigation, { int(std::bit_cast<std::int16_t>(x)) * 256, int(std::bit_cast<std::int16_t>(y)) * 256 });
}
} //namespace simcoaster
