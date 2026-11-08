#include "simcoaster/mesh_animation.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

[[noreturn]] void fail(MeshFailure reason, std::size_t index) {
    throw MeshError { reason, index, {}, {} };
}

void require(bool valid, std::size_t index = 0) {
    if (!valid) {
        fail(MeshFailure::animation_track, index);
    }
}

template <std::size_t N>
std::array<float, N> blend(const std::array<float, N>& a, const std::array<float, N>& b, float t) {
    std::array<float, N> value;
    for (std::size_t i = 0; i < N; ++i) {
        value[i] = (1 - t) * a[i] + t * b[i];
    }
    return value;
}

float length(const Point& p) {
    return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
}

Point cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

Point normalized(Point point) {
    const auto size = length(point);
    require(size > 0 && std::isfinite(size));
    for (auto& v : point) {
        v /= size;
    }
    return point;
}

template <class T>
std::uint16_t frame_of(const T& key) {
    if constexpr (requires { key.key.frame; }) {
        return key.key.frame;
    } else {
        return key.frame;
    }
}

struct Interval {
    std::size_t first {}, second {};
    float fraction {};
};

template <class T>
std::optional<Interval> interval(const std::vector<T>& keys, float frame, bool wrap) {
    if (keys.empty()) {
        return std::nullopt;
    }
    const auto integral = static_cast<std::uint32_t>(frame);
    std::size_t scan = 0;
    while (scan < keys.size() && frame_of(keys[scan]) <= integral) {
        ++scan;
    }
    if (!scan) {
        return std::nullopt;
    }
    const auto first = scan - 1, second = scan == keys.size() ? (wrap ? 0 : first) : scan;
    const auto end = scan == keys.size() ? (wrap ? static_cast<std::uint32_t>(frame_of(keys[0])) : static_cast<std::uint32_t>(frame_of(keys[first])) + 1) : static_cast<std::uint32_t>(frame_of(keys[second]));
    const auto span = static_cast<float>(end) - frame_of(keys[first]);
    require(span != 0, first);
    return Interval { first, second, (frame - frame_of(keys[first])) / span };
}

Point curve_sample(const MeshVectorCurve& curve, std::size_t base, float fraction, bool tangent = false) {
    require(!curve.points.empty());
    const auto whole = static_cast<std::int64_t>(fraction);
    const auto t = fraction - static_cast<float>(whole);
    const bool bezier = (curve.flags & 2u) != 0, linear = (curve.flags & 8u) != 0;
    const auto anchor = static_cast<std::int64_t>(base) + (bezier ? 3 : 1) * whole;
    if (linear && !bezier) {
        require(anchor >= 0 && static_cast<std::size_t>(anchor) < curve.points.size());
        const auto i = static_cast<std::size_t>(anchor);
        //Native linear tracks read a dead next point at the final key. The value
        //at fraction zero needs only the endpoint; avoid that out-of-bounds read.
        if (i + 1 == curve.points.size() && t == 0 && !tangent) {
            return curve.points[i];
        }
        require(i + 1 < curve.points.size(), i);
        if (tangent) {
            Point p;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                p[axis] = curve.points[i + 1][axis] - curve.points[i][axis];
            }
            return p;
        }
        return blend(curve.points[i], curve.points[i + 1], t);
    }
    const auto point = [&](std::int64_t index) -> const Point& {
        const auto count = static_cast<std::int64_t>(curve.points.size());
        auto i = index % count;
        if (i < 0) {
            i += count;
        }
        return curve.points[static_cast<std::size_t>(i)];
    };
    const auto &a = point(anchor - 1), &b = point(anchor), &c = point(anchor + 1), &d = point(anchor + 2);
    Point out;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (bezier) {
            const auto cubic = -a[axis] + 3 * b[axis] - 3 * c[axis] + d[axis], quadratic = 3 * (a[axis] + c[axis]) - 6 * b[axis], slope = 3 * (b[axis] - a[axis]);
            out[axis] = tangent ? (3 * cubic * t + 2 * quadratic) * t + slope : ((cubic * t + quadratic) * t + slope) * t + a[axis];
        } else {
            out[axis] = 0.5f * (((-a[axis] + 3 * b[axis] - 3 * c[axis] + d[axis]) * t + (2 * a[axis] - 5 * b[axis] + 4 * c[axis] - d[axis])) * t * t + (c[axis] - a[axis]) * t + 2 * b[axis]);
        }
    }
    return out;
}

