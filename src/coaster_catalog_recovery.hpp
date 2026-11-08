#pragma once
#include "simcoaster/world_domains_fwd.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/world_object_controls.hpp"
#include <bitset>
#include <cstring>
#include <map>
#include <set>

namespace simcoaster::construction_detail {
struct PlacementRecovery {
    std::unique_ptr<decltype(World::map_cells)> maps;
    std::unique_ptr<decltype(World::track_cells)> tracks;
    ObjectControlTable controls;
    BankRecord account;
    std::map<CellId, ThingId> owners;

    explicit PlacementRecovery(AttractionWorld& c)
        : maps(std::make_unique<decltype(World::map_cells)>(world(c).map_cells)),
          tracks(std::make_unique<decltype(World::track_cells)>(world(c).track_cells)),
          controls(world(c).object_controls),
          account(bank(c)) {
        for (auto* row = c.entities.active_head; row; row = row->active_next)
            if (row->payload && row->payload->kind == ThingKind::park_object) {
                const auto& owner = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
                owners.emplace(static_cast<CellId>((owner.base.y >> 8) * 128u + (owner.base.x >> 8) + 1u),
                               owner.base.header.id);
            }
    }

    void restore(AttractionWorld& c) const {
        std::bitset<map_cell_count> changed;
        std::set<std::uint32_t> meshes, borrowed;
        std::set<CellId> retired_origins;
        for (const auto& [origin, id] : owners)
            if (!object(c, id))
                retired_origins.insert(origin);
        for (auto* row = c.entities.active_head; row; row = row->active_next)
            if (row->payload && row->payload->kind == ThingKind::park_object)
                borrowed.insert(reinterpret_cast<const ParkObjectRuntimeView*>(row->payload)->mesh_instance_id);
        for (std::size_t index = 0; index < map_cell_count; ++index) {
            auto& current_map = world(c).map_cells[index];
            auto& current_track = world(c).track_cells[index];
            const auto& old_map = maps->at(index);
            const auto& old_track = tracks->at(index);
            if (std::memcmp(&current_map.base, &old_map.base, sizeof(CellBase)) == 0
                && std::memcmp(&current_track.base, &old_track.base, sizeof(CellBase)) == 0)
                continue;
            changed.set(index);
            if (current_map.base.common.type != 21) {
                meshes.insert(current_map.base.common.mesh_instance);
                meshes.insert(current_track.base.common.mesh_instance);
            }
            if (old_map.base.common.type != 21) {
                meshes.insert(old_map.base.common.mesh_instance);
                meshes.insert(old_track.base.common.mesh_instance);
            }
        }
        for (auto mesh : meshes)
            if (mesh && !borrowed.contains(mesh))
                world_operations::destroy_mesh(c, mesh);
        for (std::size_t index = 0; index < map_cell_count; ++index)
            if (changed.test(index)) {
                auto& current_map = world(c).map_cells[index];
                auto& current_track = world(c).track_cells[index];
                const auto& old_map = maps->at(index);
                const auto& old_track = tracks->at(index);
                if (!retired_origins.contains(old_map.base.common.parent_id)) {
                    current_map.base = old_map.base;
                    current_map.is_obstacle = old_map.is_obstacle;
                }
                if (!retired_origins.contains(old_track.base.common.parent_id))
                    current_track = old_track;
                //who/litter belong to live lifetime managers, not the placement cell
                //transaction. Preserve their current lists and resource handles.
                if (current_map.base.common.type != 21) {
                    current_map.base.common.mesh_instance = 0;
                    current_track.base.common.mesh_instance = 0;
                }
            }
        world(c).object_controls = controls;
        for (std::uint32_t index = 0; index < world(c).object_controls.count; ++index)
            world(c).object_controls.rows.at(index).live_instance_count = 0;
        for (auto* row = c.entities.active_head; row; row = row->active_next)
            if (row->payload && row->payload->kind == ThingKind::park_object) {
                const auto& owner = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
                auto* control = find_world_object_control(world(c).object_controls, owner.definition_id);
                if (control)
                    ++control->live_instance_count;
            }
        bank(c) = account;
        for (std::size_t index = 0; index < map_cell_count; ++index)
            if (changed.test(index)) {
                refresh_world_cell_render_state(c, &world(c).map_cells[index].base);
                update_world_cell_mesh(c, &world(c).track_cells[index].base);
            }
        c.ports.meshes.rebuild_terrain_cell_shading(c);
        world_operations::refresh_active_park_object_meshes(c);
    }
};
} //namespace simcoaster::construction_detail
