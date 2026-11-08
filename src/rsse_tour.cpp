#include "simcoaster/save_stream.hpp"
#include "simcoaster/rsse_tour.hpp"
#include "simcoaster/rsse_tour_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
void world_operations::builtin::destroy_rsse_tour(WorldRuntime& c, std::uint32_t handle) {
    if (!handle || handle >= c.scripts.tours.slots.size() || !c.scripts.tours.slots[handle])
        return;
    auto& tour = *c.scripts.tours.slots[handle];
    for (auto& entry : tour.entries)
        if (entry.element_state != RsseTourElementState::inactive) {
            const auto passengers = entry.signed_passenger_count < 0
                                      ? -static_cast<std::int64_t>(entry.signed_passenger_count)
                                      : entry.signed_passenger_count;
            if (passengers > static_cast<std::int64_t>(entry.passenger_ids.size()))
                throw std::out_of_range("TOUR passenger count");
            if (entry.mesh_object_count && entry.mesh_instance_handle)
                for (auto remaining = passengers; remaining > 0; --remaining) {
                    const auto node =
                        static_cast<std::uint32_t>((remaining + entry.mesh_object_start) % entry.mesh_object_count + 1);
                    const auto index =
                        c.ports.meshes.find_embedded_node_resource_record(c,
                                                                          entry.mesh_instance_handle,
                                                                          128,
                                                                          static_cast<std::int32_t>(node));
                    if (index >= 0)
                        world_operations::release_mesh_model_object_instance(c, entry.mesh_instance_handle, index);
                }
            if (entry.particle_runtime_handle)
                c.ports.effects.set_particle_effect_lifetime(c, entry.particle_runtime_handle, -2);
            entry.particle_runtime_handle = 0;
            world_operations::destroy_mesh(c, entry.mesh_instance_handle);
            entry.mesh_instance_handle = 0;
            --tour.element_count;
            if (entry.positioned_sound_request)
                c.ports.resources.request_target_recycle(c, entry.positioned_sound_request);
            entry.positioned_sound_request = 0;
        }
    c.scripts.tours.slots[handle].reset();
}

void world_operations::builtin::set_rsse_tour_duration(WorldRuntime& c, std::uint32_t handle, std::int32_t duration) {
    if (handle && handle < c.scripts.tours.slots.size())
        if (auto* tour = c.scripts.tours.slots[handle].get()) {
            tour->flight_duration_ticks = duration / 64;
            for (auto& entry : tour->entries)
                entry.flight_time_remaining_ticks =
                    std::min(entry.flight_time_remaining_ticks, tour->flight_duration_ticks);
        }
}

void world_operations::builtin::set_rsse_tour_speed(WorldRuntime& c, std::uint32_t handle, std::int32_t speed) {
    if (handle && handle < c.scripts.tours.slots.size())
        if (auto* tour = c.scripts.tours.slots[handle].get())
            tour->speed_adjustment_tenths = speed / 10;
}

namespace {
using save::read;
using save::write;
constexpr std::uint32_t section_tag = 1414676811u, flying_tag = 1599687750u, padding_tag = 1598308688u;
} //namespace

bool write_rsse_tour_save_section(ScriptWorld& c, Stream& stream) {
    const auto& manager = c.scripts.tours;
    if (!write(stream, section_tag)
        || !write(stream, flying_tag)
        || !write(stream, static_cast<std::uint32_t>(manager.initialized)))
        return false;
    if (!manager.initialized)
        return true;
    std::array<std::uint32_t, 100> markers{};
    for (std::size_t i = 1; i < markers.size(); ++i)
        markers[i] = manager.slots[i] ? 1u : 0u;
    if (!write(stream, 400u)
        || !write(stream, markers)
        || !write(stream, manager.bounds.max_x)
        || !write(stream, manager.bounds.min_x)
        || !write(stream, manager.bounds.max_z)
        || !write(stream, manager.bounds.min_z))
        return false;
    for (int i = 0; i < 5; ++i)
        if (!write(stream, padding_tag))
            return false;
    if (!write(stream, 4652u))
        return false;
    for (std::size_t i = 1; i < markers.size(); ++i)
        if (manager.slots[i] && !write(stream, *manager.slots[i]))
            return false;
    return true;
}

bool restore_rsse_tour_save_section(ScriptWorld& c, Stream& stream) {
    std::uint32_t tag{};
    if (!read(stream, tag) || tag != section_tag)
        return false;
    auto& manager = c.scripts.tours;
    if (manager.initialized)
        for (std::size_t i = 1; i < manager.slots.size(); ++i)
            if (manager.slots[i]) {
                world_operations::destroy_rsse_tour(c, static_cast<std::uint32_t>(i));
                manager.slots[i].reset();
            }
    manager.initialized = false;
    std::uint32_t initialized{};
    if (!read(stream, tag) || tag != flying_tag || !read(stream, initialized))
        return false;
    if (!initialized)
        return true;
    initialize_rsse_tours(c);
    std::uint32_t size{};
    std::array<std::uint32_t, 100> markers{};
    if (!read(stream, size)
        || size != 400
        || !read(stream, markers)
        || !read(stream, manager.bounds.max_x)
        || !read(stream, manager.bounds.min_x)
        || !read(stream, manager.bounds.max_z)
        || !read(stream, manager.bounds.min_z))
        return false;
    for (int i = 0; i < 5; ++i)
        if (!read(stream, tag))
            return false;
    if (!read(stream, size) || size != 4652)
        return false;
    for (std::size_t i = 1; i < markers.size(); ++i)
        if (markers[i]) {
            auto record = std::make_unique<RsseTourRecord>();
            if (!read(stream, *record))
                return false;
            manager.slots[i] = std::move(record);
        }
    return true;
}

void refresh_rsse_tour_mesh_instances(ScriptWorld& c) {
    for (std::size_t i = 1; i < c.scripts.tours.slots.size(); ++i)
        if (auto* tour = c.scripts.tours.slots[i].get()) {
            for (auto& entry : tour->entries)
                if (entry.element_state != RsseTourElementState::inactive) {
                    auto* owner = c.entities.at(tour->owner_used_thing_id).payload;
                    if (!owner || owner->kind != ThingKind::park_object)
                        throw std::out_of_range("tour owner is not a park object");
                    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(owner);
                    const auto* definition =
                        find_object_definition_by_id(&c.resources.object_catalog, object->definition_id);
                    if (!definition)
                        throw std::out_of_range("tour owner definition");
                    entry.mesh_instance_handle = world_operations::mark_or_find_matching_mesh_instance_for_refresh(
                        c,
                        { entry.mesh_instance_handle,
                          std::bit_cast<std::int32_t>(definition->additional_mesh_template_ids[0]),
                          257u,
                          {},
                          0,
                          nullptr,
                          nullptr,
                          nullptr,
                          false });
                }
        }
}
} //namespace simcoaster