float percent_sample(const MeshFloatCurve& curve, float position, bool wrap) {
    require(!curve.samples.empty());
    const auto whole = static_cast<std::size_t>(position);
    auto i = whole;
    auto t = position - static_cast<float>(whole);
    if (i == curve.samples.size()) {
        --i;
        t += 1;
    }
    require(i < curve.samples.size(), i);
    const auto next = wrap ? (i + 1) % curve.samples.size() : std::min(i + 1, curve.samples.size() - 1);
    auto a = curve.samples[i], b = curve.samples[next];
    if (std::abs(a - b) <= 50) {
        return (1 - t) * a + t * b;
    }
    if (a >= b) {
        b += 100;
    } else {
        a += 100;
    }
    return std::fmod((1 - t) * a + t * b, 100.0f);
}

std::array<float, 4> quaternion(const std::array<float, 4>& a, const std::array<float, 4>& b, float t, bool linear) {
    const auto dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (linear) {
        return blend(a, b, t);
    }
    if (dot + 1 <= 0.001f) {
        auto out = std::array { -a[1], a[0], -a[3], a[2] };
        const auto wa = simulation_sine_at_rounded_index((1 - t) * 1.5707964f * simulation_sine_samples_per_radian);
        const auto wb = simulation_sine_at_rounded_index(t * 1.5707964f * simulation_sine_samples_per_radian);
        //Native antipodal fallback deliberately leaves W at a.z after XYZ blends.
        for (std::size_t i = 0; i < 3; ++i) {
            out[i] = wa * a[i] + wb * out[i];
        }
        return out;
    }
    if (1 - dot <= 0.001f) {
        return blend(a, b, t);
    }
    const auto angle = std::acos(dot), denominator = simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian);
    const auto wa = simulation_sine_at_rounded_index((1 - t) * angle * simulation_sine_samples_per_radian) / denominator;
    const auto wb = simulation_sine_at_rounded_index(t * angle * simulation_sine_samples_per_radian) / denominator;
    std::array<float, 4> out;
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = wa * a[i] + wb * b[i];
    }
    return out;
}

std::array<float, 9> basis(const std::array<float, 4>& q) {
    const auto norm = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    require(norm > 0 && std::isfinite(norm));
    const auto s = 2 / norm, x = q[0], y = q[1], z = q[2], w = q[3];
    return { 1 - s * (y * y + z * z), s * (x * y + z * w), s * (x * z - y * w), s * (x * y - z * w), 1 - s * (x * x + z * z), s * (y * z + x * w), s * (x * z + y * w), s * (y * z - x * w), 1 - s * (x * x + y * y) };
}

void rotate(MeshMatrix& matrix, const std::array<float, 9>& rotation, bool additive) {
    const auto before = matrix;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            matrix[row * 4 + col] = additive ? rotation[row * 3] * before[col] + rotation[row * 3 + 1] * before[4 + col] + rotation[row * 3 + 2] * before[8 + col] : rotation[row * 3 + col];
        }
    }
}

void rescale(MeshMatrix& matrix, const Point& scale) {
    for (std::size_t row = 0; row < 3; ++row) {
        const auto size = length({ matrix[row * 4], matrix[row * 4 + 1], matrix[row * 4 + 2] });
        require(size > 0, row);
        const auto factor = scale[row] / size;
        for (std::size_t col = 0; col < 3; ++col) {
            matrix[row * 4 + col] *= factor;
        }
    }
}

void orient(MeshMatrix& matrix, const Point& direction, bool rotation_keys) {
    const auto up = rotation_keys ? Point { matrix[4], matrix[5], matrix[6] } : Point { 0, 1, 0 };
    const auto right = normalized(cross(up, direction)), corrected = cross(direction, right);
    for (std::size_t i = 0; i < 3; ++i) {
        matrix[i] = right[i];
        matrix[4 + i] = corrected[i];
        matrix[8 + i] = direction[i];
    }
}

