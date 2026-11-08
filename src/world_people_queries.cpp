#include "simcoaster/world_objects.hpp"
#include <stdexcept>

namespace simcoaster {
bool record_passes_grid_predicates(VisitorWorld& c, const MapObjectBase* record) {
    if (!record) {
        throw std::invalid_argument("null map object");
    }
    if (!c.simulation.map_cell_records) {
        throw std::invalid_argument("null global map cell array");
    }
    //Native0x60BE00 reads the high bytes of the two saved coordinate WORDs,
    //forms a one-based cell ID, then indexes the process-global cell array.
    //A high X tile can alias the following row; there is no separate X bound.
    const auto tile_x = static_cast<std::uint32_t>(record->x >> 8);
    const auto tile_y = static_cast<std::uint32_t>(record->y >> 8);
    const auto cell_id = static_cast<CellId>((tile_y << 7) + tile_x + 1);
    if (!cell_id || cell_id > map_cell_count) {
        throw std::out_of_range("map object cell index");
    }
    const auto type = c.simulation.map_cell_records[cell_id - 1].base.common.type;
    //These are five exact DWORD predicates, including the redundant native9
    //check; unrelated nonzero bits and the cell's storage kind do not qualify.
    return type == 0 || type == 1 || type == 3 || type == 9 || type == 10;
}

std::uint32_t count_eligible_person_records(VisitorWorld& c, std::int32_t generation_selector) {
    //Native0x5B2060 walks the process-global active list. Retirement-pending
    //people remain visible, and selector4 bypasses the generation comparison.
    ThingId id = c.entities.active_head ? static_cast<ThingId>(c.entities.active_head->id_storage) : 0;
    std::size_t traversed = 0;
    std::uint32_t count = 0;
    while (id) {
        if (traversed++ >= c.entities.size()) {
            throw std::length_error("cyclic active thing list");
        }
        const auto* payload = c.entities.at(id).payload;
        if (!payload) {
            throw std::invalid_argument("null active thing payload");
        }
        if (payload->kind == ThingKind::person) {
            const auto* person = reinterpret_cast<const PersonRuntimeView*>(payload);
            if (record_passes_grid_predicates(c, &person->base) && (generation_selector == 4 || static_cast<std::int32_t>(person->generation) == generation_selector)) {
                ++count;
            }
        }
        //As in the native helper, the payload's own header ID selects the next
        //metadata link; it need not equal the cursor used to fetch that payload.
        const auto* next = c.entities.at(payload->id).active_next;
        id = next ? static_cast<ThingId>(next->id_storage) : 0;
    }
    return count;
}
} //namespace simcoaster
