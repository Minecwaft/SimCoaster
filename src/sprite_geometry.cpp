#include "simcoaster/rotation_tables.hpp"
#include "simcoaster/sprite_geometry.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
bool flag(const ResourceSpriteCommand& command, SpriteCommandFlag bit) {
    return (command.flags & static_cast<std::uint32_t>(bit)) != 0;
}

void rotate_quad(SpriteQuadVertices& vertices, float angle, bool xz, const SpriteVertex* anchor = nullptr) {
    const auto axis = xz ? 2u : 1u;
    const auto pivot_x = anchor ? static_cast<double>((*anchor)[0]) : (static_cast<double>(vertices[1][0]) + vertices[0][0]) * .5;
    const auto pivot_y = anchor ? static_cast<double>((*anchor)[axis]) : (static_cast<double>(vertices[2][axis]) + vertices[0][axis]) * .5;
    const double cosine = xz ? std::cos(static_cast<double>(angle)) : sprite_rotation_sines()[static_cast<std::uint8_t>(64u - static_cast<std::uint32_t>(fixed::truncate_integer(static_cast<double>(angle) * -256.)))];
    const double sine = xz ? std::sin(static_cast<double>(angle)) : sprite_rotation_sines()[static_cast<std::uint8_t>(fixed::truncate_integer(static_cast<double>(angle) * 256.))];
    for (auto& vertex : vertices) {
        //The original stores relative coordinates as floats before rotation.
        const auto dx = static_cast<float>(static_cast<double>(vertex[0]) - pivot_x);
        const auto relative_y = static_cast<double>(vertex[axis]) - pivot_y;
        const auto dy = static_cast<float>(relative_y);
        vertex[0] = static_cast<float>(relative_y * sine + static_cast<double>(dx) * cosine + pivot_x);
        vertex[axis] = static_cast<float>(cosine * dy - static_cast<double>(dx) * sine + pivot_y);
    }
}

void apply_terrain_offset(ResourceSpriteCommand& command, const SpriteGeometrySettings& settings) {
    if (!settings.terrain || !settings.terrain->grid) {
        throw std::logic_error("sprite terrain adjustment requires terrain samples");
    }
    const auto& sampling = *settings.terrain;
    const auto& grid = *sampling.grid;
    const auto column = fixed::truncate_integer((static_cast<double>(command.position[0]) - grid.origin_x) * sampling.inverse_scale_x);
    const auto row = fixed::truncate_integer((static_cast<double>(command.position[2]) - grid.origin_y) * sampling.inverse_scale_y);
    if (sampling.sample_width < 0 || sampling.sample_height < 0) {
        throw std::domain_error("negative sprite sample dimensions");
    }
    const auto next_column = std::clamp(static_cast<std::int64_t>(column) + 1, std::int64_t { 0 }, static_cast<std::int64_t>(sampling.sample_width));
    const auto next_row = std::clamp(static_cast<std::int64_t>(row) + 1, std::int64_t { 0 }, static_cast<std::int64_t>(sampling.sample_height));
    const auto height = [&](std::int64_t x, std::int64_t y) {
        const auto stride = sampling.sample_stride ? sampling.sample_stride : static_cast<std::size_t>(sampling.sample_width);
        if (x < 0 || y < 0) {
            throw std::out_of_range("sprite terrain sample");
        }
        const auto sample_column = static_cast<std::size_t>(x), row_index = static_cast<std::size_t>(y);
        if (stride && row_index > (std::numeric_limits<std::size_t>::max() - sample_column) / stride) {
            throw std::out_of_range("sprite terrain sample pitch");
        }
        const auto index = row_index * stride + sample_column;
        if (index >= grid.height_samples.size()) {
            throw std::out_of_range("sprite terrain sample");
        }
        return static_cast<double>(grid.height_samples[index]);
    };
    const auto base = height(column, row);
    const auto offset = (static_cast<double>(command.position[2]) - (static_cast<double>(row) * grid.cell_scale_y + grid.origin_y)) * sampling.inverse_scale_y * (height(column, next_row) - base) + (static_cast<double>(command.position[0]) - (static_cast<double>(column) * grid.cell_scale_x + grid.origin_x)) * sampling.inverse_scale_x * (height(next_column, row) - base) + base;
    command.position[1] = static_cast<float>(offset + command.position[1] + settings.height_bias);
}
} //namespace

