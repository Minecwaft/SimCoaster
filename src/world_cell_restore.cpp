#include "simcoaster/world_grid.hpp"
#include <stdexcept>

namespace simcoaster {
void restore_world_map_cell_mesh(WorldRuntime& c, MapCell* cell) {
    if (!cell)
        throw std::invalid_argument("null map mesh restore cell");
    auto& base = cell->base;
    auto& fields = base.common;
    const auto group = fields.render_data.texture_group, coordinate = static_cast<std::uint32_t>(base.index) - 1u;
    const auto x = static_cast<std::int32_t>(coordinate & 127u), y = static_cast<std::int32_t>(coordinate >> 7);
    //Restored slots are stale derived IDs. This path never releases them.
    fields.mesh_instance = 0;
    if (group == 2) {
        fields.mesh_instance = create_or_refresh_queue_mesh_instance(c, x, y, fields.render_data, true);
        //Unlike the ordinary render refresh, this tuple lives on the stack and
        //does not overwrite either shared render scratch global.
        const CellRenderData empty{ 0, 8, 0 };
        c.ports.meshes.submit_world_cell_render_update(c, x, y, empty);
    } else
        c.ports.meshes.submit_world_cell_render_update(c, x, y, fields.render_data);
}

void update_world_track_cell_mesh(WorldRuntime& c, TrackCell* cell) {
    if (!cell)
        throw std::invalid_argument("null track mesh restore cell");
    auto& base = cell->base;
    auto& fields = base.common;
    const auto coordinate = static_cast<std::uint32_t>(base.index) - 1u;
    const auto x = static_cast<std::int32_t>(coordinate & 127u), y = static_cast<std::int32_t>(coordinate >> 7);
    if (get_world_map_cell_record_by_id(c, base.index)->base.common.type != 21)
        fields.mesh_instance = 0;
    if (!(fields.flags & 16u)) {
        if (fields.render_data.texture_group == 3)
            fields.mesh_instance = create_or_refresh_gokart_track_mesh_instance(c, x, y, fields.render_data, true);
        else if (fields.render_data.texture_group == 4)
            fields.mesh_instance = create_or_refresh_water_ride_track_mesh_instance(c, x, y, fields.render_data, true);
    }
}

void refresh_world_cell_meshes_after_restore(WorldRuntime& c, World& restored) {
    //The native receiver points at the map array within this World; the track
    //array follows it. Traverse physical storage, using each record's OWN ID
    //inside its refresh helper. WorldRuntime World bindings may differ or change.
    for (std::size_t i = 0; i < map_cell_count; ++i) {
        restore_world_map_cell_mesh(c, &restored.map_cells[i]);
        update_world_track_cell_mesh(c, &restored.track_cells[i]);
    }
}
} //namespace simcoaster
