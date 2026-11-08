#pragma once
#include "save_schema.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace simcoaster {
struct WorldRuntime;
struct Stream;
struct DefinitionGrid;
struct MeshAnimation;

//The fixed header is packed on disk. Annotations express the wire order;
//the host object retains natural x64 alignment.
struct SavedMeshInstance {
    [[=save::Field { "slot", 0 }]] std::uint32_t slot {};
    [[=save::Field { "definition_key", 1 }]] std::uint32_t definition_key {};
    [[=save::Field { "placement_x", 2 }]] std::int32_t placement_x {};
    [[=save::Field { "placement_y", 3 }]] std::int32_t placement_y {};
    [[=save::Field { "footprint_width", 4 }]] std::int32_t footprint_width {};
    [[=save::Field { "footprint_height", 5 }]] std::int32_t footprint_height {};
    [[=save::Field { "creation_flags", 6 }]] std::uint32_t creation_flags {};
    [[=save::Field { "owner_context", 7 }]] std::uint32_t owner_context {};
    [[=save::Field { "packed_runtime_flags", 8 }]] std::uint32_t packed_runtime_flags {};
    [[=save::Field { "runtime_state", 9 }]] std::uint16_t runtime_state {};
    [[=save::Field { "animation_phase", 10 }]] float animation_phase {};
    [[=save::Field { "orientation_degrees", 11 }]] std::int32_t orientation_degrees {};
    [[=save::Field { "node_count", 12 }]] std::uint16_t node_count {};
    [[=save::Field { "embedded_node_count", 13 }]] std::uint16_t embedded_node_count {};
    [[=save::Field { "embedded_flags", 14 }]] std::uint32_t embedded_flags {};
    [[=save::Field { "embedded_extra", 15 }]] std::uint32_t embedded_extra {};
};

struct MeshEmbeddedState {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "resource_pool_handle", 1 }]] std::uint32_t resource_pool_handle {};
};

struct MeshAnimationControl {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "animation_id", 1 }]] std::uint32_t animation_id {};
    [[=save::Field { "sub_animation_id", 2 }]] std::uint32_t sub_animation_id {};
    [[=save::Field { "start_time", 3 }]] std::int32_t start_time {};
    [[=save::Field { "animation_time", 4 }]] std::int32_t animation_time {};
    [[=save::Field { "unpaused_time", 5 }]] std::int32_t unpaused_time {};
    [[=save::Field { "duration_divisor", 6 }]] float duration_divisor {};
    [[=save::Field { "deferred_animation_id", 7 }]] std::uint32_t deferred_animation_id {};
    [[=save::Field { "deferred_sub_animation_id", 8 }]] std::uint32_t deferred_sub_animation_id {};
    [[=save::Field { "deferred_flags", 9 }]] std::uint32_t deferred_flags {};
    [[=save::Field { "deferred_duration_divisor", 10 }]] float deferred_duration_divisor {};
    float total_animation_frames {}, animation_frame {};
    const MeshAnimation* previous_resource_block {}; //Runtime-only binding, never serialized.
};

static_assert(save::record_width<SavedMeshInstance> == 58);
static_assert(save::record_width<MeshEmbeddedState> == 8);
static_assert(save::record_width<MeshAnimationControl> == 44);

struct MeshAnimationTiming {
    std::uint32_t start_frame {}, end_frame {};
};

//Mutable projection into a renderer-owned instance; the renderer keeps the
//storage alive through every callback. The spans describe actual model layout,
//not the counts from the save file.
struct MeshRestoreInstance {
    std::uint32_t runtime_flags {}, transient_state {}, model_flags {}, active_animation_count {};
    std::uint16_t runtime_state {};
    float animation_phase {};
    bool has_embedded_state {};
    std::uint32_t embedded_flags {}, embedded_extra {};
    std::span<MeshEmbeddedState> embedded_nodes;
    std::span<std::uint32_t> node_flags;
    std::span<MeshAnimationControl> animation_controls;
};

struct MeshSlotAllocatorState {
    std::uint32_t live_count {}, hole_count {}, next_slot_cursor {};
};

//Process-only diagnostics for the bool-returning native section boundary.
//Offsets address the buffered mesh body, excluding its DWORD size prefix.
struct MeshRestoreFailure {
    std::string_view stage;
    std::size_t body_offset {};
    std::uint32_t ordinal {}, slot {}, definition_key {};
};

struct MeshRestoreBackend {
    std::optional<MeshRestoreFailure> last_mesh_restore_failure;
    virtual ~MeshRestoreBackend() = default;
    virtual void begin_mesh_restore(WorldRuntime&, const MeshSlotAllocatorState&) = 0;
    virtual void update_mesh_animation_clock_sources(WorldRuntime&) = 0;
    //Return the most recently registered template for the key, or zero.
    virtual std::int32_t find_mesh_template_by_key(WorldRuntime&, std::uint32_t) = 0;
    //Used only when the object catalog lacks a definition but the save provides
    //footprint dimensions. The renderer supplies a safe geometry descriptor.
    virtual const DefinitionGrid* fallback_mesh_geometry(WorldRuntime&, std::int32_t width, std::int32_t height) = 0;
    virtual MeshRestoreInstance& mesh_restore_instance(WorldRuntime&, std::uint32_t slot, const SavedMeshInstance&) = 0;
    virtual void rebuild_mesh_deformation_after_restore(WorldRuntime&, std::uint32_t slot) = 0;
    virtual std::optional<MeshAnimationTiming> mesh_animation_timing(WorldRuntime&, std::uint32_t slot, std::uint32_t animation, std::uint32_t sub_animation) = 0;
    virtual void invalidate_mesh_renderer_cache(WorldRuntime&) = 0;

    //Direct projections already own their decoded state. Backends staging into
    //wire records use these hooks to commit native owners and validate the pool.
    virtual bool finish_mesh_restore_instance(WorldRuntime&, std::uint32_t, MeshRestoreInstance&) {
        return true;
    }

    virtual bool finish_mesh_restore(WorldRuntime&) {
        return true;
    }
};

//Read-only renderer projection for save. Metadata contains placement and
//creation values; counts and packed flags are derived from the live view.
struct MeshSaveInstance {
    SavedMeshInstance metadata {};
    std::uint32_t runtime_flags {};
    std::uint16_t runtime_state {};
    float animation_phase {};
    bool has_embedded_state {};
    std::uint32_t embedded_flags {}, embedded_extra {};
    std::span<const MeshEmbeddedState> embedded_nodes;
    std::span<const std::uint32_t> node_flags;
    std::span<const MeshAnimationControl> animation_controls;
};

struct MeshSaveBackend {
    virtual ~MeshSaveBackend() = default;
    virtual MeshSlotAllocatorState mesh_slot_allocator_state(WorldRuntime&) const = 0;
    virtual std::optional<MeshSaveInstance> mesh_save_instance(WorldRuntime&, std::uint32_t slot) const = 0;
};

//Writes the original size-prefixed allocator and mesh instance body.
bool write_mesh_instance_save_section(WorldRuntime&, Stream&, const MeshSaveBackend&);
//Consumes the length-prefixed mesh body after its enclosing section tag.
bool restore_mesh_instance_save_section(WorldRuntime&, Stream&, MeshRestoreBackend&);
} //namespace simcoaster
