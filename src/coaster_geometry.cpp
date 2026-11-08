#include "simcoaster/coaster_geometry.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

float weighted_distance(const Point& a, const Point& b) {
    const auto x = a[0] - b[0], y = (a[1] - b[1]) * 0.7f, z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

float length(const Point& a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

float curvature_for(const CoasterGeometryObjectInput& object, const CoasterGeometryObjectInput* previous, const CoasterGeometryObjectInput* next, float base) {
    const auto* neighbor = (object.geometry_flags & 0x80u) ? next : previous;
    auto dx = neighbor ? neighbor->inline_position[0] - object.inline_position[0] : 0.0f;
    auto dz = neighbor ? neighbor->inline_position[2] - object.inline_position[2] : 1.0f;
    const auto magnitude = std::sqrt(dx * dx + dz * dz);
    if (magnitude == 0) {
        dx = 0;
        dz = 1;
    } else {
        dx /= magnitude;
        dz /= magnitude;
    }
    const auto heading = object.animation_heading * 6.2831855f;
    const auto sine = simulation_sine_at_rounded_index(heading * simulation_sine_samples_per_radian);
    const auto cosine = simulation_sine_at_rounded_index((heading + 1.5707964f) * simulation_sine_samples_per_radian);
    const auto alignment = std::clamp(std::abs(-cosine * dz - sine * dx), 0.0f, 1.0f);
    const auto aligned = (1 - alignment * alignment) * base + alignment * alignment * 2.0999999f;
    const auto turn = std::clamp(std::fmod(object.animation_wobble, 0.5f) * 2, 0.0f, 1.0f);
    const auto blend = (simulation_sine_at_rounded_index((turn * 6.2831855f + 1.5707964f) * simulation_sine_samples_per_radian) + 1) * 0.5f;
    return (1 - blend) * aligned + blend * 2.0999999f;
}

void set_controls(RideTrackAttachmentRow& row, const Point& axis, float incoming, float outgoing, float in_scale, float out_scale) {
    for (std::size_t i = 0; i < 3; ++i) {
        row.incoming_tangent[i] = -axis[i] * incoming * in_scale;
        row.outgoing_tangent[i] = axis[i] * outgoing * out_scale;
        row.incoming_control[i] = row.position[i] + row.incoming_tangent[i] * 0.33333334f;
        row.outgoing_control[i] = row.position[i] + row.outgoing_tangent[i] * 0.33333334f;
    }
}
} //namespace

std::expected<void, MeshError> bind_coaster_model_attachments(CoasterGeometryObjectInput& object, const MeshModel& model, std::span<const MeshMatrix> pose, std::uint32_t first_id, std::uint32_t count, float heading) {
    const auto failure = [&](std::size_t index) {
        return std::unexpected(MeshError { MeshFailure::attachment, index, model.name, {} });
    };
    if (!count || count > model.resources.size() || count - 1 > std::numeric_limits<std::uint32_t>::max() - first_id || !std::isfinite(heading)) {
        return failure(first_id);
    }
    std::vector<CoasterAttachmentInput> attachments;
    float axis_angle = object.attachment_axis_angle;
    const auto sincos = [](float angle) {
        return std::pair { simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian), simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian) };
    };
    const auto rotate_xz = [&](std::array<float, 3>& vector, float angle) {
        const auto [sine, cosine] = sincos(angle);
        const auto x = vector[0];
        vector[0] = sine * vector[2] + cosine * x;
        vector[2] = cosine * vector[2] - sine * x;
    };
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto resource = model.find_resource(std::to_underlying(MeshResourceFlag::track_attachment), first_id + i);
        if (!resource) {
            return failure(first_id + i);
        }
        auto transform = mesh_attachment_transform(model, pose, *resource);
        if (!transform) {
            return std::unexpected(transform.error());
        }
        if (object.geometry_flags & 0x10000000u) {
            const auto [sine, cosine] = sincos(-3.1415927f);
            for (std::size_t row = 0; row < 3; ++row) {
                const auto x = (*transform)[row * 4];
                (*transform)[row * 4] = sine * (*transform)[row * 4 + 2] + cosine * x;
                (*transform)[row * 4 + 2] = cosine * (*transform)[row * 4 + 2] - sine * x;
            }
        }
        CoasterAttachmentInput input;
        input.source_mesh_resource_index = static_cast<std::int32_t>(*resource);
        input.brace_position = { (*transform)[12], (*transform)[13], (*transform)[14] };
        std::array<float, 3> up { (*transform)[4], (*transform)[5], (*transform)[6] }, forward { (*transform)[8], (*transform)[9], (*transform)[10] };
        input.mesh_axis = forward;
        input.mesh_position = input.brace_position;
        if (object.geometry_flags & 0x40u) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                input.mesh_position[axis] += 2.0f * up[axis];
            }
        } else {
            input.mesh_position[1] += 1;
            rotate_xz(up, -heading);
            rotate_xz(forward, -heading);
            const auto pitch = static_cast<float>(-std::atan2(-forward[1], forward[2]));
            const auto [sine, cosine] = sincos(pitch);
            const auto y = up[1];
            up[1] = y * cosine - sine * up[2];
            up[2] = y * sine + cosine * up[2];
            axis_angle = static_cast<float>(std::atan2(-up[0], up[1]));
        }
        attachments.push_back(input);
    }
    object.attachments = std::move(attachments);
    object.attachment_axis_angle = axis_angle;
    return {};
}