SpriteVertex transform_sprite_position(const SpriteTransform& matrix, const WorldVector3& source) {
    SpriteVertex result {};
    for (std::size_t i = 0; i < 4; ++i) {
        result[i] = static_cast<float>(static_cast<double>(source[2]) * matrix[i + 8] + static_cast<double>(source[1]) * matrix[i + 4] + static_cast<double>(source[0]) * matrix[i] + matrix[i + 12]);
    }
    return result;
}

float sprite_camera_orientation_turns(const SpriteTransform& camera_to_world) {
    const auto sine = static_cast<double>(camera_to_world[1]);
    const auto cosine = static_cast<double>(camera_to_world[5]);
    if (!std::isfinite(sine) || !std::isfinite(cosine)) {
        throw std::domain_error("non-finite sprite camera orientation");
    }
    if (sine == 0.) {
        return cosine < 0. ? -.5f : 0.f;
    }
    //Native6DF6C0 normalizes this pair and combines acos(cosine) with the
    //sign of asin(-sine). FLD between FCOMP and FNSTSW leaves the tested x87
    //condition flags undefined. Repair that legacy behavior explicitly:
    //atan2 preserves the intended signed basis without intermediate float
    //rounding, overflow-prone normalization or an undefined status word.
    return static_cast<float>(std::atan2(-sine, cosine) / (2. * std::numbers::pi_v<double>));
}

void rotate_sprite_quad_xy_turns(SpriteQuadVertices& vertices, float turns) {
    rotate_quad(vertices, turns, false);
}

std::uint32_t sprite_texture_render_state(std::uint32_t source_flags, bool direct3d) {
    auto state = (source_flags & 0x30000u) && direct3d ? 0x400000u : 0u;
    if (source_flags & 0x4000000u) {
        state |= 0x7000000u;
    }
    return state | match (source_flags & 0x300u) -> std::uint32_t {
        case 0x100u => 10070u;
        case 0x200u => 1366u;
        case 0x300u => 2098514u;
        case _      => 4694u;
    };
}

bool prepare_sprite_geometry(ResourceSpriteCommand& command, const SpriteGeometrySettings& settings) {
    constexpr auto work = static_cast<std::uint32_t>(SpriteCommandFlag::work_eligible);
    if (flag(command, SpriteCommandFlag::skip_preparation)) {
        command.flags &= ~work;
        return false;
    }
    if (flag(command, SpriteCommandFlag::explicit_coordinates)) {
        command.flags |= work;
        return true;
    }
    command.transformed = transform_sprite_position(settings.transform, command.position);
    const auto& v = command.transformed;
    const auto extent = static_cast<float>(20. * command.scale);
    const auto negative_w = -v[3];
    const bool visible = v[2] > 0.f && static_cast<double>(v[2]) < v[3] && static_cast<double>(v[0]) - extent < v[3] && static_cast<double>(extent) + v[0] > negative_w && static_cast<double>(v[1]) - extent < v[3] && static_cast<double>(extent) + v[1] > negative_w;
    if (visible) {
        command.flags |= work;
    } else {
        command.flags &= ~work;
    }
    return visible;
}

