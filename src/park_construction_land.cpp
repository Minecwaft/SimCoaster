#include "park_construction_internal.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <bitset>
#include <filesystem>

namespace simcoaster {
namespace {
using namespace construction_detail;
constexpr std::array<CellLocalOffset, 4> block_offsets{ { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } };
using HoardingClicks = HoardingBalance;

bool same(WorldCellCoordinates a, WorldCellCoordinates b) {
    return a.x == b.x && a.y == b.y;
}

bool straight(ConstructionWorld& c, const CellBase& cell) {
    const auto mask = get_world_cell_neighbor_mask(c, &cell);
    return count_world_cell_cardinal_neighbor_bits(c, &cell) == 2
        && (((mask & 1u) && (mask & 16u)) || ((mask & 4u) && (mask & 64u)));
}

WorldCellCoordinates boundary_start(const ConstructionQuery& c) {
    return { c.construction.hoarding_chain_start_x, c.construction.hoarding_chain_start_y };
}

WorldCellCoordinates boundary_target(const ConstructionQuery& c) {
    return { c.construction.hoarding_target_x, c.construction.hoarding_target_y };
}

bool gate_endpoint(const ConstructionQuery& c, WorldCellCoordinates p) {
    return same(p, boundary_start(c)) || same(p, boundary_target(c));
}

CellBase& resolved_track(ConstructionWorld& c, WorldCellCoordinates p) {
    auto* row = &track(c, cell_id(p)).base;
    if (world_cell_is_track_child(row))
        row = &track(c, row->common.parent_id).base;
    return *row;
}

void unlock_block(ConstructionWorld& c, CellId root) {
    auto& origin = map(c, root).base;
    for (const auto offset : block_offsets) {
        auto* cell =
            offset.x || offset.y ? resolve_world_cell_by_scaled_local_offset(c, &origin, offset, 1) : &map(c, root);
        if (!cell)
            throw std::out_of_range("land boundary block outside World");
        clear_world_cell_flags_mask(&cell->base, 64);
        refresh_world_cell_render_state(c, &cell->base);
    }
}

void clear_support_track_block(ConstructionWorld& c, CellBase& root) {
    //Native686880 deliberately retains mesh/overlap fields. The map21 owner
    //borrows its mesh slot, so this path must not release that object.
    for (const auto offset : block_offsets) {
        auto* cell =
            offset.x || offset.y ? resolve_track_cell_at_local_offset(c, &root, offset, 1) : &track(c, root.index);
        if (!cell)
            continue;
        clear_world_cell_neighbor_mask_bits(c, &cell->base, 255);
        set_world_cell_direction(&cell->base, 0);
        cell->base.common.flags = 0;
        (void)try_assign_world_cell_type(c, &cell->base, 0);
        set_world_cell_parent_id_unchecked(&cell->base, 0);
    }
}

struct Rectangle {
    int left, top, right, bottom;

