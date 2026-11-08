#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
struct TileChoice {
    std::uint32_t packed, orientation;
    std::uint8_t neighbors;
};

//Exact ordered rows from the native static tables. Duplicate masks retain
//their order; an exact match takes precedence over every subset match.
constexpr std::array<TileChoice, 11> queue_tiles { { { 131074, 0, 17 }, { 131074, 90, 68 }, { 131073, 0, 16 }, { 131073, 90, 64 }, { 131073, 180, 1 }, { 131073, 270, 4 }, { 131075, 0, 20 }, { 131075, 90, 80 }, { 131075, 180, 65 }, { 131075, 270, 5 }, { 131072, 0, 0 } } };
constexpr std::array<TileChoice, 16> gokart_tiles { { { 196610, 0, 17 }, { 196610, 90, 68 }, { 196609, 0, 16 }, { 196609, 90, 64 }, { 196609, 180, 1 }, { 196609, 270, 4 }, { 196611, 0, 20 }, { 196611, 90, 80 }, { 196611, 180, 65 }, { 196611, 270, 5 }, { 196613, 0, 21 }, { 196614, 0, 69 }, { 196615, 0, 81 }, { 196616, 0, 84 }, { 196612, 0, 85 }, { 196608, 0, 0 } } };
constexpr std::array<TileChoice, 16> water_tiles { { { 262146, 0, 17 }, { 262146, 90, 68 }, { 262145, 0, 16 }, { 262145, 90, 64 }, { 262145, 180, 1 }, { 262145, 270, 4 }, { 262147, 0, 20 }, { 262147, 90, 80 }, { 262147, 180, 65 }, { 262147, 270, 5 }, { 262149, 0, 21 }, { 262150, 0, 69 }, { 262151, 0, 81 }, { 262152, 0, 84 }, { 262148, 0, 85 }, { 262144, 0, 0 } } };
constexpr std::array<TileChoice, 49> ordinary_tiles { { { 65546, 0, 31 },    { 65546, 90, 124 }, { 65546, 180, 241 }, { 65546, 270, 199 }, { 65543, 0, 29 },    { 65543, 90, 116 },  { 65543, 180, 209 }, { 65543, 270, 71 }, { 65551, 0, 113 },   { 65551, 90, 197 }, { 65551, 180, 23 }, { 65551, 270, 92 }, { 65540, 0, 21 },    { 65540, 90, 84 },   { 65540, 180, 81 }, { 65540, 270, 69 }, { 65538, 0, 17 },
                                                        { 65538, 90, 68 },   { 65549, 0, 255 },  { 65547, 0, 127 },   { 65547, 90, 253 },  { 65547, 180, 247 }, { 65547, 270, 223 }, { 65542, 0, 125 },   { 65542, 90, 245 }, { 65542, 180, 215 }, { 65542, 270, 95 }, { 65548, 0, 119 },  { 65548, 90, 221 }, { 65548, 180, 119 }, { 65548, 270, 221 }, { 65544, 0, 213 },  { 65544, 90, 87 },  { 65544, 180, 93 },
                                                        { 65544, 270, 117 }, { 65541, 0, 85 },   { 65545, 0, 28 },    { 65545, 90, 112 },  { 65545, 180, 193 }, { 65545, 270, 7 },   { 65539, 0, 20 },    { 65539, 90, 80 },  { 65539, 180, 65 },  { 65539, 270, 5 },  { 65537, 0, 16 },   { 65537, 90, 64 },  { 65537, 180, 1 },   { 65537, 270, 4 },   { 65536, 0, 0 } } };
static_assert(queue_tiles.back().neighbors == 0 && ordinary_tiles.back().neighbors == 0);

template <std::size_t N>
CellRenderData select_tile(const std::array<TileChoice, N>& choices, std::uint8_t mask, std::uint32_t orientation) {
    auto selected = std::ranges::find(choices, mask, &TileChoice::neighbors);
    if (selected == choices.end()) {
        selected = std::ranges::find_if(choices, [&](auto row) {
            return (mask & row.neighbors) == row.neighbors;
        });
    }
    if (selected == choices.end()) {
        throw std::logic_error("missing cell tile fallback row");
    }
    return { selected->packed >> 16, selected->packed & 0xFFFFu, selected->orientation + orientation };
}