bool expand_sprite_quad(const ResourceSpriteCommand& command, const EncodedSpriteFrame& frame, SpriteQuadVertices& vertices, const SpriteGeometrySettings& settings, float width_override, float height_override, SpritePickState* pick) {
    const double width = width_override == 0.f || std::isnan(width_override) ? static_cast<double>(settings.coordinate_scale) * command.scale * 20. : width_override;
    const auto height = height_override == 0.f || std::isnan(height_override) ? static_cast<float>(20. * command.scale) : height_override;
    auto left = static_cast<float>(static_cast<double>(frame.origin_x) / frame.reference_width * width);
    auto right = static_cast<float>(static_cast<double>(static_cast<int>(frame.width) + frame.origin_x) / frame.reference_width * width);
    auto bottom = static_cast<double>(static_cast<int>(frame.height) + frame.origin_y) / frame.reference_height * height;
    auto top = static_cast<double>(frame.origin_y) / frame.reference_height * height;
    const auto anchor = vertices[0];
    bool parity = false;
    if (flag(command, SpriteCommandFlag::mirror_x)) {
        left = -left;
        right = -right;
        parity = true;
    }
    if (command.flags & 0x60u) {
        top = -top;
        bottom = -bottom;
        parity = !parity;
    }
    const auto x0 = static_cast<float>(static_cast<double>(left) + vertices[0][0]);
    const auto x1 = static_cast<float>(static_cast<double>(right) + vertices[1][0]);
    vertices[0][0] = vertices[3][0] = x0;
    vertices[1][0] = vertices[2][0] = x1;
    if (pick && flag(command, SpriteCommandFlag::projected_frames) && (settings.runtime_flags & 0x1000000u)) {
        const double reciprocal = 1. / vertices[0][3];
        const auto dx = static_cast<double>(pick->x) - static_cast<double>(x0) * reciprocal;
        const auto dx_squared = static_cast<float>(dx * dx);
        const auto dy = static_cast<double>(pick->y) - static_cast<double>(vertices[0][1]) * reciprocal;
        const auto horizontal = static_cast<double>(right) - left, vertical = bottom - top;
        if ((horizontal * horizontal + vertical * vertical) * static_cast<double>(.0002f) > dy * dy + dx_squared && (!pick->handle || static_cast<double>(pick->homogeneous_w) > vertices[0][3])) {
            pick->handle = command.handle;
            pick->homogeneous_w = command.transformed[3];
        }
    }
    if (flag(command, SpriteCommandFlag::world_quad)) {
        parity = !parity;
        vertices[0][2] = vertices[1][2] = static_cast<float>(top + vertices[0][2]);
        vertices[2][2] = vertices[3][2] = static_cast<float>(bottom + vertices[2][2]);
        for (auto& vertex : vertices) {
            vertex[1] = static_cast<float>(static_cast<double>(settings.height_bias) + vertex[1]);
        }
        if (flag(command, SpriteCommandFlag::rotate)) {
            rotate_quad(vertices, std::bit_cast<float>(command.slot9_bits), true);
        }
    } else {
        vertices[0][1] = vertices[1][1] = static_cast<float>(top + vertices[0][1]);
        vertices[2][1] = vertices[3][1] = static_cast<float>(bottom + vertices[2][1]);
        if (flag(command, SpriteCommandFlag::rotate)) {
            rotate_quad(vertices, std::bit_cast<float>(command.slot9_bits), false);
        }
        if (flag(command, SpriteCommandFlag::rotate_about_anchor)) {
            rotate_quad(vertices, settings.orientation_turns, false, &anchor);
        }
    }
    return parity;
}