    Rectangle(WorldCellCoordinates a, WorldCellCoordinates b)
        : left(std::min(a.x, b.x)),
          top(std::min(a.y, b.y)),
          right(std::max(a.x, b.x)),
          bottom(std::max(a.y, b.y)) {}
};

using Raster = std::array<std::uint8_t, 67 * 67>;

std::size_t raster_index(int x, int y) {
    return static_cast<std::size_t>(x) * 67 + static_cast<std::size_t>(y);
}

Raster ownership_raster(ConstructionWorld& c) {
    Raster result{};
    result.fill(32);
    //Native679AB0 visits each physical map cell, so the last X/Y parity alias
    //wins each half-resolution raster slot. Preserve that ordering.
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            auto& root = resolved_track(c, { x, y });
            auto& value = result[raster_index(x / 2, y / 2)];
            value = root.common.type == 25 ? 64 : !(map(c, root.index).base.common.flags & 64u) ? 35 : 32;
        }
    return result;
}

bool has_no_closed_unowned_component(ConstructionWorld& c, const Rectangle& rectangle) {
    const auto source = ownership_raster(c);
    Raster work{};
    work.fill(32);
    for (int x = 0; x < 66; ++x)
        for (int y = 0; y < 66; ++y)
            work[raster_index(x + 1, y + 1)] = source[raster_index(x, y)];
    for (int i = 0; i < 66; ++i)
        for (const auto p : std::array<WorldCellCoordinates, 4>{ { { 0, i }, { 65, i }, { i, 0 }, { i, 65 } } })
            work[raster_index(p.x, p.y)] = 32;
    for (int y = rectangle.top + 1; y <= rectangle.bottom + 1; y += 2)
        for (int x = rectangle.left + 1; x <= rectangle.right + 1; x += 2)
            work[raster_index(x / 2, y / 2)] = 35;
    std::vector<WorldCellCoordinates> pending{ { 65, 0 } };
    while (!pending.empty()) {
        const auto p = pending.back();
        pending.pop_back();
        if (p.x < 0 || p.x > 65 || p.y < 0 || p.y > 65)
            continue;
        auto& value = work[raster_index(p.x, p.y)];
        if (value != 32 && value != 64)
            continue;
        value = 35;
        for (const auto delta : std::array<CellLocalOffset, 4>{ { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } } })
            pending.push_back({ p.x + delta.x, p.y + delta.y });
    }
    for (int x = 1; x < 65; ++x)
        for (int y = 1; y < 65; ++y) {
            const auto value = work[raster_index(x, y)];
            if (value == 32 || value == 64)
                return false;
        }
    return true;
}
} //namespace

void bind_land_boundary_anchors(ConstructionWorld& c) {
    const auto& fixed = c.visitors.environment.FixedItemInfo;
    c.construction.hoarding_chain_start_x = fixed.EntranceBPosX + 3;
    c.construction.hoarding_chain_start_y = fixed.EntranceBPosY;
    c.construction.hoarding_target_x = fixed.EntranceAPosX - 4;
    c.construction.hoarding_target_y = fixed.EntranceAPosY;
    if (!bounds(boundary_start(c)) || !bounds(boundary_target(c)))
        throw std::out_of_range("land boundary entrance anchors outside World");
}

void sync_land_boundary_meshes(ConstructionWorld& c) {
    using namespace construction_detail;
    RestoreValue mode{ c.construction.world_hoarding_mode };
    c.construction.world_hoarding_mode = 1;
    for (auto& row : world(c).track_cells) {
        auto& cell = row.base;
        if (cell.common.type != 25 || map(c, cell.index).base.common.type == 21 || cell.common.mesh_instance)
            continue;
        const auto p = coordinate(cell.index);
        if (cell.common.render_data.texture_group != 5)
            update_world_cell_mesh(c, &cell);
        if (!cell.common.render_data.texture_member)
            continue;
        cell.common.mesh_instance =
            create_or_refresh_hoarding_mesh_instance(c, p.x, p.y, cell.common.render_data, false);
        if (!cell.common.mesh_instance)
            throw std::runtime_error(
                "land boundary drawable creation failed at "
                + std::to_string(p.x)
                + ","
                + std::to_string(p.y)
                + " member "
                + std::to_string(cell.common.render_data.texture_member)
                + " template "
                + std::to_string(
                    c.construction.hoarding_mesh_templates.at(cell.common.render_data.texture_member).definition_index)
                + (c.resources.meshes && c.resources.meshes->last_error
                       ? ": " + describe_mesh_error(*c.resources.meshes->last_error)
                       : ""));
    }
}

