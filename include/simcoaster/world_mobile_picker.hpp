#pragma once
#include "world_domains_fwd.hpp"
#include "mesh_scene.hpp"
#include "sprite_geometry.hpp"

namespace simcoaster {
struct ThingHeader;

struct WorldMobilePick {
    std::uint16_t thing {};
    const ThingHeader* identity {};
    std::uint32_t resource {};
    SpriteFrameReference frame;
    float homogeneous_w {};
};

//User-approved repair of original6FB8C0/67A040: pick the displayed sprite
//triangles and source alpha mask, then resolve its actual mobile body owner.
//Camera FOV, mirror/rotation and animation use the same drawing kernels.
//Querying never ticks scripts, advances animation, or rewrites mobile poses.
class WorldMobilePicker {
    SpritePixelCatalog pixels_;
    ResourceSpritePass pass_;
    std::vector<const ThingHeader*> body_owners_;

public:
    std::optional<WorldMobilePick> pick(VisitorWorld&, const SceneCamera&, const TerrainGridView&, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height);
};
} //namespace simcoaster
