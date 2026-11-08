#include "simcoaster/mesh_animation.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "mesh_binary.hpp"
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;
using mesh_binary::Reader;

struct BlockHeader {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "start", 1 }]] std::uint32_t start {};
    [[=save::Field { "end", 2 }]] std::uint32_t end {};
    [[=save::Field { "position_count", 3 }]] std::uint16_t position_count {};
    [[=save::Field { "scale_count", 4 }]] std::uint16_t scale_count {};
    [[=save::Field { "rotation_count", 5 }]] std::uint16_t rotation_count {};
    [[=save::Field { "entry_count", 6 }]] std::uint16_t entry_count {};
    [[=save::Field { "reserved_014", 7 }]] std::array<std::byte, 4> reserved_014 {};
    [[=save::Field { "texture_count", 8 }]] std::uint16_t texture_count {};
    [[=save::Field { "hidden_count", 9 }]] std::uint16_t hidden_count {};
    [[=save::Field { "reserved_01c", 10 }]] std::uint32_t reserved_01c {};
    [[=save::Field { "positions", 11 }]] std::uint32_t positions {};
    [[=save::Field { "scales", 12 }]] std::uint32_t scales {};
    [[=save::Field { "rotations", 13 }]] std::uint32_t rotations {};
    [[=save::Field { "entries", 14 }]] std::uint32_t entries {};
    [[=save::Field { "textures", 15 }]] std::uint32_t textures {};
    [[=save::Field { "reserved_034", 16 }]] std::uint32_t reserved_034 {};
    [[=save::Field { "hidden_nodes", 17 }]] std::uint32_t hidden_nodes {};
    [[=save::Field { "fraction_curves", 18 }]] std::uint32_t fraction_curves {};
    [[=save::Field { "tail", 19 }]] std::array<std::byte, 8> tail {};
};

struct EntryHeader {
    [[=save::Field { "index", 0 }]] std::uint32_t index {};
    [[=save::Field { "flags", 1 }]] std::uint32_t flags {};
    [[=save::Field { "start", 2 }]] std::uint32_t start {};
    [[=save::Field { "end", 3 }]] std::uint32_t end {};
    [[=save::Field { "rotation_count", 4 }]] std::uint16_t rotation_count {};
    [[=save::Field { "scale_count", 5 }]] std::uint16_t scale_count {};
    [[=save::Field { "target", 6 }]] std::uint16_t target {};
    [[=save::Field { "visibility_count", 7 }]] std::uint16_t visibility_count {};
    [[=save::Field { "position", 8 }]] std::uint32_t position {};
    [[=save::Field { "rotation", 9 }]] std::uint32_t rotation {};
    [[=save::Field { "scale", 10 }]] std::uint32_t scale {};
    [[=save::Field { "percentage", 11 }]] std::uint32_t percentage {};
    [[=save::Field { "deformation", 12 }]] std::uint32_t deformation {};
    [[=save::Field { "uv", 13 }]] std::uint32_t uv {};
    [[=save::Field { "visibility", 14 }]] std::uint32_t visibility {};
    [[=save::Field { "fractions", 15 }]] std::uint32_t fractions {};
    [[=save::Field { "reserved", 16 }]] std::uint32_t reserved {};
    [[=save::Field { "resolved_node", 17 }]] std::uint32_t resolved_node {};
};

struct CompressedHeader {
    [[=save::Field { "flags", 0 }]] std::uint16_t flags {};
    [[=save::Field { "channel_count", 1 }]] std::uint16_t channel_count {};
    [[=save::Field { "reserved", 2 }]] std::array<std::byte, 8> reserved {};
    [[=save::Field { "channels", 3 }]] std::uint32_t channels {};
    [[=save::Field { "auxiliary", 4 }]] std::uint32_t auxiliary {};
    [[=save::Field { "bias", 5 }]] Point bias {};
    [[=save::Field { "scale", 6 }]] Point scale {};
    [[=save::Field { "tail", 7 }]] std::array<std::byte, 8> tail {};
};

struct CompressedChannelHeader {
    [[=save::Field { "key_count", 0 }]] std::uint16_t key_count {};
    [[=save::Field { "elements", 1 }]] std::uint16_t elements {};
    [[=save::Field { "indices", 2 }]] std::uint32_t indices {};
    [[=save::Field { "frames", 3 }]] std::uint32_t frames {};
    [[=save::Field { "samples", 4 }]] std::uint32_t samples {};
    [[=save::Field { "cursor", 5 }]] std::uint32_t cursor {};
};

