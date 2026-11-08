#include "simcoaster/schema_projection.hpp"
#include "simcoaster/save_stream.hpp"
#include "simcoaster/rsse_save.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/save_schema.hpp"
#include "simcoaster/world_save.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using save::read;
using save::write;

//Separate compatibility schemas: field order is the executable's 32-bit
//wire layout, independent of the host's pointer size and object layout.
struct SavedManager {
    [[=save::Field { "active", 0 }]] std::int32_t active {};
    [[=save::Field { "scheduler_tick", 1 }]] std::uint32_t scheduler_tick {};
    [[=save::Field { "next_state_id", 2 }]] std::uint32_t next_state_id {};
    [[=save::Field { "state_count", 3 }]] std::uint32_t state_count {};
    [[=save::Field { "head_marker", 4 }]] std::uint32_t head_marker {};
};

struct SavedState {
    [[=save::Field { "next_marker", 0 }]] std::uint32_t next_marker {};
    [[=save::Field { "previous_marker", 1 }]] std::uint32_t previous_marker {};
    [[=save::Field { "id", 2 }]] std::uint32_t id {};
    [[=save::Field { "child_script_state_id", 3 }]] std::uint32_t child_script_state_id {};
    [[=save::Field { "parent_script_state_id", 4 }]] std::uint32_t parent_script_state_id {};
    [[=save::Field { "sound_script_state_id", 5 }]] std::uint32_t sound_script_state_id {};
    [[=save::Field { "instruction_marker", 6 }]] std::uint32_t instruction_marker {};
    [[=save::Field { "values_marker", 7 }]] std::uint32_t values_marker {};
    [[=save::Field { "stack_marker", 8 }]] std::uint32_t stack_marker {};
    [[=save::Field { "limbo_marker", 9 }]] std::uint32_t limbo_marker {};
    [[=save::Field { "bounce_marker", 10 }]] std::uint32_t bounce_marker {};
    [[=save::Field { "walk_marker", 11 }]] std::uint32_t walk_marker {};
    [[=save::Field { "attachment_marker", 12 }]] std::uint32_t attachment_marker {};
    [[=save::Field { "string_marker", 13 }]] std::uint32_t string_marker {};
    [[=save::Field { "directory_marker", 14 }]] std::uint32_t directory_marker {};
    [[=save::Field { "instruction_cursor", 15 }]] std::int32_t instruction_cursor {};
    [[=save::Field { "stack_high_free_index", 16 }]] std::int32_t stack_high_free_index {};
    [[=save::Field { "stack_low_count", 17 }]] std::int32_t stack_low_count {};
    [[=save::Field { "current_value", 18 }]] std::int32_t current_value {};
    [[=save::Field { "attachment_capacity", 19 }]] std::int32_t attachment_capacity {};
    [[=save::Field { "instruction_word_count", 20 }]] std::int32_t instruction_word_count {};
    [[=save::Field { "operand_stack_capacity", 21 }]] std::int32_t operand_stack_capacity {};
    [[=save::Field { "limbo_capacity", 22 }]] std::int32_t limbo_capacity {};
    [[=save::Field { "reserved_092", 23 }]] std::uint32_t reserved_092 {};
    [[=save::Field { "limbo_count", 24 }]] std::int32_t limbo_count {};
    [[=save::Field { "bounce_capacity", 25 }]] std::int32_t bounce_capacity {};
    [[=save::Field { "reserved_104", 26 }]] std::uint32_t reserved_104 {};
    [[=save::Field { "bounce_count", 27 }]] std::uint16_t bounce_count {};
    [[=save::Field { "bounce_base_height", 28 }]] std::int16_t bounce_base_height {};
    [[=save::Field { "bounce_first_node_id", 29 }]] std::int32_t bounce_first_node_id {};
    [[=save::Field { "program_name_string_offset", 30 }]] std::int32_t program_name_string_offset {};
    [[=save::Field { "reserved_120", 31 }]] std::uint32_t reserved_120 {};
    [[=save::Field { "walk_capacity", 32 }]] std::int32_t walk_capacity {};
    [[=save::Field { "walk_float_start_ms", 33 }]] std::uint32_t walk_float_start_ms {};
    [[=save::Field { "walk_float_duration_ms", 34 }]] std::int32_t walk_float_duration_ms {};
    [[=save::Field { "walk_float_horizontal_scale_milli", 35 }]] std::uint16_t walk_float_horizontal_scale_milli {};
    [[=save::Field { "walk_float_vertical_scale_milli", 36 }]] std::uint16_t walk_float_vertical_scale_milli {};
    [[=save::Field { "value_slot_count", 37 }]] std::int32_t value_slot_count {};
    [[=save::Field { "string_table_size", 38 }]] std::int32_t string_table_size {};
    [[=save::Field { "instruction_step_budget", 39 }]] std::int32_t instruction_step_budget {};
    [[=save::Field { "remaining_instruction_steps", 40 }]] std::int32_t remaining_instruction_steps {};
    [[=save::Field { "auxiliary_runtime_object_handle", 41 }]] std::uint32_t auxiliary_runtime_object_handle {};
    [[=save::Field { "wait_deadline_ms", 42 }]] std::uint32_t wait_deadline_ms {};
    [[=save::Field { "animation_deadline_ms", 43 }]] std::uint32_t animation_deadline_ms {};
    [[=save::Field { "last_loop_animation_key", 44 }]] std::uint32_t last_loop_animation_key {};
    [[=save::Field { "owner_used_thing_id", 45 }]] std::uint16_t owner_used_thing_id {};
    [[=save::Field { "padding_174", 46 }]] std::uint16_t padding_174 {};
    [[=save::Field { "actions_marker", 47 }]] std::uint32_t actions_marker {};
    [[=save::Field { "inherited_context_word", 48 }]] std::uint32_t inherited_context_word {};
    [[=save::Field { "turbo_enabled", 49 }]] std::uint8_t turbo_enabled {};
    [[=save::Field { "music_dip_requested", 50 }]] std::uint8_t music_dip_requested {};
    [[=save::Field { "padding_186", 51 }]] std::uint16_t padding_186 {};
    [[=save::Field { "awaited_animation_id_plus_one", 52 }]] std::uint32_t awaited_animation_id_plus_one {};
    [[=save::Field { "operating_speed", 53 }]] std::int16_t operating_speed {};
    [[=save::Field { "padding_194", 54 }]] std::uint16_t padding_194 {};
    [[=save::Field { "timer_deadline_ms", 55 }]] std::uint32_t timer_deadline_ms {};
    [[=save::Field { "mesh_instance_handle", 56 }]] std::uint32_t mesh_instance_handle {};
    [[=save::Field { "repair_particle_handle", 57 }]] std::uint32_t repair_particle_handle {};
    [[=save::Field { "scream_request", 58 }]] std::uint32_t scream_request {};
    [[=save::Field { "occlusion_marker", 59 }]] std::uint32_t occlusion_marker {};
    [[=save::Field { "reverb_preset_override", 60 }]] std::int32_t reverb_preset_override {};
    [[=save::Field { "spark_source_node_id", 61 }]] std::int16_t spark_source_node_id {};
    [[=save::Field { "spark_target_node_id", 62 }]] std::int16_t spark_target_node_id {};
    [[=save::Field { "coaster_runtime_map_id", 63 }]] std::uint32_t coaster_runtime_map_id {};
    [[=save::Field { "animation_duration_scale_milli", 64 }]] std::int16_t animation_duration_scale_milli {};
    [[=save::Field { "endpoint_animation_closed", 65 }]] std::int16_t endpoint_animation_closed {};
    [[=save::Field { "reserved_232", 66 }]] std::uint32_t reserved_232 {};
    [[=save::Field { "updates_suspended", 67 }]] std::uint32_t updates_suspended {};
    [[=save::Field { "reserved_240", 68 }]] std::uint32_t reserved_240 {};
};

