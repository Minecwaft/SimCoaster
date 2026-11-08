#include "park_construction_internal.hpp"
#include "simcoaster/world_person.hpp"
#include <bit>

namespace simcoaster {
namespace {
using namespace construction_detail;

bool special(ConstructionWorld& c, const ParkObjectRuntimeView* owner) {
    return owner && definition(c, owner->definition_id).balance->Info.IsSpecial == 1;
}

bool tracked_type(int type) {
    return (type >= 9 && type <= 14) || (type >= 16 && type <= 19) || type == 21;
}

bool rectangle_target_feedback(ConstructionWorld& c, WorldCellCoordinates first, WorldCellCoordinates last) {
    //Native66C390 deduplicates only against the FIRST footprint owner. A second
    //owner's subsequent cells each increment the count; do not replace this
    //with a set of all distinct owners.
    int count = 0;
    ThingId first_owner{};
    WorldCellCoordinates candidate = last;
    bool path_or_queue = false;
    for (int y = first.y; y <= last.y && count <= 2; ++y)
        for (int x = first.x; x <= last.x && count <= 2; ++x) {
            const auto& cell = map(c, cell_id({ x, y })).base;
            const auto type = cell.common.type;
            if (type == 1 || type == 3)
                path_or_queue = true;
            if (type == 4) {
                candidate = { x, y };
                const auto parent = cell.common.parent_id;
                const ThingId id = parent ? find_first_park_object_id_in_map_cell(c, &map(c, parent)) : ThingId{};
                if (!first_owner) {
                    first_owner = id;
                    ++count;
                } else if (first_owner != id)
                    ++count;
            } else if (type == 3 || tracked_type(type)) {
                candidate = { x, y };
                ++count;
            }
        }
    if (path_or_queue)
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 95, 0, 0, 0);
    if (count != 1)
        return false;
    const auto type = map(c, cell_id(candidate)).base.common.type;
    if (type == 4)
        return true;
    if (type == 3)
        world_operations::queue_scored_advisor_action(c, 638);
    else if ((type >= 11 && type <= 14) || (type >= 16 && type <= 19))
        world_operations::queue_scored_advisor_action(c, 637);
    return false;
}

bool
whole_footprint(ConstructionWorld& c, const CellBase& source, WorldCellCoordinates first, WorldCellCoordinates last) {
    const auto at = coordinate(source.index);
    const auto parent = source.common.parent_id;
    //Native66C754..66C92C also examines the first NONmatching cell of each
    //run. Its flag16 can reject the run even though its type/parent differs.
    const auto run = [&](int dx, int dy, int limit) {
        auto p = at;
        for (;;) {
            const auto& cell = map(c, cell_id(p)).base;
            if (cell.common.flags & 16u)
                return false;
            const int value = dx ? p.x : p.y;
            if (((dx + dy) > 0 && value > limit) || ((dx + dy) < 0 && value < limit))
                return false;
            const bool matching = footprint(cell.common.type) && cell.common.parent_id == parent;
            p.x += dx;
            p.y += dy;
            if (!matching || !bounds(p))
                return true;
        }
    };
    return run(1, 0, last.x + 1) && run(-1, 0, first.x - 1) && run(0, 1, last.y + 1) && run(0, -1, first.y - 1);
}

std::int16_t segment(const TrackCell& cell) {
    return std::bit_cast<std::int16_t>(cell.segment_number);
}
} //namespace

bool ParkConstructionRuntime::demolish_rectangle(ConstructionWorld& c, WorldCellCoordinates a, WorldCellCoordinates b) {
    using namespace construction_detail;
    if (!bounds(a) || !bounds(b))
        return false;
    const WorldCellCoordinates first{ std::min(a.x, b.x), std::min(a.y, b.y) },
        last{ std::max(a.x, b.x), std::max(a.y, b.y) };
    bool single_feedback = rectangle_target_feedback(c, first, last);
    bool changed = false, has_support = false;
    ThingId consistent{};
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x) {
            auto& cell = map(c, cell_id({ x, y })).base;
            if (footprint(cell.common.type)) {
                auto* owner = object_in(c, cell);
                if (whole_footprint(c, cell, first, last) && !special(c, owner)) {
                    (void)demolish(c, { x, y });
                    changed = true;
                } else if (!special(c, owner) && single_feedback) {
                    world_operations::queue_scored_advisor_action(c, 639);
                    single_feedback = false;
                }
            } else if (cell.common.type == 21) {
                const auto parent = cell.common.parent_id;
                const ThingId id = parent ? find_first_park_object_id_in_map_cell(c, &map(c, parent)) : ThingId{};
                if (!has_support) {
                    has_support = true;
                    consistent = id;
                } else if (consistent != id)
                    consistent = 0;
            }
        }
    //Native66CAB1 performs this second row-major pass only after owner removal.
    //Queue cells are deliberately excluded; only current type1 cells are erased.
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x) {
            auto& cell = map(c, cell_id({ x, y })).base;
            if (cell.common.type == 1) {
                (void)remove_map_cell_contents(c, &cell, 0, 0);
                changed = true;
            }
        }
    if (has_support && !changed && is_first_hour_of_gameplay(c))
        if (const auto* owner = object(c, consistent))
            world_operations::queue_scored_advisor_action(c, owner->can_load ? 643 : 636);
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    return true;
}

