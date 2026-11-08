#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace simcoaster {
namespace {
void remove_map_id(WorldCoasterRuntime& world, std::uint32_t id) {
    //Original 4CBDF0 maintains the count of interior holes separately from the
    //next candidate cursor. In particular, trimming trailing holes does not
    //normalize that cursor; preserve the native saved allocator state.
    auto& count = world.registered_record_count;
    auto& holes = world.available_id_holes;
    auto& next = world.next_map_id;
    if (holes) {
        if (id == count + holes) {
            --count;
            while (holes && !world.find_map(count + holes)) {
                --holes;
            }
        } else {
            ++holes;
            --count;
            if (id < next) {
                next = id;
            }
        }
    } else {
        if (id == count) {
            --next;
        } else {
            holes = 1;
            next = id;
        }
        --count;
    }
}

void remove_placement_cell(AttractionWorld& c, const CoasterPlacement& placement) {
    //Native 4A8A80 clamps to the loaded map dimensions, then uses the ordinary
    //map and track removal contracts. Explicit-position station nodes have no
    //world-cell placement and are released with their geometry owner instead.
    if (!c.weather.map_heightfield_width || !c.weather.map_heightfield_height) {
        return;
    }
    const auto x = std::clamp(placement.tile_x, 0, static_cast<std::int32_t>(c.weather.map_heightfield_width - 1));
    const auto y = std::clamp(placement.tile_y, 0, static_cast<std::int32_t>(c.weather.map_heightfield_height - 1));
    if (!is_world_map_coordinate_in_bounds(x, y)) {
        return;
    }
    const auto id = static_cast<CellId>(y * 128 + x + 1);
    auto* cell = get_world_map_cell_record_by_id(c, id);
    auto* track = resolve_track_cell_for_grid_entry(c, &cell->base);
    remove_map_cell_contents(c, &cell->base, 0, 0);
    remove_track_cell_contents(c, &track->base, 0, 0, 1);
}
} //namespace

