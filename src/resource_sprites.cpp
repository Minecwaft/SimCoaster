#include "simcoaster/resource_sprites.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
SpriteFrameIndexEntry frame_layout(const SpriteDescriptor& sprite, std::int32_t selector) {
    if (selector == -1) {
        //The original frame[-1] aliases the last metadata pair and count WORD.
        const auto& last = sprite.component_metadata.back();
        const auto count = sprite.nonzero_frame_stride_entry_count();
        return { static_cast<std::uint16_t>(last.value_02 | (std::uint16_t{ last.value_03 } << 8)),
                 static_cast<std::uint8_t>(count),
                 static_cast<std::uint8_t>(count >> 8) };
    }
    return sprite.frame_index_entries.at(static_cast<std::size_t>(selector));
}

std::int32_t region_coordinate(float value, std::int32_t size) {
    if (!size)
        throw std::domain_error("zero sprite region dimension");
    return std::clamp((fixed::truncate_integer(value) / 10) / size, 0, 3);
}
} //namespace

void begin_resource_sprite_pass(WorldRuntime& c, ResourceSpritePass& pass, const ResourceSpritePassSettings& settings) {
    for (auto& region : pass.regions)
        region.clear();
    pass.special.clear();
    pass.commands.clear();
    pass.commands.resize(c.resources.variants.slots.size());
    if (c.view.flags & 0x16)
        pass.camera_heading = settings.first_person_heading_radians;
    else {
        const auto heading = std::fmod(static_cast<double>(settings.heading_radians), 6.283185482025146);
        pass.camera_heading = static_cast<float>(heading);
        if (heading < static_cast<double>(-0.001f))
            pass.camera_heading =
                static_cast<float>(static_cast<double>(pass.camera_heading) + static_cast<double>(6.2831855f));
    }
}

bool queue_resource_sprite(WorldRuntime& c,
                           SpritePixelCatalog& pixels,
                           ResourceSpritePass& pass,
                           const ResourceSpritePassSettings& settings,
                           std::uint32_t handle) {
    const auto* state = find_resource_variant(c, handle);
    if (!state
        || !state->render_enabled
        || state->values[13].bits == std::numeric_limits<std::uint32_t>::max()
        || !c.resources.sprite_resource_rendering_enabled)
        return false;
    if (!c.resources.variants.catalog)
        throw std::logic_error("sprite commands require a loaded catalog");
    const auto& values = state->values;
    const auto category = values[10].bits, variant = values[11].bits, flags = values[16].bits,
               rotation = values[15].bits, frame_offset = values[13].bits;
    const auto selector = values[12].integer();
    const auto x = values[1].floating(), y = values[3].floating();
    const auto height_offset = values[2].floating();
    auto scale = values[8].floating();
    const auto slot9 = values[9].bits;
    const auto alpha = static_cast<std::uint8_t>(values[7].bits);
    const double height = (flags & 0x1000000u)
                            ? static_cast<double>(height_offset)
                            : world_operations::sample_terrain_surface_height(c, x, y) + height_offset;
    const auto region = static_cast<std::size_t>(region_coordinate(y, settings.region_height_cells)
                                                 + 4 * region_coordinate(x, settings.region_width_cells));
    if (pass.regions[region].size() >= 2048)
        return false;
    const auto& assets = *c.resources.variants.catalog;
    if (pixels.catalog != &assets)
        pixels.bind(assets);
    const auto resource = static_cast<std::uint32_t>(assets.variants.at(category).first_loaded_resource_index)
                        + variant
                        + static_cast<std::uint32_t>(selector / 16);
    const auto layout = frame_layout(assets.resources.at(resource).descriptor, selector % 16);
    const auto camera = static_cast<double>(pass.camera_heading);
    const double camera_position =
        c.view.flags & 0x16 ? (camera * static_cast<double>(0.31830987f) + 1.0) * 4.0 + 4.5
                            : (camera + static_cast<double>(0.39269909f)) * static_cast<double>(0.15915494f) * 8.0;
    const auto direction =
        (flags & 0x40000u) ? 0 : 8 - (static_cast<std::uint32_t>(fixed::truncate_integer(camera_position)) & 7u);
    pass.camera_direction_offset = static_cast<std::int32_t>(direction);
    std::uint32_t selected = 0;
    bool mirror = false;
    if (layout.variant_frame_count && !(flags & 0x2000000u)) {
        selected = (static_cast<std::uint8_t>(rotation) + direction) & 7u;
        if (selected > static_cast<std::uint32_t>(layout.variant_frame_count - 1)) {
            mirror = true;
            selected = 8u - selected;
        }
    }
    const auto selected_frame =
        static_cast<std::uint32_t>(layout.base_frame_index) + frame_offset + selected * layout.frame_stride;
    const auto& payload = pixels.payload(resource);
    if (selected_frame >= payload.frames.size())
        throw std::out_of_range("sprite command selects an absent frame");
    if (category == 19) {
        const auto factor = c.visitors.environment.GenericUmbrellaModifiers.ScalingFactor;
        if (factor != 0.f && !std::isnan(factor))
            scale = static_cast<float>(static_cast<double>(factor) * scale);
    }
    ResourceSpriteCommand command{ handle,
                                   (flags & 0xFFFFFFu) | (category == 9 ? 0x100000u : 0u) | (mirror ? 0x10u : 0u),
                                   (std::uint32_t{ alpha } << 24) + 0xFFFFFFu,
                                   slot9,
                                   { x, static_cast<float>(height), y },
                                   scale,
                                   { resource, selected_frame },
                                   {} };
    if (flags & 0x200u) {
        auto reflected = static_cast<std::uint8_t>(rotation);
        if (std::bit_cast<std::int32_t>(rotation) > static_cast<int>(layout.variant_frame_count) - 1)
            reflected = static_cast<std::uint8_t>(8u - reflected);
        command.extended =
            ExtendedSpriteFrame{ resource,
                                 layout.base_frame_index,
                                 static_cast<std::uint8_t>(layout.frame_stride + 16u * layout.variant_frame_count),
                                 static_cast<std::uint8_t>(static_cast<std::uint8_t>(frame_offset) + 16u * reflected) };
    }
    if (handle >= pass.commands.size())
        pass.commands.resize(static_cast<std::size_t>(handle) + 1u);
    pass.commands[handle] = command;
    if (flags & 0x40000u) {
        if (pass.special.size() >= 32)
            throw std::length_error("special sprite queue capacity");
        pass.special.push_back(handle);
    } else
        pass.regions[region].push_back(handle);
    return true;
}

void build_resource_sprite_pass(WorldRuntime& c,
                                SpritePixelCatalog& pixels,
                                ResourceSpritePass& pass,
                                const ResourceSpritePassSettings& settings) {
    if (!c.resources.variants.initialized)
        return;
    begin_resource_sprite_pass(c, pass, settings);
    for (std::size_t id = 1; id < c.resources.variants.slots.size(); ++id)
        if (c.resources.variants.slots[id])
            (void)queue_resource_sprite(c, pixels, pass, settings, static_cast<std::uint32_t>(id));
}
} //namespace simcoaster