struct SavedAction {
    [[=save::Field { "next_marker", 0 }]] std::uint32_t next_marker {};
    [[=save::Field { "previous_marker", 1 }]] std::uint32_t previous_marker {};
    [[=save::Field { "type", 2 }]] RsseActionType type {};
    [[=save::Field { "handle", 3 }]] std::uint32_t handle {};
    [[=save::Field { "source_node_id", 4 }]] std::int32_t source_node_id {};
    [[=save::Field { "source_clone_index", 5 }]] std::int32_t source_clone_index {};
    [[=save::Field { "key", 6 }]] std::int32_t key {};
};

struct SavedLimbo {
    [[=save::Field { "person_id", 0 }]] std::uint32_t person_id {};
    [[=save::Field { "release_after_ms", 1 }]] std::uint32_t release_after_ms {};
};

struct SavedBounce {
    [[=save::Field { "person_id", 0 }]] std::uint32_t person_id {};
    [[=save::Field { "attachment_node_id", 1 }]] std::int32_t attachment_node_id {};
    [[=save::Field { "release_after_ms", 2 }]] std::uint32_t release_after_ms {};
    [[=save::Field { "start_ms", 3 }]] std::uint32_t start_ms {};
};

struct SavedWalk {
    [[=save::Field { "entry_start_node", 0 }]] std::int16_t entry_start_node {};
    [[=save::Field { "entry_end_node", 1 }]] std::int16_t entry_end_node {};
    [[=save::Field { "exit_start_node", 2 }]] std::int16_t exit_start_node {};
    [[=save::Field { "exit_end_node", 3 }]] std::int16_t exit_end_node {};
    [[=save::Field { "phase_start_ms", 4 }]] std::uint32_t phase_start_ms {};
    [[=save::Field { "phase_end_ms", 5 }]] std::uint32_t phase_end_ms {};
    [[=save::Field { "person_id", 6 }]] std::uint32_t person_id {};
    [[=save::Field { "direction", 7 }]] std::int16_t direction {};
    [[=save::Field { "kind", 8 }]] RsseWalkKind kind {};
    [[=save::Field { "phase", 9 }]] RsseWalkPhase phase {};
    [[=save::Field { "flags", 10 }]] RsseWalkFlags flags {};
    [[=save::Field { "reserved", 11 }]] std::uint32_t reserved {};
};

