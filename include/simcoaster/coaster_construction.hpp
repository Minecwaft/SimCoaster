#pragma once
#include "world_domains_fwd.hpp"
#include "coaster_design.hpp"
#include "world_coaster_runtime.hpp"
#include <span>

namespace simcoaster {
enum class CoasterGeometryFlag : std::uint32_t {
    hold_heading = 1,
    hold_incline = 2,
    hold_bank = 4,
    single_axis = 0x40,
    first_of_two_piece = 0x80,
    has_upper_link = 0x100,
    winch = 0x800,
    skip_mesh_updates = 0x2000,
    hide_following_track = 0x10000,
    show_following_track = 0x20000,
    no_next_tangent = 0x40000,
    no_previous_tangent = 0x80000,
    close_tangent_to_node_one = 0x100000,
    peers_dirty = 0x200000,
    peer_capacity_overflow = 0x400000,
    suspended_half_turn = 0x10000000
};

//The parameter defaults and nonzero SAM overrides come from 46DF10/46C7C0.
//Tile distances are distinct from world-space heights (10 world units/cell).
struct CoasterPlacementRules {
    float minimum_height{ 10 }, maximum_height{ 10 }, height_gradient{};
    std::array<float, 4> entrance_angles{ 2.4434609f, 3.1415927f, 1.9198623f, 3.1415927f };
    std::array<float, 4> ordinary_angles{ 1.3962635f, 3.1415927f, 0.87266463f, 3.1415927f };
    float minimum_distance{ 3 }, maximum_distance{ 8 };
    std::uint32_t minimum_distance_squared{ 9 }, maximum_distance_squared{ 64 }, maximum_stack{ 1 };
    float maximum_rotation_deviation{ 0.78539819f }, maximum_loop_rotation_deviation{},
        maximum_loop_height_difference{};
    float cost_per_unit{}, cost_per_length{}, cost_per_height{};
};

CoasterPlacementRules coaster_placement_rules(const DynamicTexturePylonBalance&, bool special);
enum class CoasterPlacementValidity : std::uint32_t {
    valid = 0,
    too_close = 1,
    turn_angle = 2,
    rejected = 3
};

struct CoasterConnectionCandidate {
    std::array<std::int32_t, 2> origin{}, candidate{};
    std::optional<std::array<std::int32_t, 2>> preceding;
    std::uint32_t origin_ordinal{}, origin_owner_flags{};
};

CoasterPlacementValidity validate_coaster_connection(const CoasterPlacementRules&, const CoasterConnectionCandidate&);

struct CoasterSupportLevel {
    float loft{};
    std::uint32_t flags{};
};

struct CoasterSupportSelection {
    std::size_t level{};
    float height{};
};

CoasterSupportSelection select_coaster_support_level(float requested,
                                                     const CoasterPlacementRules&,
                                                     const CoasterPylonRuntime&,
                                                     std::span<const CoasterSupportLevel>,
                                                     bool moving_existing = false);

struct CoasterNodePlacement {
    std::int32_t tile_x{}, tile_y{};
    std::size_t pylon{};
    std::optional<std::size_t> link_slot, insertion_after;
    std::optional<float> attachment_height;
    bool second_piece{}, charge_cost{ true };
};

struct CoasterNodeParameters {
    float loft{}, heading{}, bank{}, incline{}, loop_rotation{};
    bool hold_heading{}, hold_bank{}, hold_incline{};
};
enum class CoasterParameter {
    loft,
    heading,
    bank,
    incline,
    loop_rotation
};

struct CoasterParameterRange {
    float value{}, minimum{}, maximum{}, per_pixel{};
};

//Raw setters preserve the loader/replay contract. Interactive edits use
//the native SAM enable bits, stack-dependent limits, manual-angle flags,
//action records and loft cost settlement recovered from485D00..486480.
std::expected<std::optional<CoasterParameterRange>, MeshError>
coaster_node_parameter_range(const WorldMeshRuntime&, std::uint32_t map_id, std::size_t, CoasterParameter);
std::expected<void, MeshError> edit_coaster_node_parameter(AttractionWorld&,
                                                           WorldMeshRuntime&,
                                                           std::uint32_t map_id,
                                                           std::size_t,
                                                           CoasterParameter,
                                                           float value,
                                                           bool skip_camera_fit = false);
//Fresh construction and editing operate on the existing World-owned mesh,
//train, placement and generated-track owners. No shadow design simulation.
std::expected<std::uint32_t, MeshError> initialize_fresh_coaster_runtime(AttractionWorld&,
                                                                         WorldMeshRuntime&,
                                                                         WorldCoasterRuntime&,
                                                                         std::uint32_t parent_mesh,
                                                                         ParkObjectRuntimeView*);
std::expected<std::size_t, MeshError>
place_coaster_node(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id, const CoasterNodePlacement&);
std::expected<void, MeshError> set_coaster_node_parameters(AttractionWorld&,
                                                           WorldMeshRuntime&,
                                                           std::uint32_t map_id,
                                                           std::size_t,
                                                           const CoasterNodeParameters&);
std::expected<void, MeshError> move_coaster_node(AttractionWorld&,
                                                 WorldMeshRuntime&,
                                                 std::uint32_t map_id,
                                                 std::size_t,
                                                 std::int32_t x,
                                                 std::int32_t y);
std::expected<void, MeshError>
swap_coaster_stack_levels(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id, std::size_t, std::size_t);
std::expected<void, MeshError>
remove_coaster_node(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id, std::size_t);
std::expected<void, MeshError>
set_coaster_track_closed(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id, bool);
std::expected<void, MeshError>
import_coaster_design(AttractionWorld&, WorldMeshRuntime&, std::uint32_t map_id, const CoasterDesign&);
std::expected<CoasterDesign, MeshError>
export_coaster_design(AttractionWorld&, const WorldMeshRuntime&, std::uint32_t map_id);

//Geometry-builder integration, shared by fresh construction and restored
//owners. These retain the verified model animation and attachment pipeline.
std::expected<void, MeshError>
initialize_fresh_coaster_geometry(AttractionWorld&, WorldMeshRuntime&, std::size_t ride_index);
std::expected<void, MeshError>
finalize_coaster_geometry_edit(AttractionWorld&, WorldMeshRuntime&, std::size_t ride_index);
//Collision ownership publishes native ordinal2 root peers and eligible-node
//peers after the wire geometry/header list has been regenerated.
void capture_coaster_geometry_peer_snapshot(WorldCoasterBinding&);
void insert_coaster_geometry_peer_node(WorldCoasterBinding&, std::size_t index);
void erase_coaster_geometry_peer_node(WorldCoasterBinding&, std::size_t index);
} //namespace simcoaster
