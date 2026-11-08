#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
ResearchCatalogDefinition& require_definition(WorldRuntime& c, std::uint16_t id) {
    return require_bound<std::out_of_range>(find_object_definition_by_id(&c.resources.object_catalog, id), "AfterShape object definition");
}

DefinitionGrid& require_grid(DefinitionGrid* grid) {
    return require_bound<std::invalid_argument>(grid, "null AfterShape grid");
}

MapCell& primary_map_cell(WorldRuntime& c, CellId id) {
    if (!c.simulation.reference) {
        throw std::invalid_argument("null AfterShape primary World");
    }
    if (!id) {
        throw std::out_of_range("zero AfterShape cell ID");
    }
    //Native address arithmetic is biased by one record. The natural array begins at
    //logical cell1, matching the existing used-thing placement/link helpers.
    return c.simulation.reference->map_cells.at(static_cast<std::size_t>(id) - 1);
}

void create_replacement(WorldRuntime& c, ResearchCatalogDefinition& definition) {
    const auto x = definition.shape_offset_x, y = definition.shape_offset_y;
    const auto* descriptor = record_descriptor(ThingKind::park_object);
    if (!descriptor) {
        throw std::logic_error("missing park object allocation descriptor");
    }
    auto* raw = static_cast<ParkObjectRuntimeView*>(c.entities.allocate_payload(c.ports.memory, descriptor->allocation_size));
    if (!raw) {
        return; //Native nullable allocation still retires the old object.
    }
    try {
        auto* object = std::construct_at(raw);
        const auto cell = static_cast<CellId>((y << 7) + x + 1u);
        //Allocation may mutate the definition. Its identity is re-read here;
        //placement uses the two coordinates captured before allocation.
        world_operations::initialize_park_object_instance(c, object, cell, definition.id, 0, 0, 0);
    } catch (...) {
        c.entities.release_payload(c.ports.memory, raw);
        throw;
    }
}
} //namespace

void apply_object_definition_after_shape_grid(WorldRuntime& c, ThingHeader* record) {
    if (!record) {
        throw std::invalid_argument("null AfterShape object");
    }
    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(record);
    //The original performs both catalog lookups and discards the first result.
    (void)find_object_definition_by_id(&c.resources.object_catalog, object->definition_id);
    auto& definition = require_definition(c, object->definition_id);
    const auto x = definition.shape_offset_x, y = definition.shape_offset_y;
    if (!definition.gate_to) {
        auto* grid = &require_grid(definition.after_shape);
        std::int32_t row = 0;
        while (row < grid->height) {
            std::int32_t column = 0;
            const auto row_start = ((y + static_cast<std::uint32_t>(row)) << 7) + x + 1u;
            while (column < grid->width) {
                //Resource grids have a fixed40-cell row stride, independent of their
                //current width. The attribute byte is not an AfterShape input.
                const auto index = static_cast<std::size_t>(row) * 40 + static_cast<std::size_t>(column);
                const auto command = grid->cells.at(index).code;
                const auto id = static_cast<CellId>(row_start + static_cast<std::uint32_t>(column));
                auto& cell = primary_map_cell(c, id);
                cell.is_obstacle = 0; //Also cleared when code33 preserves cell contents.
                if (command != 33) {
                    //Parser-produced grid codes0..33 use the native dispatcher's
                    //ordinary type-assignment branch. Its success value is ignored.
                    (void)apply_definition_grid_cell_type(c, &cell.base, 0);
                    (void)apply_definition_grid_cell_type(c, &cell.base, command);
                    refresh_world_cell_render_state(c, &cell.base);
                    set_world_cell_parent_id_unchecked(&cell.base, 0);
                    const auto absolute_x = x + static_cast<std::uint32_t>(column);
                    const auto absolute_y = y + static_cast<std::uint32_t>(row);
                    if (!absolute_x || !absolute_y || absolute_x == c.weather.map_heightfield_width || absolute_y == c.weather.map_heightfield_height) {
                        (void)apply_definition_grid_cell_type(c, &cell.base, 5);
                    }
                }
                //Keep the captured cell through all callbacks. Only the next visit
                //resolves the current primary World and the current grid pointer.
                grid = &require_grid(definition.after_shape);
                ++column;
            }
            grid = &require_grid(definition.after_shape);
            ++row;
        }
    }
    const auto replacement_id = static_cast<std::uint16_t>(definition.replace_with_id);
    if (replacement_id) {
        create_replacement(c, require_definition(c, replacement_id));
    } else if (definition.delete_after_use != 1) {
        return;
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    retire_world_used_thing(c, record);
}
} //namespace simcoaster