static_assert(save::record_width<SavedManager> == 20 && save::record_width<SavedState> == 244);
static_assert(save::record_width<SavedAction> == 28 && save::record_width<SavedLimbo> == 8 && save::record_width<SavedBounce> == 16 && save::record_width<SavedWalk> == 32);

using schema::copy_matching;

template <class Wire, class Runtime>
bool read_sequence(Stream& stream, std::vector<Runtime>& values, std::uint32_t bytes, bool zero_transfer = false) {
    constexpr auto width = save::value_width<Wire>;
    if (bytes % width || bytes > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    std::vector<std::byte> data(bytes);
    if ((bytes || zero_transfer) && stream.read(data) != static_cast<std::int32_t>(bytes)) {
        return false;
    }
    values.resize(bytes / width);
    for (std::size_t i = 0; i < values.size(); ++i) {
        Wire wire {};
        save::detail::decode(std::span<const std::byte, width> { data.data() + i * width, width }, wire);
        if constexpr (std::same_as<Wire, Runtime>) {
            values[i] = wire;
        } else {
            copy_matching(values[i], wire);
        }
    }
    return true;
}

template <class Wire, class Runtime>
bool write_sequence(Stream& stream, std::span<const Runtime> values, bool count_is_elements = false, bool zero_transfer = false) {
    constexpr auto width = save::value_width<Wire>;
    if (values.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) / width) {
        return false;
    }
    const auto bytes = values.size() * width;
    if (!write(stream, static_cast<std::uint32_t>(count_is_elements ? values.size() : bytes))) {
        return false;
    }
    std::vector<std::byte> data;
    data.reserve(bytes);
    for (const auto& value : values) {
        Wire wire {};
        if constexpr (std::same_as<Wire, Runtime>) {
            wire = value;
        } else {
            copy_matching(wire, value);
        }
        const auto encoded = save::detail::encode(wire);
        data.insert(data.end(), encoded.begin(), encoded.end());
    }
    return (!bytes && !zero_transfer) || stream.write(data) == static_cast<std::int32_t>(bytes);
}