void ParkConstructionRuntime::refresh_hoarding_cell(ConstructionWorld& c, CellBase& cell) {
    using namespace construction_detail;
    const auto direction = get_world_cell_direction(&cell);
    const auto mask = get_world_cell_neighbor_mask(c, &cell);
    const bool inward_corner = (direction == 1 && (mask == 65 || mask == 80))
                            || (direction == 16 && (mask == 20 || mask == 5))
                            || (direction == 4 && (mask == 5 || mask == 65))
                            || (direction == 64 && (mask == 80 || mask == 20));
    const bool line = straight(c, cell) || gate_endpoint(c, coordinate(cell.index));
    const bool corner = map_corner(c, cell);
    std::array<bool, 4> allowed{}; //NW, Y+, XY+, X+ in block_offsets order.
    match (direction) {
        case 1 => {
            if (line)
                allowed[3] = allowed[2] = true;
            else if (corner) {
                allowed[3] = true;
                if (inward_corner)
                    allowed.fill(true);
            }
        }
        case 4 => {
            if (line)
                allowed[1] = allowed[2] = true;
            else if (corner) {
                allowed[2] = true;
                if (inward_corner)
                    allowed.fill(true);
            }
        }
        case 16 => {
            if (line)
                allowed[0] = allowed[1] = true;
            else if (corner) {
                allowed[1] = true;
                if (inward_corner)
                    allowed.fill(true);
            }
        }
        case 64 => {
            if (line)
                allowed[0] = allowed[3] = true;
            else if (corner) {
                //681E39 only opens X+ for an inward corner. The other corner
                //orientation clears NW but retains the X+ ownership restriction.
                allowed[0] = true;
                if (inward_corner)
                    allowed.fill(true);
            }
        }
        case _ => {}
    }
    auto& origin = map(c, cell.index).base;
    const auto apply = [&](CellBase& target, bool open) {
        if (open)
            clear_world_cell_flags_mask(&target, 64);
        else
            //Native copies the live NW flag word, rather than ORing the neighbor's
            //previous word. Preserve this otherwise surprising propagation.
            target.common.flags = static_cast<std::uint16_t>(origin.common.flags | 64u);
        refresh_world_cell_render_state(c, &target);
    };
    apply(origin, allowed[0]);
    //Native applies X+, Y+, XY+ after the origin, retaining its live flags.
    for (const auto index : std::array<std::size_t, 3>{ 3, 1, 2 })
        if (auto* neighbor = resolve_world_cell_by_scaled_local_offset(c, &origin, block_offsets[index], 1))
            apply(neighbor->base, allowed[index]);
}

void ParkConstructionRuntime::trace_land_boundary(ConstructionWorld& c, bool update_direction) {
    using namespace construction_detail;
    auto p = boundary_start(c);
    const auto target = boundary_target(c);
    std::uint8_t direction = 4;
    std::bitset<map_cell_count * 4> visited;
    for (;;) {
        if (!bounds(p))
            throw std::out_of_range("land boundary route left World");
        auto& cell = track(c, cell_id(p)).base;
        const auto direction_ordinal = direction == 1 ? 0u : direction == 4 ? 1u : direction == 16 ? 2u : 3u;
        const auto visit = (static_cast<std::size_t>(cell.index) - 1) * 4 + direction_ordinal;
        if (visited.test(visit))
            throw std::length_error("land boundary cannot reach entrance target");
        visited.set(visit);
        if (cell.common.type != 25 && !get_world_cell_neighbor_mask(c, &cell))
            throw std::logic_error("land boundary route has no outgoing segment");
        if (map_corner(c, cell)) {
            auto incoming = opposite_world_cell_direction(direction);
            if (!(get_world_cell_neighbor_mask(c, &cell) & incoming))
                incoming = direction;
            direction = incoming ^ get_world_cell_neighbor_mask(c, &cell);
        }
        if (get_world_cell_branch_neighbor_mask(c, &cell)) {
            auto selected = advance_world_cell_direction(direction, 64);
            for (unsigned probe = 0; !(get_world_cell_neighbor_mask(c, &cell) & selected); ++probe) {
                if (probe == 4)
                    throw std::logic_error("land boundary branch has no cardinal exit");
                selected = advance_world_cell_direction(selected, 4);
            }
            if (selected == direction)
                clear_world_cell_neighbor_mask_bits(
                    c,
                    &cell,
                    static_cast<std::uint8_t>(~(direction | opposite_world_cell_direction(direction))));
            else {
                set_world_cell_direction(&cell, selected);
                clear_world_cell_neighbor_mask_bits(c, &cell, direction);
                clear_world_cell_neighbor_mask_bits(c, &cell, opposite_world_cell_direction(selected));
            }
            (void)track_command(c, cell, 129);
            update_world_cell_mesh(c, &cell);
            direction = selected;
        }
        if (update_direction && get_world_cell_direction(&cell) != direction) {
            set_world_cell_direction(&cell, direction);
            (void)track_command(c, cell, 129);
        }
        for (const auto offset : std::span{ block_offsets }.subspan(1))
            if (auto* child = resolve_track_cell_at_local_offset(c, &cell, offset, 1)) {
                set_world_cell_parent_id_unchecked(&child->base, cell.index);
                (void)try_assign_world_cell_type(c, &child->base, 12);
            }
        if (same(p, target))
            break;
        p = stepped(p, direction, 2);
    }
    c.construction.world_hoarding_route_valid = true;
}

