#include "simcoaster/sprite_clipping.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr auto make_clip_lookup() {
    std::array<std::uint8_t, 128> lookup{};
    for (unsigned i = 0; i < lookup.size(); ++i) {
        const auto x = static_cast<int>(3 * (i & 1u) + 1) * (1 - 2 * static_cast<int>((i >> 1) & 1u));
        const auto y = static_cast<int>(3 * ((i >> 2) & 1u) + 1) * (1 - 2 * static_cast<int>((i >> 3) & 1u));
        const auto z = static_cast<int>(3 * ((i >> 4) & 1u) + 1) * (1 - 2 * static_cast<int>((i >> 5) & 1u));
        const auto w = 2 - 4 * static_cast<int>((i >> 6) & 1u);
        lookup[i] = static_cast<std::uint8_t>((z < 0 ? 1 : 0)
                                              | (z > w ? 2 : 0)
                                              | (x < -w ? 4 : 0)
                                              | (x > w ? 8 : 0)
                                              | (y < -w ? 16 : 0)
                                              | (y > w ? 32 : 0));
    }
    return lookup;
}

constexpr auto clip_lookup = make_clip_lookup();

double plane_distance(const SpriteVertex& v, unsigned plane) {
    match (plane) {
        case 0 => return v[2];
        case 1 => return static_cast<double>(v[3]) - v[2];
        case 2 => return static_cast<double>(v[3]) + v[0];
        case 3 => return static_cast<double>(v[3]) - v[0];
        case 4 => return static_cast<double>(v[3]) + v[1];
        case _ => return static_cast<double>(v[3]) - v[1];
    }
}

bool inside_plane(const SpriteVertex& v, unsigned plane) {
    constexpr std::array<std::array<double, 4>, 6> coefficients{
        { { 0, 0, 1, 0 }, { 0, 0, -1, 1 }, { 1, 0, 0, 1 }, { -1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, -1, 0, 1 } }
    };
    const auto& p = coefficients[plane];
    const auto distance = v[3] * p[3] + v[1] * p[1] + v[0] * p[0] + v[2] * p[2];
    return distance >= 0.; //Unordered is outside in the original clipper.
}

std::uint32_t blend_color(std::uint32_t a, std::uint32_t b, double weight) {
    if (a == b)
        return a;
    const auto scaled = static_cast<float>(static_cast<double>(weight) * 255.);
    const auto rounded = std::nearbyint(static_cast<double>(scaled));
    const auto fixed = std::isfinite(rounded)
                            && rounded >= std::numeric_limits<std::int32_t>::min()
                            && rounded <= std::numeric_limits<std::int32_t>::max()
                         ? static_cast<std::int32_t>(rounded)
                         : std::numeric_limits<std::int32_t>::min();
    //Native IMUL wraps before SAR. Unsigned arithmetic makes that wrap defined.
    const auto product = static_cast<std::uint32_t>(fixed) * ((a & 0xff00ffu) - (b & 0xff00ffu));
    const auto signed_product = std::bit_cast<std::int32_t>(product);
    const auto rb = ((b & 0xff00ffu) + static_cast<std::uint32_t>(signed_product >> 8)) & 0xff00ffu;
    const auto green_b = static_cast<std::int32_t>((b >> 8) & 255u),
               green_a = static_cast<std::int32_t>((a >> 8) & 255u);
    const auto green = (static_cast<std::uint32_t>(green_b) << 8)
                     + static_cast<std::uint32_t>(fixed) * static_cast<std::uint32_t>(green_a - green_b);
    return rb | (((a & 0xffff00ffu) | green) & 0xff00ff00u);
}

ClipVertex intersect(const ClipVertex& a, const ClipVertex& b, unsigned plane) {
    const auto distance_a = plane_distance(a.position, plane);
    const auto distance_b = plane_distance(b.position, plane);
    const auto weight = static_cast<double>(.9999f) * distance_b / (distance_b - distance_a);
    const auto lerp = [&](float x, float y) {
        return static_cast<float>((static_cast<double>(x) - y) * weight + y);
    };
    ClipVertex result;
    for (unsigned i = 0; i < 4; ++i)
        result.position[i] = lerp(a.position[i], b.position[i]);
    for (unsigned i = 0; i < 2; ++i)
        result.uv[i] = lerp(a.uv[i], b.uv[i]);
    result.diffuse = blend_color(a.diffuse, b.diffuse, weight);
    result.specular = blend_color(a.specular, b.specular, weight);
    match (plane) {
        case 0 => result.position[2] = 0;
        case 1 => result.position[2] = result.position[3];
        case 2 => result.position[0] = -result.position[3];
        case 3 => result.position[0] = result.position[3];
        case 4 => result.position[1] = -result.position[3];
        case _ => result.position[1] = result.position[3];
    }
    return result;
}
} //namespace

