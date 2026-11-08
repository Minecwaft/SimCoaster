#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_save.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::size_t cell_index(CellId id) {
    if (!id || id > map_cell_count) {
        throw std::out_of_range("hoarding cell ID");
    }
    return static_cast<std::size_t>(id - 1);
}

World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null hoarding World");
}

struct ResetHoardingMode {
    WorldRuntime& context;

    ~ResetHoardingMode() {
        context.construction.world_hoarding_mode = 0;
    }
};
} //namespace

TrackCell* resolve_track_cell_at_local_offset(WorldRuntime& c, const CellBase* origin, CellLocalOffset offset, std::int32_t distance) {
    if (!origin) {
        throw std::invalid_argument("null track origin");
    }
    cell_index(origin->index);
    const auto scaled = scale_world_cell_local_offset_pair(offset, distance);
    const auto original = static_cast<std::uint32_t>(origin->index) - 1u;
    const auto x = numeric::word((original & 127u) + numeric::bits(scaled.x));
    const auto y = numeric::word((original >> 7) + numeric::bits(scaled.y));
    if (!is_world_map_coordinate_in_bounds(x, y)) {
        return nullptr;
    }
    //Native0x639120 applies the scale to BOTH the bounds candidate and returned
    //track ID. The separate map-cell resolver has an unscaled-index quirk.
    const auto target = static_cast<CellId>(static_cast<std::uint32_t>(origin->index) + numeric::bits(scaled.x) + (numeric::bits(scaled.y) << 7));
    return &require_world(c.simulation.current).track_cells.at(cell_index(target));
}

TrackCell* resolve_track_cell_in_direction(WorldRuntime& c, const CellBase* origin, std::uint8_t direction, std::int32_t distance) {
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
    } return resolve_track_cell_at_local_offset(c, origin, offset, distance);
}

void set_hoarding_chain_nomodify(WorldRuntime& c, std::uint8_t enabled) {
    auto x = c.construction.hoarding_chain_start_x, y = c.construction.hoarding_chain_start_y;
    std::uint8_t direction = 4;
    c.construction.hoarding_direction = 4;
    //Native0x676530 has no cycle bound. Four cardinal traversal states per cell
    //bound malformed routes without allocating a visited table on every call.
    for (std::size_t visited = 0; visited < map_cell_count * 4; ++visited) {
        const auto id = static_cast<CellId>((numeric::bits(y) << 7) + numeric::bits(x) + 1u);
        auto& cell = require_world(c.simulation.reference).track_cells.at(cell_index(id)).base;
        if (enabled) {
            assign_world_cell_flags(&cell, static_cast<std::uint16_t>(cell.common.flags | 32u));
        } else {
            clear_world_cell_flags(&cell, 32u);
        }
        if (cell.common.type == 25 && get_world_cell_corner_neighbor_mask(c, &cell)) {
            direction = static_cast<std::uint8_t>(opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &cell));
        }
        const auto* next = resolve_track_cell_in_direction(c, &cell, direction, 2);
        if ((next && next->base.common.type != 25) || (count_world_cell_neighbor_bits(c, &cell) == 1 && visited)) {
            return;
        }
        //The BASE-World neighbor pointer is only a stop query. The next actual
        //visit is computed from local coordinates in PRIMARY-World storage.
        match (direction) {
            case 1  => y = numeric::word(numeric::bits(y) - 2u);
            case 4  => x = numeric::word(numeric::bits(x) + 2u);
            case 16 => y = numeric::word(numeric::bits(y) + 2u);
            case 64 => x = numeric::word(numeric::bits(x) - 2u);
            case _  => {
            }
        }
    }
    throw std::length_error("cyclic or stalled hoarding route");
}

bool apply_hoardings_map_cells_to_world(WorldRuntime& c, Stream& stream) {
    //Native0x678B00 consumes one raw BYTE per cell, X outer/Y inner. Reads and
    //updates interleave; an eventual short read preserves earlier cell changes.
    for (std::uint32_t x = 0; x < 128; ++x) {
        for (std::uint32_t y = 0; y < 128; ++y) {
            std::byte value;
            if (stream.read({ &value, 1 }) != 1) {
                return false;
            }
            if (value != std::byte {} || !x || x == c.weather.map_heightfield_width - 1u || !y || y == c.weather.map_heightfield_height - 1u) {
                continue;
            }
            const auto id = static_cast<CellId>((y << 7) + x + 1);
            clear_world_cell_flags_mask(&require_world(c.simulation.reference).map_cells.at(cell_index(id)).base, 64u);
            set_hoarding_chain_nomodify(c, 1);
            auto* track = resolve_track_cell_for_grid_entry(c, &require_world(c.simulation.reference).map_cells.at(cell_index(id)).base);
            if (world_cell_is_track_child(&track->base)) {
                track = &require_world(c.simulation.reference).track_cells.at(cell_index(track->base.common.parent_id));
            }
            c.construction.world_hoarding_mode = 1;
            ResetHoardingMode reset_mode { c };
            if (track->base.common.type == 25) {
                clear_world_cell_flags(&track->base, 32u);
                //The native type21 test here cannot succeed after its exact type25
                //predicate and pure flag clear; the reachable path is removal(0,0,0).
                remove_track_cell_contents(c, &track->base, 0, 0, 0);
                track->base.common.overlap_counter = 0;
                (void)apply_hoarding_track_cell_command(c, &track->base, 0);
                (void)apply_hoarding_track_cell_command(c, &track->base, 129);
            }
            clear_world_cell_flags(&track->base, 64u);
            update_world_cell_mesh(c, &track->base);
        }
    }
    return true;
}

bool apply_hoardings_map_cells_to_world(WorldRuntime& c, std::string_view path) {
    auto stream = c.ports.assets.open_asset_stream(c, path);
    //Natural ownership closes failed/throwing reads too. The native helper
    //leaked its opened stream on short reads and only closed the success path.
    return stream && apply_hoardings_map_cells_to_world(c, *stream);
}
} //namespace simcoaster
