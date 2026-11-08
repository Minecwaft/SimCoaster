#pragma once
#include "sprite_catalog.hpp"
#include <bit>

namespace simcoaster {
struct WorldRuntime;
enum class ResourceOpcode : std::uint8_t {
    operand,
    jump,
    compare,
    end_if,
    otherwise,
    call,
    return_,
    repeat,
    until,
    push,
    pop,
    do_,
    loop,
    end,
    yield,
    latch,
    set,
    add,
    subtract,
    multiply,
    multiply_position,
    divide,
    divide_slot,
    remainder,
    and_mask,
    remainder_slot,
    add_slot,
    copy,
    random,
    create,
    variant,
    frame_entry,
    frame_slot,
    frame,
    frame_always,
    hide,
    reset_scale,
    reset_slot9,
    advance_slot9
};

//Addresses in the original threaded stream become typed operations and word
//indices. Neither executable pointers nor an x86 instruction engine survive.
struct ResourceScriptWord {
    ResourceOpcode opcode;
    std::uint32_t value{};
};

struct ResourceScriptEntry {
    bool metadata;
    std::uint32_t index;
};
enum class ResourceRunState : std::uint32_t {
    running = 1,
    yielded = 2,
    suspended = 3,
    completed = 4
};

struct ResourceValue {
    std::uint32_t bits{};

    std::int32_t integer() const {
        return std::bit_cast<std::int32_t>(bits);
    }

    float floating() const {
        return std::bit_cast<float>(bits);
    }

    void floating(float value) {
        bits = std::bit_cast<std::uint32_t>(value);
    }
};

struct ResourceVariant {
    //Host allocation identity; never serialized or copied into a new lease.
    std::uint64_t generation{};
    std::uint32_t latched_word{}, handle{}, cursor{}, entry{}, last_cursor{};
    ResourceRunState state = ResourceRunState::running;
    std::int32_t stack_top = 20;
    std::array<std::uint32_t, 20> stack{};
    std::int32_t control_depth{}, call_depth{}, loop_depth{};
    std::uint32_t deadline{}, delay = 62;
    std::array<ResourceValue, 19> values{};
    bool render_enabled{};
};

std::span<const ResourceScriptWord> builtin_resource_variant_programs();
ResourceScriptEntry resource_program_entry(ResourceProgram);
std::uint32_t resource_metadata_program_entry(std::uint32_t);

struct ResourceVariantRuntime {
    std::uint64_t next_generation{}; //Monotonic across pool reset/restore.
    bool initialized{};
    std::uint32_t step_budget = 50, steps_this_update{}, time_snapshot{};
    std::vector<std::unique_ptr<ResourceVariant>> slots;
    ResourceVariant* active{};
    const LoadedSpriteCatalog* catalog{}; //Borrowed from the owning asset session.
    //Quality affects subsequent appearance choices, not the admission of
    //already-live category-relative variants or the immutable catalog ranges.
    std::vector<std::int32_t> appearance_variant_counts;
    std::span<const ResourceScriptWord> program = builtin_resource_variant_programs();
};

void initialize_resource_variant_runtime(WorldRuntime&, const LoadedSpriteCatalog&);
void release_resource_variant_runtime(WorldRuntime&);
ResourceVariant* find_resource_variant(WorldRuntime&, std::uint32_t);
std::uint32_t allocate_resource_variant(WorldRuntime&, ResourceScriptEntry, ResourceArguments);
std::uint32_t create_resource_variant(WorldRuntime&, std::uint32_t entry, std::uint32_t variant, const WorldVector3&);
void refresh_resource_variant(WorldRuntime&, std::uint32_t handle, ResourceScriptEntry);
bool resource_variant_matches(WorldRuntime&, std::uint32_t handle, ResourceScriptEntry);
void release_resource_variant(WorldRuntime&, std::uint32_t);
std::uint32_t run_resource_variant_script(WorldRuntime&, ResourceVariant&);
std::uint32_t process_due_resource_variant_scripts(WorldRuntime&);
void set_resource_variant_integer(WorldRuntime&, std::uint32_t handle, std::uint32_t slot, std::uint32_t);
void set_resource_variant_float(WorldRuntime&, std::uint32_t handle, std::uint32_t slot, float);
void set_resource_variant_extended_frame_metadata(WorldRuntime&, std::uint32_t handle, bool);
void suspend_active_resource_variant(WorldRuntime&, std::uint32_t validation_handle);
void resume_active_resource_variant(WorldRuntime&, std::uint32_t validation_handle);
void call_resource_variant(WorldRuntime&, std::uint32_t handle, std::uint32_t entry);
void return_resource_variant(WorldRuntime&, std::uint32_t handle);
std::uint32_t clone_resource_variant_for_preview(WorldRuntime&,
                                                 std::uint32_t handle,
                                                 float x,
                                                 float y,
                                                 float scale,
                                                 float adjustment);
void copy_resource_variant_state(WorldRuntime&, std::uint32_t destination, std::uint32_t source);
void normalize_resource_variant_indices(WorldRuntime&);
} //namespace simcoaster
