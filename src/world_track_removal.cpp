#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_bank.hpp"
#include <bit>
#include <stdexcept>
#include <vector>

namespace simcoaster {
namespace {
World& primary(WorldRuntime& c) {
    return require_bound<std::invalid_argument>(c.simulation.reference, "null track-removal primary World");
}

TrackCell& primary_track(WorldRuntime& c, CellId id) {
    if (!id)
        throw std::out_of_range("zero track-removal parent ID");
    return primary(c).track_cells.at(static_cast<std::size_t>(id) - 1);
}

CellId anchor_id(const std::array<std::int32_t, 2>& point) {
    return is_world_map_coordinate_in_bounds(point[0], point[1])
             ? static_cast<CellId>(
                   (static_cast<std::uint32_t>(point[1]) << 7) + static_cast<std::uint32_t>(point[0]) + 1u)
             : 0;
}

void clear_neighbors(WorldRuntime& c, CellBase& cell, std::uint8_t mask) {
    clear_world_cell_neighbor_mask_bits(c, &cell, mask);
}

void clear_direction(CellBase& cell, std::uint8_t mask) {
    cell.common.direction = static_cast<std::uint8_t>(cell.common.direction & static_cast<std::uint8_t>(~mask));
}

void or_neighbors(WorldRuntime& c, CellBase& cell, std::uint8_t mask) {
    auto& field = c.construction.world_hoarding_mode && cell.record_kind == CellStorageKind::track
                    ? cell.common.hoarding_neighbors
                    : cell.common.neighbors;
    field = static_cast<std::uint8_t>(field | mask);
}

bool root_type(std::int32_t type) {
    return type == 11 || type == 13 || type == 16 || type == 18 || type == 25;
}

bool variant_neighbor(WorldRuntime& c, const CellBase& cell) {
    if (world_cell_is_track_child(&cell)) {
        const auto type = primary_track(c, cell.common.parent_id).base.common.type;
        if (type == 13 || type == 18)
            return true;
    }
    return cell.common.type == 13 || cell.common.type == 18;
}

void refund_track_cell(WorldRuntime& c, CellBase& cell, std::int32_t base_cost) {
    auto& parent = primary_track(c, cell.common.parent_id).base;
    const auto id = find_first_park_object_id_in_map_cell(c, get_world_map_cell_record_by_id(c, parent.index));
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::invalid_argument("track refund without park object");
    const auto percent = get_park_object_scrap_value_percent(c, reinterpret_cast<ParkObjectRuntimeView*>(payload));
    const auto amount = (std::bit_cast<std::uint32_t>(base_cost) * std::bit_cast<std::uint32_t>(percent)) / 100u;
    auto* bank = c.entities.at(primary(c).saved.bank_account).payload;
    if (!bank)
        throw std::invalid_argument("track refund without bank");
    apply_bank_balance_and_profit_delta(c, reinterpret_cast<BankRecord*>(bank), static_cast<std::int32_t>(amount));
}

void finish_track_removal(WorldRuntime& c, CellBase& cell, CellId current) {
    if (cell.index == current && root_type(cell.common.type) && !c.construction.track_anchor_removal_enabled) {
        for (std::uint8_t direction = 1;; direction = advance_world_cell_direction(direction, 4)) {
            if (!(get_world_cell_neighbor_mask(c, &cell) & direction)) {
                auto* neighbor = resolve_track_cell_in_direction(c, &cell, direction, 2);
                if (neighbor) {
                    auto& other = neighbor->base;
                    if (variant_neighbor(c, other) && other.common.parent_id == cell.common.parent_id) {
                        set_world_cell_direction(&cell, opposite_world_cell_direction(direction));
                        or_neighbors(c, cell, direction);
                        update_world_cell_mesh(c, &cell);
                    }
                    if (!root_type(other.common.type)) {
                        clear_neighbors(c, cell, direction);
                        update_world_cell_mesh(c, &cell);
                    }
                }
            }
            if (advance_world_cell_direction(direction, 4) == 1)
                break;
        }
        if (count_world_cell_neighbor_bits(c, &cell) == 1) {
            clear_direction(cell, 255);
            for (std::uint8_t direction = 1;; direction = advance_world_cell_direction(direction, 4)) {
                if (get_world_cell_neighbor_mask(c, &cell) & direction) {
                    const auto* neighbor = resolve_track_cell_in_direction(c, &cell, direction, 2);
                    if (neighbor
                        && variant_neighbor(c, neighbor->base)
                        && neighbor->base.common.parent_id == cell.common.parent_id)
                        set_world_cell_direction(&cell, opposite_world_cell_direction(direction));
                }
                if (advance_world_cell_direction(direction, 4) == 1)
                    break;
            }
        }
    }
    if (cell.common.type && !cell.common.mesh_instance)
        update_world_cell_mesh(c, &cell);
}

void remove_root(WorldRuntime& c, CellBase& cell, std::int32_t x, std::int32_t y, std::int32_t refresh) {
    //Cost is selected from the initial type before any mutation or callback.
    const auto base_cost = cell.common.type == 11 ? c.construction.kart_track_cell_base_cost
                         : cell.common.type == 16 ? c.construction.water_track_cell_base_cost
                                                  : 0;
    const auto current = anchor_id(c.construction.interaction_cursor_anchor),
               start = anchor_id(c.construction.interaction_secondary_anchor);
    if (root_type(cell.common.type)) {
        bool refresh_before_gate = false;
        if (!x && !y && cell.index != current)
            cell.common.overlap_counter = -1;
        else {
            cell.common.overlap_counter = std::bit_cast<std::int16_t>(
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(cell.common.overlap_counter) - 1u));
            if (x == -1 && y == -1) {
                const auto direction = get_world_cell_direction(&cell);
                if (direction == 1 || direction == 16) {
                    clear_neighbors(c, cell, 4);
                    clear_neighbors(c, cell, 64);
                } else if (direction == 4 || direction == 64) {
                    clear_neighbors(c, cell, 1);
                    clear_neighbors(c, cell, 16);
                }
                refresh_before_gate = true;
            } else if (std::popcount(static_cast<unsigned>(cell.common.direction & 85)) >= 2) {
                if (x == -2)
                    clear_direction(cell, 4);
                else if (x == 2)
                    clear_direction(cell, 64);
                if (y == -2)
                    clear_direction(cell, 16);
                else if (y == 2)
                    clear_direction(cell, 1);
                if (x == 2 || x == -2) {
                    clear_neighbors(c, cell, 4);
                    clear_neighbors(c, cell, 64);
                }
                if (y == 2 || y == -2) {
                    clear_neighbors(c, cell, 16);
                    clear_neighbors(c, cell, 1);
                }
                refresh_before_gate = true;
            } else if (cell.index != current && cell.index != start) {
                const bool was_corner = get_world_cell_corner_neighbor_mask(c, &cell) != 0;
                if (x == -2)
                    clear_neighbors(c, cell, 4);
                else if (x == 2)
                    clear_neighbors(c, cell, 64);
                if (y == -2)
                    clear_neighbors(c, cell, 16);
                else if (y == 2)
                    clear_neighbors(c, cell, 1);
                if (was_corner && count_world_cell_neighbor_bits(c, &cell) == 1)
                    set_world_cell_direction(&cell,
                                             opposite_world_cell_direction(get_world_cell_neighbor_mask(c, &cell)));
                refresh_before_gate = true;
            }
        }
        if (refresh_before_gate)
            update_world_cell_mesh(c, &cell);
        if ((cell.common.overlap_counter < 0
             && ((cell.index != current && cell.index != start) || c.construction.track_anchor_removal_enabled))
            || c.construction.force_cell_removal) {
            const auto old_type = cell.common.type;
            cell.common.overlap_counter = 0;
            if (old_type != 25 && !c.construction.action_recording_disabled)
                refund_track_cell(c, cell, base_cost);
            for (std::uint8_t direction = 1;; direction = advance_world_cell_direction(direction, 4)) {
                if (get_world_cell_neighbor_mask(c, &cell) & direction) {
                    clear_neighbors(c, cell, direction);
                    auto* neighbor = resolve_track_cell_in_direction(c, &cell, direction, 2);
                    if (neighbor) {
                        auto& other = neighbor->base;
                        clear_neighbors(c, other, opposite_world_cell_direction(direction));
                        update_world_cell_mesh(c, &other);
                        if (count_world_cell_neighbor_bits(c, &other) == 1) {
                            const auto mask = get_world_cell_neighbor_mask(c, &other);
                            if (get_world_cell_direction(&other) != mask
                                && get_world_cell_direction(&other) != opposite_world_cell_direction(mask))
                                set_world_cell_direction(&other, opposite_world_cell_direction(mask));
                        }
                    }
                }
                if (advance_world_cell_direction(direction, 4) == 1)
                    break;
            }
            if (cell.common.flags & 16) {
                const auto index = static_cast<std::uint32_t>(cell.index) - 1;
                c.ports.construction.remove_park_object_at_world_tile(c,
                                                                      static_cast<std::int32_t>(index & 127u),
                                                                      static_cast<std::int32_t>(index >> 7),
                                                                      1);
            }
            set_world_cell_direction(&cell, 0);
            cell.common.flags = 0;
            (void)try_assign_world_cell_type(c, &cell, 0);
            if (cell.common.type != 25) {
                set_world_cell_parent_id_unchecked(&cell, 0);
                reinterpret_cast<TrackCell*>(&cell)->segment_number = 65535;
            }
            update_world_cell_mesh(c, &cell);
            //The reference cell's LIVE type controls all three parent clears.
            for (const CellLocalOffset offset : std::array<CellLocalOffset, 3>{ { { 0, 1 }, { 1, 0 }, { 1, 1 } } }) {
                auto* neighbor = resolve_track_cell_at_local_offset(c, &cell, offset, 1);
                if (neighbor && neighbor->base.common.parent_id == cell.index) {
                    auto& other = neighbor->base;
                    clear_neighbors(c, other, 255);
                    set_world_cell_direction(&other, 0);
                    other.common.flags = 0;
                    (void)try_assign_world_cell_type(c, &other, 0);
                    other.common.overlap_counter = 0;
                    if (cell.common.type != 25)
                        set_world_cell_parent_id_unchecked(&other, 0);
                    if (static_cast<std::uint8_t>(refresh))
                        update_world_cell_mesh(c, &other);
                }
            }
        }
    }
    finish_track_removal(c, cell, current);
}
} //namespace

void remove_track_cell_contents(WorldRuntime& c, CellBase* cell, std::int32_t x, std::int32_t y, std::int32_t refresh) {
    if (!cell)
        throw std::invalid_argument("null track-removal cell");
    //Native recursively follows12/17 parents. An explicit stack preserves the
    //return-time child mesh checks without risking host stack overflow on a bad
    //chain. All parent lookups use the live PRIMARY binding.
    std::vector<CellBase*> children;
    while (world_cell_is_track_child(cell)) {
        if (children.size() >= map_cell_count)
            throw std::length_error("cyclic track-removal parent chain");
        children.push_back(cell);
        cell = &primary_track(c, cell->common.parent_id).base;
        refresh = 1;
    }
    remove_root(c, *cell, x, y, refresh);
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        finish_track_removal(c, **it, 0);
}
} //namespace simcoaster
