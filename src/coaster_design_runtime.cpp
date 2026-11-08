#include "coaster_editor_internal.hpp"
#include "simcoaster/coaster_collision.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world_research.hpp"
#include <limits>

namespace simcoaster {
namespace {
using namespace coaster_editor_detail;

void close_track_raw(EditTransaction& edit, std::size_t r, bool close) {
    auto& binding = edit.world.bindings.at(r);
    const bool was_closed = (binding.train_state.placement_flags & 1u) != 0;
    if (was_closed == close)
        return;
    if (close) {
        require(binding.geometry_state.size() >= 3);
        const auto point = profile_position(edit.meshes, edit.world, binding.geometry_state[0]);
        require((binding.geometry.back().geometry_flags & 0xC0u) != 0xC0u, binding.geometry.size() - 1);
        CoasterPlacement owner;
        owner.flags = 19;
        set_explicit_owner_position(edit.context, owner, point);
        const auto index = binding.geometry.size();
        owner.links.push_back(CoasterNodeReference{ r, index });
        const auto owner_id = edit.world.placements.size();
        edit.world.placements.push_back(std::move(owner));
        CoasterGeometryObjectInput input;
        input.geometry_index = static_cast<std::uint32_t>(index);
        input.geometry_flags = 0x180000u;
        input.inline_position = point;
        binding.geometry.push_back(std::move(input));
        binding.geometry_state.push_back({ owner_id, 0, {}, {}, {} });
        insert_coaster_geometry_peer_node(binding, index);
        binding.train_state.placement_flags |= 1u;
        mark(binding, index);
    } else {
        require(!binding.geometry.empty() && (binding.geometry.back().geometry_flags & 0x100000u));
        erase_node_raw(edit, r, binding.geometry.size() - 1);
        binding.train_state.placement_flags &= ~1u;
    }
}

std::array<std::int32_t, 2> rotate_offset(std::int32_t x, std::int32_t y, std::int32_t angle) {
    match (angle) {
        case 0 => return { x, y };
        case 90 => return { y, -x };
        case 180 => return { -x, -y };
        case 270 => return { -y, x };
        case _ => {
            require(false);
            return {};
        }
    }
}
} //namespace

std::expected<void, MeshError>
set_coaster_track_closed(AttractionWorld& c, WorldMeshRuntime& meshes, std::uint32_t map, bool close) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        const auto flags = edit.world.bindings[r].train_state.placement_flags;
        if (((flags & 1u) != 0) == close && !(close && (flags & 2u))) {
            edit.commit();
            return {};
        }
        close_track_raw(edit, r, close);
        auto& binding = edit.world.bindings[r];
        //475220 ends the active editing phase before transitioning to vehicles.
        //COS import48FF00 only builds the open graph; this explicit command owns
        //the support/closing node and clears the construction-selection latch.
        if (close)
            binding.train_state.placement_flags &= ~2u;
        finish(edit, r);
        edit.commit();
        //Native editor entry48CEA0 first transitions an operating closed track to
        //vehicles-only, releasing passenger head ownership and moving queueA toB.
        //Removing the closing support then permits the inactive construction mode.
        if (!close && (binding.train_state.detail_state_flags & 0xCu))
            transition_coaster_detail_state(c, meshes, binding, RideDetailState::vehicles_only);
        transition_coaster_detail_state(c,
                                        meshes,
                                        binding,
                                        close ? RideDetailState::vehicles_only : RideDetailState::inactive);
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError>
import_coaster_design(AttractionWorld& c, WorldMeshRuntime& meshes, std::uint32_t map, const CoasterDesign& design) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings[r];
        const auto* object = binding.park_object;
        require(object != nullptr);
        //The native reader runs in the already opened construction context. A
        //caller replacing an operating ride must explicitly open it first so its
        //passenger queues/resources follow475570's normal editing transition.
        require(!(binding.train_state.placement_flags & 1u) && (binding.train_state.detail_state_flags & 0xFu) == 1u,
                map);
        require(design.geometry.size() <= std::numeric_limits<std::uint16_t>::max() - binding.geometry.size());
        for (std::size_t i = 0; i < design.geometry.size(); ++i) {
            const auto& geometry = design.geometry[i];
            //A difference between two valid128-cell map coordinates is bounded by
            //127. Check before cardinal negation/addition, avoiding signed overflow
            //from an unchecked external COS while retaining all native-valid tiles.
            require(geometry.tile_offset_x >= -127
                        && geometry.tile_offset_x <= 127
                        && geometry.tile_offset_y >= -127
                        && geometry.tile_offset_y <= 127,
                    i);
            require(std::isfinite(geometry.loft + geometry.heading + geometry.secondary_parameter + geometry.angle_b),
                    i);
        }
        //The caller's station/World object supplies origin and rotation. Native
        //48FF00 consumes the COS source/definition/grid prelude without applying
        //it. Empty existing designs are required by the import workflow;
        //replacement uses the same owned-node removal path before applying the new
        //design.
        for (std::size_t i = binding.geometry.size(); i > 0; --i)
            if (!(edit.world.placements[binding.geometry_state[i - 1].placement].flags & 0x10u))
                erase_node_raw(edit, r, i - 1);
        const auto origin_x = static_cast<std::int32_t>(object->base.x >> 8),
                   origin_y = static_cast<std::int32_t>(object->base.y >> 8);
        const auto angle = static_cast<std::int32_t>(object->angle);
        std::vector<std::size_t> created;
        created.reserve(design.geometry.size());
        unsigned remaining = 0;
        //Preserve the native two-pass order: all geometry/stack meshes exist
        //before serialized link slots and animation parameters are installed.
        for (const auto& source : design.geometry) {
            if (source.starts_two_piece && !remaining && binding.definition->pylon_templates[1])
                remaining = 2;
            const auto offset = rotate_offset(source.tile_offset_x, source.tile_offset_y, angle);
            CoasterNodePlacement placement;
            placement.tile_x = origin_x + offset[0];
            placement.tile_y = origin_y + offset[1];
            placement.pylon = remaining ? 1u : 0u;
            placement.second_piece = remaining == 1;
            placement.charge_cost = false;
            created.push_back(place_node_raw(edit, r, placement, false));
            if (remaining)
                --remaining;
        }
        for (std::size_t i = 0; i < design.geometry.size(); ++i) {
            const auto& source = design.geometry[i];
            const auto index = created[i];
            auto& input = binding.geometry[index];
            auto& local = binding.geometry_state[index];
            auto& owner = edit.world.placements[local.placement];
            require(source.link_slot < owner.links.size(), source.link_slot);
            const auto current = level(owner, { r, index });
            if (current != source.link_slot) {
                const auto other = owner.links[source.link_slot];
                require(other.has_value());
                std::swap(local.controls[0], state(edit.world, *other).controls[0]);
                std::swap(owner.links[current], owner.links[source.link_slot]);
            }
            //4906B7..744 applies only these persistent state bits, then always
            //clears 200000. Import forces the three manual parameter flags before
            //setters.
            input.geometry_flags = (input.geometry_flags & ~0x200000u) | (source.flags & 0x4C0007u) | 7u;
            local.controls[0] = source.loft;
            local.angles[3] =
                static_cast<float>(static_cast<double>(3.1415927f) * angle * 0.005555555555555556 + source.heading);
            if (input.geometry_flags & 0x40u) {
                local.angles[1] = local.angles[2] = 0;
                local.controls[4] = source.secondary_parameter;
            } else {
                local.angles[1] = source.secondary_parameter;
                local.angles[2] = source.angle_b;
                local.controls[4] = 0;
            }
            if (i + 1 == design.geometry.size() && !(pylon(owner).flags & 0x4000u)) {
                const auto& data = pylon(owner);
                binding.default_attachment_height =
                    static_cast<float>((static_cast<double>(data.height_maximum) - data.height_minimum)
                                           * std::clamp(source.loft, 0.0f, 1.0f)
                                       + data.height_minimum);
            }
            //Serialized peer ordinals are intentionally not installed as pointers;
            //finalization rebuilds them from the actual transformed track geometry.
        }
        //490939 explicitly invokes475900, publishing samples, train selection and
        //initial car poses immediately at the current clock. Support completion
        //475220 is a separate World/editor event; the imported owner stays open
        //and inactive until that command initializes the selected train resources.
        finish(edit, r);
        edit.commit();
        return update_coaster_animation(c, meshes);
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<CoasterDesign, MeshError>
export_coaster_design(AttractionWorld& c, const WorldMeshRuntime& meshes, std::uint32_t map) {
    using namespace coaster_editor_detail;
    try {
        const auto* world = meshes.coasters();
        require(world != nullptr);
        const auto* binding = world->find_map(map);
        require(binding && binding->park_object, map);
        const auto& object = *binding->park_object;
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, object.definition_id);
        require(definition && definition->balance && definition->balance->Info.Shape);
        const auto& shape = *definition->balance->Info.Shape;
        const auto origin_x = static_cast<std::int32_t>(object.base.x >> 8),
                   origin_y = static_cast<std::int32_t>(object.base.y >> 8);
        auto live_snapshot = binding->saved_geometry;
        capture_coaster_geometry_peer_snapshot(*binding, live_snapshot);
        CoasterDesign design;
        design.source_index = static_cast<std::uint32_t>(c.ports.assets.selected_resource_source(c));
        design.definition_id = object.definition_id;
        design.build_cost = static_cast<std::uint32_t>(definition->balance->Upgrades[0].CostOfUpgrade);
        for (std::size_t i = 0; i < 33; ++i) {
            design.name_a[i] = static_cast<std::uint16_t>(object.name_a[i]);
            design.name_b[i] = static_cast<std::uint16_t>(object.name_b[i]);
        }
        std::int32_t minimum_x = 0, minimum_y = 0, maximum_x = 0, maximum_y = 0;
        for (std::size_t i = 0; i < binding->geometry.size(); ++i) {
            const auto& local = binding->geometry_state[i];
            const auto& owner = world->placements[local.placement];
            if (owner.flags & 0x10u)
                continue;
            const auto& input = binding->geometry[i];
            CoasterDesignGeometry geometry;
            geometry.tile_offset_x = owner.tile_x - origin_x;
            geometry.tile_offset_y = owner.tile_y - origin_y;
            geometry.flags = input.geometry_flags;
            geometry.starts_two_piece = (input.geometry_flags & 0x80u) != 0;
            geometry.axis_mode = (input.geometry_flags & 0x40u) != 0;
            geometry.link_slot = static_cast<std::uint32_t>(
                level(owner, { static_cast<std::size_t>(binding - world->bindings.data()), i }));
            geometry.heading = local.angles[3];
            geometry.loft = local.controls[0];
            geometry.secondary_parameter = geometry.axis_mode ? local.controls[4] : local.angles[1];
            geometry.angle_b = local.angles[2];
            //Project live ordered peers, including changes from the frame rebuild,
            //rather than treating a previous save/import snapshot as current state.
            if (local.saved_ordinal)
                for (const auto peer : live_snapshot.geometry.at(*local.saved_ordinal).peers)
                    geometry.peers.push_back(peer);
            minimum_x = std::min(minimum_x, geometry.tile_offset_x);
            minimum_y = std::min(minimum_y, geometry.tile_offset_y);
            maximum_x = std::max(maximum_x, geometry.tile_offset_x);
            maximum_y = std::max(maximum_y, geometry.tile_offset_y);
            design.build_cost += static_cast<std::uint32_t>(static_cast<std::int64_t>(
                piece_cost(*world, static_cast<std::size_t>(binding - world->bindings.data()), i)));
            design.geometry.push_back(std::move(geometry));
        }
        design.placement_offset_x = -minimum_x;
        design.placement_offset_y = -minimum_y;
        const auto width = std::max(maximum_x - minimum_x + 1, shape.width - minimum_x),
                   height = std::max(maximum_y - minimum_y + 1, shape.height - minimum_y);
        require(width > 0 && width <= 40 && height > 0 && height <= 40);
        design.grid_width = static_cast<std::uint32_t>(width);
        design.grid_height = static_cast<std::uint32_t>(height);
        for (const auto& geometry : design.geometry)
            design.grid
                .at(static_cast<std::size_t>(
                    (geometry.tile_offset_y - minimum_y) * 40 + geometry.tile_offset_x - minimum_x))
                .code = 4;
        for (std::int32_t y = 0; y < shape.height; ++y)
            for (std::int32_t x = 0; x < shape.width; ++x) {
                const auto& source = shape.cells.at(static_cast<std::size_t>(y * 40 + x));
                auto& cell = design.grid.at(static_cast<std::size_t>((y - minimum_y) * 40 + x - minimum_x));
                cell.code = static_cast<std::uint32_t>(source.code);
                cell.attribute = source.attribute;
            }
        return design;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
