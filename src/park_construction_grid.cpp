#include "simcoaster/world_object_placement.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_staff.hpp"
#include <format>
#include <memory>

namespace simcoaster {
namespace {
using namespace construction_detail;

struct RotatedGrid {
    WorldCellCoordinates base;
    int width {}, height {}, half_width {}, half_height {}, rotation {};
    CellLocalOffset bias90 {}, bias180 {}, bias270 {};

    RotatedGrid(WorldCellCoordinates input, const DefinitionGrid& grid, int angle, bool from_origin) : base(input), width(grid.width), height(grid.height), half_width((11 - width) / 2), half_height((11 - height) / 2), rotation(angle) {
        if (width < 0 || width > 40 || height < 0 || height > 40) {
            throw std::out_of_range("placement grid extent");
        }
        if (from_origin) {
            bias90 = { 0, -11 };
            bias180 = { -11, -11 };
            bias270 = { -11, 0 };
        } else {
            base.x = fixed::add(base.x, width / -2);
            base.y = fixed::add(base.y, height / -2);
            bias90 = { half_height - half_width, -1 - half_height - half_width };
            bias180 = { -1 - 2 * half_width, -1 - 2 * half_height };
            bias270 = { bias90.y, half_width - half_height };
        }
    }

    WorldCellCoordinates at(int column, int row) const {
        match (rotation) {
            case 0   => return { fixed::add(base.x, column), fixed::add(base.y, row) };
            case 90  => return { base.x + row + bias90.x, bias90.y - column + base.y + 11 };
            case 180 => return { bias180.x - column + base.x + 11, bias180.y - row + base.y + 11 };
            case 270 => return { bias270.x - row + base.x + 11, base.y + column + bias270.y };
            case _   => throw std::invalid_argument("placement rotation is not cardinal");
        }
    }

