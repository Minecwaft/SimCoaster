#pragma once
#include "resource_variant.hpp"
#include "sprite_pixels.hpp"

namespace simcoaster {
struct SpriteFrameReference {
    std::size_t resource {}, frame {};
};

struct ExtendedSpriteFrame {
    std::size_t resource {};
    std::uint16_t base_frame {};
    std::uint8_t packed_stride {}, selected_frame {};
};

struct ResourceSpriteCommand {
    std::uint32_t handle {}, flags {}, color {}, slot9_bits {};
    WorldVector3 position {};
    float scale {};
    SpriteFrameReference selected_frame;
    std::optional<ExtendedSpriteFrame> extended;
    //Derived by frame preparation; never serialized as the original pointer cache.
    std::array<float, 4> transformed {};
};

struct ResourceSpritePassSettings {
    float heading_radians {}, first_person_heading_radians {};
    std::int32_t region_width_cells {}, region_height_cells {};
};

struct ResourceSpritePass {
    float camera_heading {};
    std::int32_t camera_direction_offset {};
    std::vector<std::optional<ResourceSpriteCommand>> commands;
    std::array<std::vector<std::uint32_t>, 16> regions;
    std::vector<std::uint32_t> special;
};

void begin_resource_sprite_pass(WorldRuntime&, ResourceSpritePass&, const ResourceSpritePassSettings&);
bool queue_resource_sprite(WorldRuntime&, SpritePixelCatalog&, ResourceSpritePass&, const ResourceSpritePassSettings&, std::uint32_t handle);
void build_resource_sprite_pass(WorldRuntime&, SpritePixelCatalog&, ResourceSpritePass&, const ResourceSpritePassSettings&);
} //namespace simcoaster
