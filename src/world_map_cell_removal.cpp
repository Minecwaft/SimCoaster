#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int16_t decrement_overlap(std::int16_t count) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint16_t>(count) - 1u));
}

World& primary_world(WorldRuntime& c) {
    return require_bound<std::invalid_argument>(c.simulation.reference, "null map-removal primary World");
}

MapCell& primary_map_cell(WorldRuntime& c, CellId id) {
    if (!id || id > map_cell_count)
        throw std::out_of_range("map-removal parent cell ID");
    return primary_world(c).map_cells.at(static_cast<std::size_t>(id - 1));
}

BankRecord& primary_bank(WorldRuntime& c) {
    auto* payload = c.entities.at(primary_world(c).saved.bank_account).payload;
    if (!payload)
        throw std::invalid_argument("null map-removal bank");
    return *reinterpret_cast<BankRecord*>(payload);
}

void refund_remaining_queue_cell(WorldRuntime& c, CellBase* cell) {
    auto& parent = primary_map_cell(c, cell->common.parent_id);
    //The native eligibility DWORD is the parent's cell TYPE, at record+8.
    if (!parent.base.common.type)
        return;
    const auto id = find_first_park_object_id_in_map_cell(c, &parent);
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::invalid_argument("queue refund has no park object");
    const auto percent = get_park_object_scrap_value_percent(c, reinterpret_cast<ParkObjectRuntimeView*>(payload));
    //IMUL retains32 bits; the following division is UNSIGNED. Read the live
    //configured cost and primary bank after the scrap getter's real callbacks.
    const auto amount = numeric::bits(percent) * numeric::bits(c.construction.queue_cell_base_cost) / 100u;
    apply_bank_balance_and_profit_delta(c, &primary_bank(c), numeric::word(amount));
}

void disconnect_neighbors(WorldRuntime& c, CellBase* cell, std::uint8_t multiplier, bool rebuild_adjacent_queues) {
    std::uint8_t direction = 1;
    do {
        if (get_world_cell_neighbor_mask(c, cell) & direction) {
            clear_world_cell_neighbor_mask_bits(c, cell, direction);
            if (auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, cell, direction)) {
                clear_world_cell_neighbor_mask_bits(c, &neighbor->base, opposite_world_cell_direction(direction));
                refresh_world_cell_render_state(c, &neighbor->base);
                //Refresh can change either record, its parent or the primary World.
                if (rebuild_adjacent_queues
                    && cell->common.type == 1
                    && neighbor->base.common.type == 9
                    && (direction & 85)) {
                    const auto parent = neighbor->base.common.parent_id;
                    if (parent) {
                        const auto id = find_first_park_object_id_in_map_cell(c, &primary_map_cell(c, parent));
                        if (id) {
                            auto* object = c.entities.at(id).payload;
                            if (object && object->kind == ThingKind::park_object)
                                rebuild_park_object_queue_layout(c, reinterpret_cast<ParkObjectRuntimeView*>(object));
                        }
                    }
                }
            }
        }
        direction = advance_world_cell_direction(direction, multiplier);
    } while (direction != 1);
}

void finish_removal(WorldRuntime& c, CellBase* cell, bool path_or_exit) {
    clear_world_cell_neighbor_mask_bits(c, cell, 255);
    set_world_cell_direction(cell, 0);
    if (path_or_exit)
        cell->common.flags = 0;
    set_world_cell_parent_id_unchecked(cell, 0);
    if (!path_or_exit)
        cell->common.flags = 0;
    (void)try_change_world_cell_type(c, cell, 0);
    refresh_world_cell_render_state(c, cell);
}
} //namespace

void remove_map_cell_contents(WorldRuntime& c, CellBase* cell, std::uint32_t option_bits, std::int32_t option) {
    if (!cell)
        throw std::invalid_argument("null map-removal cell");
    if (cell->common.type == 1 && (cell->common.flags & 32) && !count_world_cell_neighbor_bits(c, cell))
        clear_world_cell_flags(cell, 32);
    //The debug call in the native zero-neighbor branch is an empty function.
    if ((cell->common.flags & 32) && !c.construction.force_cell_removal)
        return;
    match (cell->common.type) {
        case 1 or 10 => {
            cell->common.overlap_counter = option_bits || option ? decrement_overlap(cell->common.overlap_counter) : -1;
            if (cell->common.overlap_counter >= 0 && !c.construction.force_cell_removal)
                return;
            cell->common.overlap_counter = 0;
            disconnect_neighbors(c, cell, 2, true);
            finish_removal(c, cell, true);
            return;
        }
        case 3 or 9 => {
            cell->common.overlap_counter = (!option_bits && !option) || get_world_cell_corner_neighbor_mask(c, cell)
                                             ? -1
                                             : decrement_overlap(cell->common.overlap_counter);
            const bool forced = c.construction.force_cell_removal != 0;
            if (!forced && cell->common.overlap_counter >= 0)
                return;
            cell->common.overlap_counter = 0;
            if (cell->common.type != 9)
                refund_remaining_queue_cell(c, cell);
            //Forced queue/entry removal bypasses reciprocal neighbor disconnection.
            if (!forced)
                disconnect_neighbors(c, cell, 4, false);
            finish_removal(c, cell, false);
            return;
        }
        case 4 => {
            finish_removal(c, cell, false);
            return;
        }
        case 21 => {
            resolve_track_cell_for_grid_entry(c, cell)->base.common.mesh_instance = 0;
            finish_removal(c, cell, false);
            return;
        }
        case _ => return;
    }
}
} //namespace simcoaster