std::uint8_t sprite_clip_outcode(const SpriteVertex& vertex, std::uint8_t enabled) {
    const auto x = std::bit_cast<std::uint32_t>(vertex[0]), y = std::bit_cast<std::uint32_t>(vertex[1]);
    const auto z = std::bit_cast<std::uint32_t>(vertex[2]), w = std::bit_cast<std::uint32_t>(vertex[3]);
    const auto shifted_w = w << 1;
    const auto pattern = ((w >> 31) << 6)
                       | ((z >> 31) << 5)
                       | ((shifted_w < (z << 1)) << 4)
                       | ((y >> 31) << 3)
                       | ((shifted_w < (y << 1)) << 2)
                       | ((x >> 31) << 1)
                       | (shifted_w < (x << 1));
    return clip_lookup[pattern] & enabled;
}

ClipSummary sprite_clip_summary(std::span<const ClipVertex> vertices, std::uint8_t enabled) {
    ClipSummary result{ 255, 0 };
    for (const auto& vertex : vertices) {
        const auto code = sprite_clip_outcode(vertex.position, enabled);
        result.common &= code;
        result.combined |= code;
    }
    return result;
}

std::vector<ClipVertex> clip_sprite_polygon(std::span<const ClipVertex> input, std::uint8_t planes) {
    std::vector<ClipVertex> current(input.begin(), input.end()), next;
    if (current.size() < 3)
        return {};
    for (unsigned plane = 0; plane < 6; ++plane)
        if (planes & (1u << plane)) {
            next.clear();
            next.reserve(current.size() + 1);
            auto previous = current.back();
            bool previous_inside = inside_plane(previous.position, plane);
            for (const auto& vertex : current) {
                const bool inside = inside_plane(vertex.position, plane);
                if (inside != previous_inside)
                    next.push_back(inside ? intersect(previous, vertex, plane) : intersect(vertex, previous, plane));
                if (inside)
                    next.push_back(vertex);
                previous = vertex;
                previous_inside = inside;
            }
            if (next.size() < 3)
                return {};
            current.swap(next);
        }
    return current;
}

std::vector<SpriteIndexedBatch>
build_sprite_indexed_batches(const SpriteGeometryPass& pass, std::uint8_t enabled, std::uint32_t diffuse_mask) {
    std::vector<SpriteIndexedBatch> batches;
    for (const auto& quad : pass.quads) {
        const auto color = quad.color & diffuse_mask;
        const std::array<ClipVertex, 4> input{ { { quad.vertices[0], color, 0, { quad.uv[0], quad.uv[1] } },
                                                 { quad.vertices[1], color, 0, { quad.uv[2], quad.uv[1] } },
                                                 { quad.vertices[2], color, 0, { quad.uv[2], quad.uv[3] } },
                                                 { quad.vertices[3], color, 0, { quad.uv[0], quad.uv[3] } } } };
        const auto summary = sprite_clip_summary(input, enabled);
        if (summary.common)
            continue;
        auto vertices = summary.combined ? clip_sprite_polygon(input, summary.combined)
                                         : std::vector<ClipVertex>(input.begin(), input.end());
        if (vertices.size() < 3)
            continue;
        const bool distinct_unbound_source =
            !batches.empty()
            && !quad.texture_storage
            && (batches.back().frame.resource != quad.frame.resource || batches.back().frame.frame != quad.frame.frame);
        if (batches.empty()
            || batches.back().render_state != quad.render_state
            || batches.back().texture_storage != quad.texture_storage
            || batches.back().depth_mode != quad.depth_mode
            || distinct_unbound_source)
            batches.push_back({ quad.render_state, quad.texture_storage, quad.depth_mode, quad.frame, {}, {} });
        auto& batch = batches.back();
        if (batch.vertices.size() > std::numeric_limits<std::uint32_t>::max() - vertices.size())
            throw std::length_error("sprite batch vertex count");
        const auto first = static_cast<std::uint32_t>(batch.vertices.size());
        batch.vertices.insert(batch.vertices.end(), vertices.begin(), vertices.end());
        for (auto i = vertices.size() - 1; i >= 2; --i) {
            const auto last = first + static_cast<std::uint32_t>(i), prior = last - 1;
            batch.indices.push_back(first);
            batch.indices.push_back(quad.packet_flags & 2u ? last : prior);
            batch.indices.push_back(quad.packet_flags & 2u ? prior : last);
        }
    }
    return batches;
}

bool submit_sprite_indexed_batches(std::span<const SpriteIndexedBatch> batches, SpriteIndexedBatchSink& sink) {
    for (const auto& batch : batches)
        if (!sink.submit(batch))
            return false;
    return true;
}
} //namespace simcoaster