void ParkConstructionRuntime::apply_land_boundary_ownership(ConstructionWorld& c) {
    using namespace construction_detail;
    auto p = boundary_start(c);
    std::uint8_t direction = 4;
    std::bitset<map_cell_count> visited;
    bool upward_ray_exhausted = false;
    for (std::size_t visits = 0;; ++visits) {
        if (!bounds(p))
            throw std::out_of_range("land ownership boundary left World");
        auto& cell = track(c, cell_id(p)).base;
        if (visited.test(cell.index - 1u))
            throw std::length_error("cyclic land ownership boundary");
        visited.set(cell.index - 1u);
        if (cell.common.type == 25) {
            const auto inward = advance_world_cell_direction(direction, 4);
            auto scan = stepped(p, inward, 2);
            for (std::size_t steps = 0;; ++steps) {
                //676C98..676CB6 explicitly ends an upward ray once Y becomes
                //negative. Entrance gaps need not have a marked boundary above them.
                if (inward == 1 && scan.y < 0) {
                    upward_ray_exhausted = true;
                    break;
                }
                if (!bounds(scan) || steps >= map_cell_count)
                    throw std::out_of_range("land interior ray from "
                                            + std::to_string(p.x)
                                            + ","
                                            + std::to_string(p.y)
                                            + " direction "
                                            + std::to_string(inward)
                                            + " has no marked boundary");
                auto& interior = track(c, cell_id(scan)).base;
                const bool marked = (interior.common.flags & 32u) != 0;
                if (!marked)
                    unlock_block(c, interior.index);
                scan = stepped(scan, inward, 2);
                //Native var52 is initialized once for the entire boundary walk.
                //After an upward ray crosses Y0, later upward rays execute only
                //their first iteration. Even a marked final cell updates this latch.
                if (inward == 1 && scan.y < 0)
                    upward_ray_exhausted = true;
                if (marked || (inward == 1 && upward_ray_exhausted))
                    break;
            }
            if (map_corner(c, cell))
                direction = opposite_world_cell_direction(direction) ^ get_world_cell_neighbor_mask(c, &cell);
        }
        const auto* next = resolve_track_cell_in_direction(c, &cell, direction, 2);
        if ((next && next->base.common.type != 25) || (count_world_cell_neighbor_bits(c, &cell) == 1 && visits))
            break;
        p = stepped(p, direction, 2);
    }
    for (int y = 0; y < 127; ++y)
        for (int x = 0; x < 127; ++x) {
            auto& root = resolved_track(c, { x, y });
            if (root.common.type != 25 || (root.common.flags & 32u))
                continue;
            if (map(c, root.index).base.common.type == 21)
                clear_support_track_block(c, root);
            else
                remove_track_cell_contents(c, &root, 0, 0, 0);
            root.common.overlap_counter = 0;
            unlock_block(c, root.index);
        }
}