std::optional<SpriteQuad> build_sprite_quad(ResourceSpriteCommand& command, const EncodedSpriteFrame& frame, const SpriteTextureState* texture, const SpriteGeometrySettings& settings, SpritePickState* pick) {
    if (!flag(command, SpriteCommandFlag::work_eligible)) {
        return std::nullopt;
    }
    SpriteQuad result;
    result.frame = command.selected_frame;
    result.handle = command.handle;
    result.color = command.color;
    if (texture) {
        result.texture_storage = texture->source_flags & 0x1000u ? texture->storage : 0;
        result.render_state = sprite_texture_render_state(texture->source_flags, settings.direct3d_state);
        result.uv = { texture->u_origin, texture->v_origin, static_cast<float>(static_cast<double>(texture->u_extent) + texture->u_origin), static_cast<float>(static_cast<double>(texture->v_extent) + texture->v_origin) };
    } else {
        result.render_state = 4694;
    }
    if (flag(command, SpriteCommandFlag::adjust_terrain)) {
        apply_terrain_offset(command, settings);
    }
    bool parity;
    if (flag(command, SpriteCommandFlag::explicit_coordinates)) {
        auto anchor = SpriteVertex { command.position[0], -command.position[1], command.position[2], 1 };
        if (static_cast<double>(anchor[2]) > .00125) {
            anchor[2] = static_cast<float>(static_cast<double>(anchor[2]) - .00125);
        }
        result.vertices.fill(anchor);
        result.depth_mode = SpriteDepthMode::default_z_scale;
        const auto width = static_cast<float>(static_cast<double>(settings.coordinate_scale) * command.scale * static_cast<double>(.05f));
        const auto height = static_cast<float>(static_cast<double>(command.scale) * static_cast<double>(.05f));
        parity = expand_sprite_quad(command, frame, result.vertices, settings, width, height, pick);
    } else {
        result.depth_mode = SpriteDepthMode::threshold_mapping;
        if (flag(command, SpriteCommandFlag::world_quad)) {
            //Original leaves W unwritten here; XYZ transform replaces it. W=1 keeps
            //optional picking defined without carrying an uninitialized stack word.
            result.vertices.fill({ command.position[0], command.position[1], command.position[2], 1 });
            parity = expand_sprite_quad(command, frame, result.vertices, settings, 0, 0, pick);
            for (auto& vertex : result.vertices) {
                vertex = transform_sprite_position(settings.transform, { vertex[0], vertex[1], vertex[2] });
            }
            result.render_state |= 0x800;
        } else {
            result.vertices.fill(command.transformed);
            parity = expand_sprite_quad(command, frame, result.vertices, settings, 0, 0, pick);
            if (flag(command, SpriteCommandFlag::projected_frames) && !(settings.runtime_flags & 0x2000u)) {
                const auto depth = result.vertices[3][2];
                const auto adjusted = static_cast<float>(static_cast<double>(depth) - .2 / (static_cast<double>(depth) + 1.));
                for (auto& vertex : result.vertices) {
                    vertex[2] = adjusted;
                }
            }
        }
    }
    if (parity) {
        result.packet_flags |= 2;
    }
    if (flag(command, SpriteCommandFlag::reverse_texture_v)) {
        std::swap(result.uv[1], result.uv[3]);
    }
    return result;
}

SpriteGeometryPass build_resource_sprite_geometry(ResourceSpritePass& pass, SpritePixelCatalog& pixels, const SpriteGeometrySettings& settings, const SpriteTextureLookup& lookup, SpritePickState* pick) {
    SpriteGeometryPass output;
    const auto visit = [&](std::uint32_t handle) {
        if (handle >= pass.commands.size() || !pass.commands[handle]) {
            throw std::out_of_range("sprite queue handle");
        }
        auto& command = *pass.commands[handle];
        auto billboard = command;
        if (!flag(command, SpriteCommandFlag::world_quad) && !flag(command, SpriteCommandFlag::explicit_coordinates)) {
            billboard.scale *= settings.billboard_scale;
        }
        const bool eligible = prepare_sprite_geometry(billboard, settings);
        command.flags = billboard.flags;
        command.transformed = billboard.transformed;
        if (!eligible) {
            return;
        }
        const auto& frame = pixels.payload(command.selected_frame.resource).frames.at(command.selected_frame.frame);
        output.cache_requests.push_back({ command.selected_frame, flag(command, SpriteCommandFlag::explicit_coordinates) ? .5f : command.transformed[2] });
        const auto texture = lookup(command.selected_frame);
        if (auto quad = build_sprite_quad(billboard, frame, texture ? &*texture : nullptr, settings, pick)) {
            output.quads.push_back(std::move(*quad));
        }
        command.position = billboard.position;
    };
    for (const auto& region : pass.regions) {
        for (auto handle : region) {
            visit(handle);
        }
    }
    for (auto handle : pass.special) {
        visit(handle);
    }
    return output;
}

bool submit_sprite_geometry(const SpriteGeometryPass& pass, SpriteQuadSink& sink) {
    for (const auto& quad : pass.quads) {
        if (!sink.submit(quad)) {
            return false;
        }
    }
    return true;
}
} //namespace simcoaster
