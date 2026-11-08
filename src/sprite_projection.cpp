#include "simcoaster/sprite_projection.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
constexpr bool has(const ResourceSpriteCommand& command, SpriteCommandFlag bit) {
    return (command.flags & std::to_underlying(bit)) != 0;
}

template <class Range>
bool finite(const Range& range) {
    return std::ranges::all_of(range, [](float value) {
        return std::isfinite(value);
    });
}

bool outside_clip(const SpriteQuadVertices& vertices) {
    const auto outside = [&](auto distance) {
        return std::ranges::all_of(vertices, [&](const auto& vertex) {
            return distance(vertex) < 0.f;
        });
    };
    return outside([](const auto& v) {
               return v[0] + v[3];
           })
        || outside([](const auto& v) {
               return v[3] - v[0];
           })
        || outside([](const auto& v) {
               return v[1] + v[3];
           })
        || outside([](const auto& v) {
               return v[3] - v[1];
           })
        || outside([](const auto& v) {
               return v[2];
           })
        || outside([](const auto& v) {
               return v[3] - v[2];
           });
}

//The native terrain renderer's sample pitch is an independent field. Owned
//TerrainGridView expresses it unambiguously as width_cells+1; it is not the
//cell-record pitch and is not the legacy SpriteTerrainSampling width limit.
bool valid_terrain(const TerrainGridView& terrain) {
    if (!terrain.width_cells || !terrain.height_cells || !std::isfinite(terrain.origin_x) || !std::isfinite(terrain.origin_y) || !std::isfinite(terrain.cell_scale_x) || !std::isfinite(terrain.cell_scale_y) || terrain.cell_scale_x <= 0 || terrain.cell_scale_y <= 0) {
        return false;
    }
    const auto columns = static_cast<std::size_t>(terrain.width_cells) + 1;
    const auto rows = static_cast<std::size_t>(terrain.height_cells) + 1;
    return columns && rows <= std::numeric_limits<std::size_t>::max() / columns && terrain.height_samples.size() == columns * rows;
}

std::expected<double, SpriteProjectionFailure> terrain_height(const SpriteTerrainSampling& sampling, float x, float z) {
    const auto& terrain = *sampling.grid;
    const auto column_position = (static_cast<double>(x) - terrain.origin_x) * sampling.inverse_scale_x;
    const auto row_position = (static_cast<double>(z) - terrain.origin_y) * sampling.inverse_scale_y;
    //Check before conversion. Native truncation of a negative subcell value
    //could index row0 and extrapolate; do not extend the owned World outside
    //its actual heightfield or wrap an extreme coordinate through int32.
    if (!std::isfinite(column_position) || !std::isfinite(row_position)) {
        return std::unexpected(SpriteProjectionFailure::nonfinite);
    }
    const auto domain_column = (static_cast<double>(x) - terrain.origin_x) / terrain.cell_scale_x;
    const auto domain_row = (static_cast<double>(z) - terrain.origin_y) / terrain.cell_scale_y;
    if (domain_column < 0 || domain_row < 0 || domain_column > terrain.width_cells || domain_row > terrain.height_cells) {
        return std::unexpected(SpriteProjectionFailure::outside_terrain);
    }
    if (column_position >= static_cast<double>(terrain.width_cells) + 1 || row_position >= static_cast<double>(terrain.height_cells) + 1) {
        return std::unexpected(SpriteProjectionFailure::terrain);
    }
    const auto column = std::min(static_cast<std::size_t>(column_position), static_cast<std::size_t>(terrain.width_cells));
    const auto row = std::min(static_cast<std::size_t>(row_position), static_cast<std::size_t>(terrain.height_cells));
    const auto next_column = std::min(column + 1, static_cast<std::size_t>(terrain.width_cells));
    const auto next_row = std::min(row + 1, static_cast<std::size_t>(terrain.height_cells));
    const auto pitch = static_cast<std::size_t>(terrain.width_cells) + 1;
    const double base = terrain.height_samples[row * pitch + column];
    const double right = terrain.height_samples[row * pitch + next_column];
    const double below = terrain.height_samples[next_row * pitch + column];
    const auto column_fraction = (static_cast<double>(x) - (static_cast<double>(column) * terrain.cell_scale_x + terrain.origin_x)) * sampling.inverse_scale_x;
    const auto row_fraction = (static_cast<double>(z) - (static_cast<double>(row) * terrain.cell_scale_y + terrain.origin_y)) * sampling.inverse_scale_y;
    const auto result = base + column_fraction * (right - base) + row_fraction * (below - base);
    if (!std::isfinite(result)) {
        return std::unexpected(SpriteProjectionFailure::nonfinite);
    }
    return result;
}
} //namespace

