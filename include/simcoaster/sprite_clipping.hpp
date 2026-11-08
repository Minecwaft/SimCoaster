#pragma once
#include "sprite_geometry.hpp"

namespace simcoaster {
enum class ClipPlane : std::uint8_t {
    z_min = 1,
    z_max = 2,
    x_min = 4,
    x_max = 8,
    y_min = 16,
    y_max = 32,
    all = 63
};

struct ClipVertex {
    SpriteVertex position{};
    std::uint32_t diffuse{}, specular{};
    std::array<float, 2> uv{};
};

struct ClipSummary {
    std::uint8_t common{}, combined{};
};

//The original uses the bit patterns of float inputs, including signed zero
//and NaN, rather than ordered floating-point comparisons for this stage.
std::uint8_t sprite_clip_outcode(const SpriteVertex&, std::uint8_t enabled = 255);
ClipSummary sprite_clip_summary(std::span<const ClipVertex>, std::uint8_t enabled = 255);
std::vector<ClipVertex> clip_sprite_polygon(std::span<const ClipVertex>, std::uint8_t planes);

struct SpriteIndexedBatch {
    std::uint32_t render_state{};
    std::uint64_t texture_storage{};
    SpriteDepthMode depth_mode{};
    //Needed to resolve the pixel source when a GPU storage handle is not yet assigned.
    SpriteFrameReference frame;
    std::vector<ClipVertex> vertices;
    std::vector<std::uint32_t> indices;
};

//Group consecutive compatible quads, preserving input order and using owned
//vertex/index storage. Empty or fully clipped quads produce no batch entries.
std::vector<SpriteIndexedBatch> build_sprite_indexed_batches(const SpriteGeometryPass&,
                                                             std::uint8_t enabled_clip_planes = 255,
                                                             std::uint32_t diffuse_mask = 0xffffffffu);

struct SpriteIndexedBatchSink {
    virtual ~SpriteIndexedBatchSink() = default;
    virtual bool submit(const SpriteIndexedBatch&) = 0;
};

bool submit_sprite_indexed_batches(std::span<const SpriteIndexedBatch>, SpriteIndexedBatchSink&);
} //namespace simcoaster
