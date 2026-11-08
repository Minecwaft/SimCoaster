#pragma once
#include "resource_sprites.hpp"
#include "terrain_grid.hpp"
#include <functional>

namespace simcoaster {
enum class SpriteCommandFlag : std::uint32_t {
    skip_preparation = 1,
    mirror_x = 0x10,
    reverse_vertical_a = 0x20,
    reverse_vertical_b = 0x40,
    rotate = 0x80,
    projected_frames = 0x200,
    adjust_terrain = 0x400,
    rotate_about_anchor = 0x1000,
    reverse_texture_v = 0x8000,
    explicit_coordinates = 0x40000,
    world_quad = 0x80000,
    work_eligible = 0x10000000
};
enum class SpriteDepthMode {
    threshold_mapping = 0,
    constant_depth = 1,
    default_z_scale = 2
};
using SpriteVertex = std::array<float, 4>;
using SpriteQuadVertices = std::array<SpriteVertex, 4>;
//Column-major matrix: source XYZ is promoted with W=1.
using SpriteTransform = std::array<float, 16>;

struct SpriteTerrainSampling {
    const TerrainGridView* grid{};
    std::int32_t sample_width{}, sample_height{};
    float inverse_scale_x{}, inverse_scale_y{};
    //The owned heightfield's pitch differs from its inclusive sample limit.
    //Zero retains an explicitly supplied native raw-context width as pitch.
    std::size_t sample_stride{};
};

struct SpriteGeometrySettings {
    SpriteTransform transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float coordinate_scale = 1, height_bias{}, orientation_turns{};
    std::uint32_t runtime_flags{};
    bool direct3d_state{};
    const SpriteTerrainSampling* terrain{};
    //Adapt only the camera-facing base quad. World projection consumers keep
    //command.scale unchanged and do not inherit the camera focal length.
    float billboard_scale = 1;
};

struct SpritePickState {
    float x{}, y{};
    std::optional<std::uint32_t> handle;
    float homogeneous_w{};
};

struct SpriteTextureState {
    std::uint32_t source_flags{};
    std::uint64_t storage{};
    float u_origin{}, v_origin{}, u_extent{}, v_extent{};
};

struct SpriteQuad {
    SpriteFrameReference frame;
    std::uint32_t handle{}, packet_flags{}, color{}, render_state{};
    std::uint64_t texture_storage{};
    std::array<float, 4> uv{}; //uMin,vMin,uMax,vMax
    SpriteDepthMode depth_mode{};
    SpriteQuadVertices vertices{};
};

struct SpriteCacheRequest {
    SpriteFrameReference frame;
    float sampling_bias{};
};

struct SpriteGeometryPass {
    std::vector<SpriteQuad> quads;
    std::vector<SpriteCacheRequest> cache_requests;
};

SpriteVertex transform_sprite_position(const SpriteTransform&, const WorldVector3&);
//Defined camera-roll replacement for native6DF6C0's undefined x87 flags.
//Zero projected basis returns0; the negative axis is the wrapped -half-turn.
float sprite_camera_orientation_turns(const SpriteTransform& camera_to_world);
//Shared707490 kernel:256-entry turn quantization around the quad centre.
void rotate_sprite_quad_xy_turns(SpriteQuadVertices&, float turns);
std::uint32_t sprite_texture_render_state(std::uint32_t source_flags, bool direct3d);
//Returns queue eligibility, whereas the original routine always returned 1.
bool prepare_sprite_geometry(ResourceSpriteCommand&, const SpriteGeometrySettings&);
//Return value is winding parity, not success. Offsets use frame reference dimensions.
bool expand_sprite_quad(const ResourceSpriteCommand&,
                        const EncodedSpriteFrame&,
                        SpriteQuadVertices&,
                        const SpriteGeometrySettings&,
                        float width_override = 0,
                        float height_override = 0,
                        SpritePickState* pick = nullptr);
std::optional<SpriteQuad> build_sprite_quad(ResourceSpriteCommand&,
                                            const EncodedSpriteFrame&,
                                            const SpriteTextureState*,
                                            const SpriteGeometrySettings&,
                                            SpritePickState* pick = nullptr);
using SpriteTextureLookup = std::function<std::optional<SpriteTextureState>(SpriteFrameReference)>;
//Visits region queues in their stored order followed by the explicit-coordinate queue.
SpriteGeometryPass build_resource_sprite_geometry(ResourceSpritePass&,
                                                  SpritePixelCatalog&,
                                                  const SpriteGeometrySettings&,
                                                  const SpriteTextureLookup&,
                                                  SpritePickState* pick = nullptr);

//Backend owns actual GPU submission and clipping; a failed submission stops this pass.
struct SpriteQuadSink {
    virtual ~SpriteQuadSink() = default;
    virtual bool submit(const SpriteQuad&) = 0;
};

bool submit_sprite_geometry(const SpriteGeometryPass&, SpriteQuadSink&);
} //namespace simcoaster
