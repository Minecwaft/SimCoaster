#include "simcoaster/standard_balance.hpp"
#include "simcoaster/mesh_material.hpp"
#include "simcoaster/sam_record.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
float dot(const MeshPoint& a, const MeshPoint& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

MeshPoint normalized(MeshPoint value) {
    const auto length = std::sqrt(dot(value, value));
    if (length > 0 && std::isfinite(length)) {
        for (auto& component : value) {
            component /= length;
        }
    } else {
        value = {};
    }
    return value;
}

using ThemeSettings = ThemeRenderBalance;

MeshPoint rgb(std::uint32_t value) {
    const auto bytes = mesh_argb_color(value);
    return { bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f };
}
} //namespace

RgbaPixel mesh_argb_color(std::uint32_t color) {
    return { static_cast<std::uint8_t>(color >> 16), static_cast<std::uint8_t>(color >> 8), static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(color >> 24) };
}

std::array<float, 4> mesh_texture_uv_transform(const TextureImage& image, std::uint16_t flags) {
    if (!image.width || !image.height) {
        throw std::invalid_argument("Empty mesh texture UV window");
    }
    //6DAAF0 maps slot10/20 to the per-axis zero-inset source flags.
    //Generated coordinates (next-load4) suppress both insets at6D9F1E.
    const bool generated = (flags & 0x6000u) == 0x2000u;
    const auto x = generated || (flags & 0x10u) ? 0.f : .5f / static_cast<float>(image.width);
    const auto y = generated || (flags & 0x20u) ? 0.f : .5f / static_cast<float>(image.height);
    return { 1.f - 2.f * x, 1.f - 2.f * y, x, y };
}

MeshMaterial derive_mesh_material(std::uint32_t node, std::uint8_t group, const MeshTextureSlot* slot, const TextureImage* texture, bool secondary) {
    MeshMaterial result;
    result.secondary_texture = secondary;
    if (texture && slot) {
        result.texture_uv = mesh_texture_uv_transform(*texture, slot->flags);
    }
    result.generated_uv = texture && slot && (slot->flags & 0x6000u) == 0x2000u;
    if (texture && slot && !(slot->flags & 0x800u)) {
        auto mode = texture->source_mode.value_or(texture->alpha == TextureAlpha::opaque ? TextureSourceMode::opaque : (texture->alpha == TextureAlpha::cutout ? TextureSourceMode::alpha_high : TextureSourceMode::alpha_low));
        //Native successive next-load assignments:4000 overrides2000, which overrides1000.
        if (result.generated_uv) {
            mode = TextureSourceMode::opaque;
        }
        if (slot->flags & 0x4000u) {
            mode = TextureSourceMode::additive;
        }
        result.flags = match (mode) -> std::uint32_t {
            case TextureSourceMode::opaque     => 0x1256u;
            case TextureSourceMode::alpha_high => 0x2756u;
            case TextureSourceMode::alpha_low  => 0x556u;
            case TextureSourceMode::additive   => 0x200552u;
        };
    }
    //These source tags apply even when slot800 suppresses its base state.
    if (texture && slot && ((slot->flags & 0x30u) || result.generated_uv)) {
        result.flags |= 0x400000u;
    }
    if (node & 2u) {
        result.flags |= 0x800u;
    }
    if (node & 0x8000000u) {
        result.flags |= 0x20000u;
    }
    if (group & 2u) {
        result.flags |= 0x800000u;
    }
    if (secondary) {
        result.flags |= 0x80005156u;
    }
    return result;
}

std::array<float, 2> mesh_reflection_uv(MeshPoint position, MeshPoint normal) {
    position = normalized(position);
    normal = normalized(normal);
    const auto twice_dot = 2 * dot(position, normal);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        position[axis] -= normal[axis] * twice_dot;
    }
    position[2] += 1;
    const auto denominator = 2 * std::sqrt(dot(position, position));
    if (!(denominator > 0) || !std::isfinite(denominator)) {
        return { 0.5f, 0.5f };
    }
    return { position[0] / denominator + 0.5f, position[1] / denominator + 0.5f };
}