bool build_coaster_attachment_rows(std::span<const CoasterGeometryObjectInput> objects, float curviness, CoasterAttachmentRows& output) {
    if (objects.empty() || !std::isfinite(curviness)) {
        return false;
    }
    CoasterAttachmentRows result;
    for (const auto& object : objects) {
        if (object.attachments.empty()) {
            return false;
        }
        const auto first = result.braces.size();
        result.object_rows.emplace_back(first, object.attachments.size());
        const bool has_mesh = object.attachments.front().mesh_axis.has_value();
        for (const auto& attachment : object.attachments) {
            if (attachment.mesh_axis.has_value() != has_mesh) {
                return false;
            }
            RideTrackAttachmentRow brace, mesh;
            brace.position = attachment.brace_position;
            mesh.position = attachment.mesh_position;
            brace.geometry_index = mesh.geometry_index = object.geometry_index;
            brace.geometry_fraction = mesh.geometry_fraction = attachment.geometry_fraction;
            brace.geometry_axis_angle = mesh.geometry_axis_angle = object.attachment_axis_angle;
            brace.source_mesh_resource_index = mesh.source_mesh_resource_index = attachment.source_mesh_resource_index;
            result.braces.push_back(brace);
            result.mesh_rows.push_back(mesh);
        }
    }
    const auto row_count = result.braces.size();
    for (std::size_t i = 0; i < row_count; ++i) {
        if (i) {
            result.braces[i].previous = i - 1;
            result.mesh_rows[i].previous = i - 1;
        }
        if (i + 1 < row_count) {
            result.braces[i].next = i + 1;
            result.mesh_rows[i].next = i + 1;
        }
    }
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const auto& object = objects[object_index];
        const auto* previous = object_index ? &objects[object_index - 1] : nullptr;
        const auto* next = object_index + 1 < objects.size() ? &objects[object_index + 1] : nullptr;
        float in_scale = curviness, out_scale = curviness;
        if (object.runtime_data_flags & 0x4000u) {
            const auto adjusted = curvature_for(object, previous, next, curviness);
            if (object.geometry_flags & 0x80u) {
                in_scale = previous && (previous->geometry_flags & 0x40u) ? adjusted : 1.7f;
            } else {
                out_scale = next && (next->geometry_flags & 0x40u) ? adjusted : 1.7f;
            }
            if (object.geometry_flags & 0x80u) {
                out_scale = adjusted;
            } else {
                in_scale = adjusted;
            }
        }
        const auto [start, count] = result.object_rows[object_index];
        for (std::size_t j = 0; j < count; ++j) {
            const auto index = start + j;
            const auto& input = object.attachments[j];
            Point axis;
            if (input.mesh_axis) {
                axis = *input.mesh_axis;
            } else if (object.geometry_flags & 0x40u) {
                axis = { 0, (object.geometry_flags & 0x80u) ? 1.0f : -1.0f, 0 };
            } else {
                axis = object.inline_outgoing_tangent;
            }
            const auto magnitude = length(axis);
            if (!(magnitude > 0) || !std::isfinite(magnitude)) {
                return false;
            }
            for (auto& component : axis) {
                component /= magnitude;
            }
            const auto normalized_magnitude = length(axis);
            auto& brace = result.braces[index];
            auto& mesh = result.mesh_rows[index];
            const auto brace_in = index ? weighted_distance(result.braces[index - 1].position, brace.position) : 1.0f;
            const auto brace_out = index + 1 < row_count ? weighted_distance(brace.position, result.braces[index + 1].position) : 1.0f;
            const auto mesh_in = index ? weighted_distance(result.mesh_rows[index - 1].position, mesh.position) : 1.0f;
            const auto mesh_out = index + 1 < row_count ? weighted_distance(mesh.position, result.mesh_rows[index + 1].position) : 1.0f;
            set_controls(brace, axis, brace_in, brace_out, in_scale / normalized_magnitude, out_scale / normalized_magnitude);
            set_controls(mesh, axis, mesh_in, mesh_out, in_scale / normalized_magnitude, out_scale / normalized_magnitude);
        }
    }
    output = std::move(result);
    return true;
}

