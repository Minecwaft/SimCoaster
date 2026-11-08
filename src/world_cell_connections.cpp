#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_queue.hpp"
#include <stdexcept>

namespace simcoaster {
namespace {
struct CornerCheck {
    CellLocalOffset offset;
    std::uint8_t side, add;
};

struct Cardinal {
    CellLocalOffset offset;
    std::uint8_t direction, opposite;
    std::array<CornerCheck, 2> corners;
    std::array<std::uint8_t, 2> clear_diagonals;
};

constexpr std::array<Cardinal, 4> cardinals { { { { 0, -1 }, 1, 16, { { { { 1, 1 }, 4, 8 }, { { -1, 1 }, 64, 32 } } }, { 2, 128 } }, { { 1, 0 }, 4, 64, { { { { -1, -1 }, 1, 128 }, { { -1, 1 }, 16, 32 } } }, { 2, 8 } }, { { 0, 1 }, 16, 1, { { { { 1, -1 }, 4, 2 }, { { -1, -1 }, 64, 128 } } }, { 8, 32 } }, { { -1, 0 }, 64, 4, { { { { 1, -1 }, 1, 2 }, { { 1, 1 }, 16, 8 } } }, { 128, 32 } } } };

struct Diagonal {
    CellLocalOffset offset;
    std::uint8_t direction, opposite, vertical, horizontal;
};

constexpr std::array<Diagonal, 4> diagonals { { { { 1, -1 }, 2, 32, 1, 4 }, { { 1, 1 }, 8, 128, 16, 4 }, { { -1, 1 }, 32, 2, 16, 64 }, { { -1, -1 }, 128, 8, 1, 64 } } };

CellBase& require_base(MapCell* cell) {
    if (!cell) {
        throw std::out_of_range("missing connection side cell");
    }
    return cell->base;
}

MapCell& primary_map(WorldRuntime& c, CellId id) {
    if (!c.simulation.reference) {
        throw std::invalid_argument("null connection primary World");
    }
    if (!id) {
        throw std::out_of_range("zero connection parent/entry ID");
    }
    return c.simulation.reference->map_cells.at(static_cast<std::size_t>(id) - 1);
}

void link(WorldRuntime& c, CellBase& origin, CellBase& other, std::uint8_t direction, std::uint8_t opposite) {
    or_world_cell_neighbor_mask_bits(c, &origin, direction);
    or_world_cell_neighbor_mask_bits(c, &other, opposite);
}

bool queue_or_entry(std::int32_t type) {
    return type == 3 || type == 9;
}

void path_cardinal(WorldRuntime& c, CellBase& cell, const Cardinal& rule, bool refresh, bool linked, std::int32_t audio_x) {
    auto* resolved = resolve_world_cell_by_scaled_local_offset(c, &cell, rule.offset, 1);
    if (!resolved) {
        return;
    }
    auto& other = resolved->base;
    if (other.common.type == 1) {
        link(c, cell, other, rule.direction, rule.opposite);
        for (const auto& corner : rule.corners) {
            const auto* across = resolve_world_cell_by_scaled_local_offset(c, &other, corner.offset, 1);
            if (across && across->base.common.type == 1) {
                const auto side = require_base(resolve_world_cell_from_selector_flag_byte(c, &other, corner.side)).common.type;
                if (!queue_or_entry(side)) {
                    or_world_cell_neighbor_mask_bits(c, &other, corner.add);
                }
            }
        }
        if (refresh) {
            refresh_world_cell_render_state(c, &other);
        }
    }
    //These are independent live tests. A preceding render callback may change
    //the neighbor's type and cause another link/refresh within this same visit.
    if (other.common.type == 10 && (get_world_cell_direction(&other) & rule.opposite)) {
        link(c, cell, other, rule.direction, rule.opposite);
        if (refresh) {
            refresh_world_cell_render_state(c, &other);
        }
    }
    const auto live_type = other.common.type;
    if (live_type == 9) {
        if (get_world_cell_direction(&other) & rule.direction) {
            link(c, cell, other, rule.direction, rule.opposite);
            if (refresh) {
                refresh_world_cell_render_state(c, &other);
            }
        }
    } else if (live_type == 3 && count_world_cell_neighbor_bits(c, &other) == 1 && linked) {
        link(c, cell, other, rule.direction, rule.opposite);
        const auto parent_id = other.common.parent_id;
        const auto parent_type = primary_map(c, parent_id).base.common.type;
        if (parent_type == 4 || parent_type == 9 || parent_type == 10) {
            const auto id = find_first_park_object_id_in_map_cell(c, &primary_map(c, parent_id));
            if (id) {
                auto* object = c.entities.at(id).payload;
                if (!object) {
                    throw std::invalid_argument("null connection queue owner");
                }
                rebuild_park_object_queue_layout(c, reinterpret_cast<ParkObjectRuntimeView*>(object));
            }
        }
    }
    if (queue_or_entry(other.common.type) && (get_world_cell_neighbor_mask(c, &other) & rule.opposite) && refresh) {
        refresh_world_cell_render_state(c, &other);
        if (!c.construction.action_recording_disabled) {
            //Both native coordinates are the captured X. Integer multiplication is
            //exact over0..127 and needs none of the original FILD/ftol machinery.
            submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 0x8Bu, audio_x * 10, 0, audio_x * 10);
        }
    }
}

void path_diagonal(WorldRuntime& c, CellBase& cell, const Diagonal& rule, bool refresh) {
    auto* resolved = resolve_world_cell_by_scaled_local_offset(c, &cell, rule.offset, 1);
    if (resolved && resolved->base.common.type == 1 && require_base(resolve_world_cell_from_selector_flag_byte(c, &cell, rule.vertical)).common.type == 1 && require_base(resolve_world_cell_from_selector_flag_byte(c, &cell, rule.horizontal)).common.type == 1) {
        link(c, cell, resolved->base, rule.direction, rule.opposite);
        if (refresh) {
            refresh_world_cell_render_state(c, &resolved->base);
        }
    }
}

void directional_steps(WorldRuntime& c, CellBase& cell, std::int32_t selector, std::int32_t x, std::int32_t y) {
    const std::array<bool, 4> selected { y == 1, x == -1, y == -1, x == 1 };
    for (std::size_t i = 0; i < cardinals.size(); ++i) {
        if (selected[i]) {
            const auto& rule = cardinals[i];
            auto* resolved = resolve_world_cell_by_scaled_local_offset(c, &cell, rule.offset, 1);
            if (resolved && resolved->base.common.type == selector) {
                auto& other = resolved->base;
                if (count_world_cell_neighbor_bits(c, &other) < 2 && (!other.common.parent_id || other.common.parent_id == c.construction.origin_cell)) {
                    link(c, cell, other, rule.direction, rule.opposite);
                }
                //Diagonal clearing is outside the degree and ownership tests.
                if (cell.common.type == 1) {
                    for (auto mask : rule.clear_diagonals) {
                        clear_world_cell_neighbor_mask_bits(c, &cell, mask);
                    }
                }
            }
        }
    }
}
} //namespace