const CellBase& require_cell(const CellBase* cell) {
    return require_bound<std::invalid_argument>(cell, "null cell tile receiver");
}

const CellBase& primary_track_parent(WorldRuntime& c, CellId id) {
    if (!c.simulation.reference) {
        throw std::invalid_argument("null tile primary World");
    }
    if (!id) {
        throw std::out_of_range("zero tile track parent ID");
    }
    return c.simulation.reference->track_cells.at(static_cast<std::size_t>(id) - 1).base;
}

bool has_opposite_cardinals(std::uint8_t mask) {
    return ((mask & 1) && (mask & 16)) || ((mask & 4) && (mask & 64));
}

bool has_corner_cardinals(std::uint8_t mask) {
    return ((mask & 1) && ((mask & 4) || (mask & 64))) || ((mask & 16) && ((mask & 4) || (mask & 64)));
}

bool directed_corner(std::uint8_t direction, std::uint8_t mask) {
    return (direction == 64 && mask == 80) || (direction == 16 && mask == 20) || (direction == 1 && mask == 65) || (direction == 4 && mask == 5);
}

constexpr auto track_animation_flags = MeshAnimationTransitionFlags::loop | MeshAnimationTransitionFlags::force | MeshAnimationTransitionFlags::bit_4;

void clear_track_animation(WorldRuntime& c, std::uint32_t slot) {
    world_operations::request_mesh_animation_control_transition(c, slot, MeshAnimationRequest::clear, 0, track_animation_flags, 1.0f, 0);
}

std::uint32_t register_cell_mesh(WorldRuntime& c, const CellMeshTemplate& row, CellMeshPlacement placement, bool refresh) {
    //The refresh API calls this position unused_argument_5. The caller still
    //supplies the template metadata's low byte, exactly as for construction.
    if (refresh) {
        return world_operations::mark_or_find_matching_mesh_instance_for_refresh(c, { 0, row.definition_index, 0x33Au, placement, static_cast<std::int32_t>(row.metadata & 255u), nullptr, nullptr, nullptr });
    }
    return world_operations::create_mesh_instance_and_register_slot(c, { row.definition_index, 0x33Au, placement, row.metadata & 255u, 0, nullptr, 0, 0, -1, 0 });
}

std::uint32_t create_track_mesh(WorldRuntime& c, const CellMeshTemplate& row, std::int32_t x, std::int32_t y, std::uint32_t orientation, bool refresh) {
    const auto rotation = orientation ? 360u - orientation : 0u;
    auto ux = std::bit_cast<std::uint32_t>(x), uy = std::bit_cast<std::uint32_t>(y);
    if (rotation == 180 || rotation == 270) {
        ++ux;
    }
    if (rotation == 90 || rotation == 180) {
        ++uy;
    }
    return register_cell_mesh(c, row, { std::bit_cast<std::int32_t>(rotation), std::bit_cast<std::int32_t>(ux), std::bit_cast<std::int32_t>(uy), &c.construction.cell_mesh_placement_geometry }, refresh);
}
} //namespace