void position(MeshModel& model, MeshNode& node, const MeshAnimationEntry& entry, float frame, bool additive) {
    const MeshVectorCurve* curve = nullptr;
    std::size_t index = 0;
    float fraction = 0;
    if (entry.flags & 1u) {
        require(entry.position.has_value());
        curve = &*entry.position;
        const auto keys = interval(curve->keys, frame, false);
        if (!keys) {
            return;
        }
        index = keys->first;
        fraction = keys->fraction;
    } else if ((entry.flags & 0x200u) && node.parent && (model.nodes[*node.parent].flags & 8u)) {
        require(entry.path_percentage.has_value());
        const auto& percentage = *entry.path_percentage;
        const auto start = static_cast<float>(percentage.start_frame);
        if (frame < start || frame > start + static_cast<float>(percentage.samples.size())) {
            return;
        }
        const auto curve_index = model.nodes[*node.parent].vector_curve_index;
        require(curve_index < model.vector_curves.size(), curve_index);
        curve = &model.vector_curves[curve_index];
        const auto value = std::fmod(percent_sample(percentage, frame - start, (curve->flags & 1u) != 0) + 1000, 100.0f);
        const auto count = (curve->flags & 2u) ? curve->points.size() / 3 : curve->points.size();
        require(count > 0);
        const auto span = static_cast<float>((curve->flags & 1u) ? count : count - 1);
        const auto coordinate = span * value * 0.0099999998f;
        require(coordinate >= 0 && std::isfinite(coordinate));
        index = static_cast<std::size_t>(coordinate);
        fraction = coordinate - static_cast<float>(index);
    } else {
        return;
    }
    if (curve->flags & 2u) {
        index = index * 3 + 1;
    }
    const auto value = curve_sample(*curve, index, fraction);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        node.local_transform[12 + axis] = (additive ? node.local_transform[12 + axis] : 0) + value[axis];
    }
    if (entry.flags & 0x400u) {
        const auto tangent = (curve->flags & (2u | 8u)) ? curve_sample(*curve, index, fraction, true) : curve_sample(*curve, index, fraction + 0.1f);
        orient(node.local_transform, normalized(tangent), (entry.flags & 8u) != 0);
    }
}

void uv(MeshNode& node, const MeshAnimationEntry& entry, float frame, bool additive) {
    require(node.drawable.has_value());
    auto& output = node.drawable->texture_coordinates;
    require(entry.uv_channels.size() <= output.size());
    for (std::size_t channel = 0; channel < entry.uv_channels.size(); ++channel) {
        const auto& track = entry.uv_channels[channel];
        require(track.frames.size() >= 2 && track.frames.size() == track.samples.size(), channel);
        std::size_t scan = 0;
        while (scan < track.frames.size() && static_cast<std::uint32_t>(frame) >= track.frames[scan]) {
            ++scan;
        }
        require(scan > 0, channel);
        const auto first = scan == track.frames.size() ? scan - 2 : scan - 1;
        const auto span = static_cast<float>(track.frames[first + 1]) - track.frames[first];
        require(span != 0, channel);
        const auto t = scan == track.frames.size() ? 1.0f : (frame - track.frames[first]) / span;
        const auto sampled = blend(track.samples[first], track.samples[first + 1], t);
        for (std::size_t axis = 0; axis < 2; ++axis) {
            output[channel][axis] = (additive ? output[channel][axis] : 0) + sampled[axis];
        }
    }
}

Interval compressed_interval(const MeshCompressedChannel& channel, float frame, std::size_t& cursor) {
    require(channel.frames.size() >= 2);
    require(cursor + 1 < channel.frames.size(), cursor);
    while (cursor + 1 < channel.frames.size() && channel.frames[cursor + 1] < frame) {
        ++cursor;
    }
    require(cursor + 1 < channel.frames.size(), cursor);
    const auto span = static_cast<float>(channel.frames[cursor + 1]) - channel.frames[cursor];
    require(span != 0, cursor);
    return { cursor, cursor + 1, (frame - channel.frames[cursor]) / span };
}

