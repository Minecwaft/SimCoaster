#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_navigation.hpp"
#include <limits>

namespace simcoaster {
bool has_clear_entry_exit_path(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    const auto& exit = get_world_map_cell_record_by_id(c, object.exit_cell)->base;
    auto direction = get_world_cell_direction(&exit);
    if (object.entry_cell == object.exit_cell)
        direction = opposite_world_cell_direction(direction);
    if (!direction)
        return true;
    const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &exit, direction);
    if (!neighbor)
        throw std::logic_error("object exit has an invalid directional neighbor");
    const auto cell = static_cast<std::uint32_t>(neighbor->base.index) - 1u;
    constexpr std::array<std::uint8_t, 4> selectors{ 1, 4, 16, 64 };
    const GridPathCell position{ static_cast<std::uint8_t>(cell & 127u), static_cast<std::uint8_t>(cell >> 7) };
    const auto reverse = opposite_world_cell_direction(direction);
    for (std::size_t i = 0; i < selectors.size(); ++i)
        if (selectors[i] != reverse && !is_world_cell_step_blocked(c, position, static_cast<GridPathDirection>(i), 0))
            return true;
    return false;
}

CellLocalOffset rotate_park_object_local_cell_offset(CellLocalOffset value, std::int32_t rotation) {
    return match (rotation % 360) -> CellLocalOffset {
        case 90 or -270 => {
            value.y, fixed::negate(value.x)
        };
        case 180 or -180 => {
            fixed::negate(value.x), fixed::negate(value.y)
        };
        case 270 or -90 => {
            fixed::negate(value.y), value.x
        };
        case _ => value;
    };
}

CellNeighborDirection get_cell_direction_toward_point(WorldCellCoordinates target, WorldCellCoordinates source) {
    const auto x = fixed::subtract(source.x, target.x), y = fixed::subtract(source.y, target.y);
    const auto nx = fixed::negate(x), ny = fixed::negate(y);
    const auto twice = [](std::int32_t value) {
        return fixed::add(value, value);
    };
    CellNeighborDirection result;
    if (x < 0) {
        if (y < 0) {
            result = twice(nx) < ny ? CellNeighborDirection::south : CellNeighborDirection::southeast;
            if (twice(ny) < nx)
                return CellNeighborDirection::east;
        } else {
            result = twice(nx) < y ? CellNeighborDirection::north : CellNeighborDirection::northeast;
            if (twice(y) < nx)
                return CellNeighborDirection::east;
        }
    } else if (y < 0) {
        result = twice(x) < ny ? CellNeighborDirection::south : CellNeighborDirection::southwest;
        if (twice(ny) < x)
            return CellNeighborDirection::west;
    } else {
        result = twice(x) < y ? CellNeighborDirection::north : CellNeighborDirection::northwest;
        if (twice(y) < x)
            return CellNeighborDirection::west;
    }
    return result;
}

std::array<std::uint16_t, 2> compute_park_object_service_target_coordinates(AttractionWorld& c,
                                                                            const ParkObjectRuntimeView& object) {
    const auto x = get_park_object_balance(c, object).UsageInfo.EntryCellStandPosX;
    const auto y = get_park_object_balance(c, object).UsageInfo.EntryCellStandPosY;
    double rotated_x;
    float rotated_y;
    match (std::bit_cast<std::int32_t>(object.angle)) {
        case 0 => {
            rotated_x = x;
            rotated_y = y;
        }
        case 90 => {
            rotated_x = y;
            rotated_y = static_cast<float>(1.0 - x);
        }
        case 180 => {
            rotated_x = 1.0 - x;
            rotated_y = static_cast<float>(1.0 - y);
        }
        case 270 => {
            rotated_x = 1.0 - y;
            rotated_y = x;
        }
        case _ => throw std::domain_error("service target requires a quarter-turn orientation");
    }
    const auto value = static_cast<std::uint32_t>(object.entry_cell) - 1u;
    return { static_cast<std::uint16_t>(((value & 127u) << 8)
                                        | static_cast<std::uint8_t>(fixed::truncate_integer(rotated_x * 255.0))),
             static_cast<std::uint16_t>(
                 (((value >> 7) & 255u) << 8)
                 | static_cast<std::uint8_t>(fixed::truncate_integer(static_cast<double>(rotated_y) * 255.0))) };
}

