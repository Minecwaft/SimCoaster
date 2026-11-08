#include "simcoaster/ride_train.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

struct SinCos {
    float sine, cosine;
};

SinCos sin_cos(float radians) {
    return { simulation_sine_at_rounded_index(radians * simulation_sine_samples_per_radian),
             simulation_sine_at_rounded_index((radians + 1.5707964f) * simulation_sine_samples_per_radian) };
}

Point bezier(const RideTrackAttachmentRow& first, const RideTrackAttachmentRow& last, float t) {
    Point out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto p = first.position[i], q = first.outgoing_control[i], r = last.incoming_control[i],
                   s = last.position[i];
        out[i] = (((q * 3 - r * 3 - p + s) * t + (r + p) * 3 - q * 6) * t + q * 3 - p * 3) * t + p;
    }
    return out;
}

Point tangent(const RideTrackAttachmentRow& first, const RideTrackAttachmentRow& last, float t) {
    Point out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto p = first.position[i], q = first.outgoing_control[i], r = last.incoming_control[i],
                   s = last.position[i];
        const auto middle = (r + p) * 3 - q * 6;
        out[i] = ((q * 3 - r * 3 - p + s) * t * 3 + middle + middle) * t + q * 3 - p * 3;
    }
    return out;
}

void rotate_y(Point& p, SinCos sc) {
    const auto x = p[0], z = p[2];
    p[0] = sc.sine * z + sc.cosine * x;
    p[2] = sc.cosine * z - x * sc.sine;
}

void rotate_x(Point& p, SinCos sc) {
    const auto y = p[1], z = p[2];
    p[1] = y * sc.cosine - z * sc.sine;
    p[2] = z * sc.cosine + y * sc.sine;
}

void rotate_z(Point& p, SinCos sc) {
    const auto x = p[0], y = p[1];
    p[0] = x * sc.cosine - sc.sine * y;
    p[1] = sc.cosine * y + x * sc.sine;
}
} //namespace

std::array<float, 3> evaluate_coaster_attachment_bezier(const RideTrackAttachmentRow& first,
                                                        const RideTrackAttachmentRow& last,
                                                        float fraction) {
    return bezier(first, last, fraction);
}

bool sample_coaster_attachment_frame(const RideTrackAttachmentRow& brace,
                                     const RideTrackAttachmentRow& brace_end,
                                     const RideTrackAttachmentRow* mesh,
                                     const RideTrackAttachmentRow* mesh_end,
                                     float fraction,
                                     const std::array<float, 3>& position,
                                     std::uint32_t frame_flags,
                                     RideTrackFrame& frame) {
    frame.position = position;
    auto direction = tangent(brace, brace_end, fraction);
    const auto heading = static_cast<float>(std::atan2(direction[0], direction[2]));
    frame.original_heading = heading;
    rotate_y(direction, sin_cos(-heading));
    auto pitch = static_cast<float>(std::atan2(-direction[1], direction[2]));
    auto final_heading = heading;
    if (frame_flags & 1u) {
        final_heading += 3.1415927f;
        pitch = 3.1415927f - pitch;
        if (final_heading > 3.1415927f)
            final_heading -= 6.2831855f;
        if (pitch > 3.1415927f)
            pitch -= 6.2831855f;
    }
    float roll = 0;
    if (mesh && mesh_end) {
        auto offset = bezier(*mesh, *mesh_end, fraction);
        for (std::size_t i = 0; i < offset.size(); ++i)
            offset[i] -= position[i];
        rotate_y(offset, sin_cos(-final_heading));
        rotate_x(offset, sin_cos(-pitch));
        roll = static_cast<float>(std::atan2(-offset[0], offset[1]));
    }
    frame.rotation = { final_heading, pitch, roll };
    return true;
}

