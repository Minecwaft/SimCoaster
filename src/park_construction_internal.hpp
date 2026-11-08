#pragma once
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_surface.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster::construction_detail {
void animate_track_endpoint(ConstructionWorld&, ParkObjectRuntimeView&, bool opposite, bool valid);
void select_parent_origin(ConstructionWorld&, ParkConstructionState&, ParkObjectRuntimeView&);
void adjust_track_endpoint(ConstructionWorld&, WorldCellCoordinates&);

inline World& world(ConstructionWorld& c) {
    return require_bound(c.simulation.current, "construction World is not bound");
}

inline MapCell& map(ConstructionWorld& c, CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("construction cell ID");
    }
    return world(c).map_cells.at(static_cast<std::size_t>(id) - 1);
}

inline TrackCell& track(ConstructionWorld& c, CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("construction track ID");
    }
    return world(c).track_cells.at(static_cast<std::size_t>(id) - 1);
}

inline CellId cell_id(WorldCellCoordinates p) {
    return static_cast<CellId>(static_cast<std::uint32_t>(p.x) + static_cast<std::uint32_t>(p.y) * 128u + 1);
}

inline WorldCellCoordinates coordinate(CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("construction coordinate ID");
    }
    const auto n = static_cast<std::uint32_t>(id) - 1;
    return { static_cast<int>(n & 127u), static_cast<int>(n >> 7) };
}

inline bool bounds(WorldCellCoordinates p) {
    return is_world_map_coordinate_in_bounds(p.x, p.y);
}

inline WorldCellCoordinates snap_land_boundary_corner(const ConstructionQuery& c, WorldCellCoordinates p) {
    const auto snap = [](int value, int origin) {
        auto delta = value - origin;
        if (delta < 0 && delta % 2) {
            --delta;
        }
        return origin + 2 * (delta / 2);
    };
    return { snap(std::clamp(p.x, 1, 126), c.construction.hoarding_chain_start_x), snap(std::clamp(p.y, 1, 125), c.construction.hoarding_chain_start_y) };
}

inline bool footprint(int type) {
    return type == 4 || type == 9 || type == 10;
}

inline BankRecord& bank(VisitorWorld& c) {
    auto* p = c.entities.at(world(c).saved.bank_account).payload;
    if (!p || p->kind != ThingKind::bank_account) {
        throw std::logic_error("construction bank is missing");
    }
    return *reinterpret_cast<BankRecord*>(p);
}

inline ResearchCatalogDefinition& definition(ConstructionWorld& c, std::uint16_t id) {
    auto* d = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!d || !d->balance) {
        throw std::out_of_range("construction definition is missing");
    }
    return *d;
}

inline ParkObjectRuntimeView* object(ConstructionWorld& c, ThingId id) {
    if (!id) {
        return nullptr;
    }
    auto* p = c.entities.at(id).payload;
    return p && p->kind == ThingKind::park_object ? reinterpret_cast<ParkObjectRuntimeView*>(p) : nullptr;
}

inline ParkObjectRuntimeView* object_in(ConstructionWorld& c, const CellBase& cell) {
    const auto owner = footprint(cell.common.type) || map(c, cell.index).is_obstacle ? cell.common.parent_id : cell.index;
    return owner ? object(c, find_first_park_object_id_in_map_cell(c, &map(c, owner))) : nullptr;
}

inline ParkObjectRuntimeView* gate_or_object_in(ConstructionWorld& c, const CellBase& cell) {
    const auto p = coordinate(cell.index);
    const auto& f = c.visitors.environment.FixedItemInfo;
    if (f.EntranceAPosY && f.EntranceAPosX && p.x >= f.EntranceAPosX - 1 && p.x <= f.EntranceBPosX + 1 && p.y == f.EntranceAPosY) {
        return object(c, world(c).saved.park_gates);
    }
    return object_in(c, cell);
}

inline TrackCell& root_track(ConstructionWorld& c, const CellBase& cell) {
    auto* row = resolve_track_cell_for_grid_entry(c, &cell);
    if (world_cell_is_track_child(&row->base) && row->base.common.parent_id) {
        row = &track(c, row->base.common.parent_id);
    }
    return *row;
}

inline bool rejected(CellInteractionResult result) {
    return result == CellInteractionResult::rejected;
}

inline std::int32_t multiply_integer(std::int32_t a, std::int32_t b) {
    return fixed::word(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

inline bool track_variant(ConstructionWorld& c, const CellBase& row) {
    const auto* selected = &row;
    if (world_cell_is_track_child(selected)) {
        selected = &track(c, selected->common.parent_id).base;
    }
    return selected->common.type == 13 || selected->common.type == 18;
}

inline bool map_corner(ConstructionWorld& c, const CellBase& cell) {
    const auto n = get_world_cell_neighbor_mask(c, &cell);
    return count_world_cell_cardinal_neighbor_bits(c, &cell) == 2 && (((n & 1u) && (n & 4u)) || ((n & 1u) && (n & 64u)) || ((n & 16u) && (n & 4u)) || ((n & 16u) && (n & 64u)));
}

inline WorldCellCoordinates stepped(WorldCellCoordinates p, std::uint8_t direction, int scale = 1) {
    match (direction) {
        case 1  => p.y = fixed::subtract(p.y, scale);
        case 4  => p.x = fixed::add(p.x, scale);
        case 16 => p.y = fixed::add(p.y, scale);
        case 64 => p.x = fixed::subtract(p.x, scale);
        case _  => throw std::invalid_argument("construction direction is not cardinal");
    } return p;
}

inline std::uint8_t object_anchor(WorldCellCoordinates& p, int rotation, bool opposite = false) {
    std::uint8_t direction;
    match (rotation) {
        case 0 => {
            p.x += opposite ? -2 : 4;
            ++p.y;
            direction = 4;
        }
        case 90 => {
            ++p.x;
            p.y += opposite ? 1 : -5;
            direction = 1;
        }
        case 180 => {
            p.x += opposite ? 1 : -5;
            p.y -= 2;
            direction = 64;
        }
        case 270 => {
            p.x -= 2;
            p.y += opposite ? -2 : 4;
            direction = 16;
        }
        case _ => throw std::invalid_argument("construction object angle is not cardinal");
    } return opposite
        ? opposite_world_cell_direction(direction)
        : direction;
}

template <class T>
struct RestoreValue {
    T& value;
    T saved;

    explicit RestoreValue(T& v) : value(v), saved(v) {}

    ~RestoreValue() {
        value = saved;
    }
};
} //namespace simcoaster::construction_detail