static_assert(save::record_width<BlockHeader> == 72 && save::record_width<EntryHeader> == 64);
static_assert(save::record_width<CompressedHeader> == 52 && save::record_width<CompressedChannelHeader> == 20);

Point unpack(std::uint32_t packed, const Point& scale, const Point& bias) {
    Point result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto raw = (packed >> (axis * 10)) & 1023u;
        const auto component = (raw & 512u) ? static_cast<std::int32_t>(raw) - 1024 : static_cast<std::int32_t>(raw);
        result[axis] = static_cast<float>(component) * scale[axis] + bias[axis];
    }
    return result;
}

MeshCompressedDeformation compressed(const Reader& r, std::uint32_t offset) {
    r.range(offset, 1, 52);
    const auto h = r.get<CompressedHeader>(offset);
    if (!h.channel_count || ((h.flags & 2u) && h.channel_count < 2)) {
        r.fail(MeshFailure::animation_track, offset);
    }
    MeshCompressedDeformation result { h.flags, h.scale, h.bias, {} };
    for (const auto& c : r.array<CompressedChannelHeader>(h.channels, h.channel_count)) {
        const bool fixed = (h.flags & 2u) && result.channels.size() == 1;
        if (c.key_count < (fixed ? 1 : 2) || (result.channels.empty() && c.elements < 2)) {
            r.fail(MeshFailure::animation_track, h.channels);
        }
        MeshCompressedChannel channel;
        channel.elements_per_key = c.elements;
        channel.frames = r.array<std::uint16_t>(c.frames, c.key_count);
        channel.vertex_indices = r.array<std::uint16_t>(c.indices, c.elements);
        const auto words = r.array<std::uint32_t>(c.samples, static_cast<std::size_t>(c.key_count) * c.elements);
        for (auto word : words) {
            channel.samples.push_back(unpack(word, h.scale, h.bias));
        }
        result.channels.push_back(std::move(channel));
    }
    return result;
}
} //namespace