bool sample_ride_track_frame(const RideTrackGeometry& geometry, float coordinate, RideTrackFrame& frame) {
    if (!(coordinate >= 0) || !std::isfinite(coordinate) || coordinate >= static_cast<double>(geometry.samples.size()))
        return false;
    const auto sample_index = static_cast<std::size_t>(coordinate);
    const auto& sample = geometry.samples[sample_index];
    auto a = sample.brace_row, b = sample.mesh_row;
    if (a >= geometry.brace_rows.size() || b >= geometry.mesh_rows.size())
        return false;
    std::size_t visits = 0;
    for (;;) {
        const auto& brace = geometry.brace_rows[a];
        const auto& mesh = geometry.mesh_rows[b];
        if (!brace.next
            || !mesh.next
            || *brace.next >= geometry.brace_rows.size()
            || *mesh.next >= geometry.mesh_rows.size())
            return false;
        const auto next_a = *brace.next, next_b = *mesh.next;
        const auto& end = geometry.brace_rows[next_a];
        if (end.sample_position < coordinate) {
            if (++visits >= geometry.brace_rows.size())
                return false;
            a = next_a;
            b = next_b;
            continue;
        }
        const auto denominator = end.sample_position - brace.sample_position;
        if (denominator == 0)
            return false;
        const auto fraction = (coordinate - brace.sample_position) / denominator;
        return sample_ride_track_segment_frame(geometry, a, b, fraction, frame);
    }
}

bool sample_ride_track_segment_frame(const RideTrackGeometry& geometry,
                                     std::size_t brace_row,
                                     std::size_t mesh_row,
                                     float fraction,
                                     RideTrackFrame& frame) {
    if (brace_row >= geometry.brace_rows.size() || mesh_row >= geometry.mesh_rows.size() || !std::isfinite(fraction))
        return false;
    const auto& brace = geometry.brace_rows[brace_row];
    const auto& mesh = geometry.mesh_rows[mesh_row];
    if (!brace.next
        || !mesh.next
        || *brace.next >= geometry.brace_rows.size()
        || *mesh.next >= geometry.mesh_rows.size())
        return false;
    const auto& end = geometry.brace_rows[*brace.next];
    const auto& mesh_end = geometry.mesh_rows[*mesh.next];
    const auto position = bezier(brace, end, fraction);
    if (!sample_coaster_attachment_frame(brace, end, &mesh, &mesh_end, fraction, position, geometry.frame_flags, frame))
        return false;
    auto roll = frame.rotation[2];
    if (geometry.use_sample_bank) {
        if (!(mesh.sample_position >= 0)
            || !(mesh_end.sample_position >= 0)
            || mesh.sample_position >= static_cast<double>(geometry.samples.size())
            || mesh_end.sample_position >= static_cast<double>(geometry.samples.size()))
            return false;
        const auto first = static_cast<std::size_t>(mesh.sample_position),
                   last = static_cast<std::size_t>(mesh_end.sample_position);
        roll += geometry.samples[first].bank_angle * (1 - fraction) + geometry.samples[last].bank_angle * fraction;
    } else {
        const auto first = static_cast<double>(mesh.geometry_index) + mesh.geometry_fraction;
        const auto last = static_cast<double>(mesh_end.geometry_index) + mesh_end.geometry_fraction;
        const auto axis_fraction = std::clamp((last * fraction + first * (1 - fraction)) - std::trunc(first), 0.0, 1.0);
        roll += static_cast<float>(mesh.geometry_axis_angle * (1 - axis_fraction)
                                   + mesh_end.geometry_axis_angle * axis_fraction);
    }
    frame.rotation[2] = roll;
    return true;
}

std::array<float, 16> build_ride_car_transform(RideTrainCar& car, const RideCarType& type, RideTrackFrame& frame) {
    std::array<float, 16> matrix{};
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
    const auto z = sin_cos(frame.rotation[2]), x = sin_cos(frame.rotation[1]), y = sin_cos(frame.rotation[0]);
    for (std::size_t row = 0; row < 3; ++row) {
        Point basis{ matrix[row * 4], matrix[row * 4 + 1], matrix[row * 4 + 2] };
        rotate_z(basis, z);
        rotate_x(basis, x);
        rotate_y(basis, y);
        for (std::size_t column = 0; column < 3; ++column)
            matrix[row * 4 + column] = basis[column];
    }
    if (type.vertical_offset != 0) {
        Point offset{ 0, type.vertical_offset, 0 };
        rotate_z(offset, z);
        rotate_x(offset, x);
        rotate_y(offset, y);
        for (std::size_t i = 0; i < offset.size(); ++i)
            frame.position[i] += offset[i];
    }
    for (std::size_t i = 0; i < 3; ++i) {
        car.mesh_transform_row2[i] = matrix[8 + i];
        matrix[12 + i] = frame.position[i];
    }
    return matrix;
}
} //namespace simcoaster
