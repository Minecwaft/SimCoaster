#include "simcoaster/ride_train.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
constexpr auto flag(RideTrainFlag value) {
    return std::to_underlying(value);
}

constexpr auto flag(RideTrainCarFlag value) {
    return std::to_underlying(value);
}

using Point = std::array<float, 3>;

bool sample_position(std::span<const RideMotionSample> samples,
                     float last,
                     float coordinate,
                     Point& point,
                     std::size_t* index = nullptr) {
    if (!std::isfinite(coordinate))
        return false;
    coordinate = coordinate >= 0 ? std::fmod(coordinate, last) : coordinate + last;
    if (!(coordinate >= 0) || coordinate >= last)
        return false;
    const auto whole = std::floor(coordinate);
    const auto i = static_cast<std::size_t>(whole);
    if (i + 1 >= samples.size())
        return false;
    const auto fraction = coordinate - whole;
    for (std::size_t j = 0; j < point.size(); ++j)
        point[j] = (1.0f - fraction) * samples[i].position[j] + fraction * samples[i + 1].position[j];
    if (index)
        *index = i;
    return true;
}

float length_between(const Point& a, const Point& b) {
    const auto x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

void update_bounds(const RideTrainState& state, RideTrain& train) {
    const auto& first = train.cars[train.lead_car];
    const auto& last = train.cars[train.lead_car + train.car_count - 1];
    const auto spacing = state.track_sample_spacing;
    const auto& types = state.configuration.car_types;
    train.activation_lower =
        first.track_offset / spacing + train.distance + types[first.type_index].front_extent / spacing;
    train.activation_upper =
        last.track_offset / spacing + train.distance - types[last.type_index].rear_extent / spacing;
}

std::optional<std::uint32_t> install_train_pose(RideTrainState& state,
                                                RideTrain& train,
                                                const RideTrackGeometry& geometry,
                                                float last,
                                                RideTrainMotionBackend& backend) {
    const auto& configuration = state.configuration;
    std::uint32_t driven = 0;
    for (std::uint32_t j = 0; j < train.car_count; ++j) {
        auto& car = train.cars[j];
        if (!(car.flags & flag(RideTrainCarFlag::active))) {
            car.track_position = 0;
            continue;
        }
        auto position = car.track_offset / state.track_sample_spacing + train.distance;
        position = position >= 0 ? std::fmod(position, last) : position + last;
        if (!(position >= 0) || position >= last || !std::isfinite(position))
            return std::nullopt;
        car.track_position = position;
        RideTrackFrame frame;
        if (!sample_ride_track_frame(geometry, position, frame))
            return std::nullopt;
        const auto& type = configuration.car_types.at(car.type_index);
        if (car.mesh_slot) {
            const auto matrix = build_ride_car_transform(car, type, frame);
            if (!backend.set_car_pose(car,
                                      matrix,
                                      type.bend ? std::nullopt : std::optional{ train.speed * type.animation_speed }))
                return std::nullopt;
        }
        if (j == train.center_car)
            train.center_position = frame.position;
        if (j == train.lead_car)
            train.lead_position = frame.position;
        if (type.bend)
            backend.update_bend_car_pose(car);
        if (car.flags & flag(RideTrainCarFlag::winch_driven))
            ++driven;
    }
    return driven;
}
} //namespace

bool install_ride_train_poses(RideTrainState& state,
                              const RideTrackGeometry& geometry,
                              float last,
                              RideTrainMotionBackend& backend) {
    if (state.active_train_count > state.trains.size() || !(last > 0) || !(state.track_sample_spacing > 0))
        return false;
    for (std::size_t i = 0; i < state.active_train_count; ++i) {
        auto& train = state.trains[i];
        if (train.car_count > train.cars.size() || train.center_car >= train.cars.size())
            return false;
        for (const auto& car : train.cars)
            if (car.type_index >= state.configuration.car_types.size())
                return false;
        if (!install_train_pose(state, train, geometry, last, backend))
            return false;
    }
    return true;
}

