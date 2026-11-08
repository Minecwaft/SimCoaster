#include "simcoaster/world_grid.hpp"
#include <stdexcept>

namespace simcoaster {
bool apply_definition_grid_cell_type(WorldRuntime& c, CellBase* cell, std::int32_t requested) {
    if (!cell) {
        throw std::invalid_argument("null definition-grid cell");
    }
    c.construction.endpoint_stop_pending = 0;
    const auto old = cell->common.type;
    if (c.construction.line_endpoint_dispatch && requested != 21 && (old == 1 || old == 3)) {
        c.construction.endpoint_stop_pending = 1;
    }
    if (requested == 21 && ((cell->common.flags & 256) || old == 4 || old == 9 || old == 10)) {
        c.construction.endpoint_stop_pending = 1;
    }
    if (!try_change_world_cell_type(c, cell, requested)) {
        return false;
    }
    c.construction.last_successful_type = requested;
    return true;
}

bool apply_hoarding_track_cell_command(WorldRuntime& c, CellBase* cell, std::int32_t command) {
    if (!cell) {
        throw std::invalid_argument("null hoarding track cell");
    }
    if (command == 0) {
        c.construction.track_endpoint_stop_pending = 0;
        const auto old = cell->common.type;
        if ((c.construction.track_endpoint_phase && (old == 11 || old == 13 || old == 16 || old == 18)) || old == 25) {
            c.construction.track_endpoint_stop_pending = 1;
        }
        if (!try_assign_world_cell_type(c, cell, 0)) {
            return false;
        }
        c.construction.last_successful_type = 0;
        return true;
    }
    if (command != 129 || cell->common.type != 0) {
        throw std::invalid_argument("hoarding loader requires cleared track before operation129");
    }
    //The loader has just assigned0. The dispatcher's substantive type25 mesh/
    //footprint propagation branch is therefore unreachable in this sequence.
    if (c.construction.world_hoarding_mode && get_world_map_cell_record_by_id(c, cell->index)->base.common.type == 21) {
        return true;
    }
    if (get_world_cell_corner_neighbor_mask(c, cell) || get_world_cell_branch_neighbor_mask(c, cell)) {
        clear_world_cell_flags(cell, 15);
    }
    c.construction.last_successful_type = 0;
    update_world_cell_mesh(c, cell);
    if (c.construction.track_endpoint_phase && c.construction.track_endpoint_stop_pending) {
        c.construction.track_endpoint_stop_pending = 0;
        return false;
    }
    return true;
}
} //namespace simcoaster