std::expected<MeshAnimation, MeshError> parse_mesh_animation(std::span<const std::byte> bytes, const MeshModel* model) {
    try {
        Reader r { bytes };
        if (bytes.size() < 184) {
            r.fail(MeshFailure::short_header, bytes.size());
        }
        const auto wrapper = r.get<mesh_binary::ModelHeader>(0);
        if (wrapper.magic != 483482950u) {
            r.fail(MeshFailure::signature, 0);
        }
        if (wrapper.version != 221) {
            r.fail(MeshFailure::version, 4);
        }
        if (wrapper.animation_version != 203) {
            r.fail(MeshFailure::animation_version, 8);
        }
        if (!wrapper.auxiliary) {
            r.fail(MeshFailure::base_resource, 152);
        }
        r.range(wrapper.auxiliary, 1, 72);
        const auto h = r.get<BlockHeader>(wrapper.auxiliary);
        MeshAnimation result { h.flags, h.start, h.end, {}, {}, {} };
        if (h.end < h.start) {
            r.fail(MeshFailure::animation_track, wrapper.auxiliary + 8);
        }
        for (const auto& raw : r.array<EntryHeader>(h.entries, h.entry_count)) {
            MeshAnimationEntry entry;
            entry.flags = raw.flags;
            entry.authored_start_frame = raw.start;
            entry.authored_end_frame = raw.end;
            entry.target_node = raw.target;
            if (model && raw.target >= model->nodes.size()) {
                r.fail(MeshFailure::node_link, raw.target);
            }
            if (raw.position) {
                entry.position = mesh_binary::vector_curve(r, raw.position);
            }
            r.range(raw.rotation, raw.rotation_count, 20);
            std::size_t fraction_count = 0;
            for (std::size_t i = 0; i < raw.rotation_count; ++i) {
                const auto key = r.get<MeshFrameKey>(raw.rotation + i * 20);
                entry.rotations.push_back({ key, r.get<std::array<float, 4>>(raw.rotation + i * 20 + 4) });
                if (key.metadata != 65535) {
                    fraction_count = std::max(fraction_count, static_cast<std::size_t>(key.metadata) + 1);
                }
            }
            entry.quaternion_fraction_curves = r.array<std::array<std::uint8_t, 8>>(raw.fractions, fraction_count);
            r.range(raw.scale, raw.scale_count, 16);
            for (std::size_t i = 0; i < raw.scale_count; ++i) {
                entry.scales.push_back({ r.get<MeshFrameKey>(raw.scale + i * 16), r.get<Point>(raw.scale + i * 16 + 4) });
            }
            if (raw.percentage) {
                r.range(raw.percentage, 1, 16);
                MeshFloatCurve curve;
                curve.start_frame = r.get<std::uint32_t>(raw.percentage);
                curve.samples = r.array<float>(r.get<std::uint32_t>(raw.percentage + 12), r.get<std::uint32_t>(raw.percentage + 4));
                entry.path_percentage = std::move(curve);
            }
            if (raw.flags & 0x1000u) {
                if (raw.flags & 0x4000u) {
                    r.range(raw.deformation, 1, 12);
                    MeshVertexFrameSamples samples;
                    const auto map = r.get<std::uint32_t>(raw.deformation);
                    samples.samples_per_frame = r.get<std::uint32_t>(raw.deformation + 4);
                    if (map) {
                        if (!model || !model->nodes[raw.target].drawable) {
                            r.fail(MeshFailure::animation_track, raw.deformation);
                        }
                        samples.vertex_indices = r.array<std::uint32_t>(map, model->nodes[raw.target].drawable->positions.size());
                    }
                    samples.samples = r.array<Point>(r.get<std::uint32_t>(raw.deformation + 8), (static_cast<std::size_t>(h.end - h.start) + 1) * samples.samples_per_frame);
                    entry.deformation = std::move(samples);
                } else {
                    entry.deformation = compressed(r, raw.deformation);
                }
            }
            if (raw.uv) {
                r.range(raw.uv, 1, 20);
                const auto count = r.get<std::uint32_t>(raw.uv), channels = r.get<std::uint32_t>(raw.uv + 4);
                const auto frames = r.get<std::uint32_t>(raw.uv + 12), values = r.get<std::uint32_t>(raw.uv + 16);
                for (const auto& channel : r.array<std::array<std::uint16_t, 2>>(channels, count)) {
                    entry.uv_channels.push_back({ r.array<std::uint16_t>(frames + static_cast<std::size_t>(channel[0]) * 2, channel[1]), r.array<std::array<float, 2>>(values + static_cast<std::size_t>(channel[0]) * 8, channel[1]) });
                }
            }
            entry.visibility_events = r.array<std::int16_t>(raw.visibility, raw.visibility_count);
            result.entries.push_back(std::move(entry));
        }
        r.range(h.textures, h.texture_count, 8);
        for (std::size_t i = 0; i < h.texture_count; ++i) {
            const auto at = h.textures + i * 8;
            MeshTextureVariantTrack track;
            track.slot = r.get<std::uint16_t>(at);
            track.keys = r.array<MeshFrameKey>(r.get<std::uint32_t>(at + 4), r.get<std::uint16_t>(at + 2));
            if (model && track.slot >= model->textures.size()) {
                r.fail(MeshFailure::index, at);
            }
            result.texture_variants.push_back(std::move(track));
        }
        result.hidden_nodes = r.array<std::uint16_t>(h.hidden_nodes, h.hidden_count);
        if (model) {
            if (const auto bound = validate_mesh_animation_binding(*model, result); !bound) {
                return std::unexpected(bound.error());
            }
        }
        return result;
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

std::expected<void, MeshError> validate_mesh_animation_binding(const MeshModel& model, const MeshAnimation& animation) {
    const auto failure = [](std::size_t index) {
        return std::unexpected(MeshError { MeshFailure::animation_binding, index, {}, {} });
    };
    for (auto node : animation.hidden_nodes) {
        if (node >= model.nodes.size()) {
            return failure(node);
        }
    }
    for (const auto& track : animation.texture_variants) {
        if (track.slot >= model.textures.size()) {
            return failure(track.slot);
        }
    }
    for (const auto& entry : animation.entries) {
        if (entry.target_node >= model.nodes.size()) {
            return failure(entry.target_node);
        }
        const auto& node = model.nodes[entry.target_node];
        if ((entry.flags & 0x11000u) && !node.drawable) {
            return failure(entry.target_node);
        }
        if (entry.flags & 0x1000u) {
            const auto count = node.drawable->positions.size();
            match (entry.deformation) {
                case { const MeshCompressedDeformation& deformation } => {
                    for (std::size_t channel = 1; channel < deformation.channels.size(); ++channel) {
                        for (auto vertex : deformation.channels[channel].vertex_indices) {
                            if (vertex >= count) {
                                return failure(vertex);
                            }
                        }
                    }
                }
                case { const MeshVertexFrameSamples& samples } => {
                    if (samples.vertex_indices.empty()) {
                        if (samples.samples_per_frame < count) {
                            return failure(count);
                        }
                    } else {
                        if (samples.vertex_indices.size() != count) {
                            return failure(count);
                        }
                        for (auto index : samples.vertex_indices) {
                            if (index >= samples.samples_per_frame) {
                                return failure(index);
                            }
                        }
                    }
                }
                case _ => return failure(entry.target_node);
            }
        }
        if ((entry.flags & 0x10000u) && entry.uv_channels.size() > node.drawable->texture_coordinates.size()) {
            return failure(entry.target_node);
        }
    }
    return {};
}

const MeshAnimation* MeshAnimationLibrary::find(std::size_t category, std::size_t sub_animation) const {
    return category < groups.size() && sub_animation < groups[category].size() ? &groups[category][sub_animation].animation : nullptr;
}

std::expected<MeshAnimationLibrary, MeshError> load_mesh_animation_library(const MeshModelLibrary& source, std::string_view filename, const MeshModel* model) {
    auto base = std::filesystem::path { filename };
    base.replace_extension();
    const auto name = base.native_encoded_string();
    MeshAnimationLibrary library;
    constexpr std::array suffixes { 'C', 'D', 'I', 'L', 'S', 'M', 'E', 'U', 'W', 'B', 'R', 'O' };
    for (std::size_t category = 0; category < suffixes.size(); ++category) {
        bool numbered = true;
        for (std::uint32_t variant = 1;; ++variant) {
            auto candidate = name + suffixes[category] + (numbered ? std::to_string(variant) : std::string {}) + ".md2";
            auto bytes = source.read_resource(candidate);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            if (!*bytes) {
                if (numbered && variant == 1) {
                    numbered = false;
                    --variant;
                    continue;
                }
                break;
            }
            auto parsed = parse_mesh_animation(**bytes, model);
            if (!parsed) {
                if (parsed.error().reason == MeshFailure::base_resource) {
                    break;
                }
                auto failure = parsed.error();
                failure.resource = candidate;
                return std::unexpected(std::move(failure));
            }
            library.groups[category].push_back({ std::move(candidate), std::move(*parsed) });
            if (!numbered) {
                break;
            }
        }
    }
    return library;
}

std::array<float, 5> coaster_animation_frames(std::array<float, 5>& controls, const std::array<CoasterAnimationChannelRange, 5>& ranges, const std::array<float, 5>& spans, std::uint32_t flags) {
    controls[0] = std::clamp(controls[0], 0.0f, 1.0f);
    controls[4] = std::fmod(controls[4], 1.0f);
    if (controls[4] < 0) {
        controls[4] += 1;
    }
    std::array<float, 5> frames;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto input = controls[i];
        if (i == 1 && (flags & 0x10000000u)) {
            input = std::fmod(input + 0.5f, 1.0f);
        }
        auto value = input * ranges[i].scale + ranges[i].bias;
        if (i == 2 || i == 3) {
            value += 0.5f;
        }
        value = std::max(value, ranges[i].minimum);
        value = std::min(value, ranges[i].maximum);
        frames[i] = value * spans[i];
    }
    return frames;
}

std::expected<void, MeshError> animate_coaster_pylon(const MeshModel& source, MeshModel& runtime, const MeshAnimationLibrary& library, std::array<float, 5>& controls, const std::array<CoasterAnimationChannelRange, 5>& ranges, std::uint32_t flags, std::array<MeshAnimationState, 5>& states) {
    constexpr std::array<std::size_t, 5> categories { 3, 10, 2, 9, 10 }, variants { 0, 0, 0, 0, 1 };
    std::array<MeshAnimationLayer, 5> layers;
    std::array<float, 5> spans;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        layers[i].animation = library.find(categories[i], variants[i]);
        spans[i] = layers[i].animation ? layers[i].animation->frame_span() : 0;
    }
    const auto frames = coaster_animation_frames(controls, ranges, spans, flags);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        layers[i].frame = frames[i];
    }
    return evaluate_mesh_animation_layers(source, runtime, layers, states);
}
} //namespace simcoaster