bool read_string(Stream& stream, std::string& output) {
    std::uint32_t bytes {};
    if (!read(stream, bytes) || bytes > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    output.resize(bytes);
    return !bytes || stream.read(std::as_writable_bytes(std::span { output })) == static_cast<std::int32_t>(bytes);
}

bool write_string(Stream& stream, std::string_view value, bool terminate) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - (terminate ? 1u : 0u)) {
        return false;
    }
    if (!write(stream, static_cast<std::uint32_t>(value.size() + (terminate ? 1u : 0u)))) {
        return false;
    }
    if (!terminate) {
        return value.empty() || stream.write(std::as_bytes(std::span { value })) == static_cast<std::int32_t>(value.size());
    }
    std::string terminated { value };
    terminated.push_back('\0');
    return stream.write(std::as_bytes(std::span { terminated })) == static_cast<std::int32_t>(terminated.size());
}

std::string directory_for_write(const RsseScriptState& state) {
    if (!state.script_directory.empty()) {
        return state.script_directory;
    }
    if (!state.script_source) {
        return {};
    }
    auto directory = state.script_source->path;
    if (state.script_source->archive_entry.empty()) {
        directory = directory.parent_path();
    } else {
        directory.replace_extension();
    }
    auto result = directory.native_encoded_string();
    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
        result += '\\';
    }
    if (!state.script_source->archive_entry.empty()) {
        const auto slash = state.script_source->archive_entry.find_last_of("\\/");
        if (slash != std::string::npos) {
            result += state.script_source->archive_entry.substr(0, slash + 1);
        }
    }
    return result;
}

constexpr std::uint32_t outer_tag = 1179408722u, rsse_tag = 1163088722u, pad_tag = 1598308688u, object_tag = 541737551u;
} //namespace

namespace {
void restore_occlusion(ScriptWorld& c, RsseScriptState& state) {
    if (state.sound_occlusion_pending && state.mesh_instance_handle) {
        const auto first = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle);
        if (!first) {
            return;
        }
        const auto root_y = c.ports.meshes.get_mesh_root_translation_height(c, state.mesh_instance_handle);
        const auto x = static_cast<float>((static_cast<double>(first->max[0]) + first->min[0]) * 0.5);
        const auto z = static_cast<float>((static_cast<double>(first->max[2]) + first->min[2]) * 0.5);
        const auto second = c.ports.meshes.get_ride_mesh_world_bounds(c, state.mesh_instance_handle);
        if (!second) {
            return;
        }
        const auto radius = static_cast<float>((static_cast<double>(second->max[0]) - second->min[0] + second->max[2] - second->min[2]) * 0.5);
        const auto height = second->max[1] - second->min[1];
        state.sound_occlusion_region = c.ports.audio.create_rsse_sound_occlusion_region(c, x, root_y, z, radius, height);
        state.sound_occlusion_pending = false;
    }
}
} //namespace

void restore_pending_rsse_occlusion_regions(ScriptWorld& c) {
    for (auto* state = c.scripts.states.head; state; state = state->next) {
        restore_occlusion(c, *state);
    }
}

