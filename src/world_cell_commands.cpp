#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_queue.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::size_t cell_index(CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("world cell ID");
    }
    return static_cast<std::size_t>(id - 1);
}

const CellBase& require_cell(const CellBase* cell) {
    return require_bound<std::invalid_argument>(cell, "null world cell");
}

World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null world cell storage");
}

BankRecord& primary_bank(WorldRuntime& c) {
    const auto id = require_world(c.simulation.reference).saved.bank_account;
    auto* payload = c.entities.at(id).payload;
    if (!payload) {
        throw std::invalid_argument("null cell-change bank");
    }
    return *reinterpret_cast<BankRecord*>(payload);
}

MapCell& primary_map_cell(WorldRuntime& c, CellId id) {
    return require_world(c.simulation.reference).map_cells.at(cell_index(id));
}

std::uint8_t& effective_neighbors(const WorldRuntime& c, CellBase& cell) {
    return c.construction.world_hoarding_mode && cell.record_kind == CellStorageKind::track ? cell.common.hoarding_neighbors : cell.common.neighbors;
}
} //namespace

bool is_world_map_coordinate_in_bounds(std::int32_t x, std::int32_t y) {
    return x >= 0 && y >= 0 && x < 128 && y < 128;
}

CellLocalOffset scale_world_cell_local_offset_pair(CellLocalOffset offset, std::int32_t scale) {
    return { numeric::word(numeric::bits(offset.x) * numeric::bits(scale)), numeric::word(numeric::bits(offset.y) * numeric::bits(scale)) };
}

MapCell* resolve_world_cell_by_scaled_local_offset(WorldRuntime& c, const CellBase* base, CellLocalOffset offset, std::int32_t scale) {
    const auto id = require_cell(base).index;
    cell_index(id);
    const auto scaled = scale_world_cell_local_offset_pair(offset, scale);
    const auto original = static_cast<std::uint32_t>(id) - 1u;
    const auto x = numeric::word((original & 127u) + numeric::bits(scaled.x));
    const auto y = numeric::word((original >> 7) + numeric::bits(scaled.y));
    if (!is_world_map_coordinate_in_bounds(x, y)) {
        return nullptr;
    }
    //The native helper bounds-checks the scaled offset, but computes the final
    //ID from the unscaled original offset. Valid flat-array aliases survive.
    const auto target = static_cast<CellId>(static_cast<std::uint32_t>(id) + numeric::bits(offset.x) + (numeric::bits(offset.y) << 7));
    const auto index = cell_index(target);
    return &require_world(c.simulation.current).map_cells.at(index);
}

MapCell* resolve_world_cell_from_selector_flag_byte(WorldRuntime& c, const CellBase* cell, std::uint8_t direction) {
    CellLocalOffset offset;
    match (direction) {
        case 1   => offset = { 0, -1 };
        case 2   => offset = { 1, -1 };
        case 4   => offset = { 1, 0 };
        case 8   => offset = { 1, 1 };
        case 16  => offset = { 0, 1 };
        case 32  => offset = { -1, 1 };
        case 64  => offset = { -1, 0 };
        case 128 => offset = { -1, -1 };
        case _   => return nullptr;
    } return resolve_world_cell_by_scaled_local_offset(c, cell, offset, 1);
}

MapCell* get_world_map_cell_record_by_id(WorldRuntime& c, CellId id) {
    const auto index = cell_index(id);
    if (!c.simulation.map_cell_records) {
        throw std::invalid_argument("null global map cell array");
    }
    return &c.simulation.map_cell_records[index];
}

TrackCell* resolve_track_cell_for_grid_entry(WorldRuntime& c, const CellBase* cell) {
    const auto index = cell_index(require_cell(cell).index);
    if (!c.simulation.track_cell_records) {
        throw std::invalid_argument("null global track cell array");
    }
    return &c.simulation.track_cell_records[index];
}

bool world_track_reference_matches_type_set(WorldRuntime& c, CellId id) {
    const auto index = cell_index(id);
    if (!c.simulation.track_cell_records) {
        throw std::invalid_argument("null global track cell array");
    }
    const auto* cell = &c.simulation.track_cell_records[index];
    if (cell->base.common.type == 12 || cell->base.common.type == 17) {
        const auto parent = cell->base.common.parent_id;
        if (!parent) {
            return false;
        }
        cell = &require_world(c.simulation.reference).track_cells.at(cell_index(parent));
    }
    const auto type = cell->base.common.type;
    return type == 11 || type == 13 || type == 16 || type == 18 || type == 25;
}

