#include "simcoaster/ride_train.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
std::pair<float, float> sincos(float angle) {
    return { simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian),
             simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian) };
}

float wrapped_angle(float start, float end, float fraction) {
    const auto gap = std::abs(static_cast<double>(start) - end);
    if (gap <= 3.1415927f)
        return (1 - fraction) * start + fraction * end;
    const auto remaining = 6.2831855f - gap;
    const auto angle = start >= 0 ? start + remaining * fraction : end + remaining * (1 - fraction);
    return static_cast<float>(std::fmod(angle + 3.141592741012573, 6.283185482025146) - 3.1415927f);
}
} //namespace

std::optional<RideBendPose> build_bend_car_pose(std::span<const RideMotionSample> samples,
                                                std::size_t count,
                                                const RideTrainCar& car,
                                                const RideCarType& type,
                                                float spacing,
                                                float low,
                                                float high) {
    if (!(spacing > 0)
        || !std::isfinite(spacing)
        || count < 2
        || count > samples.size()
        || !(car.track_position >= 0)
        || car.track_position >= static_cast<double>(count - 1)
        || !std::isfinite(type.rear_extent + type.front_extent)
        || type.rear_extent < 0
        || type.front_extent < 0)
        return std::nullopt;
    const auto back_distance = type.rear_extent / spacing, front_distance = type.front_extent / spacing;
    if (back_distance >= static_cast<double>(std::numeric_limits<std::size_t>::max() / 2 - 2)
        || front_distance >= static_cast<double>(std::numeric_limits<std::size_t>::max() / 2 - 2))
        return std::nullopt;
    const auto rear = static_cast<std::size_t>(back_distance) + 1, front = static_cast<std::size_t>(front_distance) + 1;
    const auto center = static_cast<std::size_t>(car.track_position);
    const auto fraction = car.track_position - static_cast<float>(center);
    RideBendPose result;
    result.center = rear;
    result.samples.resize(rear + front + 1);
    const auto position = [&](std::size_t i) {
        std::array<float, 3> p;
        for (std::size_t axis = 0; axis < 3; ++axis)
            p[axis] = samples[i].position[axis] * (1 - fraction) + samples[i + 1].position[axis] * fraction;
        return p;
    };
    const auto elevation = [&](std::size_t i) {
        return ((samples[i].sample_flags & 8u) ? high : low) * (1 - fraction)
             + ((samples[i + 1].sample_flags & 8u) ? high : low) * fraction;
    };
    const auto origin = position(center);
    const auto heading = wrapped_angle(samples[center].heading_angle, samples[center + 1].heading_angle, fraction);
    const auto [sine, cosine] = sincos(-heading);
    const auto& forward = car.mesh_transform_row2;
    const auto horizontal = std::hypot(forward[0], forward[2]);
    const auto aligned_z = cosine * forward[2] - sine * forward[0], vertical = std::hypot(aligned_z, forward[1]);
    if (!(horizontal > 0) || !(vertical > 0))
        return std::nullopt;
    const auto base_x = forward[0] / horizontal, base_z = forward[2] / horizontal, base_y = forward[1] / vertical,
               base_pitch_z = aligned_z / vertical;
    result.samples[rear].elevation = elevation(center);
    const auto sample = [&](std::size_t at, bool backward) -> std::optional<RideBendSample> {
        const auto p = position(at);
        const auto x = p[0] - origin[0], y = p[1] - origin[1], z = p[2] - origin[2];
        const auto xz = std::hypot(x, z);
        if (!(xz > 0))
            return std::nullopt;
        const auto [s, c] = sincos(static_cast<float>(-std::atan2(x, z)));
        const auto pitched_z = c * z - s * x, yz = std::hypot(y, pitched_z);
        if (!(yz > 0))
            return std::nullopt;
        const auto dx = x / xz, dz = z / xz, dy = y / yz, dpz = pitched_z / yz, sign = backward ? -1.0f : 1.0f;
        RideBendSample value;
        value.yaw = std::acos(std::clamp(sign * (dx * base_x + dz * base_z), 0.0f, 1.0f));
        if (sign * (dz * base_x - dx * base_z) > 0)
            value.yaw = -value.yaw;
        value.pitch = std::acos(std::clamp(dpz * base_pitch_z + dy * base_y * sign, 0.0f, 1.0f));
        const auto cross = dpz * base_y * sign - base_pitch_z * dy;
        if (backward ? cross > 0 : cross < 0)
            value.pitch = -value.pitch;
        value.elevation = elevation(at);
        return value;
    };
    for (std::size_t k = 1; k <= std::min(rear, center); ++k) {
        const auto value = sample(center - k, true);
        if (!value)
            return std::nullopt;
        result.samples[rear - k] = *value;
    }
    //The native final component may read its explicitly duplicated sentinel.
    const auto available = std::min(front, count - 1 - center);
    for (std::size_t k = 1; k <= available; ++k) {
        if (center + k + 1 >= samples.size())
            break;
        const auto value = sample(center + k, false);
        if (!value)
            return std::nullopt;
        result.samples[rear + k] = *value;
    }
    return result;
}
} //namespace simcoaster
