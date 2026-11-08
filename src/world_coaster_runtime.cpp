#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_save.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
constexpr std::array saved_runtime_bits { 0x20u, 0x40u, 0x80u, 0x100u, 0x200u, 0x400u, 0x800u };

float speed_cap(const RideTrainConfiguration& configuration, std::uint32_t percent) {
    const auto fraction = static_cast<double>(percent) * static_cast<double>(0.01f);
    return static_cast<float>(configuration.maximum_speed_at_minimum_setting * (1.0 - fraction) + configuration.maximum_speed_at_maximum_setting * fraction);
}

struct TrainMeshes final : RideTrainMeshBackend {
    AttractionWorld& context;
    WorldMeshRuntime& meshes;

    TrainMeshes(AttractionWorld& c, WorldMeshRuntime& m) : context(c), meshes(m) {}

    std::optional<std::uint32_t> create_car_mesh(std::uint32_t type) override {
        const auto created = meshes.create(context, { static_cast<std::int32_t>(type), 0x101u, { 0, 0, 0, nullptr }, 0, 0, nullptr, 0, 0, -1, 0 });
        return created ? std::optional { *created } : std::nullopt;
    }

    bool refresh_car_mesh(std::uint32_t slot, std::uint32_t type) override {
        const auto* car = meshes.instance(slot);
        const auto* expected = meshes.definition(type);
        if (!car || !expected) {
            return false;
        }
        const auto* actual = meshes.definition(car->template_id);
        if (!actual || actual->definition_key != expected->definition_key) {
            return false;
        }
        return meshes.mark_for_refresh(context, { slot, static_cast<std::int32_t>(type), 0x101u, {}, 0, nullptr, nullptr, nullptr, false }) == slot;
    }

    void unregister_car_mesh(std::uint32_t slot) override {
        meshes.set_registered(context, slot, false);
    }

    void activate_car_animation(std::uint32_t slot) override {
        meshes.force_animation_active(slot);
    }
};
} //namespace

std::expected<void, MeshError> WorldMeshRuntime::install_coaster_snapshot(AttractionWorld& c, CoasterSaveSection saved) {
    last_error.reset();
    auto result = std::make_unique<WorldCoasterRuntime>();
    result->registered_record_count = saved.registered_record_count;
    result->available_id_holes = saved.available_id_holes;
    result->next_map_id = saved.next_map_id;
    TrainMeshes renderer { c, *this };
    const auto reject = [&](MeshFailure reason, std::size_t index) {
        last_error = MeshError { reason, index, "Coaster runtime restoration", {} };
        return std::unexpected(*last_error);
    };
    for (auto& record : saved.records) {
        const auto slot = record.header.mesh_instance_id;
        const auto* parent = instance(slot);
        const auto* source = parent ? definition(parent->template_id) : nullptr;
        if (!parent || !source || !source->coaster || !(parent->playback.runtime_flags & 0x40000u) || !record.header.map_id || result->find(slot) || result->find_map(record.header.map_id)) {
            return reject(MeshFailure::configuration, slot);
        }
        for (const auto& geometry : record.geometry) {
            if (geometry.header.mesh_slot && !instance(geometry.header.mesh_slot)) {
                return reject(MeshFailure::index, geometry.header.mesh_slot);
            }
        }
        WorldCoasterBinding binding;
        binding.mesh_slot = slot;
        binding.map_id = record.header.map_id;
        binding.script_id = parent->metadata.owner_context;
        binding.definition = source->coaster;
        const auto initial_height = source->coaster->balance.coaster.fInitialPylonLoftHeight;
        binding.default_attachment_height = initial_height != 0 ? initial_height : 10.0f;
        if (!restore_ride_train_state(record, source->coaster->trains, 3.0f, renderer, binding.train_state)) {
            return reject(MeshFailure::configuration, slot);
        }
        //Creation first sets BASE/EXTENDED through the default speed setter;
        //the section reader adds restore bits 0x4000 and 2 before wire flags.
        auto& trains = binding.train_state;
        trains.runtime_flags = 0x1001u | 0x4002u;
        for (std::size_t bit = 0; bit < saved_runtime_bits.size(); ++bit) {
            if (record.header.packed_flags & (1u << (bit + 2))) {
                trains.runtime_flags |= saved_runtime_bits[bit];
            }
        }
        trains.detail_state_flags = record.header.detail_state_flags;
        trains.placement_flags = record.header.packed_flags & 3u;
        if (source->coaster->balance.coaster.bIsSuspendedCoaster) {
            trains.placement_flags |= 8u;
        }
        trains.animation_speed_multiplier = record.header.animation_speed;
        binding.speed_cap = speed_cap(trains.configuration, binding.speed_setting);
        binding.saved_geometry = std::move(record);
        binding.saved_geometry.trains = {};
        result->bindings.push_back(std::move(binding));
    }
    coasters_ = std::move(result);
    for (auto& binding : coasters_->bindings) {
        bind_rsse(c, binding.mesh_slot, binding.script_id);
    }
    return {};
}