bool world_cell_blocks_person_weather_appearance(const TrackCell& cell) {
    const auto type = cell.base.common.type;
    return type == 11 || type == 12 || type == 16 || type == 17;
}

ThingId find_first_park_object_id_in_map_cell(WorldRuntime& c, const MapCell* cell) {
    if (!cell) {
        throw std::invalid_argument("null park-object map cell");
    }
    auto id = cell->who;
    std::size_t traversed = 0;
    while (id) {
        if (traversed++ >= c.entities.size()) {
            throw std::length_error("cyclic map-cell object list");
        }
        const auto* payload = c.entities.at(id).payload;
        if (!payload) {
            throw std::invalid_argument("null map-cell object");
        }
        if (payload->kind == ThingKind::park_object) {
            return id;
        }
        id = reinterpret_cast<const MapObjectBase*>(payload)->map_child;
    }
    return 0;
}

std::uint8_t opposite_world_cell_direction(std::uint8_t direction) {
    return static_cast<std::uint8_t>(direction >= 16 ? direction >> 4 : direction << 4);
}

std::uint8_t advance_world_cell_direction(std::uint8_t direction, std::uint8_t multiplier) {
    unsigned bit_index = 0;
    while (direction > 1) {
        direction = static_cast<std::uint8_t>(direction >> 1);
        ++bit_index;
    }
    const auto advanced = static_cast<std::uint32_t>(multiplier) << bit_index;
    return static_cast<std::uint8_t>(advanced > 128 ? advanced >> 8 : advanced);
}

std::uint8_t get_world_cell_neighbor_mask(const WorldRuntime& c, const CellBase* cell) {
    const auto& value = require_cell(cell);
    return c.construction.world_hoarding_mode && value.record_kind == CellStorageKind::track ? value.common.hoarding_neighbors : value.common.neighbors;
}

void clear_world_cell_neighbor_mask_bits(const WorldRuntime& c, CellBase* cell, std::uint8_t mask) {
    require_cell(cell);
    auto& neighbors = effective_neighbors(c, *cell);
    neighbors = static_cast<std::uint8_t>(neighbors & static_cast<std::uint8_t>(~mask));
}

std::uint8_t or_world_cell_neighbor_mask_bits(const WorldRuntime& c, CellBase* cell, std::uint8_t mask) {
    require_cell(cell);
    auto& neighbors = effective_neighbors(c, *cell);
    neighbors = static_cast<std::uint8_t>(neighbors | mask);
    //The track/hoarding branch returns the input, the ordinary branch the ORed byte.
    return c.construction.world_hoarding_mode && cell->record_kind == CellStorageKind::track ? mask : neighbors;
}

std::int32_t count_world_cell_neighbor_bits(const WorldRuntime& c, const CellBase* cell) {
    return std::popcount(static_cast<unsigned>(get_world_cell_neighbor_mask(c, cell)));
}

std::int32_t count_world_cell_cardinal_neighbor_bits(const WorldRuntime& c, const CellBase* cell) {
    return std::popcount(static_cast<unsigned>(get_world_cell_neighbor_mask(c, cell) & 85));
}

std::uint8_t get_world_cell_corner_neighbor_mask(const WorldRuntime& c, const CellBase* cell) {
    const auto mask = get_world_cell_neighbor_mask(c, cell);
    return count_world_cell_cardinal_neighbor_bits(c, cell) == 2 && (((mask & 1) && (mask & 4)) || ((mask & 1) && (mask & 64)) || ((mask & 16) && (mask & 4)) || ((mask & 16) && (mask & 64))) ? mask : 0;
}

std::uint8_t get_world_cell_branch_neighbor_mask(const WorldRuntime& c, const CellBase* cell) {
    const auto mask = get_world_cell_neighbor_mask(c, cell);
    return count_world_cell_cardinal_neighbor_bits(c, cell) > 2 ? mask : 0;
}

void set_world_cell_direction(CellBase* cell, std::uint8_t direction) {
    require_cell(cell);
    cell->common.direction = direction;
}

std::uint8_t get_world_cell_direction(const CellBase* cell) {
    return require_cell(cell).common.direction;
}

CellId set_world_cell_parent_id_unchecked(CellBase* cell, CellId id) {
    require_cell(cell);
    cell->common.parent_id = id;
    return id;
}

