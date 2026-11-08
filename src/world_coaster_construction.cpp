#include "simcoaster/coaster_construction.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_save.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
MeshError invalid(std::size_t index) {
    return { MeshFailure::configuration, index, "Coaster construction", {} };
}

struct FreshTrainMeshes final : RideTrainMeshBackend {
    AttractionWorld& context;
    WorldMeshRuntime& meshes;
    std::vector<std::uint32_t> created;
    bool committed{};

    FreshTrainMeshes(AttractionWorld& c, WorldMeshRuntime& m) : context(c), meshes(m) {}

    ~FreshTrainMeshes() {
        if (!committed)
            for (const auto slot : created)
                if (meshes.instance(slot))
                    meshes.erase(context, slot);
    }

    std::optional<std::uint32_t> create_car_mesh(std::uint32_t type) override {
        //Track ownership without allocating after the renderer publishes a slot.
        created.reserve(created.size() + 1);
        const auto result = meshes.create(
            context,
            { static_cast<std::int32_t>(type), 0x101u, { 0, 0, 0, nullptr }, 0, 0, nullptr, 0, 0, -1, 0 });
        if (!result)
            return std::nullopt;
        created.push_back(*result);
        return *result;
    }

    bool refresh_car_mesh(std::uint32_t, std::uint32_t) override {
        return false;
    }

    void unregister_car_mesh(std::uint32_t slot) override {
        meshes.set_registered(context, slot, false);
    }

    void activate_car_animation(std::uint32_t slot) override {
        meshes.force_animation_active(slot);
    }
};

std::uint32_t allocate_map_id(WorldCoasterRuntime& world) {
    if (!world.available_id_holes && world.registered_record_count >= 255)
        return 0;
    const auto candidate = world.next_map_id;
    if (!candidate || world.find_map(candidate))
        throw invalid(candidate);
    if (candidate - 1 == world.registered_record_count) {
        ++world.next_map_id;
        ++world.registered_record_count;
        return candidate;
    }
    auto scan = candidate;
    const auto highest = world.registered_record_count + world.available_id_holes;
    while (scan <= highest && world.find_map(scan + 1))
        ++scan;
    if (scan <= highest)
        world.next_map_id = scan + 1;
    if (!world.available_id_holes)
        throw invalid(candidate);
    --world.available_id_holes;
    ++world.registered_record_count;
    return candidate;
}
} //namespace

std::expected<std::uint32_t, MeshError> initialize_fresh_coaster_runtime(AttractionWorld& c,
                                                                         WorldMeshRuntime& meshes,
                                                                         WorldCoasterRuntime& world,
                                                                         std::uint32_t slot,
                                                                         ParkObjectRuntimeView* object) {
    try {
        const auto* parent = meshes.instance(slot);
        const auto* source = parent ? meshes.definition(parent->template_id) : nullptr;
        if (!source || !source->coaster || world.find(slot) || slot > std::numeric_limits<std::uint16_t>::max())
            throw invalid(slot);
        if (!world.registered_record_count && !world.available_id_holes && !world.next_map_id)
            world.next_map_id = 1;
        const auto old_count = world.registered_record_count, old_holes = world.available_id_holes,
                   old_next = world.next_map_id;
        const auto old_placement_count = world.placements.size();
        bool published = false;
        try {
            const auto map = allocate_map_id(world);
            if (!map)
                return std::unexpected(invalid(slot));
            WorldCoasterBinding binding;
            binding.mesh_slot = slot;
            binding.map_id = map;
            binding.script_id = parent->metadata.owner_context;
            binding.park_object = object;
            binding.definition = source->coaster;
            binding.default_attachment_height = source->coaster->balance.coaster.fInitialPylonLoftHeight != 0
                                                  ? source->coaster->balance.coaster.fInitialPylonLoftHeight
                                                  : 10.0f;
            FreshTrainMeshes renderer{ c, meshes };
            if (!create_ride_train_state(source->coaster->trains, 3.0f, renderer, binding.train_state))
                throw invalid(slot);
            auto& trains = binding.train_state;
            trains.runtime_flags = 0x1001u;
            trains.detail_state_flags = 0x11u;
            //Zero-initialized476BE0 leaves this zero: transition0->0x11 changes
            //selectors only. The first inactive1->vehicles2 transition sets1.0.
            trains.animation_speed_multiplier = 0;
            if (source->coaster->balance.coaster.bIsSuspendedCoaster)
                trains.placement_flags |= 8u;
            const auto fraction = static_cast<double>(binding.speed_setting) * static_cast<double>(0.01f);
            binding.speed_cap =
                static_cast<float>(trains.configuration.maximum_speed_at_minimum_setting * (1.0 - fraction)
                                   + trains.configuration.maximum_speed_at_maximum_setting * fraction);
            const auto record_speed =
                !c.construction.action_recording_disabled && c.construction.action_record_streams_initialized;
            if (record_speed && !c.construction.action_record_stream)
                throw invalid(slot);
            auto& header = binding.saved_geometry.header;
            header.object_index = static_cast<std::uint16_t>(parent->template_id);
            header.mesh_instance_id = static_cast<std::uint16_t>(slot);
            header.map_id = static_cast<std::uint16_t>(map);
            header.tile_x = static_cast<std::int16_t>(parent->metadata.placement_x);
            header.tile_y = static_cast<std::int16_t>(parent->metadata.placement_y);
            //476BE0 prepends the owner. Rebase shared support references before
            //publishing it so existing coasters keep their physical stack owners.
            world.bindings.insert(world.bindings.begin(), std::move(binding));
            published = true;
            for (auto& placement : world.placements)
                for (auto& ref : placement.links)
                    if (ref)
                        ++ref->ride;
            const auto initialized = initialize_fresh_coaster_geometry(c, meshes, 0);
            if (!initialized)
                throw initialized.error();
            meshes.bind_rsse(c, slot, world.bindings.front().script_id);
            //Publish the native SETSPEED record only once resource construction has
            //succeeded. A host allocation/resource failure must not leave an action
            //referencing a map reservation that was rolled back.
            if (record_speed) {
                const std::array event{ std::byte{ 0x45 } };
                const auto map_bytes = save::detail::encode(static_cast<std::uint16_t>(map));
                const auto speed_bytes =
                    save::detail::encode(static_cast<std::uint16_t>(world.bindings.front().speed_setting));
                (void)c.construction.action_record_stream->write(event);
                (void)c.construction.action_record_stream->write(map_bytes);
                (void)c.construction.action_record_stream->write(speed_bytes);
            }
            renderer.committed = true;
            return map;
        } catch (...) {
            //Geometry construction may have committed its child meshes before an
            //action-stream/association failure. Reclaim those published children as
            //well as the train guard's cars; leave the parent to create()'s
            //rollback.
            if (published) {
                for (const auto& local : world.bindings.front().geometry_state)
                    if (local.mesh_slot && meshes.instance(local.mesh_slot))
                        meshes.erase(c, local.mesh_slot);
                world.bindings.erase(world.bindings.begin());
                world.placements.resize(old_placement_count);
                for (auto& placement : world.placements)
                    for (auto& ref : placement.links)
                        if (ref)
                            --ref->ride;
            }
            world.registered_record_count = old_count;
            world.available_id_holes = old_holes;
            world.next_map_id = old_next;
            throw;
        }
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