void compressed_vertices(MeshNode& node, const MeshCompressedDeformation& data, float frame, std::vector<std::size_t>& cursors, bool& initialized, bool additive) {
    require(node.drawable.has_value() && !data.channels.empty());
    auto& drawable = *node.drawable;
    if (!initialized) {
        cursors.assign(data.channels.size(), 0);
    }
    require(cursors.size() == data.channels.size());
    const auto& bounds = data.channels[0];
    require(bounds.elements_per_key >= 2);
    const auto bound = compressed_interval(bounds, frame, cursors[0]);
    require((bound.second + 1) * bounds.elements_per_key <= bounds.samples.size());
    const auto minimum = blend(bounds.samples[bound.first * bounds.elements_per_key], bounds.samples[bound.second * bounds.elements_per_key], bound.fraction);
    const auto maximum = blend(bounds.samples[bound.first * bounds.elements_per_key + 1], bounds.samples[bound.second * bounds.elements_per_key + 1], bound.fraction);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        drawable.bounds.minimum[axis] = (additive ? drawable.bounds.minimum[axis] : 0) + minimum[axis] - data.scale[axis] - 0.25f;
        drawable.bounds.maximum[axis] = (additive ? drawable.bounds.maximum[axis] : 0) + maximum[axis] + data.scale[axis] + 0.25f;
    }
    for (std::size_t channel = 1; channel < data.channels.size(); ++channel) {
        const auto& track = data.channels[channel];
        const bool fixed = channel == 1 && (data.flags & 2u);
        if (fixed && initialized) {
            continue;
        }
        const auto keys = fixed ? Interval {} : compressed_interval(track, frame, cursors[channel]);
        require(track.vertex_indices.size() == track.elements_per_key);
        require((keys.second + 1) * track.elements_per_key <= track.samples.size());
        for (std::size_t i = 0; i < track.elements_per_key; ++i) {
            const auto vertex = track.vertex_indices[i];
            require(vertex < drawable.positions.size(), vertex);
            const auto sampled = blend(track.samples[keys.first * track.elements_per_key + i], track.samples[keys.second * track.elements_per_key + i], keys.fraction);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                drawable.positions[vertex][axis] = (additive ? drawable.positions[vertex][axis] : 0) + sampled[axis];
            }
        }
    }
    if (!additive) {
        initialized = true;
        node.flags |= 0x800000u;
    }
    node.flags |= 0x10000u;
}

void sampled_vertices(MeshNode& node, const MeshVertexFrameSamples& data, float frame, bool additive) {
    require(node.drawable.has_value());
    auto& drawable = *node.drawable;
    const auto offset = static_cast<std::size_t>(frame) * data.samples_per_frame;
    require(data.vertex_indices.empty() || data.vertex_indices.size() == drawable.positions.size());
    for (std::size_t i = 0; i < drawable.positions.size(); ++i) {
        const auto sample = data.vertex_indices.empty() ? i : data.vertex_indices[i];
        require(sample < data.samples_per_frame && offset + sample < data.samples.size(), sample);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto& value = drawable.positions[i][axis];
            value = (additive ? value : 0) + data.samples[offset + sample][axis];
            drawable.bounds.minimum[axis] = std::min(drawable.bounds.minimum[axis], value);
            drawable.bounds.maximum[axis] = std::max(drawable.bounds.maximum[axis], value);
        }
    }
    node.flags |= 0x10000u;
}

void face_normals(MeshNode& node) {
    if (!node.drawable) {
        return;
    }
    auto& drawable = *node.drawable;
    for (const auto& triangle : drawable.triangles) {
        const auto index = static_cast<std::size_t>(triangle.normal_index_and_flags & 0x7FFFu);
        require(index < drawable.normals.size(), index);
        std::array<Point, 3> corners;
        for (std::size_t i = 0; i < 3; ++i) {
            require(triangle.corners[i] < drawable.position_indices.size());
            const auto vertex = drawable.position_indices[triangle.corners[i]];
            require(vertex < drawable.positions.size());
            corners[i] = drawable.positions[vertex];
        }
        Point a, b;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            a[axis] = corners[0][axis] - corners[1][axis];
            b[axis] = corners[2][axis] - corners[1][axis];
        }
        auto normal = cross(b, a);
        const auto size = length(normal);
        for (auto& value : normal) {
            value /= size;
        }
        if (std::isnan(normal[0])) {
            normal = { 0, -1, 0 };
        }
        drawable.normals[index] = normal;
    }
    node.flags &= ~0x10000u;
}
} //namespace