bool coaster_ride_activation_bounds(const CoasterAttachmentRows& rows, float interval, float& lower, float& upper) {
    if (rows.object_rows.size() < 3) {
        return false;
    }
    const auto first = rows.object_rows[0].first, last = rows.object_rows[2].first;
    if (first >= rows.braces.size() || last >= rows.braces.size()) {
        return false;
    }
    const auto a = rows.braces[first].sample_position, b = rows.braces[last].sample_position;
    const auto midpoint = (a + b) * 0.5f;
    lower = (1 - interval) * a + interval * midpoint;
    upper = (1 - interval) * b + interval * midpoint;
    return true;
}

bool build_coaster_track_samples(std::span<const CoasterGeometryObjectInput> objects, CoasterAttachmentRows rows, float spacing, float entrance_interval, CoasterTrackSamples& output) {
    if (objects.size() < 3 || rows.object_rows.size() != objects.size() || rows.braces.size() < 2 || rows.braces.size() != rows.mesh_rows.size() || !(spacing > 0) || !std::isfinite(spacing)) {
        return false;
    }
    const auto count = rows.braces.size();
    std::vector<std::size_t> owners(count);
    std::size_t expected = 0;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto [start, n] = rows.object_rows[i];
        if (!n || start != expected || n > count - start) {
            return false;
        }
        for (std::size_t j = 0; j < n; ++j) {
            owners[start + j] = i;
        }
        expected += n;
    }
    if (expected != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::optional<std::size_t> previous = i ? std::optional { i - 1 } : std::nullopt;
        const std::optional<std::size_t> next = i + 1 < count ? std::optional { i + 1 } : std::nullopt;
        if (rows.braces[i].previous != previous || rows.mesh_rows[i].previous != previous || rows.braces[i].next != next || rows.mesh_rows[i].next != next) {
            return false;
        }
    }
    CoasterTrackSamples built;
    built.rows = std::move(rows);
    built.geometry_component_indices.resize(objects.size());
    auto& braces = built.rows.braces;
    auto& meshes = built.rows.mesh_rows;
    braces[0].sample_position = 0;
    meshes[0].sample_position = 0;
    const auto closing_row = built.rows.object_rows[1].first;
    std::size_t cursor = 0;
    float t = 0.05f, previous_t = 0, accumulated = 0, prior_accumulated = 0;
    Point current = braces[0].position;
    bool first = true, terminal_emitted = false;
    std::size_t iterations = 0;
    while (!terminal_emitted) {
        if (++iterations > 10000000) {
            return false;
        }
        const bool terminal = cursor + 1 == count;
        const auto next = terminal ? closing_row : cursor + 1;
        const auto candidate = evaluate_coaster_attachment_bezier(braces[cursor], braces[next], t);
        Point difference {};
        for (std::size_t j = 0; j < 3; ++j) {
            difference[j] = current[j] - candidate[j];
        }
        const auto step = length(difference);
        if (!std::isfinite(step)) {
            return false;
        }
        accumulated += step;
        if (first || accumulated >= spacing) {
            const auto alpha = first ? 0.0f : (spacing - prior_accumulated) / step;
            first = false;
            if (!std::isfinite(alpha)) {
                return false;
            }
            for (std::size_t j = 0; j < 3; ++j) {
                current[j] = (1 - alpha) * current[j] + alpha * candidate[j];
            }
            previous_t = (1 - alpha) * previous_t + alpha * t;
            t = 0.05f - std::fmod(previous_t, 0.05f) + previous_t;
            accumulated = 0;
            std::size_t attachment = cursor;
            auto fraction = previous_t;
            while (fraction < 0) {
                if (!attachment) {
                    return false;
                }
                --attachment;
                fraction += 1;
            }
            const auto row_position = braces[attachment].sample_position;
            if (row_position >= 1 && static_cast<std::size_t>(row_position) == built.samples.size()) {
                braces[attachment].sample_position = static_cast<float>(built.samples.size()) - 0.001f;
                meshes[attachment].sample_position = braces[attachment].sample_position;
            }
            RideMotionSample sample;
            sample.position = current;
            sample.brace_row = attachment;
            sample.mesh_row = attachment;
            sample.attachment_fraction = fraction;
            sample.geometry_flags = objects[owners[attachment]].geometry_flags;
            const bool row_terminal = terminal;
            const auto sample_end = row_terminal ? closing_row : attachment + 1;
            if (sample_end >= count) {
                return false;
            }
            RideTrackFrame frame;
            if (!sample_coaster_attachment_frame(braces[attachment], braces[sample_end], row_terminal ? nullptr : &meshes[attachment], row_terminal ? nullptr : &meshes[sample_end], fraction, current, 0, frame)) {
                return false;
            }
            sample.heading_angle = frame.rotation[0];
            sample.pitch_angle = frame.rotation[1];
            sample.bank_angle = frame.rotation[2];
            built.samples.push_back(sample);
            if (terminal) {
                terminal_emitted = true;
            }
        } else {
            previous_t = t;
            current = candidate;
            t += 0.05f;
        }
        if (t <= 1.0f) {
            if (t < 0 && !terminal) {
                if (!cursor) {
                    return false;
                }
                --cursor;
                previous_t += 1;
                t += 1;
            }
            prior_accumulated = accumulated;
        } else if (!terminal) {
            ++cursor;
            previous_t -= 1;
            t -= 1;
            Point anchor_delta {};
            for (std::size_t j = 0; j < 3; ++j) {
                anchor_delta[j] = braces[cursor].position[j] - current[j];
            }
            if (cursor + 1 < count) {
                const auto next_point = evaluate_coaster_attachment_bezier(braces[cursor], braces[cursor + 1], t);
                Point advance {};
                for (std::size_t j = 0; j < 3; ++j) {
                    advance[j] = next_point[j] - current[j];
                }
                const auto advance_length = length(advance), anchor_length = length(anchor_delta);
                if (advance_length > 0 && anchor_length > 0) {
                    float projection = 0;
                    for (std::size_t j = 0; j < 3; ++j) {
                        projection += (anchor_delta[j] / anchor_length) * (advance[j] / advance_length);
                    }
                    for (auto& value : anchor_delta) {
                        value *= projection;
                    }
                }
            }
            const auto fraction = (length(anchor_delta) + accumulated) / spacing;
            const auto previous_index = built.samples.empty() ? 0 : built.samples.size() - 1;
            const auto row_coordinate = static_cast<float>(previous_index) + fraction;
            braces[cursor].sample_position = row_coordinate;
            meshes[cursor].sample_position = row_coordinate;
            if (cursor + 1 == count) {
                built.last_sample_position = row_coordinate;
            }
            const auto owner = owners[cursor];
            const auto local = cursor - built.rows.object_rows[owner].first;
            if (!objects[owner].component_attachment_index || (objects[owner].geometry_flags & 0x2000u) || *objects[owner].component_attachment_index == local) {
                built.geometry_component_indices[owner] = static_cast<std::uint32_t>(row_coordinate);
            }
            prior_accumulated = accumulated;
        } else {
            prior_accumulated = accumulated;
        }
    }
    if (built.samples.empty() || built.samples.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    built.reported_sample_count = static_cast<std::uint32_t>(built.samples.size());
    built.samples.push_back(built.samples.back());
    if (std::ceil(static_cast<double>(built.last_sample_position)) >= static_cast<double>(built.reported_sample_count)) {
        ++built.reported_sample_count;
    }
    for (std::size_t i = 1; i < objects.size(); ++i) {
        if (built.geometry_component_indices[i] < built.geometry_component_indices[i - 1]) {
            return false;
        }
        const auto span = built.geometry_component_indices[i] - built.geometry_component_indices[i - 1] + 1;
        built.maximum_component_index_span = std::max(built.maximum_component_index_span, span);
    }
    if (!coaster_ride_activation_bounds(built.rows, entrance_interval, built.activation_lower, built.activation_upper)) {
        return false;
    }
    output = std::move(built);
    return true;
}
} //namespace simcoaster
