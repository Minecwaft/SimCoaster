#pragma once
#include "sprite_geometry.hpp"
#include <array>
#include <expected>
#include <span>

namespace simcoaster {
//Values are GraphicalOptions.SPRITESHADOW, not the hardware-profile index:
//installed Low=off, Med/Voodoo=low, High=medium. Native high equals medium.
enum class SpriteShadowQuality : std::uint8_t {
    off,
    low,
    medium,
    high
};
enum class SpriteShadowOrder : std::uint8_t {
    batched,
    immediate
};

struct SpriteShadowPolicy {
    SpriteShadowQuality quality{ SpriteShadowQuality::off };
    SpriteShadowOrder order{ SpriteShadowOrder::batched };
};

//An invalid SAM selector preserves the caller's prior native policy.
std::optional<SpriteShadowPolicy> sprite_shadow_policy(std::int32_t selector, bool immediate);

struct SpriteProjection {
    WorldVector3 direction{}, basis{};
    SpriteTransform transform{};
    std::uint32_t color{};
};
enum class SpriteProjectionAdmission {
    admitted,
    filtered,
    full,
    invalid
};

class SpriteProjectionSet {
    std::array<SpriteProjection, 4> entries_{};
    std::size_t count_{};

public:
    //6F47F0: normalize a directional light, cross it with (0,1,0),
    //collapse Y and shear X/Z. Non-downward lights do not consume a slot.
    //Reject nonfinite inputs rather than committing native division garbage.
    SpriteProjectionAdmission add(WorldVector3 direction);

    void clear() {
        count_ = 0;
    }

    std::span<const SpriteProjection> entries() const {
        return { entries_.data(), count_ };
    }
};

enum class SpriteShadowSource {
    selected_frame,
    alphkid
};

struct SpriteShadowSources {
    const SpriteTextureState* selected_frame{};
    const SpriteTextureState* alphkid{};
};

struct SpriteShadowQuad {
    SpriteQuad geometry;
    SpriteShadowSource source{};
};

struct SpriteShadowGeometry {
    std::array<SpriteShadowQuad, 4> quads;
    std::size_t count{};

    std::span<const SpriteShadowQuad> entries() const {
        return { quads.data(), count };
    }
};
enum class SpriteProjectionFailure {
    terrain,
    frame,
    nonfinite,
    outside_terrain
};

struct SpriteProjectionError {
    SpriteProjectionFailure reason{};
    std::uint32_t handle{};
    std::size_t light{}, vertex{};
};

//6FBC30 + 6FC020/6FC2D0. command.scale MUST be the original World scale,
//before the base billboard adapter multiplies it by the camera focal length.
//The command must already have WORK_ELIGIBLE. No script, pose or input state
//is advanced or mutated. Head resources without command0x200 produce no quad.
//
//settings.transform maps World to homogeneous clip coordinates. The existing
//GPU sink clips partially visible quads; this helper only rejects a whole quad
//outside one common clip plane. It preserves native three-sample terrain
//interpolation and checks the owned sample bounds before every access.
//Native base indices were unchecked: an out-of-grid shadow is an explicit
//outside_terrain result, never an out-of-bounds read or invented flat ground.
//command_height_bias is native render-command+0x20, read only for flag0x800.
//The ResourceVariant producer690400 and its two constructors never write that
//malloc-backed field. Default0 is a defined host repair for that native edge,
//not a recovered resource-script slot or the object's World height.
std::expected<SpriteShadowGeometry, SpriteProjectionError> build_sprite_shadow_quads(const ResourceSpriteCommand&,
                                                                                     const EncodedSpriteFrame&,
                                                                                     const SpriteProjectionSet&,
                                                                                     SpriteShadowPolicy,
                                                                                     const SpriteGeometrySettings&,
                                                                                     SpriteShadowSources,
                                                                                     float command_height_bias = 0);
} //namespace simcoaster