    std::uint8_t direction() const {
        return rotation == 90 ? 64 : rotation == 180 ? 16 : rotation == 270 ? 4 : 1;
    }
};

std::uint32_t track_selector(ConstructionWorld& c) {
    return construction_track_type(c);
}

void release_cell_track_mesh(ConstructionWorld& c, TrackCell& row) {
    if (row.base.common.mesh_instance) {
        world_operations::destroy_mesh(c, row.base.common.mesh_instance);
        row.base.common.mesh_instance = 0;
    }
}
} //namespace

ParkObjectRuntimeView* ParkConstructionRuntime::create_at_placement(ConstructionWorld& c, int rotation, int cost_override) {
    using namespace construction_detail;
    auto& d = definition(c, c.construction.placement_object_definition_id);
    const auto& bumper = d.balance->Bumper;
    std::uint32_t ride = 0;
    auto* selected_header = c.view.secondary_cursor_record_snapshot;
    auto* selected = selected_header && selected_header->kind == ThingKind::park_object ? reinterpret_cast<ParkObjectRuntimeView*>(selected_header) : nullptr;
    if (d.balance->Info.IsPhotoBooth && !selected) {
        throw std::logic_error("photo booth placement requires a selected ride");
    }
    if (bumper.BumperType) {
        auto p = coordinate(state.placement_cell);
        TrackRideOrientationFlags flags = TrackRideOrientationFlags::none;
        match (rotation) {
            case 0 => {
                p.x = fixed::add(p.x, bumper.NorthXAdjust);
                p.y = fixed::add(p.y, bumper.NorthYAdjust);
                p.x = fixed::add(p.x, 4);
                p.y = fixed::add(p.y, 1);
            }
            case 90 => {
                p.x = fixed::add(p.x, bumper.EastXAdjust);
                p.y = fixed::add(p.y, bumper.EastYAdjust);
                flags = static_cast<TrackRideOrientationFlags>(5);
                p.x = fixed::add(p.x, 1);
                p.y = fixed::subtract(p.y, 5);
            }
            case 180 => {
                p.x = fixed::add(p.x, bumper.SouthXAdjust);
                p.y = fixed::add(p.y, bumper.SouthYAdjust);
                flags = static_cast<TrackRideOrientationFlags>(6);
                p.x = fixed::subtract(p.x, 5);
                p.y = fixed::subtract(p.y, 2);
            }
            case 270 => {
                p.x = fixed::add(p.x, bumper.WestXAdjust);
                p.y = fixed::add(p.y, bumper.WestYAdjust);
                flags = TrackRideOrientationFlags::use_y;
                p.x = fixed::subtract(p.x, 2);
                p.y = fixed::add(p.y, 4);
            }
            case _ => throw std::invalid_argument("placement rotation is not cardinal");
        } const auto scale = [](int v) {
            return fixed::truncate_integer(static_cast<double>(v) * static_cast<double>(307.20001f) * 10.);
        };
        ride = create_track_ride(c, bumper.BumperType, scale(p.x), scale(p.y), flags, d.id);
        if (!ride) {
            throw std::runtime_error("track ride owner creation failed");
        }
    }
    const auto offset = rotate_park_object_local_cell_offset({ d.cached_map_offset[0], d.cached_map_offset[1] }, rotation);
    const auto origin = static_cast<CellId>(static_cast<std::uint32_t>(state.placement_cell) + static_cast<std::uint32_t>(offset.x) + static_cast<std::uint32_t>(offset.y) * 128u);
    auto* storage = static_cast<ParkObjectRuntimeView*>(c.entities.allocate_payload(c.ports.memory, sizeof(ParkObjectRuntimeView)));
    if (!storage) {
        if (ride) {
            destroy_track_ride(c, ride);
        }
        throw std::bad_alloc();
    }
    ParkObjectRuntimeView* created = nullptr;
    try {
        created = world_operations::initialize_park_object_instance(c, std::construct_at(storage), origin, d.id, rotation, ride, cost_override);
    } catch (...) {
        c.entities.release_payload(c.ports.memory, storage);
        if (ride) {
            destroy_track_ride(c, ride);
        }
        throw;
    }
    if (!created) {
        c.entities.release_payload(c.ports.memory, storage);
        if (ride) {
            destroy_track_ride(c, ride);
        }
        throw std::runtime_error("park object creation failed");
    }
    created->upgrade_parent = d.balance->AddOn.UpgradeType == 2 || !c.construction.selected_track_type ? 0 : selected ? selected->base.header.id : 0;
    if (d.balance->Info.IsPhotoBooth) {
        if (!selected) {
            throw std::logic_error("photo booth placement requires a selected ride");
        }
        selected->photo_booth = created->base.header.id;
        created->upgrade_parent = selected->base.header.id;
    }
    c.view.secondary_cursor_record_snapshot = &created->base.header;
    return created;
}

bool ParkConstructionRuntime::special_placement_block(ConstructionWorld& c, WorldCellCoordinates p, int rotation, bool commit, bool validate, std::uint8_t direction) {
    (void)rotation;
    for (const auto delta : std::array<CellLocalOffset, 4> { { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } } }) {
        if (!construction_detail::bounds({ p.x + delta.x, p.y + delta.y })) {
            return false;
        }
    }
    const auto selector = track_selector(c);
    if (selector != 1 && selector != 2) {
        return true;
    }
    if (commit && !validate) {
        return true;
    }
    const auto code = selector == 1 ? 11u : 16u;
    const auto mask = validate ? 512u : 256u;
    for (const auto delta : std::array<CellLocalOffset, 4> { { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } } }) {
        auto& cell = construction_detail::map(c, construction_detail::cell_id({ p.x + delta.x, p.y + delta.y }));
        const auto result = command(c, cell.base, mask | code, 5, advance_world_cell_direction(direction, 16));
        if (validate && !result) {
            return false;
        }
    }
    return true;
}

