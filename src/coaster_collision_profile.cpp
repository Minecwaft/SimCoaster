#include "simcoaster/sam_schema.hpp"
#include "simcoaster/coaster_collision.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

struct ProfileSam {
    [[=sam::Rows{ "asTrackCollisionPoints" }]] std::array<CoasterCollisionPoint, 6> points;
    [[=sam::Extent{ "asTrackCollisionPoints" }]] std::uint32_t count{};
};

double dot(const Point& a, const Point& b) {
    return static_cast<double>(a[2]) * b[2] + static_cast<double>(a[1]) * b[1] + static_cast<double>(a[0]) * b[0];
}

Point delta(const Point& a, const Point& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

std::vector<Point>
rings(const CoasterCollisionProfile& profile, const RideMotionSample& first, const RideMotionSample& last) {
    const auto sine = [](float angle) {
        const float index = angle * simulation_sine_samples_per_radian;
        if (!std::isfinite(index)
            || static_cast<double>(index) < std::numeric_limits<std::int32_t>::min()
            || static_cast<double>(index) > std::numeric_limits<std::int32_t>::max())
            return simulation_sine_at_index(0);
        //The actual caller 0x47AC30 selects truncation around its rebuild. Express
        //the resulting index rule directly, without emulating x87 control state.
        return simulation_sine_at_index(static_cast<std::int32_t>(index));
    };
    const double sz = sine(first.bank_angle), cz = sine(first.bank_angle + 1.5707964f);
    const double sx = sine(first.pitch_angle), cx = sine(first.pitch_angle + 1.5707964f);
    const double sy = sine(first.heading_angle), cy = sine(first.heading_angle + 1.5707964f);
    std::vector<Point> result;
    result.reserve(profile.points.size() * 2);
    for (const auto* endpoint : { &first, &last })
        for (const auto& source : profile.points) {
            //Both endpoint rings use the first component's orientation. Every axis
            //writes binary32 coordinates before the following rotation (0x47B695).
            Point p{ static_cast<float>(source.x * cz - source.y * sz),
                     static_cast<float>(source.y * cz + source.x * sz),
                     0 };
            const auto y = p[1];
            p[1] = static_cast<float>(y * cx - p[2] * sx);
            p[2] = static_cast<float>(y * sx + p[2] * cx);
            const auto x = p[0];
            p[0] = static_cast<float>(x * cy + p[2] * sy);
            p[2] = static_cast<float>(p[2] * cy - x * sy);
            for (std::size_t axis = 0; axis < 3; ++axis)
                p[axis] += endpoint->position[axis];
            result.push_back(p);
        }
    return result;
}

bool contains(const std::array<Point, 4>& corners,
              const Point& origin,
              const Point& direction,
              float fraction,
              bool longitudinal) {
    const double x = static_cast<double>(fraction) * direction[0] + origin[0];
    const float y = static_cast<float>(static_cast<double>(fraction) * direction[1] + origin[1]);
    const float z = static_cast<float>(static_cast<double>(fraction) * direction[2] + origin[2]);
    std::array<Point, 4> unit;
    for (std::size_t i = 0; i < unit.size(); ++i) {
        //The delegated longitudinal test retains corner3.x through its length;
        //the inline transverse test spills every corner coordinate to float.
        const double dx = static_cast<double>(corners[i][0]) - x;
        Point p{ static_cast<float>(dx), corners[i][1] - y, corners[i][2] - z };
        double length = std::sqrt(dot(p, p));
        if (longitudinal && i == 3)
            length = static_cast<float>(
                std::sqrt(static_cast<double>(p[2]) * p[2] + static_cast<double>(p[1]) * p[1] + dx * dx));
        for (std::size_t axis = 0; axis < 3; ++axis)
            unit[i][axis] = static_cast<float>((longitudinal && i == 3 && axis == 0 ? dx : p[axis]) / length);
    }
    const auto angle = [&](std::size_t a, std::size_t b) {
        return std::acos(dot(unit[a], unit[b]));
    };
    //Ordinary x64 arithmetic is deliberate. The original caller's truncating
    //x87 mode changes four coincident endpoint fixtures; the native oracle
    //checks both control policies with lookup conversion isolated. No clamp,
    //zero-length substitute or conventional polygon order is introduced.
    if (longitudinal)
        return angle(0, 1) + angle(1, 2) + angle(2, 3) + angle(3, 0) >= 6.2821856f;
    float sum = static_cast<float>(angle(0, 1));
    sum = static_cast<float>(angle(1, 2) + sum);
    sum = static_cast<float>(angle(2, 3) + sum);
    return angle(3, 0) + sum >= 6.2821856f;
}
} //namespace

CoasterCollisionProfile default_coaster_collision_profile() {
    return { { { 4, 8 }, { 4, 0 }, { 0, -3 }, { -4, -1 }, { -4, 8 } } };
}

