#include "park_construction_internal.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world_park_admission.hpp"

namespace simcoaster {
bool ParkConstructionRuntime::demolish(ConstructionWorld& c, WorldCellCoordinates input, bool from_track_update) {
    using namespace construction_detail;
    if (!bounds(input))
        return false;
    auto& selected = map(c, cell_id(input));
    //Native666C40 rejects empty/path/track surface cells even if their map
    //linked list already contains a newly constructed Type3 payload. Grid
    //commit allocates that payload before installing its physical footprint.
    if (!footprint(selected.base.common.type) && !selected.is_obstacle)
        return false;
    auto* owner = object_in(c, selected.base);
    if (!owner || owner->base.header.retirement_pending)
        return false;
    auto& d = definition(c, owner->definition_id);
    auto* attachment_parent = d.balance->AddOn.UpgradeType == 1 ? object(c, owner->upgrade_parent) : nullptr;
    const bool rebuild_parent =
        attachment_parent && !from_track_update && !attachment_parent->base.header.retirement_pending;
    if (rebuild_parent) {
        //Auxiliary nodes borrow the upgrade's mesh. Release vehicles and route
        //geometry before retiring that mesh, then rebuild from the surviving
        //physical track. A cell-driven deletion is already inside the caller's
        //topology transaction and must not rebuild halfway through that walk.
        prune_track_ride_dynamic_path(c, attachment_parent->track_ride_handle);
        attachment_parent->is_track_ride_valid = 0;
    }
    auto* grid = d.balance->Info.Shape.get();
    if (!grid)
        throw std::logic_error("demolished object Shape is missing");
    const auto origin = coordinate(owner->top_left);
    const auto rotation = std::bit_cast<int>(owner->angle);
    const auto previous = state.previous_mode;
    const auto percent = get_park_object_scrap_value_percent(c, owner);
    if (d.balance->Info.HasQueue) {
        trace_queue(c, *owner);
        set_mode(c, 3);
        {
            RestoreValue force{ c.construction.force_cell_removal };
            c.construction.force_cell_removal = 1;
            if (std::bit_cast<int>(owner->queue_size_in_cells) <= 0)
                state.path_points.clear();
            else {
                remove_traced_path(c, false);
                (void)charge_world_bank_account_expense(c,
                                                        &bank(c),
                                                        multiply_integer(percent, c.construction.queue_cell_base_cost)
                                                            / 100);
            }
        }
        const auto selector = d.which_track_type;
        if (selector) {
            int doubled = 0;
            if (selector == 1 || selector == 2) {
                for (bool opposite : std::array{ false, true }) {
                    trace_track(c, *owner, opposite);
                    set_mode(c, selector == 1 ? 11 : 16);
                    RestoreValue anchor{ c.construction.track_anchor_removal_enabled };
                    c.construction.track_anchor_removal_enabled = 1;
                    remove_traced_path(c, true);
                }
                doubled = multiply_integer(2,
                                           selector == 1 ? c.construction.kart_track_cell_base_cost
                                                         : c.construction.water_track_cell_base_cost);
            }
            (void)charge_world_bank_account_expense(c, &bank(c), multiply_integer(percent, doubled) / 100);
        }
    }
    if (!from_track_update)
        set_mode(c, previous);
    bool clear_path = false;
    if (d.which_ui_type != ObjectUiCategory::features) {
        clear_path = has_clear_entry_exit_path(c, *owner);
        if (!clear_path) {
            auto& exit = map(c, owner->exit_cell).base;
            const auto direction = owner->exit_cell == owner->entry_cell
                                     ? opposite_world_cell_direction(get_world_cell_direction(&exit))
                                     : get_world_cell_direction(&exit);
            auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &exit, direction);
            if (!neighbor)
                throw std::logic_error("demolition exit neighbor is missing");
            const auto mode = c.construction.mode;
            set_mode(c, 50);
            {
                RestoreValue force{ c.construction.force_cell_removal };
                c.construction.force_cell_removal = 1;
                (void)dispatch(c, coordinate(neighbor->base.index), WorldInputModifierMask::none);
            }
            set_mode(c, mode);
        }
    }
    const auto visit = [&](auto&& function) {
        for (int row = 0; row < grid->height; ++row)
            for (int column = 0; column < grid->width; ++column) {
                if (!grid->cells.at(static_cast<std::size_t>(row) * 40 + static_cast<std::size_t>(column)).code)
                    continue;
                const auto offset = rotate_park_object_local_cell_offset({ column, row }, rotation);
                const WorldCellCoordinates p{ origin.x + offset.x, origin.y + offset.y };
                if (bounds(p))
                    function(map(c, cell_id(p)));
            }
    };
    int count = 0;
    visit([&](MapCell& cell) {
        if (cell.base.common.type == 4)
            ++count;
        if (!clear_path || (cell.base.common.type != 9 && cell.base.common.type != 10))
            return;
        const auto direction = cell.base.common.type == 9
                                 ? opposite_world_cell_direction(get_world_cell_direction(&cell.base))
                                 : get_world_cell_direction(&cell.base);
        auto* adjacent = resolve_world_cell_from_selector_flag_byte(c, &cell.base, direction);
        if (!adjacent || adjacent->base.common.type != 1 || !(adjacent->base.common.flags & 32u))
            return;
        int endpoints = 0;
        for (auto selector : std::array<std::uint8_t, 4>{ 1, 4, 16, 64 })
            if (get_world_cell_neighbor_mask(c, &adjacent->base) & selector) {
                auto* endpoint = resolve_world_cell_from_selector_flag_byte(c, &adjacent->base, selector);
                if (endpoint && (endpoint->base.common.type == 9 || endpoint->base.common.type == 10))
                    ++endpoints;
            }
        if (endpoints <= 1 && !(adjacent->status_flags & 8u))
            clear_world_cell_flags(&adjacent->base, 32);
    });
    visit([&](MapCell& cell) {
        clear_world_cell_flags(&cell.base, 32);
        remove_map_cell_contents(c, &cell.base, 0, 0);
        auto& paired = track(c, cell.base.index);
        if (from_track_update)
            clear_world_cell_flags(&paired.base, 16);
        else
            remove_track_cell_contents(c, &paired.base, 0, 0, 1);
    });
    if (!from_track_update && owner->track_ride_handle)
        destroy_track_ride(c, owner->track_ride_handle);
    //A rotated footprint can visit a track child before its root's map cell.
    //Removing that child redirects through the flag16 root and reenters this
    //operation with from_track_update, completing retirement while this outer
    //frame is still clearing the footprint. Queue the payload only once: the
    //generic native retirement operation intentionally does not deduplicate.
    if (!owner->base.header.retirement_pending)
        (void)retire_world_used_thing(c, &owner->base.header);
    if (rebuild_parent) {
        select_parent_origin(c, state, *attachment_parent);
        attachment_parent->is_track_ride_valid = refresh_track_path(c);
        animate_track_endpoint(c, *attachment_parent, false, attachment_parent->is_track_ride_valid != 0);
        if (attachment_parent->is_track_ride_valid) {
            if (is_park_object_open_eligible(c, *attachment_parent))
                open_park_object(c, *attachment_parent);
        } else
            animate_track_endpoint(c, *attachment_parent, true, false);
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    if (!c.construction.action_recording_disabled) {
        const auto sound = count < 4 ? 150u : count < 8 ? 151u : count < 16 ? 152u : 153u;
        (void)submit_positioned_template_request(c,
                                                 0,
                                                 c.resources.ui_catalog_slot,
                                                 sound,
                                                 grid->width * 10,
                                                 0,
                                                 grid->height * 10);
    }
    return true;
}
} //namespace simcoaster