void WorldMeshRuntime::erase_coaster(AttractionWorld& c, std::uint32_t slot) {
    auto& world = *coasters_;
    const auto found = std::ranges::find(world.bindings, slot, &WorldCoasterBinding::mesh_slot);
    if (found == world.bindings.end()) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - world.bindings.begin());
    if (!found->map_id || !world.registered_record_count || found->map_id > world.registered_record_count + world.available_id_holes) {
        throw std::logic_error("invalid coaster map allocator during retirement");
    }
    //Validate ownership before the first destructive operation. Every allocated
    //car is owned even when it lies outside the active train/car prefix.
    std::set<std::uint32_t> owned;
    const auto own = [&](std::uint32_t child) {
        if (!child) {
            return;
        }
        if (child == slot || !instance(child)) {
            throw std::logic_error("invalid coaster child mesh during retirement");
        }
        owned.insert(child);
    };
    for (const auto& train : found->train_state.trains) {
        if (train.car_count > train.cars.size() || train.lead_car > train.cars.size() - train.car_count) {
            throw std::logic_error("invalid coaster train during retirement");
        }
        for (const auto& car : train.cars) {
            if (car.type_index >= found->train_state.configuration.car_types.size()) {
                throw std::logic_error("invalid coaster car type during retirement");
            }
            own(car.mesh_slot);
        }
    }
    for (const auto& node : found->geometry_state) {
        own(node.mesh_slot);
    }
    for (const auto& node : found->saved_geometry.geometry) {
        own(node.header.mesh_slot);
    }
    for (std::size_t r = 0; r < world.bindings.size(); ++r) {
        if (r == index) {
            continue;
        }
        const auto& other = world.bindings[r];
        const auto foreign = [&](std::uint32_t child) {
            if (child && owned.contains(child)) {
                throw std::logic_error("coaster child mesh has multiple owners");
            }
        };
        foreign(other.mesh_slot);
        for (const auto& train : other.train_state.trains) {
            for (const auto& car : train.cars) {
                foreign(car.mesh_slot);
            }
        }
        for (const auto& node : other.geometry_state) {
            foreign(node.mesh_slot);
        }
    }
    for (const auto& placement : world.placements) {
        if (placement.links.size() > std::numeric_limits<std::uint8_t>::max() + 1u) {
            throw std::logic_error("coaster placement exceeds saved link index");
        }
        for (const auto& link : placement.links) {
            if (link && (link->ride >= world.bindings.size() || link->node >= world.bindings[link->ride].geometry_state.size())) {
                throw std::logic_error("invalid coaster placement reference");
            }
        }
    }
    for (const auto& ride : world.bindings) {
        for (const auto& node : ride.geometry_state) {
            if (node.placement >= world.placements.size()) {
                throw std::logic_error("coaster geometry has no placement owner");
            }
        }
    }
    //The shared editor owns detached World cells and render-only proposals.
    //Restore them before unpublishing this mapID or destroying its supports.
    if (c.construction.editor) {
        c.construction.editor->retire_context(c, found->map_id);
    }
    std::vector<CoasterPlacement> removed;
    removed.reserve(world.placements.size());
    std::vector<std::size_t> remap(world.placements.size());

    //Unpublish the map/owner before recursive child destruction. The moved
    //value keeps all train, geometry and generated-track owners alive until
    //their resource and cell cleanup has completed (native 477460/481A70).
    auto retired = std::move(*found);
    const auto retiring_object = retired.park_object ? retired.park_object->base.header.id : ThingId {};
    world.bindings.erase(found);
    remove_map_id(world, retired.map_id);
    if (auto* script = find_rsse_script_state_by_id(c, retired.script_id); script && script->coaster_runtime_map_id == retired.map_id) {
        script->coaster_runtime_map_id = 0;
    }
    if (retired.pulsing_geometry_slot) {
        if (auto* geometry = instance(*retired.pulsing_geometry_slot)) {
            geometry->playback.runtime_flags &= ~0x4000000u;
        }
    }
    destroy_coaster_train_meshes(c, *this, retired);

    std::size_t destination = 0;
    for (std::size_t p = 0; p < world.placements.size(); ++p) {
        auto& placement = world.placements[p];
        bool changed = false;
        std::erase_if(placement.links, [&](const auto& link) {
            const auto erase = link && link->ride == index;
            changed |= erase;
            return erase;
        });
        if (placement.links.empty()) {
            removed.push_back(std::move(placement));
            continue;
        }
        for (std::size_t position = 0; position < placement.links.size(); ++position) {
            if (auto& link = placement.links[position]) {
                if (link->ride > index) {
                    --link->ride;
                }
                auto& ride = world.bindings[link->ride];
                if (changed) {
                    ride.geometry_dirty = true;
                    if (const auto ordinal = ride.geometry_state[link->node].saved_ordinal) {
                        ride.saved_geometry.geometry.at(*ordinal).header.link_slot = static_cast<std::uint8_t>(position);
                    }
                }
            }
        }
        remap[p] = destination;
        if (p != destination) {
            world.placements[destination] = std::move(placement);
        }
        ++destination;
    }
    world.placements.resize(destination);
    for (auto& ride : world.bindings) {
        for (auto& node : ride.geometry_state) {
            node.placement = remap.at(node.placement);
        }
    }
    //Car erasure already released active head clones. The remaining child set
    //contains pylon/track meshes; erase their registered renderer ownership too.
    for (const auto child : owned) {
        if (instance(child)) {
            erase(c, child);
        }
    }
    for (const auto& placement : removed) {
        if (!(placement.flags & 1u)) {
            remove_placement_cell(c, placement);
        }
    }
    const auto refreshed = refresh_coaster_placements(c, *this);
    if (!refreshed) {
        throw refreshed.error();
    }
    //Normal ParkObjectRemoved delivery may already have recovered these people.
    //Mesh-only retirement must do so too before its last queue owner goes away.
    if (retiring_object) {
        const WorldEvent event { ParkObjectRemovedEvent { retiring_object } };
        const auto recover = [&](std::uint32_t id) {
            if (!id || id >= c.entities.size()) {
                return;
            }
            auto* row = c.entities[id].payload;
            if (row && row->kind == ThingKind::person && !row->retirement_pending) {
                auto& person = *reinterpret_cast<PersonRuntimeView*>(row);
                if (person.major_destination == retiring_object) {
                    handle_person_park_object_event(c, person, event);
                }
            }
        };
        for (auto id : retired.train_state.queue_a.ids) {
            recover(id);
        }
        for (auto id : retired.train_state.queue_b.ids) {
            recover(id);
        }
        for (auto id : retired.train_state.exit_overflow) {
            recover(id);
        }
        for (const auto& train : retired.train_state.trains) {
            for (const auto& car : train.cars) {
                for (auto id : car.persons_a) {
                    recover(id);
                }
                for (auto id : car.persons_b) {
                    recover(id);
                }
            }
        }
    }
    //Vectors release queues, attachment arrays, spline samples, generated track
    //mesh vertices/indices and texture phases together with the moved owner.
}
} //namespace simcoaster
