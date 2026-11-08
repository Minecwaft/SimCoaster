#include "world_person_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>
#include <array>
#include <bit>

namespace simcoaster {
void reset_mobile_entity_position(VisitorWorld& c,
                                  MapObjectBase& base,
                                  MobileEntityState& mobile,
                                  std::uint16_t x,
                                  std::uint16_t y) {
    mobile.navigation.position = { static_cast<std::int32_t>(std::bit_cast<std::int16_t>(x)) * 256,
                                   static_cast<std::int32_t>(std::bit_cast<std::int16_t>(y)) * 256 };
    mobile.navigation.velocity = {};
    mobile.previous_x = mobile.navigation.position.x;
    mobile.previous_y = mobile.navigation.position.y;
    move_world_map_object_to_coordinates(c, base, x, y);
}

namespace {
CellId compose_cell(std::int32_t x, std::int32_t y) {
    return static_cast<CellId>((static_cast<std::uint32_t>(y) << 7) + static_cast<std::uint32_t>(x) + 1u);
}

std::size_t index(CellId id) {
    if (!id || id > map_cell_count)
        throw std::out_of_range("destination map-cell ID");
    return id - 1u;
}

CellBase& global_cell(VisitorWorld& c, CellId id) {
    if (!c.simulation.map_cell_records)
        throw std::logic_error("destination map cells are unavailable");
    return c.simulation.map_cell_records[index(id)].base;
}

CellId offset_cell(CellId id, int offset) {
    return static_cast<CellId>(static_cast<std::uint32_t>(id) + static_cast<std::uint32_t>(offset));
}

bool queue_or_entry(std::int32_t type) {
    return type == 3 || type == 9;
}

std::array<std::uint16_t, 2> cell_center(CellId id) {
    const auto value = static_cast<std::uint32_t>(id) - 1u;
    return { static_cast<std::uint16_t>(((value & 127u) << 8) | 128u),
             static_cast<std::uint16_t>(((value >> 7) << 8) | 128u) };
}
} //namespace

void set_mobile_entity_stranded_state(VisitorWorld& c, MobileEntityState& mobile, bool stranded) {
    mobile.stranded_time = stranded ? next_world_grid_change_serial(c) : 0;
}

bool set_mobile_entity_destination_and_path(VisitorWorld& c,
                                            MapObjectBase& base,
                                            MobileEntityState& mobile,
                                            std::uint16_t x,
                                            std::uint16_t y) {
    auto& primary = person_detail::primary_world(c);
    if (primary.saved.park_closed && ((x >> 8) == 47 || (x >> 8) == 48) && (y >> 8) == 17)
        return false;
    const auto source_type = primary.map_cells[index(person_detail::cell_id(base))].base.common.type;
    mobile.navigation.navigation_mode =
        source_type == 0 || primary.map_cells[index(compose_cell(x >> 8, y >> 8))].base.common.type == 0;
    return request_mobile_entity_path_to_destination(c, base, mobile, x, y);
}

bool is_cell_inside_staff_patrol_area(const StaffRuntimeRecord& staff, CellId id) {
    const auto value = static_cast<std::uint32_t>(id) - 1u;
    const auto x = static_cast<std::int32_t>(value & 127u), y = static_cast<std::int32_t>(value >> 7);
    const auto& bounds = staff.area.bounds;
    return x >= std::min(bounds.start_x, bounds.end_x)
        && x <= std::max(bounds.start_x, bounds.end_x)
        && y >= std::min(bounds.start_y, bounds.end_y)
        && y <= std::max(bounds.start_y, bounds.end_y);
}

bool try_set_staff_destination_inside_patrol_area(VisitorWorld& c, StaffRuntimeRecord& staff) {
    const auto& bounds = staff.area.bounds;
    const auto first = static_cast<std::uint32_t>(compose_cell(bounds.start_x, bounds.start_y)) - 1u;
    const auto last = static_cast<std::uint32_t>(compose_cell(bounds.end_x, bounds.end_y)) - 1u;
    const auto width = (last & 127u) - (first & 127u) + 1u, height = (last >> 7) - (first >> 7) + 1u;
    //Membership normalizes reversed bounds; this random chooser does not.
    if (!width || !height)
        throw std::domain_error("empty native patrol rectangle divisor");
    for (unsigned attempt = 0; attempt < 30; ++attempt) {
        const auto x = std::bit_cast<std::int32_t>(
            (first & 127u) + static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % width);
        const auto y = std::bit_cast<std::int32_t>(
            (first >> 7) + static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % height);
        if (!is_world_map_coordinate_in_bounds(x, y))
            continue;
        const auto id = compose_cell(x, y);
        if (global_cell(c, id).common.type != 1)
            continue;
        const auto [target_x, target_y] = cell_center(id);
        if (set_mobile_entity_destination_and_path(c, staff.base, staff.mobile, target_x, target_y))
            return true;
    }
    world_operations::set_mobile_entity_thought_bubble(c,
                                                       &staff.mobile.thought_history,
                                                       MobileThoughtCode::staff_confused,
                                                       false);
    return false;
}

bool try_set_mobile_entity_destination(VisitorWorld& c, MapObjectBase& base, MobileEntityState& mobile) {
    auto selected = person_detail::cell_id(base);
    if (next_world_grid_change_serial(c) < mobile.stranded_time)
        mobile.stranded_time = 0;
    if (mobile.stranded_time && local_world_grid_unchanged_since_serial(c, base, mobile)) {
        world_operations::set_mobile_entity_thought_bubble(c,
                                                           &mobile.thought_history,
                                                           MobileThoughtCode::confused,
                                                           false);
        return false;
    }
    mobile.stranded_time = 0;
    const auto steps = static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 5u + 1u;
    auto& first = global_cell(c, person_detail::cell_id(base));
    bool ignore_patrol = false;
    auto* staff = is_staff_record_kind(base.header.kind) ? reinterpret_cast<StaffRuntimeRecord*>(&base) : nullptr;
    if (staff && !is_cell_inside_staff_patrol_area(*staff, person_detail::cell_id(base))) {
        if (try_set_staff_destination_inside_patrol_area(c, *staff))
            return true;
        ignore_patrol = true;
    }
    const auto finish = [&](bool result) {
        if (result)
            mobile.destination_set_successfully = 1;
        return result;
    };
    if (count_world_cell_cardinal_neighbor_bits(c, &first) <= 0) {
        //Native case zero repeats the current cell. The other seven cases test
        //radii 0..3 in this order, with flat WORD cell arithmetic and row aliasing.
        constexpr std::array<std::array<int, 2>, 8> rays{
            { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -1, 0 }, { -1, 1 } }
        };
        const auto origin = person_detail::cell_id(base);
        for (const auto& ray : rays)
            for (int radius = 0; radius < 4; ++radius) {
                const auto candidate = offset_cell(origin, radius * (ray[0] + 128 * ray[1]));
                const auto value = static_cast<std::uint32_t>(candidate) - 1u;
                if (!is_world_map_coordinate_in_bounds(static_cast<std::int32_t>(value & 127u),
                                                       static_cast<std::int32_t>(value >> 7))
                    || global_cell(c, candidate).common.type != 1)
                    continue;
                const auto [x, y] = cell_center(candidate);
                if (request_mobile_entity_path_to_destination(c, base, mobile, x, y))
                    return finish(true);
            }
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            const auto dx = static_cast<std::int32_t>(
                                static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 11u)
                          - 5;
            const auto dy = static_cast<std::int32_t>(
                                static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 11u)
                          - 5;
            const auto value = static_cast<std::uint32_t>(person_detail::cell_id(base)) - 1u;
            const auto x = static_cast<std::int32_t>(value & 127u) + dx, y = static_cast<std::int32_t>(value >> 7) + dy;
            if (!is_world_map_coordinate_in_bounds(x, y))
                continue;
            const auto [target_x, target_y] = cell_center(compose_cell(x, y));
            if (request_mobile_entity_path_to_destination(c, base, mobile, target_x, target_y))
                return finish(true);
        }
        return false;
    }
    auto remaining = steps - 1u;
    unsigned reverse = 9999;
    for (;;) {
        auto& current = global_cell(c, selected);
        const auto mask = get_world_cell_neighbor_mask(c, &current);
        auto count = count_world_cell_cardinal_neighbor_bits(c, &current);
        std::array<CellId, 4> neighbors{ mask & 16 ? offset_cell(selected, 128) : CellId{},
                                         mask & 4 ? offset_cell(selected, 1) : CellId{},
                                         mask & 1 ? offset_cell(selected, -128) : CellId{},
                                         mask & 64 ? offset_cell(selected, -1) : CellId{} };
        if (staff && !ignore_patrol)
            for (auto& id : neighbors) {
                if (!id || is_cell_inside_staff_patrol_area(*staff, id))
                    continue;
                id = 0;
                if (!--count)
                    return try_set_staff_destination_inside_patrol_area(c, *staff);
            }
        if (current.common.type == 1)
            for (auto& id : neighbors)
                if (id && queue_or_entry(global_cell(c, id).common.type)) {
                    id = 0;
                    --count;
                }
        if (queue_or_entry(current.common.type) && current.common.type != 9) {
            const auto excluded = match (get_world_cell_direction(&current)) -> int {
                case 1  => 2;
                case 16 => 0;
                case 4  => 1;
                case 64 => 3;
                case _  => -1;
            };
            //The original decrements even when that direction was already absent.
            if (excluded >= 0) {
                neighbors[static_cast<std::size_t>(excluded)] = 0;
                --count;
            }
        }
        for (auto& id : neighbors)
            if (id && person_detail::base_world(c).map_cells[index(id)].base.common.type == 10) {
                id = 0;
                --count;
            }
        unsigned chosen = 0;
        if (count >= 2) {
            const auto start = static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) & 3u;
            chosen = start;
            for (unsigned scanned = 0; scanned < 4; ++scanned, chosen = (chosen + 1u) & 3u)
                if (neighbors[chosen] && chosen != reverse)
                    break;
        } else {
            while (chosen < 4 && !neighbors[chosen])
                ++chosen;
            if (chosen == 4) {
                if (staff)
                    return try_set_staff_destination_inside_patrol_area(c, *staff);
                world_operations::set_mobile_entity_thought_bubble(c,
                                                                   &mobile.thought_history,
                                                                   MobileThoughtCode::confused,
                                                                   false);
                mobile.stranded_time = next_world_grid_change_serial(c);
                return false;
            }
        }
        selected = neighbors[chosen];
        reverse = (chosen - 2u) & 3u;
        if (selected == person_detail::cell_id(base) && !remaining)
            remaining = 1;
        const auto old = remaining--;
        if (!old)
            break;
    }
    const auto x_fraction =
        std::clamp(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) & 127u, 5u, 123u);
    const auto y_fraction =
        std::clamp(static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) & 127u, 5u, 123u);
    const auto value = static_cast<std::uint32_t>(selected) - 1u;
    const auto x = static_cast<std::uint16_t>(((value & 127u) << 8) | x_fraction),
               y = static_cast<std::uint16_t>(((value >> 7) << 8) | y_fraction);
    return finish(staff ? set_mobile_entity_destination_and_path(c, base, mobile, x, y)
                        : request_mobile_entity_path_to_destination(c, base, mobile, x, y));
}
} //namespace simcoaster
