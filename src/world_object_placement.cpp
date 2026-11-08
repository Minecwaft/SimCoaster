#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
ResearchCatalogDefinition& require_definition(AttractionWorld& c, std::uint16_t id) {
    auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition || !definition->balance) {
        throw std::out_of_range("missing placement definition");
    }
    return *definition;
}

std::uint8_t rotate_selector(std::uint8_t selector, std::int32_t steps) {
    //The x86 shift count is masked to five bits. The original wraps once only
    //when the signed shifted result is greater than128.
    auto result = static_cast<std::uint32_t>(selector) << (static_cast<std::uint32_t>(steps) & 31u);
    if (std::bit_cast<std::int32_t>(result) > 128) {
        result >>= 8;
    }
    return static_cast<std::uint8_t>(result);
}

CellBase& neighbor_in_primary_world(AttractionWorld& c, CellId id, std::uint8_t direction) {
    if (!c.simulation.reference || !id || id > map_cell_count) {
        throw std::out_of_range("placement endpoint cell");
    }
    auto& primary = *c.simulation.reference;
    const auto& source = primary.map_cells.at(static_cast<std::size_t>(id) - 1).base;
    //The neighbor helper resolves through BASE World; the caller then uses the
    //resulting cell ID to address the separately bound primary World.
    auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &source, direction);
    if (!neighbor || !neighbor->base.index || neighbor->base.index > map_cell_count) {
        throw std::out_of_range("placement endpoint neighbor");
    }
    return primary.map_cells.at(static_cast<std::size_t>(neighbor->base.index) - 1).base;
}
} //namespace

void apply_set_ride_action(AttractionWorld& c, const std::uint16_t* definition_id, std::uint32_t secondary) {
    if (!definition_id) {
        throw std::invalid_argument("null ride action definition");
    }
    if (c.construction.coaster_catalog) {
        c.construction.coaster_catalog->observe_set_ride_action(c);
    }
    if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (!c.construction.action_record_stream) {
            throw std::invalid_argument("unbound action recording stream");
        }
        auto* stream = c.construction.action_record_stream;
        const std::array event { std::byte { 5 } };
        const auto word = save::detail::encode(*definition_id);
        const std::array tail { static_cast<std::byte>(static_cast<std::uint8_t>(secondary)) };
        //The native stream performs three separate writes and ignores their results.
        (void)stream->write(event);
        (void)stream->write(word);
        (void)stream->write(tail);
    }
    const auto& definition = require_definition(c, *definition_id);
    const auto number = static_cast<std::uint32_t>(definition.balance->Info.DownloadedRideNumber);
    if (number) {
        const auto bit = number - 1u;
        if (bit >= c.construction.save_object_definition_mask.size() * 8u) {
            throw std::out_of_range("downloaded ride number");
        }
        c.construction.save_object_definition_mask[bit / 8u] |= static_cast<std::uint8_t>(1u << (bit % 8u));
    }
    c.construction.previous_object_definition_id = c.construction.selected_object_definition_id;
    c.construction.selected_track_type = secondary;
    c.construction.selected_object_definition_id = *definition_id;
}

void select_object_definition_and_placement_grid(AttractionWorld& c, std::uint16_t id, DefinitionGrid* override_grid) {
    c.construction.placement_object_definition_id = id;
    c.construction.selected_placement_definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!(c.construction.catalog_mode_initialized & 1u)) {
        c.construction.catalog_mode_initialized |= 1u;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3 && !c.construction.coaster_source_is_plain && !c.construction.coaster_source_grid_bypass) {
        c.construction.active_object_placement_grid = &c.construction.coaster_source_picker_grid;
        return;
    }
    if (c.mode.game != GameMode::code_3 && c.construction.prebuilt_grid_override_valid && !c.construction.prebuilt_grid_override_consumed) {
        c.construction.active_object_placement_grid = &c.construction.prebuilt_coaster_placement_grid;
        return;
    }
    if (override_grid) {
        c.construction.active_object_placement_grid = override_grid;
        return;
    }
    if (!c.construction.selected_placement_definition || !c.construction.selected_placement_definition->balance) {
        throw std::out_of_range("missing placement Shape");
    }
    c.construction.active_object_placement_grid = c.construction.selected_placement_definition->balance->Info.Shape.get();
}

void update_park_object_entry_exit_neighbor_flags(AttractionWorld& c, ParkObjectRuntimeView& object, bool clear) {
    const auto steps = std::bit_cast<std::int32_t>(object.angle) / 90;
    const auto entry = rotate_selector(require_definition(c, object.definition_id).cached_entry_direction, steps);
    auto& entry_cell = neighbor_in_primary_world(c, object.entry_cell, opposite_world_cell_direction(entry));
    if (clear) {
        (void)clear_world_cell_flags_mask(&entry_cell, 64);
    } else {
        (void)assign_world_cell_flags(&entry_cell, 64);
    }
    const auto exit = rotate_selector(require_definition(c, object.definition_id).cached_exit_direction, steps);
    auto& exit_cell = neighbor_in_primary_world(c, object.exit_cell, exit);
    if (clear) {
        (void)clear_world_cell_flags_mask(&exit_cell, 64);
    } else {
        (void)assign_world_cell_flags(&exit_cell, 64);
    }
}

void world_operations::builtin::apply_set_ride_action(WorldRuntime& c, const std::uint16_t* id, std::uint32_t secondary) {
    simcoaster::apply_set_ride_action(c, id, secondary);
}

void world_operations::builtin::select_object_definition_and_placement_grid(WorldRuntime& c, std::uint16_t id) {
    simcoaster::select_object_definition_and_placement_grid(c, id);
}

void world_operations::builtin::update_park_object_entry_exit_neighbor_flags(WorldRuntime& c, ParkObjectRuntimeView& object, bool clear) {
    simcoaster::update_park_object_entry_exit_neighbor_flags(c, object, clear);
}
} //namespace simcoaster