void ParkConstructionRuntime::preview_rectangle(ConstructionWorld& c, WorldCellCoordinates a, WorldCellCoordinates b) {
    const auto left = std::min(a.x, b.x), right = std::max(a.x, b.x), top = std::min(a.y, b.y),
               bottom = std::max(a.y, b.y);
    const int inset = left == right ? 0 : 1;
    //Native65F2A1..65F30C: horizontal interiors followed by both complete
    //vertical edges. Degenerate rectangles retain repeated endpoint dispatches.
    constexpr auto erase_preview = 256u | 58u;
    (void)command_line(c, erase_preview, { left + inset, top }, { right - inset, top });
    (void)command_line(c, erase_preview, { right, top }, { right, bottom });
    (void)command_line(c, erase_preview, { left + inset, bottom }, { right - inset, bottom });
    (void)command_line(c, erase_preview, { left, bottom }, { left, top });
}

bool ParkConstructionRuntime::preview_track_chain(ConstructionWorld& c, TrackCell& start, TrackCell& target) {
    return process_context_track_chain(c, start, target, false) >= 0;
}

bool ParkConstructionRuntime::preview_backtrack(ConstructionWorld& c, WorldCellCoordinates p) {
    using namespace construction_detail;
    const WorldCellCoordinates start{ c.construction.path_start_x, c.construction.path_start_y };
    if (!bounds(start))
        return true;
    const auto* owner = c.construction.park_object;
    const auto type = owner ? definition(c, owner->definition_id).which_track_type : 0;
    const auto& raw = track(c, cell_id(p)).base;
    const bool track_hover = world_cell_is_track_child(&raw) || world_cell_is_track_root(&root_track(c, raw).base);
    if ((state.backtrack_saved_mode == 11 || state.backtrack_saved_mode == 16)
        && type >= 1
        && type <= 3
        && track_hover) {
        auto& from = root_track(c, map(c, cell_id(start)).base);
        if (!track_variant(c, from.base)) {
            auto adjusted = start;
            adjust_track_endpoint(c, adjusted);
            if (bounds(adjusted) && segment(root_track(c, map(c, cell_id(adjusted)).base)) < segment(from))
                state.preview_coordinates = adjusted;
            state.track_preview.start = cell_id(start);
            state.track_preview.start_segment = segment(from);
            if (evaluate_track(c, from.base, 14) == TrackInteractionResult::rejected) {
                state.track_preview.start = 0;
                state.track_preview.start_segment = -1;
                return false;
            }
        }
        auto& end = root_track(c, map(c, cell_id(p)).base);
        state.track_preview.end = cell_id(p);
        state.track_preview.end_segment = segment(end);
        return preview_track_chain(c, end, from);
    }
    //Native snapshot674AF0 is a separate value owner. Preview never pops or
    //rewrites the construction history. Each reverse leg uses command314.
    const auto snapshot = state.path_points;
    if (snapshot.empty())
        return true;
    auto current = start;
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
        const auto target = *it;
        const auto sign = [](int value) {
            return (value > 0) - (value < 0);
        };
        const auto dx = sign(target.x - current.x), dy = sign(target.y - current.y);
        auto next = current;
        std::size_t steps = 0;
        while ((next.x != target.x || next.y != target.y) && (next.x != p.x || next.y != p.y)) {
            next.x += dx;
            next.y += dy;
            if (++steps > map_cell_count)
                throw std::length_error("invalid reverse construction path");
        }
        (void)command_line(c, 314u, current, next);
        current = next;
        if (current.x == p.x && current.y == p.y)
            break;
    }
    return true;
}
} //namespace simcoaster
