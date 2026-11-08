#include "coaster_editor_internal.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include <optional>

namespace simcoaster {
namespace {
using namespace coaster_editor_detail;

ParkObjectRuntimeView& parent(AttractionWorld& c, ThingId id) {
    auto* object = construction_detail::object(c, id);
    require(object && !object->base.header.retirement_pending, id);
    return *object;
}

std::uint32_t map_id(AttractionWorld& c, ThingId id) {
    auto& object = parent(c, id);
    auto* coaster = runtime(*c.resources.meshes).find(object.mesh_instance_id);
    require(coaster && coaster->park_object == &object, id);
    return coaster->map_id;
}

void install_cell(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates p) {
    require(construction_detail::bounds(p));
    if (editor.state.path_points.size() <= 1024) {
        editor.state.path_points.push_back(p);
    }
    for (const auto operation : std::array { 135u, 21u, 131u, 132u, 129u }) {
        (void)editor.command_line(c, operation, p, p);
    }
}

void remember_mesh(AttractionWorld& c, std::uint32_t map, std::size_t ordinal, WorldCellCoordinates tile) {
    auto* binding = runtime(*c.resources.meshes).find_map(map);
    require(binding != nullptr, map);
    construction_detail::track(c, construction_detail::cell_id(tile)).base.common.mesh_instance = binding->geometry_state.at(ordinal).mesh_slot;
}

void bind(AttractionWorld& c, ParkConstructionRuntime& editor, ThingId id, std::uint32_t map) {
    require(c.resources.meshes && c.construction.editor);
    auto& owner = parent(c, id);
    c.view.secondary_cursor_record_snapshot = &owner.base.header;
    editor.refresh_origin(c, true);
    editor.set_mode(c, 21, false);
    c.construction.editor->bind_catalog_context(c, map);
}
} //namespace

void build_default_coaster_catalog_layout(AttractionWorld& c, ParkConstructionRuntime& editor, ThingId id) {
    using namespace coaster_editor_detail;
    auto& owner = parent(c, id);
    const auto& balance = get_park_object_balance(c, owner);
    require(balance.DefaultCoaster.CoasterPlacementShape != nullptr, id);
    const auto& shape = *balance.DefaultCoaster.CoasterPlacementShape;
    const auto origin = construction_detail::coordinate(owner.top_left);
    std::optional<WorldCellCoordinates> first, second;
    for (int row = 0; row < shape.height; ++row) {
        for (int column = 0; column < shape.width; ++column) {
            const auto& cell = shape.cells.at(static_cast<std::size_t>(row * 40 + column));
            if (cell.code != 27) {
                continue;
            }
            const auto offset = rotate_park_object_local_cell_offset({ column - balance.DefaultCoaster.OffsetX, row + balance.DefaultCoaster.OffsetY }, std::bit_cast<std::int32_t>(owner.angle));
            const WorldCellCoordinates p { origin.x + offset.x, origin.y + offset.y };
            require(construction_detail::bounds(p));
            if (cell.attribute == 4) {
                first = p;
            } else {
                second = p; //Native last marker wins.
            }
        }
    }
    const auto map = map_id(c, id);
    bind(c, editor, id, map);
    std::optional<std::size_t> a, b;
    std::optional<std::uint32_t> first_mesh, second_mesh;
    for (unsigned marker_index = 0; marker_index < 2; ++marker_index) {
        const auto marker = marker_index ? second : first;
        if (!marker) {
            continue;
        }
        install_cell(c, editor, *marker);
        EditTransaction edit { c, *c.resources.meshes };
        const auto r = ride_index(edit.world, map);
        CoasterNodePlacement placement;
        placement.tile_x = marker->x;
        placement.tile_y = marker->y;
        placement.charge_cost = false;
        const auto index = place_node_raw(edit, r, placement, false);
        const auto slot = edit.world.bindings.at(r).geometry_state.at(index).mesh_slot;
        finish(edit, r);
        edit.commit();
        remember_mesh(c, map, index, *marker);
        if (!marker_index) {
            a = index;
            first_mesh = slot;
        } else {
            b = index;
            second_mesh = slot;
        }
    }
    auto updated = update_coaster_animation(c, *c.resources.meshes);
    if (!updated) {
        throw updated.error();
    }
    c.construction.editor->complete_supports(c);
    c.construction.editor->bind_catalog_context(c, map);
    c.construction.editor->delete_range(c, static_cast<std::uint16_t>(a.value_or(2)), static_cast<std::uint16_t>(b.value_or(0)), false);
    {
        EditTransaction edit { c, *c.resources.meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings.at(r);
        for (const auto slot : std::array { first_mesh, second_mesh }) {
            if (slot) {
                const auto found = std::ranges::find(binding.geometry_state, *slot, &CoasterNodeState::mesh_slot);
                if (found != binding.geometry_state.end()) {
                    mark(binding, static_cast<std::size_t>(found - binding.geometry_state.begin()));
                }
            }
        }
        mark(binding, 0);
        mark(binding, 2);
        finish(edit, r);
        edit.commit();
    }
    updated = update_coaster_animation(c, *c.resources.meshes);
    if (!updated) {
        throw updated.error();
    }
    c.construction.editor->clear_catalog_context(c);
}

void build_predefined_coaster_catalog_layout(AttractionWorld& c, ParkConstructionRuntime& editor, ThingId id, const PredefinedCoasterBalance& balance) {
    using namespace coaster_editor_detail;
    const auto count = balance.sentinel_count();
    const auto& object = parent(c, id);
    const WorldCellCoordinates origin { object.base.x >> 8, object.base.y >> 8 };
    const auto map = map_id(c, id);
    bind(c, editor, id, map);
    std::vector<std::size_t> created;
    created.reserve(count);
    unsigned remaining = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& row = balance.CoasterPylons[i];
        const WorldCellCoordinates p { origin.x + row.X, origin.y + row.Y };
        install_cell(c, editor, p);
        //Cell135 may retire an older owner and compact its meshes/bindings.
        //Capture the geometry transaction only after those World side effects.
        EditTransaction edit { c, *c.resources.meshes };
        const auto r = ride_index(edit.world, map);
        auto& ride = edit.world.bindings.at(r);
        if (row.IsFirstSpecial && !remaining && ride.definition->balance.pylons[1].pcMeshFilename.has_value()) {
            remaining = 2;
        }
        CoasterNodePlacement placement;
        placement.tile_x = p.x;
        placement.tile_y = p.y;
        placement.pylon = remaining ? 1u : 0u;
        placement.second_piece = remaining == 1;
        placement.charge_cost = false;
        created.push_back(place_node_raw(edit, r, placement, false));
        if (remaining) {
            --remaining;
        }
        edit.commit();
    }
    EditTransaction edit { c, *c.resources.meshes };
    const auto r = ride_index(edit.world, map);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& row = balance.CoasterPylons[i];
        const auto index = created[i];
        auto& ride = edit.world.bindings.at(r);
        auto& local = ride.geometry_state.at(index);
        auto& support = edit.world.placements.at(local.placement);
        const auto desired = static_cast<std::size_t>(row.Level);
        require(desired < support.links.size(), i);
        auto current = level(support, { r, index });
        //Native swaps adjacent levels, retaining their absolute loft ownership.
        while (current != desired) {
            const auto adjacent = current < desired ? current + 1 : current - 1;
            const auto other = support.links.at(adjacent);
            require(other.has_value(), i);
            std::swap(local.controls[0], state(edit.world, *other).controls[0]);
            std::swap(support.links[current], support.links[adjacent]);
            current = adjacent;
        }
        auto& input = ride.geometry.at(index);
        input.geometry_flags |= 7u;
        local.controls[0] = row.Loft;
        local.angles[3] = row.Rotate;
        if (input.geometry_flags & 0x40u) {
            local.angles[1] = local.angles[2] = 0;
            local.controls[4] = row.Wobble;
        } else {
            local.angles[1] = row.Wobble;
            local.angles[2] = row.IsSpecial ? 0 : row.Pitch;
            local.controls[4] = 0;
        }
        input.geometry_flags |= 0x200000u;
        mark_owner(edit.world, support);
        remember_mesh(c, map, index, { origin.x + row.X, origin.y + row.Y });
    }
    finish(edit, r);
    edit.commit();
    c.construction.editor->clear_catalog_context(c);
}

