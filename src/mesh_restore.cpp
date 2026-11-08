#include "simcoaster/save_stream.hpp"
#include "simcoaster/mesh_restore.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>
#include <utility>
#include <vector>

namespace simcoaster {
namespace {
class BufferReader {
    std::span<const std::byte> bytes_;

public:
    explicit BufferReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <class T>
    bool read(T& value) {
        constexpr auto size = save::value_width<T>;
        if (bytes_.size() < size) {
            return false;
        }
        save::detail::decode(std::span<const std::byte, size> { bytes_.data(), size }, value);
        bytes_ = bytes_.subspan(size);
        return true;
    }

    std::size_t remaining() const {
        return bytes_.size();
    }
};

constexpr std::array instance_bits { std::pair { 0x1u, 0x20u }, std::pair { 0x2u, 0x40u }, std::pair { 0x4u, 0x80u }, std::pair { 0x8u, 0x100u }, std::pair { 0x10u, 0x200u }, std::pair { 0x20u, 0x400u }, std::pair { 0x40u, 0x800u }, std::pair { 0x80u, 0x40000u }, std::pair { 0x100u, 0x8000000u } };
constexpr std::array node_bits { std::pair { 0x1u, 0x10u }, std::pair { 0x2u, 0x20u }, std::pair { 0x4u, 0x8000u }, std::pair { 0x8u, 0x10000u }, std::pair { 0x20u, 0x40000u }, std::pair { 0x40u, 0x80000u }, std::pair { 0x80u, 0x200000u }, std::pair { 0x100u, 0x400000u }, std::pair { 0x800u, 0x100000u }, std::pair { 0x200u, 0x20000000u }, std::pair { 0x400u, 0x80000000u } };

template <std::size_t N>
std::uint32_t unpack_flags(std::uint32_t original, std::uint32_t saved, std::uint32_t keep_mask, const std::array<std::pair<unsigned, unsigned>, N>& mapping) {
    auto flags = original & keep_mask;
    for (const auto [bit, target] : mapping) {
        if (saved & bit) {
            flags |= target;
        }
    }
    return flags;
}

void finish_animation_state(WorldRuntime& c, MeshRestoreBackend& backend, MeshRestoreInstance& instance) {
    bool has_primary = false;
    for (const auto& control : instance.animation_controls) {
        if (control.animation_id == 0) {
            has_primary = true;
        }
    }
    if (!has_primary) {
        bool invalidated = false;
        for (auto& flags : instance.node_flags) {
            if (flags & 0x100u) {
                flags &= ~0x800u;
                invalidated = true;
            }
        }
        if (invalidated) {
            backend.invalidate_mesh_renderer_cache(c);
        }
        instance.runtime_flags &= ~0x40000000u;
    }
    std::uint32_t active = 0;
    for (const auto& control : instance.animation_controls) {
        if (control.animation_id < 12u) {
            ++active;
            instance.runtime_flags |= 0x400000u;
        }
    }
    if (instance.runtime_flags & 0x1000000u) {
        active = 1;
    }
    instance.active_animation_count = active;
    if (active) {
        instance.model_flags |= 0x80000u;
    } else {
        instance.model_flags &= ~0x80000u;
    }
}
} //namespace

bool restore_mesh_instance_save_section(WorldRuntime& c, Stream& stream, MeshRestoreBackend& backend) {
    backend.last_mesh_restore_failure.reset();
    std::uint32_t ordinal {};
    SavedMeshInstance saved {};
    const auto fail = [&](std::string_view stage, std::size_t offset) {
        backend.last_mesh_restore_failure = MeshRestoreFailure { stage, offset, ordinal, saved.slot, saved.definition_key };
        return false;
    };
    std::uint32_t size {};
    if (!save::read(stream, size) || size < 12 || size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return fail("section size", 0);
    }
    std::vector<std::byte> data(size);
    if (stream.read(data) != static_cast<std::int32_t>(size)) {
        return fail("section body", 0);
    }
    BufferReader reader { data };
    MeshSlotAllocatorState allocator {};
    const auto offset = [&] {
        return data.size() - reader.remaining();
    };
    if (!reader.read(allocator.live_count) || !reader.read(allocator.hole_count) || !reader.read(allocator.next_slot_cursor)) {
        return fail("allocator header", offset());
    }
    const auto slot_count = static_cast<std::uint64_t>(allocator.live_count) + allocator.hole_count;
    if (slot_count > reader.remaining() || slot_count > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return fail("allocator count", offset());
    }
    backend.begin_mesh_restore(c, allocator);
    backend.update_mesh_animation_clock_sources(c);
    for (ordinal = 1; ordinal <= slot_count; ++ordinal) {
        saved = {};
        std::uint8_t present {};
        if (!reader.read(present)) {
            return fail("presence byte", offset());
        }
        if (present != 1) {
            continue;
        }
        if (!reader.read(saved) || !saved.slot || saved.slot > slot_count) {
            return fail("instance header", offset());
        }
        const auto template_index = backend.find_mesh_template_by_key(c, saved.definition_key);
        if (template_index <= 0) {
            return fail("template lookup", offset());
        }
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, static_cast<std::uint16_t>(saved.definition_key));
        const DefinitionGrid* geometry = nullptr;
        if (definition) {
            if (!definition->balance) {
                return fail("object balance", offset());
            }
            geometry = definition->balance->Info.Shape.get();
        } else if (saved.footprint_width && saved.footprint_height) {
            geometry = backend.fallback_mesh_geometry(c, saved.footprint_width, saved.footprint_height);
            if (!geometry) {
                return fail("fallback geometry", offset());
            }
        }
        const auto slot = world_operations::create_mesh_instance_and_register_slot(c, { template_index, saved.creation_flags, { saved.orientation_degrees, saved.placement_x, saved.placement_y, geometry }, 0, saved.owner_context, nullptr, saved.footprint_width, saved.footprint_height, static_cast<std::int32_t>(saved.slot), 1 });
        if (slot != saved.slot) {
            return fail("instance creation", offset());
        }
        auto& instance = backend.mesh_restore_instance(c, slot, saved);
        const bool layout_matches = instance.node_flags.size() == saved.node_count && instance.embedded_nodes.size() == saved.embedded_node_count;
        instance.runtime_flags = unpack_flags(instance.runtime_flags, saved.packed_runtime_flags, 0xF7FFF01Fu, instance_bits);
        instance.transient_state &= ~0xFFu;
        instance.runtime_state = saved.runtime_state;
        instance.animation_phase = saved.animation_phase;
        backend.rebuild_mesh_deformation_after_restore(c, slot);
        if (layout_matches && instance.has_embedded_state) {
            instance.embedded_flags = saved.embedded_flags;
            instance.embedded_extra = saved.embedded_extra;
        }
        for (std::size_t i = 0; i < saved.embedded_node_count; ++i) {
            MeshEmbeddedState entry {};
            if (!reader.read(entry)) {
                return fail("embedded state", offset());
            }
            if (layout_matches && instance.has_embedded_state) {
                instance.embedded_nodes[i] = entry;
            }
        }
        for (std::size_t i = 0; i < saved.node_count; ++i) {
            std::uint32_t flags {};
            if (!reader.read(flags)) {
                return fail("node flags", offset());
            }
            if (layout_matches) {
                instance.node_flags[i] = unpack_flags(instance.node_flags[i], flags, 0x5F827FCFu, node_bits);
            }
        }
        for (auto& control : instance.animation_controls) {
            if (!reader.read(control)) {
                return fail("animation control", offset());
            }
            if (control.flags & 4u) {
                control.flags |= 0x10u;
            }
            if (const auto timing = backend.mesh_animation_timing(c, slot, control.animation_id, control.sub_animation_id)) {
                control.total_animation_frames = static_cast<float>(timing->end_frame - timing->start_frame);
                //0x4DEE1C zeroes the high FILD word: the subtraction is unsigned.
                const auto elapsed = static_cast<std::uint32_t>(control.animation_time) - static_cast<std::uint32_t>(control.start_time);
                control.animation_frame = static_cast<float>(static_cast<double>(elapsed) * static_cast<double>(0.03f) / control.duration_divisor);
            }
        }
        finish_animation_state(c, backend, instance);
        if (!backend.finish_mesh_restore_instance(c, slot, instance)) {
            return fail("instance finalization", offset());
        }
    }
    if (reader.remaining() != 0) {
        return fail("unconsumed body", offset());
    }
    if (!backend.finish_mesh_restore(c)) {
        return fail("allocator finalization", offset());
    }
    return true;
}