bool write_rsse_script_save_section(ScriptWorld& c, Stream& stream) {
    const auto& manager = c.scripts.states;
    SavedManager saved { manager.active, manager.scheduler_tick, manager.next_state_id, manager.state_count, manager.head ? 1u : 0u };
    if (!write(stream, outer_tag) || !write(stream, rsse_tag) || !write(stream, 20u) || !write(stream, saved)) {
        return false;
    }
    if (!manager.active) {
        return true;
    }
    for (int i = 0; i < 5; ++i) {
        if (!write(stream, pad_tag)) {
            return false;
        }
    }
    std::uint32_t count = 0;
    for (auto* state = manager.head; state; state = state->next) {
        if (count == std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        ++count;
    }
    if (!write(stream, count) || !write(stream, 244u)) {
        return false;
    }
    for (auto* state = manager.head; state; state = state->next) {
        SavedState record {};
        copy_matching(record, *state);
        record.next_marker = state->next ? 1u : 0u;
        record.previous_marker = state->previous ? 1u : 0u;
        record.instruction_marker = state->instruction_words.empty() ? 0u : 1u;
        record.values_marker = state->value_slots.empty() ? 0u : 1u;
        record.stack_marker = state->operand_stack.empty() ? 0u : 1u;
        record.limbo_marker = state->limbo_records.empty() ? 0u : 1u;
        record.bounce_marker = state->bounce_records.empty() ? 0u : 1u;
        record.walk_marker = state->walk_records.empty() ? 0u : 1u;
        record.attachment_marker = state->attached_person_ids.empty() ? 0u : 1u;
        record.string_marker = state->string_table.empty() ? 0u : 1u;
        record.directory_marker = 1;
        record.actions_marker = state->actions.empty() ? 0u : 1u;
        record.occlusion_marker = state->sound_occlusion_region || state->sound_occlusion_pending ? 1u : 0u;
        const auto fit = [](std::size_t n) {
            return n <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
        };
        if (!fit(state->attached_person_ids.size()) || !fit(state->instruction_words.size()) || !fit(state->operand_stack.size()) || !fit(state->limbo_records.size()) || !fit(state->bounce_records.size()) || !fit(state->walk_records.size()) || !fit(state->value_slots.size()) || !fit(state->string_table.size())) {
            return false;
        }
        record.attachment_capacity = static_cast<std::int32_t>(state->attached_person_ids.size());
        record.instruction_word_count = static_cast<std::int32_t>(state->instruction_words.size());
        record.operand_stack_capacity = static_cast<std::int32_t>(state->operand_stack.size());
        record.limbo_capacity = static_cast<std::int32_t>(state->limbo_records.size());
        record.bounce_capacity = static_cast<std::int32_t>(state->bounce_records.size());
        record.walk_capacity = static_cast<std::int32_t>(state->walk_records.size());
        record.value_slot_count = static_cast<std::int32_t>(state->value_slots.size());
        record.string_table_size = static_cast<std::int32_t>(state->string_table.size());
        if (!write(stream, record)
            || !write_sequence<std::uint32_t>(stream, state->instruction_words, false, true)
            || !write_sequence<std::int32_t>(stream, std::span<const std::int32_t> { state->operand_stack })
            || !write_sequence<std::int32_t>(stream, std::span<const std::int32_t> { state->value_slots })
            || !write_string(stream, state->string_table, false)
            || !write_sequence<SavedLimbo>(stream, std::span<const RsseLimboRecord> { state->limbo_records })
            || !write_sequence<SavedBounce>(stream, std::span<const RsseBounceRecord> { state->bounce_records })
            || !write_sequence<SavedWalk>(stream, std::span<const RsseWalkRecord> { state->walk_records }, true)
            || !write_sequence<std::uint32_t>(stream, std::span<const std::uint32_t> { state->attached_person_ids })
            || !write_string(stream, directory_for_write(*state), true)) {
            return false;
        }
        if (state->actions.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            return false;
        }
        if (!write(stream, object_tag) || !write(stream, static_cast<std::uint32_t>(state->actions.size())) || !write(stream, 28u)) {
            return false;
        }
        std::size_t index = 0;
        for (const auto& action : state->actions) {
            SavedAction wire {};
            copy_matching(wire, action);
            wire.previous_marker = index ? 1u : 0u;
            wire.next_marker = ++index < state->actions.size() ? 1u : 0u;
            if (!write(stream, wire)) {
                return false;
            }
        }
    }
    return true;
}

bool restore_rsse_script_save_section(ScriptWorld& c, Stream& stream) {
    std::uint32_t word {};
    if (!read(stream, word) || word != outer_tag) {
        return false;
    }
    shutdown_rsse_script_state_manager(c);
    initialize_rsse_script_state_manager(c);
    if (!read(stream, word) || word != rsse_tag || !read(stream, word) || word != 20) {
        return false;
    }
    SavedManager saved {};
    if (!read(stream, saved)) {
        return false;
    }
    auto& manager = c.scripts.states;
    manager.active = saved.active;
    manager.scheduler_tick = saved.scheduler_tick;
    manager.next_state_id = saved.next_state_id;
    manager.state_count = saved.state_count;
    manager.head = nullptr;
    //The writer emits no tail for a disabled manager; avoid the original
    //reader's unchecked reads into the following section in that case.
    if (!saved.active) {
        return true;
    }
    for (int i = 0; i < 5; ++i) {
        if (!read(stream, word)) {
            return false;
        }
    }
    std::int32_t count {};
    std::uint32_t size {};
    if (!read(stream, count) || count < 0 || !read(stream, size) || size != 244) {
        return false;
    }
    for (std::int32_t i = 0; i < count; ++i) {
        auto owned = std::make_unique<RsseProgramInstance>();
        auto& state = owned->state;
        auto* instance = owned.get();
        manager.owned_states.push_back(std::move(owned));
        state.next = manager.head;
        if (state.next) {
            state.next->previous = &state;
        }
        manager.head = &state;
        ++manager.state_count;
        SavedState record {};
        if (!read(stream, record)) {
            return false;
        }
        copy_matching(state, record);
        auto& program = instance->program;
        if (!read(stream, size)
            || !read_sequence<std::uint32_t>(stream, program.instructions, size, true)
            || !read(stream, size)
            || !read_sequence<std::int32_t>(stream, instance->stack, size)
            || !read(stream, size)
            || !read_sequence<std::int32_t>(stream, instance->values, size)
            || !read_string(stream, program.string_table)
            || !read(stream, size)
            || !read_sequence<SavedLimbo>(stream, instance->limbo, size)
            || !read(stream, size)
            || !read_sequence<SavedBounce>(stream, instance->bounce, size)
            || !read(stream, size)
            || size > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) / 32u
            || !read_sequence<SavedWalk>(stream, instance->walk, size * 32u)
            || !read(stream, size)
            || !read_sequence<std::uint32_t>(stream, instance->attached_people, size)
            || !read_string(stream, state.script_directory)) {
            return false;
        }
        if (!state.script_directory.empty()) {
            if (state.script_directory.back() != '\0') {
                return false;
            }
            state.script_directory.pop_back();
            state.script_source = resolve_saved_rsse_script_directory(c, state.script_directory);
        }
        state.instruction_words = program.instructions;
        state.operand_stack = instance->stack;
        state.value_slots = instance->values;
        state.string_table = program.string_table;
        state.limbo_records = instance->limbo;
        state.bounce_records = instance->bounce;
        state.walk_records = instance->walk;
        state.attached_person_ids = instance->attached_people;
        state.sound_occlusion_pending = record.occlusion_marker != 0;
        state.sound_occlusion_region = nullptr;
        restore_occlusion(c, state);
        state.endpoint_animation_closed = -1;
        std::int32_t action_count {};
        if (!read(stream, word) || word != object_tag || !read(stream, action_count) || action_count < 0 || !read(stream, size) || size != 28) {
            return false;
        }
        for (std::int32_t j = 0; j < action_count; ++j) {
            SavedAction wire {};
            if (!read(stream, wire)) {
                return false;
            }
            RsseAction action {};
            copy_matching(action, wire);
            state.actions.push_front(action);
            ++manager.action_node_count;
        }
    }
    return true;
}
} //namespace simcoaster
