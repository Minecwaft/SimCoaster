#include "simcoaster/coaster_profile.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <algorithm>
#include <cmath>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

float nonzero(float value, float fallback) {
    return value != 0 ? value : fallback;
}

float magnitude(const Point& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

float sine(float angle) {
    return simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian);
}

float cosine(float angle) {
    return simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian);
}

void rotate_y(Point& v, float a) {
    const auto x = v[0], z = v[2], s = sine(a), c = cosine(a);
    v[0] = s * z + c * x;
    v[2] = c * z - x * s;
}

void rotate_x(Point& v, float a) {
    const auto y = v[1], z = v[2], s = sine(a), c = cosine(a);
    v[1] = c * y - s * z;
    v[2] = y * s + c * z;
}

void rotate_z(Point& v, float a) {
    const auto x = v[0], y = v[1], s = sine(a), c = cosine(a);
    v[0] = c * x - s * y;
    v[1] = x * s + c * y;
}

struct RatingAccumulator {
    float sickness {}, excitement {}, previous_excitement {}, peak_sickness {}, peak_excitement {};
    bool normal {}, terror {};

    void add(const CoasterRatingSettings& c, RideMotionSample& sample, std::size_t index) {
        const auto& a = sample.acceleration;
        const auto abs_force = std::abs(a[0]) + std::abs(a[1]) + std::abs(a[2]);
        const auto inversion = std::max(std::abs(sample.bank_angle) - 1.5707964f, 0.0f);
        const auto inverted_weight = inversion > 0 ? 1 + inversion : 0;
        const auto sickness_delta = inverted_weight * c.upside_down_sickness + abs_force * c.g_force_sickness;
        const auto excitement_delta = abs_force * c.g_force_excitement + std::abs(sample.bank_angle) * c.bank_excitement + sample.profile_speed * c.speed_excitement;
        auto discomfort = inverted_weight;
        if (inverted_weight != 0 && a[1] < 0.1f) {
            discomfort *= c.inverted_discomfort;
            //Native tests C0 after intervening FMUL. Model its preceding y<0 test.
            if (a[1] < 0) {
                discomfort *= 3;
            }
        }
        const auto contribution = std::max(excitement_delta - (std::abs(a[0]) * c.x_force_discomfort + discomfort), 0.0f);
        sickness = sickness * 0.5f + sickness_delta;
        excitement = excitement * 0.5f + contribution;
        peak_sickness = std::max(peak_sickness, sickness);
        peak_excitement = std::max(peak_excitement, excitement);
        sample.sample_flags &= ~7u;
        if (index > 2) {
            const auto delta = excitement - previous_excitement;
            if (delta > c.normal_start_delta) {
                normal = true;
            } else if (!(normal && excitement > c.normal_sustain)) {
                normal = false;
            }
            if (normal) {
                sample.sample_flags |= 4u;
            }
            if (delta > c.terror_start_delta) {
                terror = true;
            } else if (!(terror && excitement > c.terror_sustain)) {
                terror = false;
            }
            if (terror) {
                sample.sample_flags |= 2u;
            }
        }
        previous_excitement = excitement;
    }
};
} //namespace

CoasterProfileSettings coaster_profile_settings(const DynamicTextureMeshBalance& balance) {
    const auto& c = balance.coaster;
    CoasterProfileSettings result;
    auto& r = result.rating;
    result.slope_friction = c.bSpecialIsTrough != 0;
    result.force_banking = c.bIsSuspendedCoaster != 0;
    result.gravity = { nonzero(c.fGravityX, 0), nonzero(c.fGravityY, -0.3f), nonzero(c.fGravityZ, 0) };
    result.force_multiplier = nonzero(c.fForceMultiplier, 0.2f);
    result.track_curviness = nonzero(c.fCurviness, 1.25f);
    result.entrance_interval = c.fEntranceSizeInterval;
    r.initial_sickness = nonzero(c.fInitialSickness, 0);
    r.initial_excitement = nonzero(c.fInitialExcitement, 1);
    r.g_force_sickness = nonzero(c.fGForceSicknessModifier, 0.5f);
    r.upside_down_sickness = nonzero(c.fUpsideDownSicknessModifier, 3);
    r.speed_excitement = nonzero(c.fSpeedExcitementModifier, 0.1f);
    r.g_force_excitement = nonzero(c.fGForceExcitementModifier, 1);
    r.bank_excitement = nonzero(c.fBankExcitementModifier, 5);
    r.x_force_discomfort = nonzero(c.fXGForceUncomfortableModifier, 1);
    r.inverted_discomfort = nonzero(c.fInvertedUncomfortableModifier, 2);
    r.normal_start_delta = nonzero(c.fNormalScreamStartExciteDelta, 0.5f);
    r.terror_start_delta = nonzero(c.fTerrorScreamStartExciteDelta, 1);
    r.normal_sustain = nonzero(c.fNormalScreamSustainExcitement, 10);
    r.terror_sustain = nonzero(c.fTerrorScreamSustainExcitement, 20);
    return result;
}