std::expected<CoasterCollisionProfile, MeshError> parse_coaster_collision_profile(std::string_view text) {
    ProfileSam record{};
    const auto read = sam::parse_record<ProfileSam, true>(record, text);
    if (!read)
        return std::unexpected(
            MeshError{ MeshFailure::configuration, read.error().offset, "Coaster.sam collision profile", {} });
    if (!record.count)
        return default_coaster_collision_profile();
    CoasterCollisionProfile profile;
    for (std::size_t i = 0; i < record.count; ++i) {
        const auto p = record.points[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return std::unexpected(MeshError{ MeshFailure::configuration, i, "Coaster.sam collision point", {} });
        profile.points.push_back(p); //Unlike mesh sections, no track-height offset.
    }
    return profile;
}

std::vector<CoasterCollisionCandidate> collect_coaster_collision_candidates(std::span<const RideMotionSample> samples,
                                                                            std::size_t first,
                                                                            std::size_t last,
                                                                            float padding,
                                                                            const MeshBounds& overlap) {
    std::vector<CoasterCollisionCandidate> result;
    if (first > last || last >= samples.size())
        return result;
    MeshBounds expanded = overlap;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expanded.minimum[axis] -= padding;
        expanded.maximum[axis] += padding;
    }
    const auto inside = [&](const Point& p) {
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!(p[axis] >= expanded.minimum[axis] && p[axis] <= expanded.maximum[axis]))
                return false;
        return true;
    };
    for (std::size_t i = first; i < last; ++i) {
        const auto& a = samples[i].position;
        const auto& b = samples[i + 1].position;
        //The native gate admits endpoint candidates, not segment/box crossings.
        if (!inside(a) && !inside(b))
            continue;
        CoasterCollisionCandidate candidate{ i, {} };
        for (std::size_t axis = 0; axis < 3; ++axis) {
            candidate.bounds.minimum[axis] = std::min(a[axis], b[axis]) - padding;
            candidate.bounds.maximum[axis] = std::max(a[axis], b[axis]) + padding;
        }
        result.push_back(candidate);
    }
    return result;
}

bool coaster_collision_segments_intersect(const CoasterCollisionProfile& profile_a,
                                          const RideMotionSample& first_a,
                                          const RideMotionSample& last_a,
                                          const CoasterCollisionProfile& profile_b,
                                          const RideMotionSample& first_b,
                                          const RideMotionSample& last_b) {
    //Verified callers compare one runtime to itself. Different-sized profiles
    //would overrun the native scratch rings and are outside that contract.
    const auto count = profile_a.points.size();
    if (!count || count > 6 || profile_b.points.size() != count)
        return false;
    const auto a = rings(profile_a, first_a, last_a), b = rings(profile_b, first_b, last_b);
    for (std::size_t i = 0; i < count; ++i) {
        const auto j = (i + 1) % count;
        const std::array<Point, 4> quad{ a[i], a[j], a[i + count], a[j + count] };
        const double ex = static_cast<double>(a[j][0]) - a[i][0], ey = static_cast<double>(a[j][1]) - a[i][1],
                     ez = static_cast<double>(a[j][2]) - a[i][2];
        const float dx = a[j][0] - a[i + count][0], dy = a[j][1] - a[i + count][1];
        const double dz = static_cast<double>(a[j][2]) - a[i + count][2];
        Point normal{ static_cast<float>(dz * ey - dy * ez), static_cast<float>(dx * ez - dz * ex), 0 };
        const double nz = ex * dy - dx * ey, length = std::sqrt(nz * nz
                                                                + static_cast<double>(normal[1]) * normal[1]
                                                                + static_cast<double>(normal[0]) * normal[0]);
        normal[0] = static_cast<float>(normal[0] / length);
        normal[1] = static_cast<float>(normal[1] / length);
        normal[2] = static_cast<float>(nz / length);
        const float offset = static_cast<float>(-dot(normal, a[j]));
        for (std::size_t k = 0; k < count; ++k) {
            const auto transverse = delta(b[(k + 1) % count], b[k]), longitudinal = delta(b[k + count], b[k]);
            //Preserve the expression order of the plane-distance and denominator.
            const double distance = static_cast<double>(normal[1]) * b[k][1]
                                  + static_cast<double>(normal[0]) * b[k][0]
                                  + static_cast<double>(normal[2]) * b[k][2]
                                  + offset;
            for (const bool along : { false, true }) {
                const auto& direction = along ? longitudinal : transverse;
                const float fraction = static_cast<float>(-distance / dot(direction, normal));
                if (fraction >= 0 && fraction <= 1 && contains(quad, b[k], direction, fraction, along))
                    return true;
            }
        }
    }
    return false;
}

std::optional<CoasterInvalidTrackSection> coaster_invalid_track_section(std::uint32_t flags,
                                                                        std::uint32_t successor_flags,
                                                                        std::size_t peers,
                                                                        std::size_t textures,
                                                                        std::uint32_t selection_bias) {
    if (!peers && !(flags & 0x1000000u) && !(successor_flags & 0x802000u))
        return {};
    const auto bias = (flags & 0x1000000u) ? ((flags & 0x4000u) ? 2u : selection_bias)
                                           : (!peers && !(successor_flags & 0x4000u) ? 3u : 2u);
    //Native bias0 can address the last normal texture when no selection is live.
    return CoasterInvalidTrackSection{ textures + bias - 1 };
}

std::span<const SamFieldContract> coaster_collision_sam_contracts() {
    static constexpr auto fields = std::define_static_array(compiled_sam_contracts<ProfileSam>());
    return fields;
}
} //namespace simcoaster