bool update_ride_train_motion(RideTrainState& state,
                              const RideTrackGeometry& geometry,
                              const RideMotionSettings& settings,
                              std::int64_t time_ms,
                              RideTrainMotionBackend& backend) {
    const auto samples = geometry.samples;
    const auto component_count = geometry.component_count ? geometry.component_count : samples.size();
    const auto& c = state.configuration;
    const auto count = state.active_train_count;
    const auto spacing = state.track_sample_spacing;
    if (count > state.trains.size()
        || component_count < 2
        || component_count > samples.size()
        || !(spacing > 0)
        || !std::isfinite(spacing)
        || !(settings.last_sample_position > 0)
        || !std::isfinite(settings.last_sample_position)
        || settings.last_sample_position > static_cast<float>(samples.size() - 1))
        return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& train = state.trains[i];
        if (!train.car_count
            || train.car_count > train.cars.size()
            || train.lead_car > train.cars.size() - train.car_count
            || train.center_car >= train.cars.size())
            return false;
        for (const auto& car : train.cars)
            if (car.type_index >= c.car_types.size())
                return false;
    }
    if (time_ms < 0 || ((state.runtime_flags & 0x10u) && time_ms < state.last_simulation_time_ms))
        return false;
    if (!(state.runtime_flags & 0x10u)) {
        state.last_simulation_time_ms = time_ms;
        state.runtime_flags |= 0x10u;
    }
    const auto elapsed_ms = time_ms - state.last_simulation_time_ms;
    const auto seconds = static_cast<float>(static_cast<double>(elapsed_ms) * 0.001);
    const auto minimum = c.minimum_speed * state.animation_speed_multiplier;
    const auto winch = c.winch_speed * state.animation_speed_multiplier;
    state.current_train_index = 0;
    std::vector<std::uint32_t> order;
    order.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& train = state.trains[i];
        train.remaining_update_seconds = seconds;
        for (std::uint32_t j = 0; j < train.car_count; ++j) {
            auto& car = train.cars[j];
            if ((car.flags & flag(RideTrainCarFlag::active))
                && !sample_position(samples,
                                    settings.last_sample_position,
                                    car.track_offset / spacing + train.distance,
                                    car.previous_sample))
                return false;
        }
        order.push_back(i);
    }
    while (!order.empty()) {
        const auto index = order.front();
        auto& train = state.trains[index];
        const float baseline = std::min(train.distance, settings.last_sample_position);
        const auto winch_limit = ((train.flags & flag(RideTrainFlag::release_gap_pending))
                                  && (train.flags & flag(RideTrainFlag::passenger_exchange_zone)))
                                   ? winch * 0.5f
                                   : winch;
        float sum = 0;
        std::uint32_t fade_cars = 0;
        bool any_winch = false;
        for (std::uint32_t j = 0; j < train.car_count; ++j) {
            auto& car = train.cars[j];
            if (!(car.flags & flag(RideTrainCarFlag::active)))
                continue;
            auto position = car.track_offset / spacing + train.distance;
            if (position < 0)
                position += settings.last_sample_position;
            std::size_t sample{};
            if (!sample_position(samples, settings.last_sample_position, position + 0.5f, car.current_sample, &sample))
                return false;
            const auto change = (car.previous_sample[1] - car.current_sample[1]) * c.acceleration_per_height;
            const auto modifier =
                car.current_sample[1] <= car.previous_sample[1] ? c.downhill_modifier : c.uphill_modifier;
            const auto driven = (samples[sample].geometry_flags & 0x800u) != 0;
            const auto car_speed = std::max(change * modifier + train.speed, driven ? winch_limit : minimum);
            sum += car_speed;
            if (position > settings.end_fade_start)
                ++fade_cars;
            if (driven) {
                any_winch = true;
                car.flags |= flag(RideTrainCarFlag::winch_driven);
            } else
                car.flags &= ~flag(RideTrainCarFlag::winch_driven);
        }
        sum /= static_cast<float>(train.car_count);
        auto& center = train.cars[train.center_car];
        const auto half_distance = length_between(center.previous_sample, center.current_sample);
        if (!(half_distance > 0) || !std::isfinite(half_distance))
            return false;
        float target{};
        if (!fade_cars || (train.flags & flag(RideTrainFlag::spacing_contact))) {
            if (!(center.track_position >= 0) || !std::isfinite(center.track_position))
                return false;
            const auto sample = static_cast<std::size_t>(center.track_position);
            if (sample >= samples.size())
                return false;
            target = (samples[sample].sample_flags & 8u) ? sum * c.friction_multiplier : sum;
            train.flags &= ~flag(RideTrainFlag::speed_fade);
        } else {
            auto blend = (train.cars[train.lead_car].track_offset / spacing + train.distance - settings.end_fade_start)
                       / (settings.last_sample_position - settings.end_fade_start);
            if (blend < 0 || blend > 1)
                blend = 1;
            train.sound_fade_blend = blend * blend * blend * blend;
            target = (1 - train.sound_fade_blend) * sum + train.sound_fade_blend * winch_limit;
            train.flags |= flag(RideTrainFlag::speed_fade);
        }
        if (any_winch) {
            target = std::max(target, winch_limit);
            train.flags |= flag(RideTrainFlag::winch_driven);
        } else {
            target = std::max(target, minimum);
            train.flags &= ~flag(RideTrainFlag::winch_driven);
        }
        target = std::min(target, settings.speed_cap);
        const auto average = (target + train.speed) * 0.5f;
        if (average < 0 || !std::isfinite(average))
            return false;
        const auto half_seconds = average == 0 ? std::numeric_limits<float>::infinity() : half_distance / average;
        if (half_seconds < train.remaining_update_seconds) {
            train.speed = target;
            train.remaining_update_seconds -= half_seconds;
            train.distance += 0.5f;
            for (std::uint32_t j = 0; j < train.car_count; ++j)
                train.cars[j].previous_sample = train.cars[j].current_sample;
            order.erase(order.begin());
            auto at = order.begin();
            while (at != order.end() && state.trains[*at].remaining_update_seconds > train.remaining_update_seconds)
                ++at;
            order.insert(at, index);
        } else {
            const auto ratio = train.remaining_update_seconds / half_seconds;
            auto speed = (1 - ratio) * train.speed + ratio * target;
            train.distance =
                (speed + train.speed) * train.remaining_update_seconds * 0.5f / half_distance * 0.5f + baseline;
            //The executable writes its minimum-clamped temporary, then overwrites it
            //with this upper-clamped value; retaining the latter matches the store.
            train.speed = std::min(speed, settings.speed_cap);
            order.erase(order.begin());
        }
        if (train.speed < 2.0f)
            train.speed *= 0.95f;
        //FCOM is followed by FST before its status is read in the x87 binary; the
        //C0 bit is architecturally undefined there. Use the evident comparison.
        if (train.speed < 0.01f)
            train.speed = 0;
        update_bounds(state, train);
        if (!adjust_ride_train_speed_for_spacing(state, index, settings.last_sample_position, backend))
            return false;
        if (train.distance >= settings.last_sample_position)
            train.distance = std::fmod(train.distance, settings.last_sample_position);
        if (train.distance > settings.activation_upper && train.distance < settings.end_fade_start)
            train.flags &= ~flag(RideTrainFlag::spacing_contact);
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& train = state.trains[i];
        const auto posed = install_train_pose(state, train, geometry, settings.last_sample_position, backend);
        if (!posed)
            return false;
        const auto driven = *posed;
        if (train.flags & flag(RideTrainFlag::speed_fade))
            //47FB61 uses nearest-even. The quartic blend lies in [0,1], so the
            //exact half-way value rounds to zero without changing host FP state.
            set_ride_train_transition(train,
                                      RideTrainTransition::speed_fade,
                                      train.sound_fade_blend > 0.5f ? 100 : 50,
                                      settings.recording_disabled,
                                      backend);
        else if (!driven || state.animation_speed_multiplier == 0)
            set_ride_train_transition(train, RideTrainTransition::unpowered, 100, settings.recording_disabled, backend);
        else {
            if (!c.maximum_cars)
                return false;
            const auto mode = train.speed / settings.speed_cap <= 0.3f ? RideTrainTransition::winch_slow
                                                                       : RideTrainTransition::winch_fast;
            set_ride_train_transition(train,
                                      mode,
                                      static_cast<std::int32_t>(100u * driven / c.maximum_cars),
                                      settings.recording_disabled,
                                      backend);
        }
        if (train.distance < state.trains[state.current_train_index].distance)
            state.current_train_index = i;
        if (train.flags & flag(RideTrainFlag::release_gap_pending)) {
            auto next = static_cast<std::int64_t>(state.trains[(i + 1) % count].distance);
            if (static_cast<double>(next) < train.distance)
                next += static_cast<std::int64_t>(component_count - 1);
            const auto current = static_cast<std::int64_t>(train.distance);
            double gap = 0;
            if (current < next) {
                //Approved repair of47FC51: advance across the actual integer gap,
                //retaining native endpoints, lap arithmetic and zero-speed infinity.
                for (auto position = current; position < next; ++position) {
                    if (position < 0)
                        return false;
                    const auto speed = samples[static_cast<std::size_t>(position) % component_count].profile_speed;
                    if (speed < 0 || !std::isfinite(speed))
                        return false;
                    if (speed == 0) {
                        gap = std::numeric_limits<double>::infinity();
                        break;
                    }
                    gap += static_cast<double>(spacing) / speed;
                }
            }
            if (static_cast<double>(c.release_rate_ms) <= gap * 1000.0)
                train.flags &= ~flag(RideTrainFlag::release_gap_pending);
        }
    }
    state.last_simulation_time_ms = time_ms;
    return true;
}
} //namespace simcoaster