std::optional<SpriteShadowPolicy> sprite_shadow_policy(std::int32_t selector, bool immediate) {
    if (selector < 0 || selector > 3) {
        return std::nullopt;
    }
    return SpriteShadowPolicy { static_cast<SpriteShadowQuality>(selector), immediate ? SpriteShadowOrder::immediate : SpriteShadowOrder::batched };
}

SpriteProjectionAdmission SpriteProjectionSet::add(WorldVector3 direction) {
    if (count_ == entries_.size()) {
        return SpriteProjectionAdmission::full;
    }
    if (!finite(direction)) {
        return SpriteProjectionAdmission::invalid;
    }
    const auto length = std::sqrt(static_cast<double>(direction[0]) * direction[0] + static_cast<double>(direction[1]) * direction[1] + static_cast<double>(direction[2]) * direction[2]);
    if (length != 0) {
        for (auto& value : direction) {
            value = static_cast<float>(static_cast<double>(value) / length);
        }
    }
    const auto weight = -127. * direction[1];
    if (weight <= 0) {
        return SpriteProjectionAdmission::filtered;
    }
    SpriteProjection projection;
    projection.direction = direction;
    projection.basis = { direction[2], 0, -direction[0] };
    projection.transform = { 1, 0, 0, 0, -direction[0] / direction[1], 0, -direction[2] / direction[1], 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    if (!finite(projection.transform)) {
        return SpriteProjectionAdmission::invalid;
    }
    projection.color = static_cast<std::uint32_t>(weight) << 24;
    entries_[count_++] = projection;
    return SpriteProjectionAdmission::admitted;
}

std::expected<SpriteShadowGeometry, SpriteProjectionError> build_sprite_shadow_quads(const ResourceSpriteCommand& command, const EncodedSpriteFrame& frame, const SpriteProjectionSet& projections, SpriteShadowPolicy policy, const SpriteGeometrySettings& settings, SpriteShadowSources sources, float command_height_bias) {
    SpriteShadowGeometry result;
    if (policy.quality == SpriteShadowQuality::off || projections.entries().empty() || !has(command, SpriteCommandFlag::projected_frames) || !has(command, SpriteCommandFlag::work_eligible)) {
        return result;
    }
    const bool low = policy.quality == SpriteShadowQuality::low && policy.order == SpriteShadowOrder::batched;
    const auto source_kind = low ? SpriteShadowSource::alphkid : SpriteShadowSource::selected_frame;
    const auto* source = low ? sources.alphkid : sources.selected_frame;
    //6FD6E0 does not enter Low's matrix pass without an actual override source.
    //Preserve that lazy gate even when an unused frame/terrain is malformed.
    if (low && !source) {
        return result;
    }
    const auto error = [&](SpriteProjectionFailure reason, std::size_t light = 0, std::size_t vertex = 0) {
        return std::unexpected(SpriteProjectionError { reason, command.handle, light, vertex });
    };
    if (!frame.reference_width || !frame.reference_height) {
        return error(SpriteProjectionFailure::frame);
    }
    if (!settings.terrain || !settings.terrain->grid || !valid_terrain(*settings.terrain->grid) || !std::isfinite(settings.terrain->inverse_scale_x) || !std::isfinite(settings.terrain->inverse_scale_y) || settings.terrain->inverse_scale_x <= 0 || settings.terrain->inverse_scale_y <= 0) {
        return error(SpriteProjectionFailure::terrain);
    }
    if (!finite(command.position) || !std::isfinite(command.scale) || !std::isfinite(settings.coordinate_scale) || !std::isfinite(settings.height_bias) || !finite(settings.transform) || (has(command, SpriteCommandFlag::rotate) && !std::isfinite(std::bit_cast<float>(command.slot9_bits))) || ((command.flags & 0x800u) && !std::isfinite(command_height_bias))) {
        return error(SpriteProjectionFailure::nonfinite);
    }

    SpriteTextureState texture = source ? *source : SpriteTextureState {};
    if (low) {
        texture.source_flags = (texture.source_flags & ~0x80u) | 0x40040u;
    }
    if (!finite(std::array { texture.u_origin, texture.v_origin, texture.u_extent, texture.v_extent })) {
        return error(SpriteProjectionFailure::nonfinite);
    }

    const double height = 20. * command.scale;
    const double width = settings.coordinate_scale * height;
    auto left = static_cast<float>(static_cast<double>(frame.origin_x) / frame.reference_width * width);
    auto right = static_cast<float>((static_cast<int>(frame.width) + frame.origin_x) / static_cast<double>(frame.reference_width) * width);
    if (has(command, SpriteCommandFlag::mirror_x)) {
        left = -left;
        right = -right;
    }
    auto upper = static_cast<float>((static_cast<int>(frame.height) + frame.origin_y) / static_cast<double>(frame.reference_height) * height);
    double lower = static_cast<double>(frame.origin_y) / frame.reference_height * height;
    //Unlike ordinary billboards,6FBC30 tests0x20, not the combined0x60 mask.
    if (has(command, SpriteCommandFlag::reverse_vertical_a)) {
        upper = -upper;
        lower = -lower;
    }
    const auto top_y = static_cast<float>(upper + command.position[1]);
    const auto bottom_y = static_cast<float>(lower + command.position[1]);
    const auto minimum_y = std::min(top_y, bottom_y);
    const auto bias = settings.height_bias + ((command.flags & 0x800u) ? command_height_bias : 0.f);

    std::size_t light_index = 0;
    for (const auto& projection : projections.entries()) {
        SpriteShadowQuad output;
        output.source = source_kind;
        auto& quad = output.geometry;
        quad.frame = command.selected_frame;
        quad.handle = command.handle;
        quad.color = projection.color;
        quad.texture_storage = (texture.source_flags & 0x1000u) ? texture.storage : 0;
        quad.render_state = source ? sprite_texture_render_state(texture.source_flags, settings.direct3d_state) : 0x1256u;
        quad.render_state |= 0x800u;
        if (command.flags & 0x20000u) {
            quad.render_state |= 0x20000u;
        }
        quad.render_state &= ~0x200u;
        if (low) {
            quad.render_state = (quad.render_state & ~0xFFu) | 0x13u;
            quad.color = 0xFF000000u | (0xFFFFFFu - 65793u * (projection.color >> 24));
        }
        quad.uv = source ? std::array { texture.u_origin, texture.v_origin, texture.u_origin + texture.u_extent, texture.v_origin + texture.v_extent } : std::array<float, 4> {};
        //6FD3F0 makes alphkid a vertically flipped source; upload_hardware_texture
        //6FF8A0 flips rows. Adapt at UVs while leaving decoded original pixels owned.
        if (texture.source_flags & 0x40000u) {
            std::swap(quad.uv[1], quad.uv[3]);
        }
        if (texture.source_flags & 0x100000u) {
            quad.packet_flags |= 4u;
        }
        //The immediate native wrapper left this stack field unwritten. Use the
        //explicit threshold mode from6FC2D0 instead of uninitialized legacy state.
        quad.depth_mode = SpriteDepthMode::threshold_mapping;
        const auto x0 = static_cast<float>(left * projection.basis[0] + command.position[0]);
        const auto x1 = static_cast<float>(right * projection.basis[0] + command.position[0]);
        const auto z0 = static_cast<float>(left * projection.basis[2] + command.position[2]);
        const auto z1 = static_cast<float>(right * projection.basis[2] + command.position[2]);
        quad.vertices = { { { x0, top_y - minimum_y, z0, 1 }, { x1, top_y - minimum_y, z1, 1 }, { x1, bottom_y - minimum_y, z1, 1 }, { x0, bottom_y - minimum_y, z0, 1 } } };
        if (has(command, SpriteCommandFlag::rotate)) {
            rotate_sprite_quad_xy_turns(quad.vertices, -std::bit_cast<float>(command.slot9_bits));
        }
        for (std::size_t index = 0; index < quad.vertices.size(); ++index) {
            auto vertex = transform_sprite_position(projection.transform, { quad.vertices[index][0], quad.vertices[index][1], quad.vertices[index][2] });
            if (!finite(vertex)) {
                return error(SpriteProjectionFailure::nonfinite, light_index, index);
            }
            const auto ground = terrain_height(*settings.terrain, vertex[0], vertex[2]);
            if (!ground) {
                return error(ground.error(), light_index, index);
            }
            vertex[1] = static_cast<float>(*ground + vertex[1] + bias);
            quad.vertices[index] = transform_sprite_position(settings.transform, { vertex[0], vertex[1], vertex[2] });
            if (!finite(quad.vertices[index])) {
                return error(SpriteProjectionFailure::nonfinite, light_index, index);
            }
        }
        if (!outside_clip(quad.vertices)) {
            result.quads[result.count++] = std::move(output);
        }
        ++light_index;
    }
    return result;
}
} //namespace simcoaster
