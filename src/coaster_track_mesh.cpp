#include "simcoaster/coaster_mesh.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

[[noreturn]] void invalid(std::size_t at, std::string detail = {}) {
    throw MeshError { MeshFailure::configuration, at, "Coaster track mesh " + detail, {} };
}

float length(const Point& p) {
    return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
}

float distance(const Point& a, const Point& b) {
    return length({ a[0] - b[0], a[1] - b[1], a[2] - b[2] });
}

std::pair<float, float> sincos(float angle) {
    return { simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian), simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian) };
}

Point transform(float x, float y, const RideTrackFrame& frame) {
    const auto [sb, cb] = sincos(frame.rotation[2]);
    const auto [sp, cp] = sincos(frame.rotation[1]);
    const auto [sh, ch] = sincos(frame.rotation[0]);
    const auto bank_x = cb * x - sb * y, bank_y = sb * x + cb * y, z = bank_y * sp;
    return { sh * z + ch * bank_x, cp * bank_y, ch * z - sh * bank_x };
}

std::size_t select(const CoasterMeshDefinition& definition, const RideTrackFrame& pose) {
    const std::array values { 0.0f, pose.rotation[0], pose.rotation[1], std::fmod(pose.rotation[2], 3.1415927f) };
    for (std::size_t i = 0; i + 1 < definition.selectors.size(); ++i) {
        const auto& s = definition.selectors[i];
        bool match = true;
        for (std::size_t j = 0; j < 4; ++j) {
            match &= values[j] >= s.ranges[j].first && values[j] <= s.ranges[j].second;
        }
        if (match) {
            return s.section;
        }
    }
    return definition.selectors.back().section;
}

struct Transition {
    float attachment {}, coordinate {};
    std::size_t section {};
};

float attachment_coordinate(const CoasterTrackSamples& track, float coordinate) {
    if (!(coordinate >= 0) || coordinate >= static_cast<float>(track.samples.size())) {
        invalid(0, "transition coordinate");
    }
    auto index = track.samples[static_cast<std::size_t>(coordinate)].brace_row;
    const auto& rows = track.rows.braces;
    if (index >= rows.size()) {
        invalid(index);
    }
    while (rows[index].next && rows[*rows[index].next].sample_position < coordinate) {
        index = *rows[index].next;
    }
    if (!rows[index].next) {
        return static_cast<float>(index);
    }
    const auto end = *rows[index].next;
    const auto span = rows[end].sample_position - rows[index].sample_position;
    if (!(span > 0)) {
        invalid(index);
    }
    return static_cast<float>(index) + (coordinate - rows[index].sample_position) / span;
}

std::vector<Transition> transitions(const CoasterMeshDefinition& definition, const CoasterRideTrackRuntime& runtime) {
    std::vector<Transition> result;
    auto current = definition.selectors.front().section;
    for (float coordinate = 0; coordinate < runtime.track.last_sample_position; coordinate += 0.25f) {
        RideTrackFrame frame;
        if (!sample_ride_track_frame(runtime.view(), coordinate, frame)) {
            invalid(result.size(), "selector frame");
        }
        const auto next = select(definition, frame);
        if (next != current) {
            result.push_back({ attachment_coordinate(runtime.track, coordinate), coordinate, current });
            current = next;
        }
    }
    result.push_back({ std::numeric_limits<float>::max(), runtime.track.last_sample_position + 1.25f, current });
    return result;
}

struct Builder {
    const CoasterMeshDefinition& definition;
    const CoasterRideTrackRuntime& runtime;
    CoasterTrackMesh result;
    float texture_distance {};
    bool alternate_diagonal {};
    std::optional<std::size_t> previous_section, previous_texture;
    std::optional<std::uint32_t> previous_ring;
    RideTrackFrame previous_frame;

    Builder(const CoasterMeshDefinition& d, const CoasterRideTrackRuntime& r) : definition(d), runtime(r) {}

    std::uint32_t ring(const RideTrackFrame& frame, std::size_t section) {
        const auto& s = definition.sections.at(section);
        if (result.vertices.size() + s.points.size() > std::numeric_limits<std::uint32_t>::max()) {
            invalid(section, "vertex capacity");
        }
        const auto first = static_cast<std::uint32_t>(result.vertices.size());
        for (const auto& p : s.points) {
            CoasterTrackMeshVertex vertex;
            vertex.position = transform(p.x, p.y, frame);
            vertex.normal = transform(p.normal_x, p.normal_y, frame);
            const auto n = length(vertex.normal);
            if (!(n > 0) || !std::isfinite(n)) {
                invalid(section, "normal");
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                vertex.position[axis] += frame.position[axis];
                vertex.normal[axis] /= n;
                result.bounds.minimum[axis] = std::min(result.bounds.minimum[axis], vertex.position[axis]);
                result.bounds.maximum[axis] = std::max(result.bounds.maximum[axis], vertex.position[axis]);
            }
            vertex.uv = { p.u, texture_distance };
            result.vertices.push_back(vertex);
        }
        ++result.cross_sections;
        return first;
    }