bool ParkConstructionRuntime::validate_land_rectangle(ConstructionWorld& c,
                                                      WorldCellCoordinates first,
                                                      WorldCellCoordinates second) {
    using namespace construction_detail;
    state.cost = 0;
    bind_land_boundary_anchors(c);
    const Rectangle r{ first, second };
    if (!bounds({ r.left, r.top }) || !bounds({ r.right, r.bottom }) || r.right - r.left <= 2 || r.bottom - r.top <= 2)
        return false;
    RestoreValue mode{ c.construction.world_hoarding_mode };
    c.construction.world_hoarding_mode = 1;

    struct Edge {
        int straight_count{}, other_count{}, hoardings{};
    } top, bottom, left, right;

    int corner_hoardings = 0;
    const auto sample = [&](WorldCellCoordinates p, Edge& edge, std::uint8_t parallel, bool endpoint) {
        auto& cell = track(c, cell_id(p)).base;
        const bool hoarding = cell.common.type == 25;
        if (hoarding) {
            ++edge.hoardings;
            corner_hoardings += endpoint;
            if (straight(c, cell) && get_world_cell_neighbor_mask(c, &cell) == parallel)
                ++edge.straight_count;
            else
                ++edge.other_count;
        }
    };
    for (int x = r.left; x <= r.right; x += 2) {
        if (gate_endpoint(c, { x, r.top }) || gate_endpoint(c, { x, r.bottom }))
            return false;
        sample({ x, r.top }, top, 17, x == r.left || x == r.right);
        sample({ x, r.bottom }, bottom, 17, x == r.left || x == r.right);
    }
    for (int y = r.top; y <= r.bottom; y += 2) {
        if (gate_endpoint(c, { r.left, y }) || gate_endpoint(c, { r.right, y }))
            return false;
        sample({ r.left, y }, left, 68, y == r.top || y == r.bottom);
        sample({ r.right, y }, right, 68, y == r.top || y == r.bottom);
    }
    auto top_count = top.straight_count;
    auto bottom_count = bottom.straight_count;
    auto left_count = left.straight_count;
    auto right_count = right.straight_count;
    const bool start_hoarding = track(c, cell_id(first)).base.common.type == 25;
    const bool end_hoarding = track(c, cell_id(second)).base.common.type == 25;
    if (top.other_count > 1)
        start_hoarding ? --bottom_count : ++top_count;
    if (left.other_count > 1)
        start_hoarding ? --right_count : ++left_count;
    if (bottom.other_count > 1) {
        ++bottom_count;
        if (end_hoarding) {
            --top_count;
            --bottom_count;
        }
    }
    if (right.other_count > 1) {
        ++right_count;
        if (end_hoarding) {
            --left_count;
            --right_count;
        }
    }
    if (corner_hoardings == 2 && !top_count && !bottom_count && !left_count && !right_count)
        return false;
    bool owned = false, unowned = false;
    for (int y = r.top; y <= r.bottom; ++y)
        for (int x = r.left; x <= r.right; ++x) {
            const auto& root = resolved_track(c, { x, y });
            const auto& cell = map(c, cell_id({ x, y })).base;
            const bool available = !(cell.common.flags & 64u) || root.common.type == 25;
            owned |= available;
            unowned |= !available;
        }
    //Native677220 only enters its connectivity raster pass for a mixed area or
    //at least two hoarding intersections on one edge. An isolated rectangle in
    //wholly unavailable land cannot create an independent park island.
    if (!(owned && unowned) && top.hoardings < 2 && bottom.hoardings < 2 && left.hoardings < 2 && right.hoardings < 2)
        return false;
    return has_no_closed_unowned_component(c, r);
}