bool rebuild_coaster_speed_profile(std::span<CoasterGeometryObjectInput> objects, CoasterTrackSamples& track, const RideTrainConfiguration& train_config, const CoasterProfileSettings& config, float spacing, float cap, CoasterProfileResult& output) {
    const auto count = track.reported_sample_count;
    if (count < 2 || count > track.samples.size() || track.geometry_component_indices.empty() || !(spacing > 0) || !std::isfinite(spacing) || !(cap > 0) || !std::isfinite(cap)) {
        return false;
    }
    const auto tail = track.geometry_component_indices.back();
    if (!tail || tail > count) {
        return false;
    }
    std::vector<std::size_t> owners;
    owners.reserve(track.samples.size());
    for (const auto& sample : track.samples) {
        if (sample.brace_row >= track.rows.braces.size()) {
            return false;
        }
        const auto id = track.rows.braces[sample.brace_row].geometry_index;
        const auto it = std::ranges::find(objects, id, &CoasterGeometryObjectInput::geometry_index);
        if (it == objects.end()) {
            return false;
        }
        owners.push_back(static_cast<std::size_t>(it - objects.begin()));
    }
    RatingAccumulator ratings;
    ratings.sickness = config.rating.initial_sickness;
    ratings.excitement = config.rating.initial_excitement;
    ratings.previous_excitement = ratings.excitement;
    CoasterProfileResult result;
    float speed = train_config.winch_speed;
    if (!(speed > 0) || !std::isfinite(speed)) {
        return false;
    }
    for (std::size_t i = 1; i + 1 < count; ++i) {
        auto& sample = track.samples[i];
        const auto& previous = track.samples[i - 1].position;
        const auto& next = track.samples[i + 1].position;
        Point incoming {}, outgoing {};
        for (std::size_t j = 0; j < 3; ++j) {
            incoming[j] = previous[j] - sample.position[j];
            outgoing[j] = next[j] - sample.position[j];
        }
        if (config.slope_friction) {
            if (outgoing[1] / magnitude(outgoing) <= 0.017449999f) {
                sample.sample_flags &= ~8u;
            } else {
                sample.sample_flags |= 8u;
            }
        }
        Point acceleration {};
        for (std::size_t j = 0; j < 3; ++j) {
            acceleration[j] = -(incoming[j] + outgoing[j]) / spacing;
        }
        const auto vertical_delta = (sample.position[1] - next[1]) * train_config.acceleration_per_height;
        //FSUBR invalidates the native status bits. Select the downhill modifier
        //when the next point is no higher, as the motion integrator also does.
        speed = std::max(speed + vertical_delta * (next[1] <= sample.position[1] ? train_config.downhill_modifier : train_config.uphill_modifier), train_config.winch_speed);
        if (sample.sample_flags & 8u) {
            speed *= train_config.friction_multiplier;
        }
        speed = std::max(speed, train_config.winch_speed);
        const auto& row = track.rows.braces[sample.brace_row];
        if (row.next && *row.next < track.rows.braces.size() && sample.attachment_fraction >= 0.3f && row.geometry_index != track.rows.braces[*row.next].geometry_index && speed == train_config.winch_speed) {
            objects[owners[i]].geometry_flags |= 0x800u;
        }
        speed = std::min(speed, cap);
        sample.profile_speed = speed;
        for (std::size_t j = 0; j < 3; ++j) {
            sample.acceleration[j] = speed * acceleration[j] * 0.1f;
        }
        ratings.add(config.rating, sample, i);
        result.maximum_speed = std::max(result.maximum_speed, speed);
        result.maximum_y = std::max(result.maximum_y, sample.position[1]);
        result.travel_seconds += spacing / speed;
    }
    track.samples[0].acceleration = {};
    track.samples[0].profile_speed = train_config.winch_speed;
    track.samples[count - 1].acceleration = track.samples[count - 2].acceleration;
    track.samples[count - 1].profile_speed = track.samples[count - 2].profile_speed;
    result.end_fade_start = std::max(static_cast<float>(count - 1) - track.samples[tail - 1].profile_speed / spacing, 0.0f);
    if (config.force_banking) {
        float previous_blend = 3.1415927f;
        for (std::size_t i = 0; i < count; ++i) {
            auto& sample = track.samples[i];
            Point force {};
            for (std::size_t j = 0; j < 3; ++j) {
                force[j] = config.gravity[j] + sample.acceleration[j];
            }
            rotate_y(force, -sample.heading_angle);
            rotate_x(force, -sample.pitch_angle);
            rotate_z(force, -sample.bank_angle);
            const auto aligned = std::atan2(-static_cast<double>(force[0]), static_cast<double>(force[1]));
            const auto delta = static_cast<float>(std::fmod(aligned - previous_blend + 9.42477822303772, 6.283185482025146) - 3.1415927f);
            const auto weight = std::min(magnitude(force) * config.force_multiplier, 1.0f);
            const auto blended = static_cast<float>(std::fmod(weight * delta + previous_blend + 9.42477822303772, 6.283185482025146) - 3.1415927f);
            auto bank = blended - 3.1415927f;
            if (bank < -3.1415927f) {
                bank += 6.2831855f;
            }
            if (static_cast<float>(i) > result.end_fade_start) {
                const auto fraction = std::clamp((static_cast<float>(i) - result.end_fade_start) / (track.last_sample_position - result.end_fade_start), 0.0f, 1.0f);
                bank *= 1 - fraction;
            }
            sample.bank_angle = bank;
            previous_blend = blended;
        }
    }
    result.sickness = std::clamp(ratings.peak_sickness + ratings.sickness, 0.0f, 100.0f);
    result.excitement = std::clamp(ratings.peak_excitement * 1.7f + ratings.excitement * 0.5f, 0.0f, 100.0f);
    for (const auto& object : objects) {
        if ((object.geometry_flags & 0x40u) && (object.geometry_flags & 0x80u)) {
            ++result.vertical_special_count;
        }
    }
    result.track_length = spacing * track.last_sample_position;
    for (std::size_t i = 0; i < track.samples.size(); ++i) {
        track.samples[i].geometry_flags = objects[owners[i]].geometry_flags;
    }
    output = result;
    return true;
}
} //namespace simcoaster
