#pragma once
#include "world_domains_fwd.hpp"
#include "coaster_geometry.hpp"
#include "sam_record.hpp"
#include <inplace_vector>

namespace simcoaster {
struct WorldCoasterBinding;
struct WorldCoasterRuntime;
struct CoasterRecordSave;

struct CoasterCollisionPoint {
    [[=sam::Field { "fCollisionX", sam::Kind::scalar }]] float x {};
    [[=sam::Field { "fCollisionY", sam::Kind::scalar }]] float y {};
};

struct CoasterCollisionProfile {
    std::vector<CoasterCollisionPoint> points;
};
//These bits share CoasterGeometryObjectInput::geometry_flags. Names describe
//their verified peer-rebuild role, without changing the native bit values.
enum class CoasterPeerFlag : std::uint32_t {
    excluded = 0x10000,
    dirty = 0x200000,
    overflow = 0x400000
};

struct CoasterCollisionNode {
    MeshBounds bounds {};
    //Local geometry ordinals, in native insertion order. Each side of a pair
    //independently holds at most eight references; overflow can be one-sided.
    std::inplace_vector<std::size_t, 8> peers;
};

struct CoasterCollisionState {
    std::vector<CoasterCollisionNode> nodes;
    std::size_t reference_count {};
    bool initialized {};
    //Native wire records omit implicit0/1 lists even when the aggregate counts
    //them. Their original cap/history order is irretrievable. Restore detects
    //that loss and rebuilds ALL ordered lists before opening or drawing a ride.
    bool needs_full_rebuild {};
};

struct CoasterCollisionCandidate {
    std::size_t segment {};
    MeshBounds bounds {};
};

struct CoasterInvalidTrackSection {
    std::size_t texture {};
};

inline constexpr std::array<std::string_view, 3> coaster_diagnostic_texture_names { "m_erase.tga", "red.tga", "blue.tga" };
//Native 0x482710 chooses the invalid section before ordinary section blending.
//Registered diagnostic slots follow the SAM textures: erase, red, blue.
std::optional<CoasterInvalidTrackSection> coaster_invalid_track_section(std::uint32_t flags, std::uint32_t successor_flags, std::size_t peer_count, std::size_t sam_texture_count, std::uint32_t selection_texture_bias);

CoasterCollisionProfile default_coaster_collision_profile();
std::expected<CoasterCollisionProfile, MeshError> parse_coaster_collision_profile(std::string_view);
std::vector<CoasterCollisionCandidate> collect_coaster_collision_candidates(std::span<const RideMotionSample>, std::size_t first, std::size_t last, float padding, const MeshBounds& overlap);
bool coaster_collision_segments_intersect(const CoasterCollisionProfile&, const RideMotionSample& first_a, const RideMotionSample& last_a, const CoasterCollisionProfile&, const RideMotionSample& first_b, const RideMotionSample& last_b);
void remove_coaster_geometry_peer_links(CoasterCollisionState&, std::span<CoasterGeometryObjectInput>, std::size_t);
void mark_coaster_geometry_peers_dirty(WorldCoasterBinding&);
void clear_coaster_geometry_peers(WorldCoasterBinding&);
void seed_coaster_geometry_peer_snapshot(WorldCoasterBinding&);

//Call after samples/profile exist. Only the same design is compared by the
//verified native 0x47AC30 caller; this does not invent cross-ride collisions.
void rebuild_coaster_geometry_peers(WorldCoasterBinding&, const WorldCoasterRuntime&, const CoasterTrackSamples&);
//AFTER insertion into geometry and geometry_state, rebase old peer ordinals.
void insert_coaster_geometry_peer_node(WorldCoasterBinding&, std::size_t);
//BEFORE erasing from geometry and geometry_state, clear this node and its
//predecessor's native links, then rebase the remaining ordinals.
void erase_coaster_geometry_peer_node(WorldCoasterBinding&, std::size_t);
//Native root_peers belongs to geometry ordinal2. Invoke after saved_ordinal
//regeneration, so each other live row reaches its current wire record.
void capture_coaster_geometry_peer_snapshot(WorldCoasterBinding&);
void capture_coaster_geometry_peer_snapshot(const WorldCoasterBinding&, CoasterRecordSave&);
} //namespace simcoaster
