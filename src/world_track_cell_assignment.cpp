#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_bank.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
BankRecord& primary_bank(WorldRuntime& c) {
    if (!c.simulation.reference)
        throw std::invalid_argument("null track-assignment World");
    auto* payload = c.entities.at(c.simulation.reference->saved.bank_account).payload;
    if (!payload)
        throw std::invalid_argument("null track-assignment bank");
    return *reinterpret_cast<BankRecord*>(payload);
}

bool plain_map_cell(const MapCell* cell) {
    //The native instruction sequence accepts EXACT types0/1; its unsigned
    //enum decompilation can misleadingly render this as mType<=1.
    return cell->base.common.type == 0 || cell->base.common.type == 1;
}
} //namespace

bool try_assign_world_cell_type(WorldRuntime& c, CellBase* cell, std::int32_t requested) {
    if (!cell)
        throw std::invalid_argument("null track-assignment cell");
    std::int32_t cost = 0;
    if (!c.construction.cell_costs_suppressed) {
        if (requested == 11)
            cost = c.construction.kart_track_cell_base_cost;
        else if (requested == 16)
            cost = c.construction.water_track_cell_base_cost;
    }
    //Native0x682EE0 always resolves this global-map entry, even when a later
    //compatibility check rejects the request or same-type overlap is enough.
    (void)get_world_map_cell_record_by_id(c, cell->index);
    const auto old = cell->common.type;
    if (old != 25 && old != 7 && !(cell->common.flags & 64u) && requested != 0 && old != 0 && requested != old)
        return requested == 25;
    if (old == requested) {
        cell->common.overlap_counter = std::bit_cast<std::int16_t>(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(cell->common.overlap_counter) + 1u));
        return true;
    }
    bool affordable = true;
    if (!c.construction.action_recording_disabled) {
        const auto difference = numeric::bits(primary_bank(c).balance) - numeric::bits(cost);
        affordable = std::bit_cast<std::int32_t>(difference) >= 0; //SUB followed by SETNS.
    }
    if (requested != 25 && !affordable && requested != 0 && requested != 7)
        return false;

    cell->common.type = requested;
    const auto serial = next_world_grid_change_serial(c);
    store_world_grid_change_serial_for_cell_id(c.simulation.reference, serial, cell->index);
    if ((cell->common.type == 11 || cell->common.type == 16) && c.construction.track_endpoint_phase) {
        auto* origin = get_world_map_cell_record_by_id(c, cell->index);
        //Verified startup initializers establish the actual X+, XY+, Y+ order.
        //The origin is GLOBAL storage; all three neighbors are BASE-World maps.
        auto* x = resolve_world_cell_by_scaled_local_offset(c, &origin->base, { 1, 0 }, 1);
        if (x) {
            auto* xy = resolve_world_cell_by_scaled_local_offset(c, &origin->base, { 1, 1 }, 1);
            if (xy) {
                auto* y = resolve_world_cell_by_scaled_local_offset(c, &origin->base, { 0, 1 }, 1);
                if (y && plain_map_cell(origin) && plain_map_cell(x) && plain_map_cell(xy) && plain_map_cell(y)) {
                    //Validate every cell first, then retain these pointers across all
                    //four callbacks, even if a callback changes types or World aliases.
                    remove_map_cell_contents(c, &origin->base, 0, 0);
                    remove_map_cell_contents(c, &x->base, 0, 0);
                    remove_map_cell_contents(c, &xy->base, 0, 0);
                    remove_map_cell_contents(c, &y->base, 0, 0);
                }
            }
        }
    }
    //Suppression is live after removal callbacks; the amount remains the cost
    //captured at entry. A zero cost still performs the real bank operation.
    if (requested != 0 && requested != 7 && !c.construction.cell_costs_suppressed)
        charge_world_bank_account_expense(c, &primary_bank(c), cost);
    return true;
}
} //namespace simcoaster
