#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_grid.hpp"
#include <stdexcept>
#include <utility>

namespace simcoaster {
bool is_source_grid_coordinate_eligible(const WorldRuntime& c, std::uint32_t x, std::uint32_t y) {
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid) {
        return true;
    }
    const auto& grid = *c.construction.active_terrain_model_data->terrain_grid;
    if (x >= grid.width_cells || y >= grid.height_cells) {
        return false;
    }
    const auto index = std::size_t { x } + std::size_t { y } * grid.width_cells;
    if (index >= grid.cell_metadata.size()) {
        throw std::out_of_range("terrain cell metadata extent");
    }
    return !(std::to_underlying(grid.cell_metadata[index].flags) & std::to_underlying(TerrainCellFlags::source_grid_excluded));
}

void reset_world_track_cell_defaults(WorldRuntime& c, TrackCell* cell) {
    if (!cell) {
        throw std::invalid_argument("null track reset cell");
    }
    auto& base = cell->base;
    auto& fields = base.common;
    const auto coordinate = static_cast<std::uint32_t>(base.index) - 1u;
    //A rejected assignment keeps the previous type. This is a request through
    //the normal assignment rules, including their real serial/bank dependencies.
    (void)try_assign_world_cell_type(c, &base, is_source_grid_coordinate_eligible(c, coordinate & 127u, coordinate >> 7) ? 0 : 7);
    clear_world_cell_neighbor_mask_bits(c, &base, 255);
    c.construction.world_hoarding_mode = 1;
    clear_world_cell_neighbor_mask_bits(c, &base, 255);
    c.construction.world_hoarding_mode = 0;
    set_world_cell_direction(&base, 0);
    fields.flags = 0;
    set_world_cell_parent_id_unchecked(&base, 0);
    fields.mesh_instance = 0;
    fields.overlap_counter = 0;
    fields.render_data = { 0, 0, 0 };
    cell->segment_number = 65535;
}

void apply_world_map_cell_status_flags(WorldRuntime& c, MapCell* cell, bool refresh) {
    if (!cell) {
        throw std::invalid_argument("null map status cell");
    }
    auto& base = cell->base;
    auto& fields = base.common;
    const auto coordinate = static_cast<std::uint32_t>(base.index) - 1u;
    clear_world_cell_neighbor_mask_bits(c, &base, 255);
    set_world_cell_direction(&base, 0);
    set_world_cell_parent_id_unchecked(&base, 0);
    const auto status = cell->status_flags;
    fields.flags = 0;
    fields.render_data = { 0, 0, 0 };
    if (is_source_grid_coordinate_eligible(c, coordinate & 127u, coordinate >> 7)) {
        bool special_path = false;
        if (status & 8u) {
            if (!(c.mode.map_status_game_mode_initialized & 1u)) {
                c.mode.map_status_game_mode_initialized = static_cast<std::uint8_t>(c.mode.map_status_game_mode_initialized | 1u);
                initialize_game_mode_selector(c);
            }
            special_path = c.mode.game != GameMode::code_3;
        }
        if (special_path) {
            fields.type = 1;
            update_cell_connections(c, &base, 1, 0, 0, refresh, true);
            if (refresh) {
                refresh_world_cell_render_state(c, &base);
            }
            update_cell_connections(c, &base, fields.type, 0, 0, refresh, true);
            fields.flags = 32;
        } else {
            fields.type = (status & 1u) ? 5 : (status & 2u) ? 2 : (status & 0x80u) ? 30 : 0;
        }
        if (!(fields.flags & 0x20u)) {
            fields.flags = static_cast<std::uint16_t>(fields.flags | 0x40u);
            if (refresh) {
                refresh_world_cell_render_state(c, &base);
            }
        }
    } else {
        fields.type = (status & 2u) ? 2 : (status & 0x88u) ? 30 : 7;
        if (status & 1u) {
            fields.type = 5;
        }
        if (!(fields.flags & 0x20u)) {
            fields.flags = static_cast<std::uint16_t>(fields.flags | 0x40u);
        }
        if (refresh) {
            refresh_world_cell_render_state(c, &base);
        }
    }
    //Use the imported-byte snapshot after all callbacks. The final slot clear
    //deliberately follows rendering, including any newly created queue mesh.
    if (status & 0x10u) {
        fields.flags = static_cast<std::uint16_t>(fields.flags | 0x80u);
    }
    if (status & 4u) {
        fields.flags = static_cast<std::uint16_t>(fields.flags | (4u << 8));
    }
    fields.mesh_instance = 0;
    fields.overlap_counter = 0;
}

std::int32_t reset_world_cells_in_grid(WorldRuntime& c, std::int32_t width, std::int32_t height) {
    if (height <= 0) {
        return 0;
    }
    if (width <= 0) {
        return height;
    }
    const auto primary_world = [&]() -> World& {
        return require_bound<std::invalid_argument>(c.simulation.reference, "null grid reset primary World");
    };
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            const auto id = static_cast<CellId>((static_cast<std::uint32_t>(row) << 7) + static_cast<std::uint32_t>(column) + 1u);
            if (!id || id > map_cell_count) {
                throw std::out_of_range("grid reset cell reference");
            }
            const auto index = static_cast<std::size_t>(id) - 1;
            apply_world_map_cell_status_flags(c, &primary_world().map_cells[index], true);
            //The map refresh can replace PRIMARY. Re-read it for the paired track,
            //while keeping this visit's reference independent of receiver own-ID edits.
            reset_world_track_cell_defaults(c, &primary_world().track_cells[index]);
        }
    }
    return height;
}
} //namespace simcoaster