void build_imported_coaster_catalog_layout(AttractionWorld& c, ParkConstructionRuntime& editor, ThingId id, const CoasterDesign& design) {
    const auto& object = parent(c, id);
    const WorldCellCoordinates origin { object.base.x >> 8, object.base.y >> 8 };
    const auto rotation = std::bit_cast<std::int32_t>(object.angle);
    const auto map = map_id(c, id);
    bind(c, editor, id, map);
    //Native4903A7..490450 installs the real World support cells before each
    //pylon. Complete that destructive ownership phase before the core's mesh
    //transaction; rollback must retain retirement of overwritten older owners.
    for (const auto& row : design.geometry) {
        install_cell(c, editor, [&] {
            const auto offset = rotate_park_object_local_cell_offset({ row.tile_offset_x, row.tile_offset_y }, rotation);
            return WorldCellCoordinates { origin.x + offset.x, origin.y + offset.y };
        }());
    }
    const auto imported = import_coaster_design(c, *c.resources.meshes, map, design);
    if (!imported) {
        throw imported.error();
    }
    const auto* ride = runtime(*c.resources.meshes).find_map(map);
    require(ride != nullptr, map);
    for (std::size_t i = 0; i < ride->geometry_state.size(); ++i) {
        const auto& owner = runtime(*c.resources.meshes).placements.at(ride->geometry_state[i].placement);
        if (!(owner.flags & 0x10u)) {
            remember_mesh(c, map, i, { owner.tile_x, owner.tile_y });
        }
    }
}

void world_operations::builtin::complete_prebuilt_coaster_catalog(WorldRuntime& c, ParkObjectRuntimeView& object) {
    using namespace coaster_editor_detail;
    require(c.resources.meshes != nullptr);
    auto* binding = runtime(*c.resources.meshes).find(object.mesh_instance_id);
    require(binding && binding->park_object == &object, object.base.header.id);
    const auto map = binding->map_id;
    if (c.construction.editor && c.construction.editor->active_map() == map) {
        c.construction.editor->complete_supports(c);
    } else {
        const auto closed = set_coaster_track_closed(c, *c.resources.meshes, map, true);
        if (!closed) {
            throw closed.error();
        }
    }
}
} //namespace simcoaster