std::optional<ParkPlacementResult> ParkConstructionRuntime::process_grid(ConstructionWorld& c, WorldCellCoordinates input, int rotation, int cost_override, bool commit, bool validate, DefinitionGrid* override_grid) {
    using namespace construction_detail;
    state.result = {};
    c.construction.interaction_cursor_anchor = { -1, -1 };
    state.direction = 0;
    select_object_definition_and_placement_grid(c, c.construction.selected_object_definition_id, override_grid);
    if (!c.construction.active_object_placement_grid) {
        throw std::logic_error("construction Shape is missing");
    }
    auto& d = definition(c, c.construction.selected_object_definition_id);
    const bool watering = c.construction.placement_object_definition_id && definition(c, c.construction.placement_object_definition_id).balance->UsageInfo.RequiresWatering != 0;
    const bool no_line = d.balance->Info.NoOrientationLine != 0;
    const RotatedGrid rotated { input, *c.construction.active_object_placement_grid, rotation, state.placement_from_origin };
    state.grid_origin = { rotated.base.x - rotated.half_width, rotated.base.y - rotated.half_height };
    state.rotation_degrees = rotation;
    state.placement_origin = rotated.at(0, 0);
    const auto visit = [&](auto&& fn) {
        for (int row = 0; row < rotated.height; ++row) {
            for (int column = 0; column < rotated.width; ++column) {
                fn(column, row, rotated.at(column, row));
            }
        }
    };
    int footprint_count = 0;
    CellId created_cell = 0;
    bool outside = false, has_entry = false, has_exit = false;
    WorldCellCoordinates entry_neighbor {}, exit_neighbor {};
    std::uint8_t entry_direction = 0, exit_direction = 0;
    if (commit) {
        visit([&](int column, int row, WorldCellCoordinates p) {
            if (!bounds(p)) {
                return;
            }
            const auto code = c.construction.active_object_placement_grid->cells.at(static_cast<std::size_t>(row) * 40 + static_cast<std::size_t>(column)).code;
            if (code == 4) {
                ++footprint_count;
            }
            if (code) {
                (void)command(c, map(c, cell_id(p)).base, 135);
            }
        });
        state.result.placement_origin = state.placement_origin;
        state.placement_cell = cell_id(state.placement_origin);
        auto* placed = create_at_placement(c, rotation, cost_override);
        state.result.created_object = placed->base.header.id;
        auto* control = find_and_cache_world_object_control(world(c).object_controls, placed->definition_id);
        if (!control) {
            throw std::logic_error("placed object control is missing");
        }
        const auto ordinal = ++control->placement_name_ordinal;
        if (c.mode.game != GameMode::code_3 && ordinal > 1) {
            auto end = std::ranges::find(placed->name_b, char16_t {});
            std::u16string name { placed->name_b.begin(), end };
            name += u' ';
            for (char digit : std::format("{}", ordinal)) {
                name += static_cast<char16_t>(digit);
            }
            const auto count = std::min<std::size_t>(32, name.size());
            std::ranges::fill(placed->name_b, char16_t {});
            std::copy_n(name.begin(), count, placed->name_b.begin());
        }
        if (!c.construction.action_recording_disabled) {
            const auto sound = footprint_count < 4 ? 142u : footprint_count < 8 ? 141u : footprint_count < 16 ? 143u : 144u;
            (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, sound, rotated.base.x * 10, 0, rotated.base.y * 10);
        }
        created_cell = static_cast<CellId>((placed->base.y >> 8) * 128u + (placed->base.x >> 8) + 1u);
    }
    if (!c.construction.selected_track_type) {
        refresh_origin(c);
    }
    bool accepted = true;
    visit([&](int column, int row, WorldCellCoordinates p) {
        if (!accepted) {
            return;
        }
        if (!bounds(p)) {
            outside = true;
            return;
        }
        auto& cell = map(c, cell_id(p));
        auto& paired = track(c, cell.base.index);
        const auto source = c.construction.active_object_placement_grid->cells.at(static_cast<std::size_t>(row) * 40 + static_cast<std::size_t>(column));
        const auto code = source.code;
        const auto direction = advance_world_cell_direction(rotated.direction(), source.attribute);
        if (commit) {
            if (code && code != 21) {
                const auto operation = code == 11 || code == 16 || code == 23 ? 4 : code;
                (void)command(c, cell.base, 135);
                remove_map_cell_contents(c, &cell.base, 0, 0);
                (void)command(c, cell.base, static_cast<std::uint32_t>(operation));
                (void)command(c, cell.base, 129);
                set_world_cell_parent_id_unchecked(&cell.base, created_cell);
                release_cell_track_mesh(c, paired);
                clear_world_cell_flags(&paired.base, 15);
            }
            if (cell.litter_selector) {
                release_world_litter_record(c, cell);
            }
        }
        const auto check = [&](unsigned request, int x = 0, int y = 0) {
            if (!command(c, cell.base, 512u | request, x, y)) {
                accepted = false;
                return false;
            }
            return true;
        };
        const auto preview = [&](unsigned request, int x = 0, int y = 0) {
            if (!command(c, cell.base, 256u | request, x, y)) {
                outside = true;
            }
        };
        const auto object_preview = [&] {
            preview(4, !row && !no_line ? 6 : 0, !row && !no_line ? rotated.direction() : 0);
        };
        (do {
            match (code) {
                case 0 => {
                }
                case 1 => {
                    if (commit) {
                        assign_world_cell_flags(&cell.base, 32);
                        refresh_connected_paths(c, cell);
                    } else if (validate) {
                        (void)check(4);
                    } else {
                        preview(4);
                    }
                }
                case 9 or 10 => {
                    if (commit || !watering) {
                        if (code == 9) {
                            has_entry = true;
                            entry_direction = direction;
                            const auto* n = resolve_world_cell_from_selector_flag_byte(c, &cell.base, opposite_world_cell_direction(direction));
                            if (n) {
                                entry_neighbor = coordinate(n->base.index);
                            } else {
                                entry_neighbor = { -1, -1 };
                            }
                        } else {
                            has_exit = true;
                            exit_direction = direction;
                            const auto* n = resolve_world_cell_from_selector_flag_byte(c, &cell.base, direction);
                            if (n) {
                                exit_neighbor = coordinate(n->base.index);
                            } else {
                                exit_neighbor = { -1, -1 };
                            }
                        }
                    }
                    if (commit) {
                        if (code == 9) {
                            state.result.entrance = p;
                            state.result.entrance_direction = direction;
                        }
                        set_world_cell_direction(&cell.base, direction);
                        auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell.base, code == 9 ? opposite_world_cell_direction(direction) : direction);
                        if (neighbor && neighbor->base.common.type == 1) {
                            update_cell_connections(c, &neighbor->base, 1, 0, 0, true, false);
                            (void)command(c, neighbor->base, 129);
                        }
                    } else if (validate) {
                        (void)check(4, code == 9 ? 3 : 0, direction);
                    } else {
                        object_preview();
                    }
                }
                case 11 or 16 or 21 => {
                    if (commit) {
                        if (code != 21) {
                            assign_world_cell_flags(&cell.base, 16);
                            assign_world_cell_flags(&paired.base, 16);
                        }
                    } else if (validate) {
                        if (!check(static_cast<unsigned>(code)) || cell.base.common.type == 4) {
                            accepted = false;
                        }
                    } else {
                        preview(static_cast<unsigned>(code));
                    }
                }
                case 23 => {
                    if (source.attribute) {
                        if (validate) {
                            if (!check(4)) {
                                do_return;
                            }
                        } else if (!commit) {
                            preview(4, 0, advance_world_cell_direction(rotated.direction(), 16));
                        }
                    }
                    auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell.base, direction);
                    if (validate) {
                        if (!neighbor || !command(c, neighbor->base, 516u)) {
                            accepted = false;
                        }
                    } else if (commit) {
                        if (!neighbor) {
                            throw std::out_of_range("coaster support placement neighbor");
                        }
                        (void)command(c, neighbor->base, 135);
                        (void)command(c, neighbor->base, 21);
                        (void)command(c, neighbor->base, 131);
                        (void)command(c, neighbor->base, 132);
                        (void)command(c, neighbor->base, 129);
                    } else if (neighbor) {
                        (void)command(c, neighbor->base, 260u, 5, advance_world_cell_direction(rotated.direction(), 16));
                    } else {
                        state.validation_failed = true;
                    }
                }
                case _ => {
                    if (!commit) {
                        if (validate) {
                            (void)check(4);
                        } else {
                            object_preview();
                        }
                    }
                }
            }
        });
    });
    if (!accepted) {
        return std::nullopt;
    }
    //Native UI constrains the placement center before this routine. Public
    //host callers have no such frontend, so reject clipped placement grids.
    if (outside && validate) {
        state.validation_failed = true;
        return std::nullopt;
    }
    if (!c.construction.selected_track_type) {
        refresh_origin(c);
    }
    const auto queue_enabled = state.queue_placement_enabled && !c.construction.selected_track_type;
    if (has_entry || has_exit) {
        if (outside) {
            state.validation_failed = true;
        }
        if (!bounds(entry_neighbor)) {
            return std::nullopt;
        }
        auto& entry = map(c, cell_id(entry_neighbor));
        const auto reversed_entry = opposite_world_cell_direction(entry_direction);
        if (queue_enabled) {
            if (validate) {
                if (!command(c, entry.base, 516u, 3, reversed_entry)) {
                    return std::nullopt;
                }
            } else if (commit) {
                auto& doorway = map(c, cell_id(state.result.entrance));
                or_world_cell_neighbor_mask_bits(c, &doorway.base, reversed_entry);
                set_world_cell_direction(&doorway.base, reversed_entry);
                (void)command(c, entry.base, 135);
                (void)command(c, entry.base, 3);
                or_world_cell_neighbor_mask_bits(c, &entry.base, entry_direction);
                set_world_cell_direction(&entry.base, entry_direction);
                set_world_cell_parent_id_unchecked(&entry.base, doorway.base.common.parent_id);
                assign_world_cell_flags(&entry.base, 32);
                (void)command(c, entry.base, 129);
                (void)command(c, entry.base, 133);
                (void)command(c, entry.base, 134);
                if (c.construction.park_object) {
                    rebuild_park_object_queue_layout(c, c.construction.park_object);
                }
            } else {
                (void)command(c, entry.base, 260u, 3, reversed_entry);
            }
        } else {
            if (validate) {
                if (!command(c, entry.base, 513u, 7, 0)) {
                    return std::nullopt;
                }
            } else if (commit) {
                if (entry.base.common.type != 1) {
                    (void)command(c, entry.base, 135);
                }
                (void)command(c, entry.base, 1);
                refresh_connected_paths(c, entry);
                (void)command(c, entry.base, 133);
                (void)command(c, entry.base, 134);
                assign_world_cell_flags(&entry.base, 32);
            } else {
                (void)command(c, entry.base, 280u, 7, reversed_entry);
            }
        }
        if (queue_enabled) {
            if (!bounds(exit_neighbor)) {
                return std::nullopt;
            }
            auto& exit = map(c, cell_id(exit_neighbor));
            const auto reversed_exit = opposite_world_cell_direction(exit_direction);
            if (validate) {
                if (!command(c, exit.base, 513u, 0, reversed_exit)) {
                    return std::nullopt;
                }
            } else if (commit) {
                if (exit.base.common.type != 1) {
                    (void)command(c, exit.base, 135);
                }
                (void)command(c, exit.base, 1);
                refresh_connected_paths(c, exit, true);
                (void)command(c, exit.base, 133);
                (void)command(c, exit.base, 134);
                assign_world_cell_flags(&exit.base, 32);
            } else {
                (void)command(c, exit.base, 280u, 4, reversed_exit);
            }
        }
    }
    const auto selector = track_selector(c);
    if (selector == 1 || selector == 2) {
        const auto& b = definition(c, c.construction.selected_object_definition_id).balance->Bumper;
        WorldCellCoordinates first {}, second {};
        int east_x, south_y, west_y;
        match (rotation) {
            case 90 => {
                first.x = rotated.base.x + rotated.half_height + b.WestXAdjust - rotated.half_width;
                west_y = b.WestYAdjust - rotated.half_height - rotated.half_width + rotated.base.y;
                first.y = west_y + 7;
                second = { first.x - 1, west_y + 12 };
            }
            case 180 => {
                first.x = b.SouthXAdjust - 2 * rotated.half_width + rotated.base.x + 1;
                south_y = b.SouthYAdjust - 2 * rotated.half_height + rotated.base.y;
                first.y = south_y + 7;
                second = { rotated.base.x + b.SouthXAdjust + 2 * (3 - rotated.half_width), south_y + 9 };
            }
            case 270 => {
                east_x = b.EastXAdjust - rotated.half_height - rotated.half_width + rotated.base.x;
                first = { east_x + 7, rotated.base.y + rotated.half_width + b.EastYAdjust - rotated.half_height };
                second = { east_x + 9, first.y - 4 };
            }
            case _ => {
                first = { b.NorthXAdjust + rotated.base.x, b.NorthYAdjust + rotated.base.y };
                second = { first.x + 8, first.y - 1 };
            }
        } if (!special_placement_block(c, first, rotation, commit, validate, rotated.direction())) {
            return std::nullopt;
        }
        match (rotation) {
            case 0 => {
                second.x -= 2;
                ++second.y;
            }
            case 90 => {
                ++second.x;
                ++second.y;
            }
            case 180 => {
                ++second.x;
                second.y -= 2;
            }
            case 270 => {
                second.x -= 2;
                second.y -= 2;
            }
            case _ => {
            }
        } if (!special_placement_block(c, second, rotation, commit, validate, rotated.direction())) {
            return std::nullopt;
        }
    }
    return state.result;
}
} //namespace simcoaster
