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

bool valid_train(const RideTrainState& state, const RideTrain& train) {
    if (train.car_count > train.cars.size() || train.lead_car > train.cars.size() - train.car_count)
        return false;
    for (const auto& car : train.cars)
        if (car.type_index >= state.configuration.car_types.size())
            return false;
    return true;
}

void update_bounds(const RideTrainState& state, RideTrain& train) {
    const auto& first = train.cars[train.lead_car];
    const auto& last = train.cars[train.lead_car + train.car_count - 1];
    const auto& types = state.configuration.car_types;
    const auto spacing = state.track_sample_spacing;
    train.activation_lower =
        first.track_offset / spacing + train.distance + types[first.type_index].front_extent / spacing;
    train.activation_upper =
        last.track_offset / spacing + train.distance - types[last.type_index].rear_extent / spacing;
}
} //namespace

bool set_ride_train_count(RideTrainState& state, std::uint32_t count, RideTrainLifecycleBackend& backend) {
    if (!count)
        return true;
    if (count > state.trains.size() || state.active_train_count > state.trains.size())
        return false;
    if ((state.detail_state_flags & 0xCu) != 0 && count < state.active_train_count)
        for (auto i = count; i < state.active_train_count; ++i) {
            backend.release_train_passenger_meshes(state.trains[i], true);
            backend.release_train_passenger_meshes(state.trains[i], false);
        }
    state.active_train_count = count;
    return true;
}

bool initialize_ride_train_resources(RideTrainState& state, RideTrain& train, RideTrainLifecycleBackend& backend) {
    if (!(state.placement_flags & 0x4u))
        return true;
    if (!valid_train(state, train))
        return false;
    state.runtime_flags |= 0x800u;
    train.flags |= flag(RideTrainFlag::resources_active);
    for (std::uint32_t i = 0; i < train.car_count; ++i)
        train.cars[i].reset_word = 0;
    for (std::uint32_t i = train.car_count; i > 0;) {
        auto& car = train.cars[--i];
        car.flags |= flag(RideTrainCarFlag::active);
        if (car.mesh_slot)
            backend.register_car_mesh(car.mesh_slot);
        car.persons_a.clear();
        car.persons_b.clear();
        if (!state.configuration.car_types[car.type_index].bend && car.mesh_slot)
            backend.reset_non_bend_car_animation(car.mesh_slot);
    }
    backend.initialize_train_rsse(train);
    return true;
}

bool stop_ride_train_resources(RideTrainState& state, RideTrain& train, RideTrainLifecycleBackend& backend) {
    if (!valid_train(state, train))
        return false;
    for (std::uint32_t i = train.lead_car; i < train.lead_car + train.car_count; ++i) {
        auto& car = train.cars[i];
        car.flags &= ~flag(RideTrainCarFlag::active);
        if (car.mesh_slot)
            backend.unregister_car_mesh(car.mesh_slot);
    }
    backend.clear_train_rsse(train);
    train.flags &= ~flag(RideTrainFlag::resources_active);
    return true;
}

void resume_ride_train_motion(RideTrainState& state) {
    state.animation_speed_multiplier = 1.0f;
    for (std::size_t i = 0; i < std::min<std::size_t>(state.active_train_count, state.trains.size()); ++i)
        state.trains[i].flags =
            (state.trains[i].flags & ~flag(RideTrainFlag::spacing_contact)) | flag(RideTrainFlag::release_gap_pending);
}

bool adjust_ride_train_speed_for_spacing(RideTrainState& state,
                                         std::uint32_t index,
                                         float last_sample_position,
                                         RideTrainEffectBackend& backend) {
    if (state.active_train_count > state.trains.size() || index >= state.active_train_count)
        return false;
    if (state.active_train_count <= 1)
        return true;
    auto& current = state.trains[index];
    auto& next = state.trains[(index + 1) % state.active_train_count];
    auto next_bound = next.activation_upper;
    if (next.distance < current.distance)
        next_bound += last_sample_position;
    if (next_bound < current.activation_lower && next.speed < current.speed) {
        const auto before = current.speed;
        current.speed = (next.speed + next.speed + before) * 0.33333334f;
        next.flags |= flag(RideTrainFlag::spacing_contact);
        next.speed = (before + before + next.speed) * 0.33333334f;
        if (std::abs(current.speed - next.speed) > 1.0f)
            backend.submit_train_spacing_effect(current);
    }
    return true;
}