std::uint32_t submit_positioned_template_request(WorldRuntime& c, std::uint32_t target, std::uint32_t catalog, std::uint32_t key, std::int32_t x, std::int32_t y, std::int32_t z) {
    if (!c.resources.resource_manager_feature_flag_0400 || !c.resources.resource_manager_dispatch_initialized || !key) {
        return 0;
    }
    const PositionedTemplateRequest request { catalog, key, { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) }, 0.0f };
    return c.ports.audio.process_positioned_sound_request(c, target, request);
}

void update_cell_connections(WorldRuntime& c, CellBase* receiver, std::int32_t selector, std::int32_t step_x, std::int32_t step_y, bool refresh, bool linked) {
    if (!receiver) {
        throw std::invalid_argument("null connection cell");
    }
    auto& cell = *receiver;
    const auto audio_x = static_cast<std::int32_t>((static_cast<std::uint32_t>(cell.index) - 1u) & 127u);
    if (selector == 1) {
        if (cell.common.type == 3) {
            cell.common.type = 1;
            const auto serial = next_world_grid_change_serial(c);
            store_world_grid_change_serial_for_cell_id(c.simulation.reference, serial, cell.index);
        }
        for (std::size_t i = 0; i < cardinals.size(); ++i) {
            path_cardinal(c, cell, cardinals[i], refresh, linked, audio_x);
            path_diagonal(c, cell, diagonals[i], refresh);
        }
        for (const auto& rule : cardinals) {
            if (get_world_cell_neighbor_mask(c, &cell) & rule.direction) {
                auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell, rule.direction);
                if (neighbor && queue_or_entry(neighbor->base.common.type)) {
                    for (auto mask : rule.clear_diagonals) {
                        clear_world_cell_neighbor_mask_bits(c, &cell, mask);
                    }
                }
            }
        }
    } else if (selector == 0 || selector == 3) {
        if (!c.construction.park_object) {
            throw std::invalid_argument("null connection target object");
        }
        const auto entry_id = c.construction.park_object->entry_cell;
        if (c.construction.cell_line_first_dispatch_pending) {
            //The original biased address is only compared, never dereferenced.
            //Zero/out-of-array IDs cannot match a valid BASE map-cell object.
            auto* entry = c.simulation.reference && entry_id && entry_id <= map_cell_count ? &c.simulation.reference->map_cells[static_cast<std::size_t>(entry_id) - 1].base : nullptr;
            for (const auto& rule : cardinals) {
                auto* neighbor = resolve_world_cell_by_scaled_local_offset(c, &cell, rule.offset, 1);
                if (neighbor && get_world_cell_direction(&neighbor->base) == rule.opposite && &neighbor->base == entry) {
                    link(c, cell, neighbor->base, rule.direction, rule.opposite);
                }
            }
            if (c.construction.previous_path_step_x != 128 && c.construction.previous_path_step_y != 128) {
                directional_steps(c, cell, selector, c.construction.previous_path_step_x, c.construction.previous_path_step_y);
            }
        } else {
            directional_steps(c, cell, selector, step_x, step_y);
        }
    }
    c.construction.previous_path_step_x = step_x;
    c.construction.previous_path_step_y = step_y;
}
} //namespace simcoaster