    void append(const RideTrackFrame& frame, std::size_t section, bool restart, std::optional<std::size_t> texture) {
        if (restart || !previous_ring) {
            previous_ring = ring(frame, section);
            previous_section = section;
            previous_texture = texture;
            previous_frame = frame;
            return;
        }
        //A diagnostic transition starts a distinct ring even when its configured
        //section has the same shape. Scrolling normal vertices stay independent.
        if (previous_section != section || previous_texture != texture) {
            previous_ring = ring(previous_frame, section);
            previous_section = section;
            previous_texture = texture;
            ++result.section_changes;
        }
        texture_distance += distance(previous_frame.position, frame.position) * 0.16666667f;
        const auto next = ring(frame, section), first = *previous_ring;
        for (const auto& edge : definition.sections.at(section).edges) {
            auto& indices = result.texture_indices.at(texture.value_or(edge.texture));
            const auto a = first + edge.first, b = first + edge.second, c = next + edge.first, d = next + edge.second;
            //0x482710 alternates the quad diagonal after every edge, including
            //the duplicated reverse-facing edges in ordinary coaster profiles.
            if (alternate_diagonal) {
                indices.insert(indices.end(), { a, c, d, d, b, a });
            } else {
                indices.insert(indices.end(), { a, c, b, d, b, c });
            }
            alternate_diagonal = !alternate_diagonal;
        }
        previous_ring = next;
        previous_frame = frame;
    }
};
} //namespace

std::expected<CoasterTrackMesh, MeshError> build_coaster_track_mesh(const CoasterMeshDefinition& definition, std::span<const CoasterGeometryObjectInput> objects, const CoasterRideTrackRuntime& runtime, const CoasterCollisionState* peers, std::uint32_t selection_texture_bias) {
    try {
        if (definition.sections.empty() || definition.selectors.empty() || definition.textures.empty() || objects.size() != runtime.track.rows.object_rows.size()) {
            invalid(0);
        }
        if (peers && (!peers->initialized || peers->needs_full_rebuild || peers->nodes.size() != objects.size())) {
            invalid(0, "peer restoration is incomplete");
        }
        //None of the twenty shipped Coaster.sam resources supplies blend points.
        //Keep that unported optional path explicit instead of drawing a guessed
        //shape or silently ignoring its transition contract.
        for (std::size_t i = 0; i < definition.sections.size(); ++i) {
            if (!definition.sections[i].blend_points.empty()) {
                invalid(i, "blended section generation is not yet rebuilt");
            }
        }
        const auto changes = transitions(definition, runtime);
        Builder builder { definition, runtime };
        builder.result.texture_indices.resize(definition.textures.size() + coaster_diagnostic_texture_names.size());
        builder.result.bounds.minimum.fill(std::numeric_limits<float>::max());
        builder.result.bounds.maximum.fill(-std::numeric_limits<float>::max());
        bool enabled = true, restart = true;
        const auto emit_segment = [&](std::size_t first, std::size_t last, std::size_t subdivisions, std::size_t owner, bool within_object) {
            std::vector<float> positions;
            const auto span = static_cast<float>(last - first);
            for (std::size_t i = 0; i <= subdivisions; ++i) {
                positions.push_back(static_cast<float>(first) + span * static_cast<float>(i) / static_cast<float>(subdivisions));
            }
            for (const auto& change : changes) {
                if (change.attachment > static_cast<float>(first) && change.attachment < static_cast<float>(last)) {
                    positions.push_back(change.attachment);
                }
            }
            std::ranges::sort(positions);
            const auto successor = within_object ? owner : owner + 1;
            const auto diagnostic = coaster_invalid_track_section(objects[owner].geometry_flags, objects[successor].geometry_flags, peers ? peers->nodes[owner].peers.size() : 0, definition.textures.size(), selection_texture_bias);
            const auto winch = definition.winch_section && (objects[owner].geometry_flags & 0x800u);
            //482806 forces a fresh section boundary for each invalid strip.
            if (diagnostic) {
                builder.previous_section.reset();
            }
            for (std::size_t i = 0; i < positions.size(); ++i) {
                if (i == 0 && !restart) {
                    continue;
                }
                const auto position = positions[i];
                auto row = std::min(static_cast<std::size_t>(position), last - 1);
                const auto fraction = position - static_cast<float>(row);
                if (row >= runtime.track.rows.braces.size()) {
                    invalid(row);
                }
                //An inter-object strip has one row interval. An object's internal
                //strip emits one ring per real attachment, plus selector boundaries.
                if (!within_object) {
                    row = first;
                }
                RideTrackFrame frame;
                if (!sample_ride_track_segment_frame(runtime.view(), row, row, fraction, frame)) {
                    invalid(row, "strip frame");
                }
                auto change = std::ranges::lower_bound(changes, position, {}, &Transition::attachment);
                const auto section = diagnostic ? definition.invalid_section : (winch ? *definition.winch_section : change->section);
                builder.append(frame, section, restart, diagnostic ? std::optional { diagnostic->texture } : std::nullopt);
                restart = false;
            }
        };
        for (std::size_t i = 0; i + 1 < objects.size(); ++i) {
            if (objects[i].geometry_flags & 0x10000u) {
                enabled = false;
            } else if (objects[i].geometry_flags & 0x20000u) {
                enabled = true;
                restart = true;
            }
            const auto [first, count] = runtime.track.rows.object_rows[i];
            const auto next = runtime.track.rows.object_rows[i + 1].first;
            if (!enabled) {
                restart = true;
                continue;
            }
            if (count > 1) {
                emit_segment(first, first + count - 1, count - 1, i, true);
            }
            emit_segment(first + count - 1, next, 10, i, false);
        }
        return std::move(builder.result);
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
