#include "simcoaster/schema_projection.hpp"
#include "simcoaster/save_stream.hpp"
#include "simcoaster/resource_variant_io.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>

namespace simcoaster {
namespace {
using save::read;
using save::write;

//Compatibility schema only; these fields do not dictate the x64 class layout.
struct OriginalResourceRecord {
    [[=save::Field { "latched_word", 0 }]] std::uint32_t latched_word {};
    [[=save::Field { "handle", 1 }]] std::uint32_t handle {};
    [[=save::Field { "cursor", 2 }]] std::uint32_t cursor {};
    [[=save::Field { "entry", 3 }]] std::uint32_t entry {};
    [[=save::Field { "last_cursor", 4 }]] std::uint32_t last_cursor {};
    [[=save::Field { "ignored_program_pointer", 5 }]] std::uint32_t ignored_program_pointer {};
    [[=save::Field { "state", 6 }]] ResourceRunState state {};
    [[=save::Field { "stack_top", 7 }]] std::int32_t stack_top {};
    [[=save::Field { "stack", 8 }]] std::array<std::uint32_t, 20> stack {};
    [[=save::Field { "control_depth", 9 }]] std::int32_t control_depth {};
    [[=save::Field { "call_depth", 10 }]] std::int32_t call_depth {};
    [[=save::Field { "loop_depth", 11 }]] std::int32_t loop_depth {};
    [[=save::Field { "deadline", 12 }]] std::uint32_t deadline {};
    [[=save::Field { "delay", 13 }]] std::uint32_t delay {};
    [[=save::Field { "raw_values", 14 }]] std::array<std::uint32_t, 19> raw_values {};
    [[=save::Field { "discarded_render_cache", 15 }]] std::array<std::uint32_t, 17> discarded_render_cache {};
    [[=save::Field { "raw_render_enabled", 16 }]] std::uint32_t raw_render_enabled {};
};

static_assert(save::record_width<OriginalResourceRecord> == 280);

void clear_slots(ResourceVariantRuntime& runtime, std::size_t size) {
    runtime.active = nullptr;
    runtime.slots.clear();
    runtime.slots.resize(size);
}
} //namespace

bool serialize_resource_variant_pool(WorldRuntime& c, Stream& stream) {
    auto& runtime = c.resources.variants;
    if (!runtime.initialized) {
        return false;
    }
    if (runtime.slots.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    if (!write(stream, 1396920404u) || !write(stream, 280u) || !write(stream, static_cast<std::uint32_t>(runtime.slots.size()))) {
        return false;
    }
    //Native pointers serve only as occupancy markers on disk. Canonical marker1
    //is accepted by the original reader and exposes no host memory addresses.
    std::vector<std::byte> markers;
    markers.reserve(runtime.slots.size() * 4u);
    for (const auto& state : runtime.slots) {
        const auto marker = save::detail::encode(static_cast<std::uint32_t>(state != nullptr));
        markers.insert(markers.end(), marker.begin(), marker.end());
    }
    if (markers.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) || stream.write(markers) != static_cast<std::int32_t>(markers.size())) {
        return false;
    }
    for (std::size_t id = 1; id < runtime.slots.size(); ++id) {
        if (const auto* state = runtime.slots[id].get()) {
            OriginalResourceRecord record {};
            schema::copy_matching(record, *state);
            for (std::size_t slot = 0; slot < state->values.size(); ++slot) {
                record.raw_values[slot] = state->values[slot].bits;
            }
            record.raw_render_enabled = state->render_enabled;
            if (!write(stream, record)) {
                return false;
            }
        }
    }
    return true;
}

bool deserialize_resource_variant_pool(WorldRuntime& c, Stream& stream) {
    auto& runtime = c.resources.variants;
    if (!runtime.initialized) {
        return false;
    }
    clear_slots(runtime, 0);
    runtime.program = builtin_resource_variant_programs();
    std::uint32_t magic {}, size {}, capacity {};
    if (!read(stream, magic) || !read(stream, size) || magic != 1396920404u || size != 280) {
        clear_slots(runtime, 100);
        return false;
    }
    if (!read(stream, capacity) || !capacity || capacity > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) / 4u) {
        return false;
    }
    std::vector<std::uint32_t> occupied(capacity);
    std::vector<std::byte> markers(static_cast<std::size_t>(capacity) * 4u);
    if (stream.read(markers) != static_cast<std::int32_t>(markers.size())) {
        return false;
    }
    for (std::size_t index = 0; index < occupied.size(); ++index) {
        save::detail::decode(std::span<const std::byte, 4> { markers.data() + index * 4u, 4 }, occupied[index]);
    }
    runtime.slots.resize(capacity);
    for (std::size_t id = 1; id < occupied.size(); ++id) {
        if (occupied[id]) {
            OriginalResourceRecord record {};
            if (!read(stream, record)) {
                return false;
            }
            auto state = std::make_unique<ResourceVariant>();
            schema::copy_matching(*state, record);
            if (runtime.next_generation == std::numeric_limits<std::uint64_t>::max()) {
                return false;
            }
            state->generation = ++runtime.next_generation;
            for (std::size_t slot = 0; slot < state->values.size(); ++slot) {
                state->values[slot].bits = record.raw_values[slot];
            }
            state->render_enabled = record.raw_render_enabled != 0;
            runtime.slots[id] = std::move(state);
        }
    }
    return true;
}

bool write_resource_variant_save_section(WorldRuntime& c, Stream& stream) {
    return write(stream, 1465011268u) && serialize_resource_variant_pool(c, stream);
}

bool restore_resource_variant_save_section(WorldRuntime& c, Stream& stream) {
    std::uint32_t marker {};
    return read(stream, marker) && marker == 1465011268u && deserialize_resource_variant_pool(c, stream);
}
} //namespace simcoaster