std::array<std::uint16_t, 2>
get_park_object_entry_exit_position(AttractionWorld& c, const ParkObjectRuntimeView& object, bool use_exit) {
    const auto x = use_exit ? get_park_object_balance(c, object).UsageInfo.ExitCellAppearPosX
                            : get_park_object_balance(c, object).UsageInfo.EntryCellStandPosX;
    const auto y = use_exit ? get_park_object_balance(c, object).UsageInfo.ExitCellAppearPosY
                            : get_park_object_balance(c, object).UsageInfo.EntryCellStandPosY;
    double rotated_x = y;
    float rotated_y = y;
    match (object.angle) {
        case 0 => rotated_x = x;
        case 90 => rotated_y = static_cast<float>(1.0 - x);
        case 180 => {
            rotated_x = 1.0 - x;
            rotated_y = static_cast<float>(1.0 - y);
        }
        case 270 => {
            rotated_x = 1.0 - y;
            rotated_y = x;
        }
        case _ => {}
    }
    const auto cell = static_cast<std::uint32_t>(use_exit ? object.exit_cell : object.entry_cell) - 1u;
    return { static_cast<std::uint16_t>(((cell & 127u) << 8)
                                        | static_cast<std::uint8_t>(fixed::truncate_integer(rotated_x * 255.0))),
             static_cast<std::uint16_t>(
                 (((cell >> 7) & 255u) << 8)
                 | static_cast<std::uint8_t>(fixed::truncate_integer(static_cast<double>(rotated_y) * 255.0))) };
}

CellId find_nearest_path_cell_around_object(AttractionWorld& c, const ParkObjectRuntimeView& object, CellId reference) {
    const auto& balance = get_park_object_balance(c, object);
    const auto offset = rotate_park_object_local_cell_offset({ balance.Info.MapOffsetX, balance.Info.MapOffsetY },
                                                             std::bit_cast<std::int32_t>(object.angle));
    if (!balance.Info.Shape)
        throw std::logic_error("object footprint is missing");
    const auto width = balance.Info.Shape->width, height = balance.Info.Shape->height;
    const auto anchor = static_cast<CellId>(static_cast<std::uint32_t>(object.top_left)
                                            + static_cast<std::uint32_t>(offset.x)
                                            + (static_cast<std::uint32_t>(offset.y) << 7));
    const auto value = static_cast<std::uint32_t>(anchor) - 1u;
    const auto ox = static_cast<std::int32_t>(value & 127u), oy = static_cast<std::int32_t>(value >> 7);
    CellId best = 0, chosen_object_cell = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    auto& primary = person_detail::primary_world(c);
    constexpr std::array<WorldCellCoordinates, 4> neighbors{ { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
    for (std::int32_t row = 0; row < height; ++row)
        for (std::int32_t column = 0; column < width; ++column) {
            WorldCellCoordinates tile{};
            match (object.angle) {
                case 0 => tile = { ox + column, oy + row };
                case 90 => tile = { ox + row, oy - column };
                case 180 => tile = { ox - column, oy + row };
                case 270 => tile = { ox + row, oy + column };
                case _ => {}
            }
            for (const auto delta : neighbors) {
                const auto id = static_cast<CellId>(
                    ((static_cast<std::uint32_t>(tile.y) + static_cast<std::uint32_t>(delta.y)) << 7)
                    + static_cast<std::uint32_t>(tile.x)
                    + static_cast<std::uint32_t>(delta.x)
                    + 1u);
                if (!id)
                    throw std::out_of_range("zero object-neighbor cell");
                if (primary.map_cells.at(id - 1u).base.common.type != 1)
                    continue;
                const auto d = person_detail::squared_cell_distance(reference, id);
                if (d < distance) {
                    distance = d;
                    best = id;
                    chosen_object_cell = static_cast<CellId>(
                        (static_cast<std::uint32_t>(tile.y) << 7) + static_cast<std::uint32_t>(tile.x) + 1u);
                }
            }
        }
    if (best) {
        const auto target = static_cast<std::uint32_t>(chosen_object_cell) - 1u,
                   source = static_cast<std::uint32_t>(best) - 1u;
        set_world_cell_direction(
            &primary.map_cells[best - 1u].base,
            std::to_underlying(get_cell_direction_toward_point(
                { static_cast<std::int32_t>(target & 127u), static_cast<std::int32_t>(target >> 7) },
                { static_cast<std::int32_t>(source & 127u), static_cast<std::int32_t>(source >> 7) })));
    }
    return best;
}

ThingId get_info_desk_promoted_attraction_id(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    return get_park_object_balance(c, object).UsageInfo.IsInfoDesk ? object.promoted_attraction : 0;
}
} //namespace simcoaster
