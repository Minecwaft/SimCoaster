#pragma once
#include "world_domains_fwd.hpp"
#include "coaster_save.hpp"
#include "dynamic_texture_balance.hpp"
#include "ride_train.hpp"
#include "coaster_profile.hpp"
#include "coaster_mesh.hpp"
#include <array>
#include <optional>
#include <memory>
#include <chrono>

namespace simcoaster {
struct ParkObjectRuntimeView;

struct RsseScriptState;
enum class RsseCoastCommand : std::uint32_t {
    enqueue_person = 1,
    get_space = 2,
    dequeue_person = 3,
    set_playback = 4,
    set_passenger_mode = 5,
    set_capacity = 6,
    resolve_discard = 7,
    bind_map = 8
};
class WorldMeshRuntime;

namespace coaster_editor_detail {
struct EditTransaction;
} //namespace coaster_editor_detail

struct CoasterPylonRuntime {
    struct Channel {
        float minimum {}, maximum { 1 }, scale { 1 }, bias {};
    };

    std::uint32_t flags {};
    std::array<Channel, 5> channels;
    std::array<std::optional<std::size_t>, 4> cap_nodes;
    std::optional<std::size_t> profile_resource, stack_resource;
    std::uint32_t first_attachment { 1 }, attachment_count {}, component_attachment {};
    float height_minimum {}, height_maximum {}, height_offset {};
    float minimum_tangent_dot { -0.99900001f };
    float collision_radius { 5 };
};

struct WorldCoasterDefinition {
    DynamicTextureMeshBalance balance;
    RideTrainConfiguration trains;
    std::array<std::uint32_t, 2> pylon_templates {};
    std::array<CoasterPylonRuntime, 2> pylons;
};

struct CoasterNodeState {
    std::size_t placement {};
    std::uint32_t mesh_slot {};
    std::array<float, 5> controls {};
    std::array<float, 4> angles {};
    std::optional<std::size_t> saved_ordinal;
};

struct CoasterNodeReference {
    std::size_t ride {}, node {};
};

struct CoasterPlacement {
    std::uint32_t flags {};
    std::int32_t tile_x {}, tile_y {};
    std::array<float, 3> position {};
    std::shared_ptr<const WorldCoasterDefinition> definition;
    std::size_t pylon {};
    //Saves specify slots directly; another ride can restore a lower slot later.
    std::vector<std::optional<CoasterNodeReference>> links;
};

//Actual train/car owners, post-load associations, and reconstructed placement,
//spline, attachment, speed-profile and generated track mesh state. Live train
//pose installation remains a separate consumer of that state.
struct WorldCoasterBinding {
    std::uint32_t mesh_slot {}, map_id {}, script_id {};
    ParkObjectRuntimeView* park_object {};
    std::optional<std::uint32_t> pulsing_geometry_slot;
    std::shared_ptr<const WorldCoasterDefinition> definition;
    RideTrainState train_state;
    CoasterRecordSave saved_geometry; //Train snapshots are consumed on restore.
    std::uint32_t speed_setting = 60;
    float speed_cap {};
    float default_attachment_height { 10 };
    std::uint32_t selection_texture_bias {};
    bool geometry_dirty = true;
    std::vector<CoasterGeometryObjectInput> geometry;
    std::vector<CoasterNodeState> geometry_state;
    CoasterCollisionState collision;
    std::optional<CoasterRideTrackRuntime> track;
    //Last published metrics are independent of the optional geometry owner.
    //They start at zero and are replaced when a profile calculation publishes.
    CoasterProfileResult metrics;
    std::optional<CoasterTrackMesh> generated_mesh;
    std::vector<float> texture_phases;
    std::chrono::milliseconds last_texture_update {};
};

struct WorldCoasterRuntime {
    coaster_editor_detail::EditTransaction* active_edit_transaction {};
    std::uint32_t registered_record_count {}, available_id_holes {}, next_map_id {};
    std::vector<WorldCoasterBinding> bindings;
    std::vector<CoasterPlacement> placements;

    WorldCoasterBinding* find(std::uint32_t slot) {
        for (auto& row : bindings) {
            if (row.mesh_slot == slot) {
                return &row;
            }
        }
        return nullptr;
    }

    const WorldCoasterBinding* find(std::uint32_t slot) const {
        for (const auto& row : bindings) {
            if (row.mesh_slot == slot) {
                return &row;
            }
        }
        return nullptr;
    }

    WorldCoasterBinding* find_map(std::uint32_t id) {
        for (auto& row : bindings) {
            if (row.map_id == id) {
                return &row;
            }
        }
        return nullptr;
    }

    const WorldCoasterBinding* find_map(std::uint32_t id) const {
        for (const auto& row : bindings) {
            if (row.map_id == id) {
                return &row;
            }
        }
        return nullptr;
    }
};

std::expected<void, MeshError> prepare_coaster_pylons(AttractionWorld&, WorldMeshRuntime&, WorldCoasterDefinition&);
std::expected<void, MeshError> restore_coaster_geometry(AttractionWorld&, WorldMeshRuntime&);
//Rebuild an existing placement graph without recreating its station/pylons or
//replacing current geometry with the original save snapshot.
std::expected<void, MeshError> refresh_coaster_geometry(AttractionWorld&, WorldMeshRuntime&, std::size_t ride_index);
//Recompute surviving shared pylon stacks after owner links have been removed.
std::expected<void, MeshError> refresh_coaster_placements(AttractionWorld&, WorldMeshRuntime&);
//The owner destructor visits every allocated train/car, including inactive slots.
void destroy_coaster_train_meshes(AttractionWorld&, WorldMeshRuntime&, WorldCoasterBinding&);
//Native479F70 visits only the active train prefix.
void reset_all_coaster_train_rsse(AttractionWorld&, WorldMeshRuntime&);
//47A1E0/479FE0: moving engine requests and sample-driven passenger audio.
void update_coaster_train_audio(AttractionWorld&, WorldMeshRuntime&, WorldCoasterBinding&, RideTrain&);
//481B30: one record, no clock/motion/background pass or GENERATE latch changes.
std::expected<void, MeshError> regenerate_coaster_record(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id);
std::expected<void, MeshError> install_restored_coaster_train_poses(AttractionWorld&, WorldMeshRuntime&);
void transition_coaster_detail_state(AttractionWorld&, WorldMeshRuntime&, WorldCoasterBinding&, RideDetailState);
void step_rsse_coaster_instruction(ScriptWorld&, RsseScriptState&);
std::expected<void, MeshError> advance_coaster_animation(AttractionWorld&, WorldMeshRuntime&, std::chrono::milliseconds);
//Clock-neutral frame consumer, shared with the fixed-step RSSE scheduler.
std::expected<void, MeshError> update_coaster_animation(AttractionWorld&, WorldMeshRuntime&);
} //namespace simcoaster