CellRenderData calculate_world_cell_render_data(WorldRuntime& c, const CellBase* cell) {
    const auto& fields = require_cell(cell).common;
    const auto mask = get_world_cell_neighbor_mask(c, cell), direction = get_world_cell_direction(cell);
    const auto type = fields.type;
    const auto bits = std::bit_cast<std::uint32_t>(type), magnitude = type < 0 ? 0u - bits : bits;
    match (magnitude) {
        case 5 or 7 or 4 or 12 or 17 or 9 or 10 => return { 0, 8, 0 };
        case _                                  => {
        }
    } const auto count = count_world_cell_cardinal_neighbor_bits(c, cell);
    const bool straight = count == 2 && has_opposite_cardinals(mask);
    const bool corner = count == 2 && has_corner_cardinals(mask), branch = count > 2 && mask;
    auto selector = mask;
    if (magnitude == 25) {
        const auto coordinate = static_cast<std::uint32_t>(cell->index) - 1u;
        const auto x = static_cast<std::int32_t>(coordinate & 127u), y = static_cast<std::int32_t>(coordinate >> 7);
        if ((x == c.construction.hoarding_chain_start_x && y == c.construction.hoarding_chain_start_y) || (x == c.construction.hoarding_target_x && y == c.construction.hoarding_target_y)) {
            selector |= opposite_world_cell_direction(selector);
        }
    }
    const bool track = magnitude == 3 || magnitude == 11 || magnitude == 16 || magnitude == 25;
    const auto orientation = track && straight && (direction == 64 || direction == 16) ? 180u : 0u;
    auto result = magnitude == 3 || magnitude == 11 ? select_tile(gokart_tiles, selector, orientation) : magnitude == 16 || magnitude == 25 ? select_tile(water_tiles, selector, orientation) : select_tile(ordinary_tiles, selector, 0);
    auto packed = (result.texture_group << 16) | result.texture_member;
    if (!corner && !branch) {
        if (fields.flags & 1u) {
            packed += 9;
        }
        if (fields.flags & 2u) {
            packed += 12;
        }
        if (fields.flags & 8u) {
            packed += 15;
        }
        if (fields.flags & 4u) {
            packed = (packed & 0xFFFF0000u) + 18;
        }
    }
    if ((type == 11 || type == 16) && corner && directed_corner(direction, mask)) {
        packed += 16;
    }
    if (type == 16 && branch) {
        if (direction == 20) {
            result.orientation_degrees = 90;
        }
        if (direction == 65 || direction == 80) {
            packed += 16;
        }
        if (direction == 80) {
            result.orientation_degrees = 270;
        }
    }
    if (type == 25) {
        if (corner) {
            const bool reversed = (direction == 1 && (mask == 65 || mask == 80)) || (direction == 16 && (mask == 20 || mask == 5)) || (direction == 4 && (mask == 5 || mask == 65)) || (direction == 64 && (mask == 80 || mask == 20));
            if (reversed) {
                packed = (packed & 0xFFFF0000u) | ((packed + 16u) & 0xFFFFu);
            }
            if (fields.flags & 15u) {
                packed = (packed & 0xFFFF0000u) | (reversed ? 27u : 26u);
            }
        }
        if (straight && !(fields.flags & 15u)) {
            //The only native writer uses unsigned PRNG output modulo30; BSS starts
            //at0. Values >=30 fall into a genuine stack-pointer residue path in
            //the original. Reject that broken state instead of leaking a host pointer.
            if (c.construction.hoarding_variant_roll >= 30) {
                throw std::domain_error("invalid hoarding variant roll");
            }
            const auto variant = c.construction.hoarding_variant_roll < 18 ? 0 : c.construction.hoarding_variant_roll < 24 ? 20 : 23;
            if (c.construction.previous_hoarding_variant != variant) {
                packed = (packed & 0xFFFF0000u) | ((packed + static_cast<std::uint32_t>(variant)) & 0xFFFFu);
            }
            c.construction.previous_hoarding_variant = variant;
            if (!c.simulation.reference) {
                throw std::invalid_argument("null hoarding variant primary World");
            }
            c.construction.hoarding_variant_roll = static_cast<std::int32_t>(static_cast<std::uint32_t>(next_world_prng_value(*c.simulation.reference)) % 30u);
        }
        packed = (packed & 0xFFFFu) | 0x50000u;
    }
    result.texture_group = packed >> 16;
    result.texture_member = packed & 0xFFFFu;
    return result;
}

std::uint32_t create_or_refresh_gokart_track_mesh_instance(WorldRuntime& c, std::int32_t x, std::int32_t y, CellRenderData data, bool refresh) {
    return create_track_mesh(c, c.construction.gokart_mesh_templates.at(data.texture_member), x, y, data.orientation_degrees, refresh);
}

std::uint32_t create_or_refresh_hoarding_mesh_instance(WorldRuntime& c, std::int32_t x, std::int32_t y, CellRenderData data, bool refresh) {
    return create_track_mesh(c, c.construction.hoarding_mesh_templates.at(data.texture_member), x, y, data.orientation_degrees, refresh);
}

