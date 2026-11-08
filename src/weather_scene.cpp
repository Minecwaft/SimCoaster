#include "simcoaster/frame_profile.hpp"
#include "simcoaster/weather_runtime.hpp"
#include "simcoaster/sprite_geometry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
using Vec = WorldVector3;
using Clip = std::array<float, 4>;

Vec rotated(const MeshMatrix& matrix, Vec p) {
    Vec out;
    for (unsigned k = 0; k < 3; ++k)
        out[k] = static_cast<float>(static_cast<double>(p[0]) * matrix[k]
                                    + static_cast<double>(p[1]) * matrix[4 + k]
                                    + static_cast<double>(p[2]) * matrix[8 + k]);
    return out;
}

Clip transformed(const MeshMatrix& matrix, Vec p) {
    Clip out;
    for (unsigned k = 0; k < 4; ++k)
        out[k] = static_cast<float>(static_cast<double>(p[0]) * matrix[k]
                                    + static_cast<double>(p[1]) * matrix[4 + k]
                                    + static_cast<double>(p[2]) * matrix[8 + k]
                                    + matrix[12 + k]);
    return out;
}

TextureImagePtr white() {
    static const TextureImagePtr pixel =
        std::make_shared<TextureImage>(TextureImage{ 1, 1, { { 255, 255, 255, 255 } }, TextureAlpha::opaque });
    return pixel;
}

struct Writer {
    EffectScene scene;

    void begin(TextureImagePtr texture, std::uint32_t flags) {
        EffectDraw draw;
        draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
        draw.texture = texture ? std::move(texture) : white();
        draw.material.flags = flags;
        //These are direct renderer batches, not6C2A70's fog-admitting World
        //quad route.6F6C20 sky,6EC260 lightning,6EC6C0 rain and6ECC50 snow
        //supply their own material flags without0x100000. The full-screen
        //flash is likewise an explicit overlay. Preserve that source policy;
        //local weather-volume W is not a physical World fog distance.
        draw.fog_eligibility = WorldFogEligibility::excluded;
        scene.draws.push_back(std::move(draw));
    }

    void polygon(std::span<const EffectVertex> polygon) {
        for (const auto& vertex : polygon)
            if (!std::ranges::all_of(vertex.position, [](float v) {
                    return std::isfinite(v);
                }))
                return;
        const auto first = static_cast<std::uint32_t>(scene.vertices.size());
        scene.vertices.insert(scene.vertices.end(), polygon.begin(), polygon.end());
        for (std::uint32_t i = 1; i + 1 < polygon.size(); ++i)
            scene.indices.insert(scene.indices.end(), { first, first + i, first + i + 1 });
        scene.draws.back().index_count =
            static_cast<std::uint32_t>(scene.indices.size()) - scene.draws.back().first_index;
    }
};

float cube(float v) {
    return v > 3 ? v - 6 : v < -3 ? v + 6 : v;
}

float camera_wrap(double v) {
    const auto integral = static_cast<std::int64_t>(v * 16384.0);
    return static_cast<float>(static_cast<double>(integral & 0x17FFF) * 0.000061035156 - 3.0);
}