std::expected<void, MeshError> recompute_mesh_face_normals(MeshModel& model) {
    try {
        for (auto& node : model.nodes) {
            face_normals(node);
        }
        model.flags &= ~0x40000u;
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> apply_mesh_animation_frame(MeshModel& model, const MeshAnimation& animation, float frame, MeshAnimationState& state, MeshAnimationOptions options) {
    try {
        if (!(frame >= 0) || !std::isfinite(frame) || static_cast<double>(frame) > std::numeric_limits<std::uint32_t>::max()) {
            fail(MeshFailure::animation_frame, 0);
        }
        const auto additive = options.additive.value_or((model.flags & 4u) != 0);
        if (!state.previous_frame) {
            if (const auto binding = validate_mesh_animation_binding(model, animation); !binding) {
                return binding;
            }
        }
        if (state.deformation_keys.size() != animation.entries.size()) {
            state.deformation_keys.resize(animation.entries.size());
            state.deformation_initialized.assign(animation.entries.size(), false);
        }
        if (state.previous_frame && frame < *state.previous_frame) {
            state.deformation_initialized.assign(animation.entries.size(), false);
        }
        if (!state.previous_frame) {
            for (auto hidden : animation.hidden_nodes) {
                require(hidden < model.nodes.size(), hidden);
                if (!(model.nodes[hidden].flags & 0x80000000u)) {
                    model.nodes[hidden].flags |= 0x10u;
                }
            }
            for (const auto& entry : animation.entries) {
                require(entry.target_node < model.nodes.size(), entry.target_node);
                auto& node = model.nodes[entry.target_node];
                node.flags |= 0x40000u;
                if (entry.flags & 0x1000u) {
                    node.flags |= 0x200000u;
                }
                if (entry.flags & 0x10000u) {
                    node.flags |= 0x400000u;
                }
                if (entry.flags & 0x289u) {
                    node.flags |= 0x100000u;
                }
            }
        }
        if (frame <= animation.frame_span()) {
            for (std::size_t i = 0; i < animation.entries.size(); ++i) {
                const auto& entry = animation.entries[i];
                require(entry.target_node < model.nodes.size(), entry.target_node);
                auto& node = model.nodes[entry.target_node];
                if (entry.flags & 0x20000u) {
                    for (auto event = entry.visibility_events.rbegin(); event != entry.visibility_events.rend(); ++event) {
                        //Native signed-WORD negation leaves -32768 ineligible for game frames.
                        if (*event == std::numeric_limits<std::int16_t>::min()) {
                            continue;
                        }
                        if (static_cast<std::uint32_t>(frame) < static_cast<std::uint32_t>(std::abs(static_cast<int>(*event)))) {
                            continue;
                        }
                        if (*event > 0) {
                            node.flags &= ~0x10u;
                        } else {
                            node.flags |= 0x10u;
                        }
                        break;
                    }
                }
                if (entry.flags & 0x1000u) {
                    match (entry.deformation) {
                        case { const MeshCompressedDeformation& deformation } => {
                            bool initialized = state.deformation_initialized[i];
                            compressed_vertices(node, deformation, frame, state.deformation_keys[i], initialized, additive);
                            state.deformation_initialized[i] = initialized;
                            model.flags |= 0x40000u;
                        }
                        case { const MeshVertexFrameSamples& samples } => sampled_vertices(node, samples, frame, additive);
                        case _                                         => fail(MeshFailure::animation_track, i);
                    }
                }
                if (entry.flags & 8u) {
                    if (const auto key = interval(entry.rotations, frame, true)) {
                        auto fraction = key->fraction;
                        const auto curve = entry.rotations[key->first].key.metadata;
                        if (curve != 65535) {
                            require(curve < entry.quaternion_fraction_curves.size(), curve);
                            const auto& values = entry.quaternion_fraction_curves[curve];
                            const auto coordinate = fraction * 8.9999952f;
                            require(coordinate >= 0 && coordinate < 10, curve);
                            const auto at = static_cast<std::size_t>(coordinate);
                            const auto t = coordinate - static_cast<float>(at);
                            const auto low = at ? static_cast<float>(values[std::min(at - 1, std::size_t { 7 })]) * 0.0039215689f : 0.0f;
                            const auto high = at >= 8 ? 1.0f : static_cast<float>(values[at]) * 0.0039215689f;
                            fraction = (1 - t) * low + t * high;
                        }
                        const auto next = std::min(key->first + 1, entry.rotations.size() - 1);
                        rotate(node.local_transform, basis(quaternion(entry.rotations[key->first].rotation, entry.rotations[next].rotation, fraction, options.linear_quaternions)), additive);
                    }
                }
                if (entry.flags & 0x80u) {
                    if (const auto key = interval(entry.scales, frame, false)) {
                        rescale(node.local_transform, blend(entry.scales[key->first].value, entry.scales[key->second].value, key->fraction));
                    }
                }
                position(model, node, entry, frame, additive);
                if ((entry.flags & 0x10000u) && ((!options.suppress_uv && !(node.flags & 0x4000u)) || additive)) {
                    uv(node, entry, frame, additive);
                }
            }
        }
        if ((animation.flags & 2u) && !options.suppress_texture_variants) {
            for (const auto& track : animation.texture_variants) {
                require(track.slot < model.textures.size(), track.slot);
                auto& slot = model.textures[track.slot];
                for (auto key = track.keys.rbegin(); key != track.keys.rend(); ++key) {
                    if (static_cast<std::uint32_t>(frame) >= key->frame) {
                        if (slot.source_variant != key->metadata) {
                            slot.source_variant = key->metadata;
                            slot.flags |= 0x400u;
                        }
                        break;
                    }
                }
            }
        }
        state.previous_frame = frame;
        return {};
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

std::expected<void, MeshError> evaluate_mesh_animation_layers(const MeshModel& source, MeshModel& runtime, std::span<const MeshAnimationLayer> layers, std::span<MeshAnimationState> states) {
    try {
        require(&source != &runtime && source.nodes.size() == runtime.nodes.size() && states.size() == layers.size());
        bool any_additive = false;
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const auto& layer = layers[i];
            if (!layer.animation || !layer.options.additive.value_or((runtime.flags & 4u) != 0)) {
                continue;
            }
            any_additive = true;
            for (const auto& entry : layer.animation->entries) {
                require(entry.target_node < runtime.nodes.size(), entry.target_node);
                auto& node = runtime.nodes[entry.target_node];
                const auto& original = source.nodes[entry.target_node];
                node.flags &= ~0x800000u;
                if (!(node.flags & 0x80000000u)) {
                    node.flags &= ~0x10u;
                }
                if (entry.flags & 0x289u) {
                    for (std::size_t row = 0; row < 4; ++row) {
                        for (std::size_t col = 0; col < 3; ++col) {
                            node.local_transform[row * 4 + col] = original.local_transform[row * 4 + col];
                        }
                    }
                }
                if (entry.flags & 0x1000u) {
                    require(node.drawable.has_value() && original.drawable.has_value());
                    node.drawable->positions = original.drawable->positions;
                    node.drawable->bounds = original.drawable->bounds;
                }
                if (entry.flags & 0x10000u) {
                    require(node.drawable.has_value() && original.drawable.has_value());
                    node.drawable->texture_coordinates = original.drawable->texture_coordinates;
                }
            }
            states[i].deformation_initialized.assign(layer.animation->entries.size(), false);
            if ((layer.animation->flags & 2u) && !layer.options.suppress_texture_variants) {
                for (auto& slot : runtime.textures) {
                    if (slot.flags & 0x40u) {
                        slot.source_variant = 0;
                        slot.flags |= 0x400u;
                    }
                }
            }
        }
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].animation && layers[i].sample) {
                if (const auto applied = apply_mesh_animation_frame(runtime, *layers[i].animation, layers[i].frame, states[i], layers[i].options); !applied) {
                    return applied;
                }
            }
        }
        if (any_additive) {
            return recompute_mesh_face_normals(runtime);
        }
        return {};
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}
} //namespace simcoaster