std::expected<CoasterSaveSection, MeshError> WorldMeshRuntime::capture_coaster_snapshot() const {
    try {
        CoasterSaveSection saved;
        if (!coasters_) {
            return saved;
        }
        saved.registered_record_count = coasters_->registered_record_count;
        saved.available_id_holes = coasters_->available_id_holes;
        saved.next_map_id = coasters_->next_map_id;
        for (const auto& binding : coasters_->bindings) {
            auto record = binding.saved_geometry;
            const auto& trains = binding.train_state;
            if (!capture_ride_train_state(trains, record)) {
                return std::unexpected(MeshError { MeshFailure::configuration, binding.mesh_slot, "Coaster runtime capture", {} });
            }
            record.header.animation_speed = trains.animation_speed_multiplier;
            record.header.detail_state_flags = trains.detail_state_flags;
            record.header.packed_flags = (record.header.packed_flags & ~0x1FFu) | (trains.placement_flags & 3u);
            for (std::size_t bit = 0; bit < saved_runtime_bits.size(); ++bit) {
                if (trains.runtime_flags & saved_runtime_bits[bit]) {
                    record.header.packed_flags |= 1u << (bit + 2);
                }
            }
            //Persist the live controls and flags, including suspension changes made by
            //the speed setter. The restore snapshot supplies only untouched wire data.
            for (std::size_t i = 0; i < binding.geometry_state.size(); ++i) {
                const auto& local = binding.geometry_state[i];
                if (!local.saved_ordinal) {
                    continue;
                }
                auto& wire = record.geometry.at(*local.saved_ordinal).header;
                const auto flags = binding.geometry.at(i).geometry_flags;
                constexpr std::array wire_bits { 0x100u, 0x200u, 0x40u, 0x80u, 0x400u, 0x800u }, live_bits { 0x40000u, 0x80000u, 0x10000u, 0x20000u, 0x200000u, 0x400000u };
                wire.packed_flags = (wire.packed_flags & ~7u) | (flags & 7u);
                for (std::size_t bit = 0; bit < wire_bits.size(); ++bit) {
                    wire.packed_flags &= ~wire_bits[bit];
                    if (flags & live_bits[bit]) {
                        wire.packed_flags |= wire_bits[bit];
                    }
                }
                wire.loft = local.controls[0];
                wire.angle_a = local.angles[1];
                wire.angle_b = local.angles[2];
                wire.rotation = local.angles[3];
                wire.wobble = local.controls[4];
            }
            capture_coaster_geometry_peer_snapshot(binding, record);
            saved.records.push_back(std::move(record));
        }
        return saved;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

bool WorldMeshRuntime::set_coaster_speed(AttractionWorld& c, std::uint32_t id, std::uint32_t percent) {
    auto* binding = coasters_ ? coasters_->find_map(id) : nullptr;
    if (!binding || binding->speed_setting == percent || (!c.construction.action_recording_disabled && (binding->train_state.runtime_flags & 0x8000u))) {
        return false;
    }
    if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        if (!c.construction.action_record_stream) {
            throw std::invalid_argument("unbound coaster action recording stream");
        }
        const std::array event { std::byte { 0x45 } };
        const auto map = save::detail::encode(static_cast<std::uint16_t>(id)), speed = save::detail::encode(static_cast<std::uint16_t>(percent));
        (void)c.construction.action_record_stream->write(event);
        (void)c.construction.action_record_stream->write(map);
        (void)c.construction.action_record_stream->write(speed);
    }
    binding->speed_setting = percent;
    binding->speed_cap = speed_cap(binding->train_state.configuration, percent);
    binding->train_state.runtime_flags |= 0x1001u;
    binding->geometry_dirty = true;
    if (binding->definition->balance.coaster.bIsSuspendedCoaster) {
        mark_coaster_geometry_peers_dirty(*binding);
    }
    return true;
}

bool WorldMeshRuntime::set_coaster_capacity(std::uint32_t id, std::int32_t capacity) {
    auto* binding = coasters_ ? coasters_->find_map(id) : nullptr;
    return binding && set_ride_train_peep_capacity(binding->train_state, capacity);
}
} //namespace simcoaster