void sky_objects(Writer& writer, const WeatherFrame& frame, const SceneCamera& camera, float aspect, bool front) {
    if (!frame.sky_detail)
        return;
    const auto view = camera.view();
    const auto focal = 1.0f / std::tan(camera.vertical_fov * 0.5f);
    const auto orientation = camera.explicit_pose ? sprite_camera_orientation_turns(*camera.explicit_pose) : 0.f;
    for (const auto& object : frame.sky_objects) {
        if (object.front != front)
            continue;
        if (!object.texture)
            throw std::logic_error("Sky object has no live texture");
        const auto position = rotated(view, object.position);
        if (position[2] < camera.near_plane)
            continue;
        const auto half = object.size * 0.5f;
        SpriteQuadVertices offsets{
            { { -half, half, 0, 0 }, { half, half, 0, 0 }, { half, -half, 0, 0 }, { -half, -half, 0, 0 } }
        };
        rotate_sprite_quad_xy_turns(offsets, orientation);
        //6F5170 forces alpha-low and default-UV suppression;6F68C0 adds1800.
        writer.begin(object.texture, 0x556u | 0x400000u | 0x1800u);
        //The registered-object route goes through6C2A70, unlike the sky dome.
        writer.scene.draws.back().fog_eligibility = WorldFogEligibility::native_geometry;
        constexpr std::array<std::array<float, 2>, 4> uv{ { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
        std::array<EffectVertex, 4> quad;
        for (std::size_t i = 0; i < quad.size(); ++i)
            quad[i] = { { position[0] * focal / aspect + offsets[i][0] / aspect,
                          position[1] * focal + offsets[i][1],
                          position[2],
                          position[2] },
                        mesh_argb_color(frame.diffuse_mask),
                        uv[i],
                        position[2] };
        //Native projection mode1 forces depth1 after clipping and suppresses the
        //far-Z clip plane. Owned GPU vertices express that as homogeneous Z=W.
        writer.polygon(quad);
    }
}

void sky(Writer& writer, const WeatherFrame& frame, const SceneCamera& camera, float aspect) {
    if (!frame.sky_layers || !frame.sky || !frame.cylinder)
        return;
    sky_objects(writer, frame, camera, aspect, false);
    auto rotation = camera;
    rotation.target = { 0, 0, 0 };
    //Remove camera translation before projection, retaining view orientation.
    const auto view = rotation.view();
    const auto focal = 1.0f / std::tan(camera.vertical_fov * 0.5f);
    for (unsigned layer = 0; layer < std::min(frame.sky_layers, 4u); ++layer) {
        writer.begin(frame.sky, 5462u | 0x400800u);
        const auto step = static_cast<double>(7 - layer) * 3.1415927 * 0.0089285718;
        //The native rotates its UV basis by90 degrees before each layer.
        constexpr std::array<double, 4> u_scale{ -1, -1, 1, 1 }, v_scale{ 1, -1, -1, 1 };
        const auto color_mask = frame.diffuse_mask & (static_cast<std::uint32_t>(-805306369) - layer * 939524096u);
        const auto first = static_cast<std::uint32_t>(writer.scene.vertices.size());
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned column = 0; column < 16; ++column) {
                auto p = rotated(view, frame.sky_vertices[row * 16 + column]);
                //Sky belongs at the far depth regardless of its background radius.
                const Clip clip{ p[0] * focal / aspect, p[1] * focal, p[2], p[2] };
                writer.scene.vertices.push_back(
                    { clip,
                      mesh_argb_color(color_mask & frame.sky_colors[column * 16 + row]),
                      { static_cast<float>((column * step + frame.sky_offsets[layer][0]) * u_scale[layer]),
                        static_cast<float>((row * step + frame.sky_offsets[layer][2]) * v_scale[layer]) } });
            }
        for (const auto index : frame.sky_indices)
            writer.scene.indices.push_back(first + index);
        writer.scene.draws.back().index_count = static_cast<std::uint32_t>(frame.sky_indices.size());
    }
    writer.begin(frame.cylinder, 0x1D56u);
    const auto first = static_cast<std::uint32_t>(writer.scene.vertices.size());
    constexpr std::array<float, 4> v{ 0.49f, 0.34f, 0.17f, 0.01f };
    for (unsigned half = 0; half < 2; ++half)
        for (unsigned sample = 0; sample < 9; ++sample)
            for (unsigned band = 0; band < 4; ++band) {
                const auto p = rotated(view, frame.cylinder_vertices[half * 36 + sample * 4 + band]);
                writer.scene.vertices.push_back(
                    { { p[0] * focal / aspect, p[1] * focal, p[2], p[2] },
                      mesh_argb_color(frame.diffuse_mask),
                      { static_cast<float>(sample) * 0.125f, v[band] + (half == 0 ? 0.5f : 0.0f) } });
            }
    for (unsigned half = 0; half < 2; ++half)
        for (unsigned sample = 0; sample < 8; ++sample)
            for (unsigned band = 0; band < 3; ++band) {
                const auto i = first + half * 36 + sample * 4 + band;
                writer.scene.indices.insert(writer.scene.indices.end(), { i, i + 5, i + 4, i, i + 1, i + 5 });
            }
    writer.scene.draws.back().index_count = 288;
    sky_objects(writer, frame, camera, aspect, true);
}

void lightning(Writer& writer, const WeatherFrame& frame, const SceneCamera& camera, float aspect) {
    const auto transform = camera.view_projection(aspect);
    for (const auto& bolt : frame.bolts) {
        if (bolt.points.size() < 2)
            continue;
        writer.begin(frame.lightning, 0x200D52u);
        std::array<float, 2> previous_direction{};
        Clip previous{};
        std::array<Clip, 2> edges{};
        bool initialized = false;
        //0x6EAF60 width1.3/(depth+1), perpendicular joins in homogeneous XY.
        const auto half_width = 1.3f / static_cast<float>(bolt.branch_depth + 1);
        const auto normalize = [&](std::array<float, 2> direction) {
            const auto length = std::hypot(static_cast<double>(direction[0]), static_cast<double>(direction[1]));
            if (length)
                for (auto& v : direction)
                    v = static_cast<float>(v * half_width / length);
            return direction;
        };
        const auto edge_pair = [](Clip center, std::array<float, 2> normal) {
            auto left = center, right = center;
            left[0] += normal[1];
            left[1] -= normal[0];
            right[0] -= normal[1];
            right[1] += normal[0];
            return std::array{ left, right };
        };
        for (std::size_t i = 0; i < bolt.points.size(); ++i) {
            const auto point = transformed(transform, bolt.points[i]);
            if (!initialized) {
                const auto next = transformed(transform, bolt.points[i + 1]);
                previous_direction = normalize({ next[0] - point[0], next[1] - point[1] });
                edges = edge_pair(point, previous_direction);
                previous = point;
                initialized = true;
                continue;
            }
            const auto direction = normalize({ point[0] - previous[0], point[1] - previous[1] });
            if (direction[0] == 0 && direction[1] == 0)
                continue;
            const bool last = i + 1 == bolt.points.size();
            const auto join = last ? edge_pair(point, direction)
                                   : edge_pair(previous,
                                               normalize({ direction[0] + previous_direction[0],
                                                           direction[1] + previous_direction[1] }));
            const auto color = mesh_argb_color(bolt.color & frame.diffuse_mask);
            const std::array quad{ EffectVertex{ edges[0], color, { 0, 0 } },
                                   EffectVertex{ edges[1], color, { 1, 0 } },
                                   EffectVertex{ join[1], color, { 1, 1 } },
                                   EffectVertex{ join[0], color, { 0, 1 } } };
            writer.polygon(quad);
            previous_direction = direction;
            previous = point;
            edges = join;
        }
    }
}
} //namespace