void set_ride_train_transition(RideTrain& train,
                               RideTrainTransition transition,
                               std::int32_t control,
                               bool disabled,
                               RideTrainEffectBackend& backend) {
    const auto raw = std::to_underlying(transition);
    if (raw != train.transition_state) {
        std::optional<std::int32_t> target;
        match (transition) {
            case RideTrainTransition::target_15 => target = 15;
            case RideTrainTransition::winch_slow => target = 30;
            case RideTrainTransition::winch_fast => target = 70;
            case RideTrainTransition::speed_fade => target = 90;
            case RideTrainTransition::unpowered => target = 50;
            case _ => {}
        }
        if (target && !disabled)
            train.positioned_request_b =
                backend.set_rsse_value(train.positioned_request_b, train.rsse_value_slot_7, *target);
        train.transition_state = raw;
    }
    if (!disabled)
        train.positioned_request_b =
            backend.set_rsse_value(train.positioned_request_b, train.rsse_value_slot_8, control);
}

bool reseed_ride_trains_from_profile(RideTrainState& state,
                                     std::span<const float> speeds,
                                     float last_position,
                                     RideTrainLifecycleBackend& backend,
                                     std::size_t component_count) {
    const auto& c = state.configuration;
    if (!component_count)
        component_count = speeds.size();
    if (component_count < 3
        || component_count > speeds.size()
        || speeds.size() < 3
        || state.trains.size() != c.maximum_trains
        || !c.maximum_trains
        || !c.release_rate_ms
        || !(state.track_sample_spacing > 0)
        || !std::isfinite(state.track_sample_spacing)
        || !(last_position > 0)
        || !std::isfinite(last_position)
        || last_position > static_cast<float>(speeds.size() - 1))
        return false;
    for (const auto speed : speeds.first(component_count))
        if (!(speed > 0) || !std::isfinite(speed))
            return false;
    for (const auto& train : state.trains)
        if (!valid_train(state, train) || !train.car_count || train.center_car >= train.car_count)
            return false;
    float total = 0;
    for (std::size_t i = 1; i + 1 < component_count; ++i)
        total += state.track_sample_spacing / speeds[i];
    const double interval = static_cast<double>(c.release_rate_ms) * 0.001;
    const auto selected =
        static_cast<std::uint32_t>(std::clamp(total / interval, 1.0, static_cast<double>(c.maximum_trains)));
    //Validate every source coordinate and every resulting sample lookup before
    //committing lifecycle side effects.
    std::vector<RideTrain> prepared = state.trains;
    for (std::uint32_t i = 0; i < selected; ++i) {
        auto& train = prepared[i];
        const auto source = train.cars[train.lead_car + train.car_count - 1].track_position;
        if (!std::isfinite(source))
            return false;
        //47E322..47E33D retains the old coordinate when an edit shortens the
        //track. The rebuild incorrectly rejected it. Approved original-defect
        //repair: wrap before 47E3D7's first speed lookup as well, so spacing
        //cannot read stale samples beyond the rebuilt circuit.
        auto position = std::fmod(static_cast<double>(source), static_cast<double>(last_position));
        if (position < 0)
            position += last_position;
        train.rear_track_position = static_cast<float>(position);
        //Rounding a tiny negative source onto the positive seam can produce
        //exactly last_position in binary32; the seam belongs to sample zero.
        if (train.rear_track_position >= last_position)
            train.rear_track_position = 0;
    }
    for (std::uint32_t i = 1; i < selected; ++i) {
        const auto previous = prepared[i - 1].rear_track_position;
        double integral{};
        const auto fraction = std::modf(static_cast<double>(previous), &integral);
        auto sample = static_cast<std::size_t>(integral);
        if (sample >= speeds.size())
            return false;
        double before = 0, elapsed = (1.0 - fraction) * state.track_sample_spacing / speeds[sample];
        while (elapsed < interval) {
            before = elapsed;
            sample = (sample + 1) % component_count;
            elapsed += state.track_sample_spacing / speeds[sample];
        }
        const auto remaining = (elapsed - interval) / (elapsed - before);
        const auto previous_sample = static_cast<double>(sample) - 1.0;
        const auto coordinate = previous_sample * (1.0 - remaining) + static_cast<double>(sample) * remaining;
        prepared[i].rear_track_position = static_cast<float>(std::fmod(coordinate + last_position, last_position));
    }
    for (std::uint32_t i = 0; i < selected; ++i) {
        auto& train = prepared[i];
        auto position = train.rear_track_position;
        for (std::uint32_t j = train.car_count; j > 0;) {
            --j;
            auto& car = train.cars[train.lead_car + j];
            car.track_position = position;
            if (j > 0) {
                const auto& current = c.car_types[car.type_index];
                const auto& preceding = c.car_types[train.cars[train.lead_car + j - 1].type_index];
                position += (current.front_extent + preceding.rear_extent) / state.track_sample_spacing;
            }
        }
        train.distance = train.cars[train.center_car].track_position;
        update_bounds(state, train);
        double speed_sum = 0;
        for (std::uint32_t j = 0; j < train.car_count; ++j) {
            const double car_position = train.cars[train.lead_car + j].track_position;
            if (!(car_position >= 0) || !std::isfinite(car_position))
                return false;
            //A train can straddle the circuit seam after reseeding. The original
            //unwrapped speed-table lookup can run past its allocation here; use the
            //same periodic coordinate as the motion/pose sampler on the x64 host.
            const double x = std::fmod(car_position, static_cast<double>(last_position));
            double whole{};
            const auto part = std::modf(x, &whole);
            if (whole >= static_cast<double>(speeds.size() - 1))
                return false;
            const auto at = static_cast<std::size_t>(whole);
            speed_sum += (1.0 - part) * speeds[at] + part * speeds[at + 1];
        }
        train.speed = static_cast<float>(speed_sum / train.car_count);
        train.distance = std::fmod(train.distance, last_position);
        update_bounds(state, train);
    }
    if (!set_ride_train_count(state, selected, backend))
        return false;
    state.current_train_index = 0; //47E360: valid before generation/camera consumers.
    for (std::uint32_t i = 0; i < c.maximum_trains; ++i) {
        auto& train = state.trains[i];
        if (i >= selected) {
            if (train.flags & flag(RideTrainFlag::selected)) {
                train.flags &= ~flag(RideTrainFlag::selected);
                if ((state.runtime_flags & 0x800u) && !stop_ride_train_resources(state, train, backend))
                    return false;
            }
        } else if (!(train.flags & flag(RideTrainFlag::selected))) {
            train.flags |= flag(RideTrainFlag::selected) | flag(RideTrainFlag::layout_dirty);
            if ((state.runtime_flags & 0x800u) && !initialize_ride_train_resources(state, train, backend))
                return false;
        }
    }
    for (std::uint32_t i = 0; i < selected; ++i) {
        auto& train = state.trains[i];
        const auto& computed = prepared[i];
        train.rear_track_position = computed.rear_track_position;
        train.distance = computed.distance;
        train.speed = computed.speed;
        train.activation_lower = computed.activation_lower;
        train.activation_upper = computed.activation_upper;
        for (std::uint32_t j = 0; j < train.car_count; ++j)
            train.cars[train.lead_car + j].track_position = computed.cars[train.lead_car + j].track_position;
        train.flags &= ~flag(RideTrainFlag::layout_dirty);
    }
    return true;
}
} //namespace simcoaster