bool write_mesh_instance_save_section(WorldRuntime& c, Stream& stream, const MeshSaveBackend& backend) {
    const auto allocator = backend.mesh_slot_allocator_state(c);
    const auto slot_count = static_cast<std::uint64_t>(allocator.live_count) + allocator.hole_count;
    if (slot_count > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    std::vector<std::byte> data;
    const auto append = [&]<class T>(const T& value) {
        const auto bytes = save::detail::encode(value);
        data.insert(data.end(), bytes.begin(), bytes.end());
    };
    append(allocator.live_count);
    append(allocator.hole_count);
    append(allocator.next_slot_cursor);
    std::uint32_t live = 0;
    for (std::uint32_t id = 1; id <= slot_count; ++id) {
        const auto instance = backend.mesh_save_instance(c, id);
        append(static_cast<std::uint8_t>(instance.has_value()));
        if (!instance) {
            continue;
        }
        ++live;
        if (instance->node_flags.size() > std::numeric_limits<std::uint16_t>::max() || instance->embedded_nodes.size() > std::numeric_limits<std::uint16_t>::max() || (!instance->has_embedded_state && !instance->embedded_nodes.empty())) {
            return false;
        }
        auto header = instance->metadata;
        header.slot = id;
        header.runtime_state = instance->runtime_state;
        header.animation_phase = instance->animation_phase;
        header.node_count = static_cast<std::uint16_t>(instance->node_flags.size());
        header.embedded_node_count = static_cast<std::uint16_t>(instance->embedded_nodes.size());
        header.embedded_flags = instance->has_embedded_state ? instance->embedded_flags : 0;
        header.embedded_extra = instance->has_embedded_state ? instance->embedded_extra : 0;
        header.packed_runtime_flags = 0;
        for (const auto [saved_bit, runtime_bit] : instance_bits) {
            if (instance->runtime_flags & runtime_bit) {
                header.packed_runtime_flags |= saved_bit;
            }
        }
        append(header);
        for (const auto& embedded : instance->embedded_nodes) {
            append(embedded);
        }
        for (const auto flags : instance->node_flags) {
            std::uint32_t packed = 0;
            for (const auto [saved_bit, runtime_bit] : node_bits) {
                if (flags & runtime_bit) {
                    packed |= saved_bit;
                }
            }
            append(packed);
        }
        for (const auto& control : instance->animation_controls) {
            append(control);
        }
    }
    if (live != allocator.live_count || data.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    const auto size = save::detail::encode(static_cast<std::uint32_t>(data.size()));
    return stream.write(size) == static_cast<std::int32_t>(size.size()) && stream.write(data) == static_cast<std::int32_t>(data.size());
}
} //namespace simcoaster