EffectScene build_weather_scene(const WeatherFrame& frame, const SceneCamera& camera, float aspect, bool background) {
    FrameTimer timer{ FrameStage::weather };
    if (!(aspect > 0))
        throw std::invalid_argument("Invalid weather camera aspect");
    Writer writer;
    if (background) {
        sky(writer, frame, camera, aspect);
        return std::move(writer.scene);
    }
    if (frame.flash) {
        writer.begin({}, 2465u);
        const RgbaPixel color{ 255, 255, 255, 255 };
        constexpr auto depth = std::bit_cast<float>(981668463u);
        const std::array quad{ EffectVertex{ { -1, -1, depth, 1 }, color, { 0, 0 } },
                               EffectVertex{ { 1, -1, depth, 1 }, color, { 1, 0 } },
                               EffectVertex{ { 1, 1, depth, 1 }, color, { 1, 1 } },
                               EffectVertex{ { -1, 1, depth, 1 }, color, { 0, 1 } } };
        writer.polygon(quad);
    }
    lightning(writer, frame, camera, aspect);
    if (frame.drops.empty() || !(frame.modes & 3u))
        return std::move(writer.scene);
    const bool rain = (frame.modes & 1u) != 0;
    writer.begin(rain ? frame.rain : frame.snow, rain ? 2100562u : 0x556u);
    const auto eye = camera.eye();
    const auto scale = frame.camera_flags & 0x16u ? 0.30000001 : 0.1;
    const Vec offset{ camera_wrap(eye[0] * scale + 3) + frame.offset[0],
                      camera_wrap(3 - eye[1] * scale) + frame.offset[1],
                      camera_wrap(eye[2] * scale + 3) + frame.offset[2] };
    const auto rotation = camera.view();
    //Camera projection replaces native renderer aspect/postmultiply globals.
    const auto focal = 1.0f / std::tan(camera.vertical_fov * 0.5f);
    const auto clip = [&](float x, float y, float z, float w) {
        return Clip{ x * focal / aspect, y * focal, z, w };
    };
    const auto snow_color = mesh_argb_color(frame.diffuse_mask);
    for (std::size_t i = 0; i < frame.drops.size(); ++i) {
        Vec p = frame.drops[i];
        p[0] = cube(p[0] - offset[0]);
        p[1] = cube(p[1] + offset[1]);
        p[2] = cube(p[2] - offset[2]);
        Vec end = p;
        if (rain)
            for (unsigned k = 0; k < 3; ++k)
                end[k] = static_cast<float>(end[k] + frame.drift[k] * 0.1);
        p = rotated(rotation, p);
        if (p[2] <= 0)
            continue;
        if (rain) {
            end = rotated(rotation, end);
            const auto dx = (end[0] - p[0]) * 0.1f, dy = (end[1] - p[1]) * 0.1f;
            const auto intensity =
                static_cast<std::uint32_t>(std::max(static_cast<std::int32_t>((6.0 - p[2]) * 12.0), 0));
            const auto near_color = mesh_argb_color(frame.diffuse_mask & ((197379u * intensity) | 0x80000000u)),
                       far_color = mesh_argb_color(frame.diffuse_mask & ((197379u * intensity) | 0xFF000000u));
            const auto z = p[2] + 0.039999999f, w = p[2] + 0.1f, end_w = end[2] + 0.1f;
            const std::array quad{ EffectVertex{ clip(p[0] - dy, p[1] + dx, z, w), near_color, { 1, 1 } },
                                   EffectVertex{ clip(p[0] + dy, p[1] - dx, z, w), near_color, { 0, 1 } },
                                   EffectVertex{ clip(end[0] + dy, end[1] - dx, z, end_w), far_color, { 0, 0 } },
                                   EffectVertex{ clip(end[0] - dy, end[1] + dx, z, end_w), far_color, { 1, 0 } } };
            writer.polygon(quad);
        } else {
            p[0] *= 1 + p[2] * 0.125f;
            p[1] *= 1 + p[2] * 0.125f;
            const auto u = static_cast<float>(i & 1u) * 0.5f, v = static_cast<float>((i >> 1) & 1u) * 0.5f;
            const auto z = p[2] + 0.1f, w = p[2] + 0.15009999f;
            const std::array triangle{
                EffectVertex{ clip(p[0], p[1] + 0.1f, z, w), snow_color, { u + 0.25f, v } },
                EffectVertex{ clip(p[0] + 0.1f, p[1] - 0.1f, z, w), snow_color, { u + 0.5f, v + 0.5f } },
                EffectVertex{ clip(p[0] - 0.1f, p[1] - 0.1f, z, w), snow_color, { u, v + 0.5f } }
            };
            writer.polygon(triangle);
        }
    }
    return std::move(writer.scene);
}
} //namespace simcoaster