std::uint32_t create_or_refresh_queue_mesh_instance(WorldRuntime& c, std::int32_t x, std::int32_t y, CellRenderData data, bool refresh) {
    const auto rotation = data.orientation_degrees ? 360u - data.orientation_degrees : 0u;
    return register_cell_mesh(c, c.construction.queue_mesh_templates.at(data.texture_member), { std::bit_cast<std::int32_t>(rotation), x, y, &c.construction.queue_mesh_placement_geometry }, refresh);
}

std::uint32_t create_or_refresh_water_ride_track_mesh_instance(WorldRuntime& c, std::int32_t x, std::int32_t y, CellRenderData data, bool refresh) {
    const auto slot = create_track_mesh(c, c.construction.water_ride_mesh_templates.at(data.texture_member), x, y, data.orientation_degrees, refresh);
    if (slot) {
        clear_track_animation(c, slot);
    }
    return slot;
}

CellRenderData& calculate_world_cell_mesh_render_scratch(WorldRuntime& c, const CellBase* cell) {
    const auto type = require_cell(cell).common.type;
    c.construction.world_cell_mesh_render_scratch = type <= 0 || type == 5 ? CellRenderData { 0, 55, 0 } : calculate_world_cell_render_data(c, cell);
    return c.construction.world_cell_mesh_render_scratch;
}

void update_world_cell_mesh(WorldRuntime& c, CellBase* cell) {
    require_cell(cell);
    auto& fields = cell->common;
    if (c.construction.world_hoarding_mode) {
        if (fields.type == 11 || fields.type == 16 || fields.type == 12 || fields.type == 17) {
            return;
        }
        if (get_world_map_cell_record_by_id(c, cell->index)->base.common.type == 21) {
            return;
        }
    }
    //The original updater builds this tuple inline; share its equivalent helper.
    calculate_world_cell_mesh_render_scratch(c, cell);
    const auto coordinate = static_cast<std::uint32_t>(cell->index) - 1u;
    const auto x = static_cast<std::int32_t>(coordinate & 127u), y = static_cast<std::int32_t>(coordinate >> 7);
    fields.render_data = c.construction.world_cell_mesh_render_scratch;
    const auto group = fields.render_data.texture_group;
    const auto release = [&] {
        world_operations::destroy_mesh(c, fields.mesh_instance);
        fields.mesh_instance = 0;
    };
    match (group) {
        case 3 or 4 => {
            if (fields.mesh_instance) {
                release();
            }
            if (!(fields.flags & 16u) && fields.render_data.texture_member) {
                const auto slot = group == 3 ? create_or_refresh_gokart_track_mesh_instance(c, x, y, fields.render_data, false) : create_or_refresh_water_ride_track_mesh_instance(c, x, y, fields.render_data, false);
                fields.mesh_instance = slot;
                //Both this caller and the water creator request the clear transition.
                if (group == 4 && slot) {
                    clear_track_animation(c, slot);
                }
            }
        }
        case 5 => {
            if (fields.mesh_instance && get_world_map_cell_record_by_id(c, cell->index)->base.common.type != 21) {
                release();
            }
        }
        case _ => {
            if (group == 0 && fields.render_data.texture_member == 55 && fields.mesh_instance) {
                release();
            }
        }
    }
}