void ParkConstructionRuntime::initialize_land_editor_boundary(ConstructionWorld& c,
                                                              LandBoundarySource boundary_source) {
    using namespace construction_detail;
    bind_land_boundary_anchors(c);
    if (track(c, cell_id(boundary_start(c))).base.common.type == 25
        && track(c, cell_id(boundary_target(c))).base.common.type == 25) {
        state.hoarding_origin = boundary_start(c);
        sync_land_boundary_meshes(c);
        return; //Retain a loaded/edited boundary rather than replaying its source.
    }
    const auto name = boundary_source == LandBoundarySource::installed_park ? "Hoardings.sam" : "cem_Hoardings.sam";
    const auto path = std::filesystem::path{ build_selected_resource_levels_path(c) } / name;
    const auto source = c.ports.assets.read_balance_asset(c, path.native_encoded_string());
    if (!source)
        throw std::logic_error(std::string{ "land editor boundary is missing: " } + name);
    HoardingClicks clicks{};
    if (!sam::parse_record(clicks, *source))
        throw std::logic_error(std::string{ "land editor boundary is malformed: " } + name);
    std::vector<WorldCellCoordinates> points;
    const auto start = boundary_start(c);
    for (std::size_t i = 0; i < clicks.count && i < clicks.points.size(); ++i) {
        const auto& point = clicks.points[i];
        if (point.x == 512)
            break;
        const WorldCellCoordinates p{ start.x + point.x, start.y + point.y };
        if (!bounds(p) || ((p.x - start.x) % 2) || ((p.y - start.y) % 2))
            throw std::logic_error("land editor boundary is outside its two-cell grid");
        points.push_back(p);
    }
    if (points.size() < 2 || !same(points.front(), start) || !same(points.back(), boundary_target(c)))
        throw std::logic_error("land editor boundary does not meet entrance anchors");
    state.hoarding_origin = start;
    c.construction.world_hoarding_mode = 1;
    RestoreValue reset{ c.construction.world_hoarding_mode };
    reset.saved = 0;
    set_mode(c, 25);
    for (const auto p : points)
        (void)dispatch(c, p, WorldInputModifierMask::none);
    set_hoarding_chain_nomodify(c, 1);
    apply_land_boundary_ownership(c);
    set_hoarding_chain_nomodify(c, 0);
    trace_land_boundary(c, true);
    set_mode(c, 0);
    for (int x = c.construction.hoarding_target_x + 2; x < c.construction.hoarding_chain_start_x; ++x)
        for (int y = c.construction.hoarding_chain_start_y; y <= c.construction.hoarding_chain_start_y + 1; ++y) {
            auto& cell = map(c, cell_id({ x, y })).base;
            clear_world_cell_flags_mask(&cell, 64);
            refresh_world_cell_render_state(c, &cell);
        }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    sync_land_boundary_meshes(c);
}

bool ParkConstructionRuntime::expand_land_rectangle(ConstructionWorld& c,
                                                    WorldCellCoordinates first,
                                                    WorldCellCoordinates second) {
    using namespace construction_detail;
    bind_land_boundary_anchors(c);
    first = snap_land_boundary_corner(c, first);
    second = snap_land_boundary_corner(c, second);
    if (!validate_land_rectangle(c, first, second))
        return false;
    if (track(c, cell_id(boundary_start(c))).base.common.type != 25)
        throw std::logic_error("land editor boundary has not been initialized");
    const Rectangle r{ first, second };
    state.hoarding_origin = boundary_start(c);
    c.construction.world_hoarding_mode = 1;
    RestoreValue reset{ c.construction.world_hoarding_mode };
    reset.saved = 0;
    set_mode(c, 25);
    for (const auto p : std::array<WorldCellCoordinates, 5>{ { { r.left, r.top },
                                                               { r.right, r.top },
                                                               { r.right, r.bottom },
                                                               { r.left, r.bottom },
                                                               { r.left, r.top } } })
        (void)dispatch(c, p, WorldInputModifierMask::none);
    set_mode(c, 0);
    c.staff.interaction.cursor_presentation = 1;
    trace_land_boundary(c, true);
    set_hoarding_chain_nomodify(c, 1);
    apply_land_boundary_ownership(c);
    set_hoarding_chain_nomodify(c, 0);
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    sync_land_boundary_meshes(c);
    //Native677220 leaves the interaction cost at zero. Keep the bank callback,
    //including its statistics/event side effects, instead of omitting debit0.
    charge_world_bank_account_expense(c, &bank(c), state.cost);
    return true;
}
} //namespace simcoaster