std::uint32_t clear_world_cell_flags_mask(CellBase* cell, std::uint32_t mask) {
    require_cell(cell);
    cell->common.flags = static_cast<std::uint16_t>(cell->common.flags & static_cast<std::uint16_t>(~mask));
    return ~mask;
}

void clear_world_cell_flags(CellBase* cell, std::uint16_t mask) {
    (void)clear_world_cell_flags_mask(cell, mask);
}

std::uint16_t assign_world_cell_flags(CellBase* cell, std::uint16_t flags) {
    require_cell(cell);
    cell->common.flags = flags;
    return flags;
}

bool world_cell_has_track_segment_flags(const CellBase* cell) {
    return (require_cell(cell).common.flags & 15) != 0;
}

bool world_cell_is_track_child(const CellBase* cell) {
    const auto type = require_cell(cell).common.type;
    return type == 12 || type == 17;
}

bool world_cell_is_track_root(const CellBase* cell) {
    const auto type = require_cell(cell).common.type;
    return type == 11 || type == 13 || type == 16 || type == 18 || type == 25;
}

std::uint32_t next_world_grid_change_serial(WorldRuntime& c) {
    return ++c.construction.grid_change_serial_counter;
}

void store_world_grid_change_serial_for_cell_id(World* world, std::uint32_t serial, CellId id) {
    if (!id) {
        return;
    }
    const auto value = static_cast<std::uint32_t>(id) - 1;
    require_world(world).grid_change_serials[value >> 11][(value >> 4) & 7] = serial;
}

bool can_change_world_cell_type(const WorldRuntime& c, const CellBase* cell, std::int32_t requested) {
    if (!requested) {
        return true;
    }
    const auto current = require_cell(cell).common.type;
    if (requested == 4 && current == 4) {
        return false;
    }
    if (requested == 21 && current == 21) {
        return true;
    }
    if (requested == 1 && current == 3) {
        return true;
    }
    if (requested == current || !current) {
        return true;
    }
    return requested == 4 ? current == 1 : requested == 3 && current == 1 && !c.construction.line_endpoint_dispatch;
}

bool try_change_world_cell_type(WorldRuntime& c, CellBase* cell, std::int32_t requested) {
    require_cell(cell);
    if (!can_change_world_cell_type(c, cell, requested)) {
        return false;
    }
    if (requested && cell->common.type == requested) {
        const auto count = static_cast<std::uint16_t>(static_cast<std::uint16_t>(cell->common.overlap_counter) + 1u);
        cell->common.overlap_counter = std::bit_cast<std::int16_t>(count);
        return true;
    }
    const bool queue_to_path = requested == 1 && cell->common.type == 3;
    std::int32_t cost = 0;
    if (requested == 1) {
        if (!c.construction.cell_costs_suppressed) {
            cost = c.construction.path_cell_base_cost;
        }
    } else if (requested == 3) {
        if (!c.construction.cell_costs_suppressed) {
            cost = c.construction.queue_cell_base_cost;
        }
        if (cell->common.type == 1) {
            c.construction.force_cell_removal = 1;
            remove_map_cell_contents(c, cell, 0, 0);
            c.construction.force_cell_removal = 0;
        }
    }
    bool affordable = true;
    if (!c.construction.action_recording_disabled) {
        affordable = numeric::word(numeric::bits(primary_bank(c).balance) - numeric::bits(cost)) >= 0;
    }
    if ((requested == 1 || requested == 3) && !affordable) {
        return false;
    }
    cell->common.type = requested;
    const auto serial = next_world_grid_change_serial(c);
    store_world_grid_change_serial_for_cell_id(c.simulation.reference, serial, cell->index);
    if (queue_to_path) {
        //A zero native parent probes storage before the map array. Reject that
        //malformed linkage after retaining the preceding type and serial stores.
        auto& parent = primary_map_cell(c, cell->common.parent_id);
        if (parent.base.common.type == 4 || parent.base.common.type == 9 || parent.base.common.type == 10) {
            const auto id = find_first_park_object_id_in_map_cell(c, &parent);
            if (id) {
                rebuild_park_object_queue_layout(c, c.entities.at_payload<ParkObjectRuntimeView>(id));
            }
        }
    }
    if (requested == 1) {
        if (c.construction.cell_costs_suppressed) {
            return true;
        }
    } else if (requested != 3 || cell->common.type == 1 || c.construction.cell_costs_suppressed) {
        return true;
    }
    charge_world_bank_account_expense(c, &primary_bank(c), cost);
    return true;
}
} //namespace simcoaster