std::expected<MeshLighting, MeshLightingTransformFailure> mesh_lighting_in_local_space(const MeshLighting& lighting, const MeshMatrix& matrix) {
    auto result = lighting;
    if (result.lights.empty()) {
        return result;
    }
    //Row-vector affine inverse, including nonuniform scale. Native transforms
    //lights through the inverse matrix; normals remain in mesh-local space.
    if (!std::ranges::all_of(matrix, [](float value) {
            return std::isfinite(value);
        })) {
        return std::unexpected(MeshLightingTransformFailure::nonfinite);
    }
    using Vector = std::array<double, 3>;
    const auto vector_dot = [](const Vector& a, const Vector& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto vector_cross = [](const Vector& a, const Vector& b) -> Vector {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    };
    const Vector a { matrix[0], matrix[1], matrix[2] }, b { matrix[4], matrix[5], matrix[6] }, c { matrix[8], matrix[9], matrix[10] };
    const std::array cofactors { vector_cross(b, c), vector_cross(c, a), vector_cross(a, b) };
    const auto determinant = vector_dot(a, cofactors[0]);
    if (determinant == 0) {
        return std::unexpected(MeshLightingTransformFailure::singular);
    }
    const auto inverse_direction = [&](const Vector& value) -> Vector {
        return { vector_dot(value, cofactors[0]) / determinant, vector_dot(value, cofactors[1]) / determinant, vector_dot(value, cofactors[2]) / determinant };
    };
    const auto store = [](MeshPoint& destination, const Vector& value) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(value[axis]) || std::abs(value[axis]) > std::numeric_limits<float>::max()) {
                return false;
            }
            destination[axis] = static_cast<float>(value[axis]);
        }
        return true;
    };
    for (auto& light : result.lights) {
        if ((static_cast<unsigned>(light.kind) & 2u) != 0) {
            auto value = inverse_direction({ light.direction[0], light.direction[1], light.direction[2] });
            const auto length = std::hypot(value[0], value[1], value[2]);
            if (!std::isfinite(length)) {
                return std::unexpected(MeshLightingTransformFailure::nonfinite);
            }
            if (length > 0) {
                for (auto& component : value) {
                    component /= length;
                }
            }
            if (!store(light.direction, value)) {
                return std::unexpected(MeshLightingTransformFailure::nonfinite);
            }
        }
        if ((static_cast<unsigned>(light.kind) & 4u) != 0) {
            Vector value;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                value[axis] = static_cast<double>(light.position[axis]) - matrix[12 + axis];
            }
            if (!store(light.position, inverse_direction(value))) {
                return std::unexpected(MeshLightingTransformFailure::nonfinite);
            }
        }
    }
    return result;
}

RgbaPixel mesh_vertex_lighting(const MeshLighting& lighting, const MeshPoint& position, const MeshPoint& normal) {
    auto accumulated = lighting.ambient;
    for (const auto& light : lighting.lights) {
        const auto intensity = match (light.kind) -> float {
            case MeshLightKind::directional                               => std::max(-dot(light.direction, normal), 0.0f);
            case MeshLightKind::point or MeshLightKind::directional_point => do {
                MeshPoint delta;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    delta[axis] = light.position[axis] - position[axis];
                }
                const auto distance_squared = dot(delta, delta);
                if (!(distance_squared < light.radius_squared)) {
                    do_return 0.0f;
                }
                const auto falloff = (light.radius_squared - distance_squared) * light.attenuation;
                //Intentional repair of0x6D7A50 kind6: compute THIS light's distance,
                //replacing the original stale pointer/previous-light scratch value.
                do_return light.kind == MeshLightKind::point ? falloff : falloff * std::max(dot(delta, normal), 0.0f);
            };
        };
        for (std::size_t channel = 0; channel < 3; ++channel) {
            accumulated[channel] += light.color[channel] * intensity;
        }
    }
    RgbaPixel result { 0, 0, 0, 255 };
    const auto mask = mesh_argb_color(lighting.diffuse_mask);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto value = accumulated[channel] * 255.0f;
        if (!std::isfinite(value)) {
            throw std::runtime_error("Nonfinite mesh light intensity");
        }
        result[channel] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
    }
    for (std::size_t channel = 0; channel < 4; ++channel) {
        result[channel] &= mask[channel];
    }
    return result;
}

ThemeRenderSettings load_installed_theme_render_settings(const std::filesystem::path& root, std::string_view theme) {
    ThemeSettings settings;
    const bool loaded = load_standard_balance_overlays(
        root,
        theme,
        [](const auto& path) -> std::optional<std::string> {
            if (!std::filesystem::is_regular_file(path)) {
                return {};
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream) {
                throw std::runtime_error("Cannot read " + path.native_encoded_string());
            }
            return std::string { std::istreambuf_iterator<char> { stream }, {} };
        },
        [&](std::string_view text, bool preserve, const auto& path) {
            const auto parsed = sam::parse_record<ThemeSettings, true>(settings, text, preserve);
            if (!parsed) {
                throw std::runtime_error("Invalid render settings in " + path.native_encoded_string() + " at byte " + std::to_string(parsed.error().offset));
            }
        });
    ThemeRenderSettings result;
    if (!loaded) {
        result.lighting.ambient = { 0.15f, 0.15f, 0.15f };
        return result;
    }
    result.lighting.ambient = rgb(settings.engine.ambient);
    MeshLight light;
    light.color = rgb(settings.engine.directional);
    light.direction = normalized({ settings.normal.x, settings.normal.y, settings.normal.z });
    result.lighting.lights.push_back(light);
    //4288F0 copies both packed fields after the same Standard/theme overlays.
    result.atmosphere = { settings.engine.background, settings.engine.fog };
    result.procedural_bounds = { settings.map.HeightfieldXStart, settings.map.HeightfieldYStart, static_cast<std::int64_t>(settings.map.HeightfieldXStart) + settings.map.HeightfieldWidth + std::max(settings.engine.texture_x, 0), static_cast<std::int64_t>(settings.map.HeightfieldYStart) + settings.map.HeightfieldHeight + std::max(settings.engine.texture_y, 0) };
    return result;
}

MeshLighting load_installed_mesh_lighting(const std::filesystem::path& root, std::string_view theme) {
    return load_installed_theme_render_settings(root, theme).lighting;
}
} //namespace simcoaster