CellRenderData calculate_world_cell_tile_data(WorldRuntime& c, const CellBase* cell) {
    const auto mask = get_world_cell_neighbor_mask(c, cell);
    const auto type = require_cell(cell).common.type;
    const auto direction = get_world_cell_direction(cell);
    const auto bits = std::bit_cast<std::uint32_t>(type);
    const auto magnitude = type < 0 ? 0u - bits : bits;
    match (magnitude) {
        case 2 or 7 or 30 or 32 or 4 or 9 or 10 or 21 or 8 => return { 0, 8, 0 };
        case _                                             => {
        }
    } std::uint32_t orientation = 0;
    if (magnitude == 3) {
        const auto queue_mask = get_world_cell_neighbor_mask(c, cell);
        if (count_world_cell_cardinal_neighbor_bits(c, cell) == 2 && has_opposite_cardinals(queue_mask)) {
            orientation = direction == 64 || direction == 16 ? 0u : 180u;
        }
    }
    auto result = magnitude == 3 ? select_tile(queue_tiles, mask, orientation) : select_tile(ordinary_tiles, mask, orientation);
    //Only positive queue type3 applies the directed corner variant. Negative3
    //still uses the queue table and straight-line orientation above.
    if (type == 3) {
        const auto queue_mask = get_world_cell_neighbor_mask(c, cell);
        if (count_world_cell_cardinal_neighbor_bits(c, cell) == 2 && has_corner_cardinals(queue_mask)) {
            const auto corner_direction = get_world_cell_direction(cell);
            const auto corner_mask = get_world_cell_neighbor_mask(c, cell);
            if ((corner_direction == 64 && corner_mask == 80) || (corner_direction == 16 && corner_mask == 20) || (corner_direction == 1 && corner_mask == 65) || (corner_direction == 4 && corner_mask == 5)) {
                ++result.texture_member;
            }
        }
    }
    if (c.construction.tile_variant_toggle && result.texture_group == 1) {
        if (result.texture_member == 2) {
            result.texture_member += 17;
        }
        if (result.texture_member == 10) {
            result.texture_member += 10;
        }
    }
    //Selection uses the old toggle. Every non-special calculation then consumes
    //exactly one shared CRT draw, including queue tiles and fallback masks.
    c.construction.tile_variant_toggle = static_cast<std::uint8_t>(next_crt_random(c) % 2);
    if (result.texture_group == 2) {
        std::uint8_t facing = 1;
        do {
            if (get_world_cell_neighbor_mask(c, cell) & facing) {
                auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, cell, facing);
                if (neighbor && neighbor->base.common.type == 1 && (get_world_cell_neighbor_mask(c, &neighbor->base) & opposite_world_cell_direction(facing))) {
                    //Both native reference helpers resolve through the global track
                    //array. A child's parent is instead read from the primary World.
                    const auto* reference = resolve_track_cell_for_grid_entry(c, cell);
                    const CellBase* segment;
                    if (world_cell_is_track_child(&reference->base)) {
                        segment = &primary_track_parent(c, resolve_track_cell_for_grid_entry(c, cell)->base.common.parent_id);
                    } else {
                        segment = &resolve_track_cell_for_grid_entry(c, cell)->base;
                    }
                    if (!world_cell_has_track_segment_flags(segment)) {
                        result.texture_member += 3;
                    }
                }
            }
            facing = advance_world_cell_direction(facing, 4);
        } while (facing != 1);
    }
    return result;
}

void refresh_world_cell_render_state(WorldRuntime& c, CellBase* cell) {
    const auto type = require_cell(cell).common.type;
    c.construction.world_cell_render_scratch = type <= 0 || type == 5 ? CellRenderData { 0, 55, 0 } : calculate_world_cell_tile_data(c, cell);
    const auto coordinate = static_cast<std::uint32_t>(cell->index) - 1u;
    cell->common.render_data = c.construction.world_cell_render_scratch;
    const auto x = static_cast<std::int32_t>(coordinate & 127u), y = static_cast<std::int32_t>(coordinate >> 7);
    const bool queue_mesh = cell->common.render_data.texture_group == 2;
    const auto old_mesh = cell->common.mesh_instance;
    if (old_mesh) {
        world_operations::destroy_mesh(c, old_mesh);
        //Deliberately follows the callback: a reentrant replacement is cleared.
        cell->common.mesh_instance = 0;
    }
    if (queue_mesh) {
        //Copy current cell data after mesh release, preserving that callback's
        //edits. The branch and the coordinates retain their earlier snapshots.
        cell->common.mesh_instance = create_or_refresh_queue_mesh_instance(c, x, y, cell->common.render_data, false);
        c.construction.world_cell_render_scratch = { 0, 8, 0 };
        c.ports.meshes.submit_world_cell_render_update(c, x, y, c.construction.world_cell_render_scratch);
    } else {
        c.ports.meshes.submit_world_cell_render_update(c, x, y, cell->common.render_data);
    }
}
} //namespace simcoaster
