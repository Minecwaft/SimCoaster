#include "simcoaster/ride_train.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
void promote_completed_ride_exits(RideTrainState& state) {
    while (!state.exit_overflow.empty() && state.queue_b.physical_space()) {
        //Push can allocate: retain the overflow head until ownership transfers.
        state.queue_b.push(state.exit_overflow.front());
        state.exit_overflow.pop_front();
    }
}

void enqueue_completed_ride_exit(RideTrainState& state, std::uint32_t person) {
    if (!person) {
        return;
    }
    if (!state.exit_overflow.empty() || !state.queue_b.physical_space()) {
        state.exit_overflow.push_back(person);
    } else {
        state.queue_b.push(person);
    }
}

std::uint32_t dequeue_completed_ride_exit(RideTrainState& state) {
    promote_completed_ride_exits(state);
    const auto id = state.queue_b.pop(true);
    //Promotion preserves B's ring cursor/read-stop, including after wrapping.
    //An optional refill must not throw after removing the ID we must return.
    try {
        promote_completed_ride_exits(state);
    } catch (const std::bad_alloc&) {}
    return id;
}

namespace {
constexpr std::uint32_t layout_dirty = 4u, car_active = 1u;

bool count_fits(std::size_t n, std::size_t max) {
    return n <= max;
}
} //namespace

bool update_ride_train_layout(RideTrainState& state, std::uint32_t desired_capacity, std::uint32_t requested_cars, bool distribute) {
    const auto& c = state.configuration;
    if (requested_cars > c.maximum_cars || state.trains.size() != c.maximum_trains) {
        return false;
    }
    for (const auto& train : state.trains) {
        if (train.cars.size() != c.maximum_cars || (requested_cars ? requested_cars : train.car_count) > c.maximum_cars) {
            return false;
        }
        for (const auto& car : train.cars) {
            if (car.type_index >= c.car_types.size()) {
                return false;
            }
        }
    }
    //Check the native division prerequisites before modifying any train.
    if (distribute) {
        for (const auto& train : state.trains) {
            const auto count = requested_cars ? requested_cars : train.car_count;
            std::uint32_t eligible = count;
            if (c.front_type != c.centre_type) {
                --eligible;
            }
            if (c.rear_type != c.centre_type) {
                --eligible;
            }
            std::uint32_t seen = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (train.cars[i].type_index == c.centre_type) {
                    if (seen >= eligible) {
                        return false;
                    }
                    ++seen;
                }
            }
        }
    }
    for (auto& train : state.trains) {
        if ((train.flags & layout_dirty) != 0 || (requested_cars && requested_cars != train.car_count)) {
            if (requested_cars) {
                train.car_count = requested_cars;
            }
            train.lead_car = 0;
            train.center_car = (train.car_count - 1u) >> 1;
            if (train.car_count) {
                auto& center = train.cars[train.center_car];
                center.track_offset = 0;
                center.relative_track_position = 0;
                const auto spacing = [&](std::uint32_t i) {
                    return i == 0 ? c.front_spacing : (i == train.car_count - 1 ? c.end_spacing : c.standard_spacing);
                };
                float distance = 0;
                for (std::uint32_t i = train.center_car; i > 0;) {
                    --i;
                    distance += spacing(i);
                    train.cars[i].track_offset = distance;
                    train.cars[i].relative_track_position = distance / state.track_sample_spacing;
                }
                distance = 0;
                for (std::uint32_t i = train.center_car + 1; i < train.car_count; ++i) {
                    distance -= spacing(i);
                    train.cars[i].track_offset = distance;
                    train.cars[i].relative_track_position = distance / state.track_sample_spacing;
                }
                const auto& first = train.cars.front();
                const auto& last = train.cars[train.car_count - 1];
                train.activation_lower = first.track_offset / state.track_sample_spacing + train.distance + c.car_types[first.type_index].front_extent / state.track_sample_spacing;
                train.activation_upper = last.track_offset / state.track_sample_spacing + train.distance - c.car_types[last.type_index].rear_extent / state.track_sample_spacing;
            }
        }
        if (distribute) {
            if (!desired_capacity) {
                desired_capacity = c.head_count_per_train;
            }
            state.peep_capacity_limit = desired_capacity;
            auto remaining = desired_capacity;
            auto eligible = train.car_count;
            if (c.front_type != c.centre_type) {
                --eligible;
            }
            if (c.rear_type != c.centre_type) {
                --eligible;
            }
            std::uint32_t seen = 0;
            for (std::uint32_t i = 0; i < train.car_count; ++i) {
                auto& car = train.cars[i];
                if (car.type_index == c.centre_type) {
                    car.passenger_capacity = remaining / (eligible - seen);
                    remaining -= car.passenger_capacity;
                    ++seen;
                } else {
                    car.passenger_capacity = 0;
                }
            }
        }
    }
    state.queue_a.entry_limit = state.peep_capacity_limit;
    return true;
}

bool create_ride_train_state(RideTrainConfiguration configuration, float spacing, RideTrainMeshBackend& meshes, RideTrainState& output) {
    RideTrainState state;
    state.configuration = std::move(configuration);
    state.track_sample_spacing = spacing;
    const auto& c = state.configuration;
    if (c.head_count_all_trains > std::numeric_limits<std::uint32_t>::max() / 2u) {
        return false;
    }
    state.queue_a.capacity = c.head_count_per_train;
    state.queue_b.capacity = 2u * c.head_count_all_trains;
    if (c.car_types.empty() || c.front_type >= c.car_types.size() || c.centre_type >= c.car_types.size() || c.rear_type >= c.car_types.size()) {
        return false;
    }
    for (std::uint32_t i = 0; i < c.maximum_trains; ++i) {
        RideTrain train;
        train.selected_car = c.front_type == c.centre_type ? 0u : 1u;
        for (std::uint32_t j = 0; j < c.maximum_cars; ++j) {
            RideTrainCar car;
            car.type_index = ride_train_car_type(c, j);
            car.index = j;
            const auto& type = c.car_types[car.type_index];
            if (type.template_id) {
                const auto slot = meshes.create_car_mesh(type.template_id);
                if (!slot || !*slot) {
                    return false;
                }
                car.mesh_slot = *slot;
                meshes.unregister_car_mesh(car.mesh_slot);
                meshes.activate_car_animation(car.mesh_slot);
            }
            train.cars.push_back(std::move(car));
        }
        state.trains.push_back(std::move(train));
    }
    if (!update_ride_train_layout(state, 0, c.maximum_cars, true)) {
        return false;
    }
    output = std::move(state);
    return true;
}

bool restore_ride_train_state(const CoasterRecordSave& saved, RideTrainConfiguration configuration, float spacing, RideTrainMeshBackend& meshes, RideTrainState& output) {
    if (saved.trains.size() != configuration.maximum_trains || saved.header.train_count > configuration.maximum_trains) {
        return false;
    }
    RideTrainState state;
    state.configuration = std::move(configuration);
    state.track_sample_spacing = spacing;
    state.active_train_count = saved.header.train_count;
    state.peep_capacity_limit = saved.header.peep_capacity_limit;
    const auto& c = state.configuration;
    if (c.head_count_all_trains > std::numeric_limits<std::uint32_t>::max() / 2u) {
        return false;
    }
    state.queue_a.capacity = c.head_count_per_train;
    state.queue_b.capacity = 2u * c.head_count_all_trains;
    if (c.car_types.empty() || c.front_type >= c.car_types.size() || c.centre_type >= c.car_types.size() || c.rear_type >= c.car_types.size()) {
        return false;
    }
    for (const auto& saved_train : saved.trains) {
        if (saved_train.cars.size() != c.maximum_cars) {
            return false;
        }
        RideTrain train;
        train.flags = saved_train.header.flags | layout_dirty;
        train.car_count = saved_train.header.car_count;
        train.passenger_count = saved_train.header.passenger_count;
        train.distance = saved_train.header.distance;
        train.speed = saved_train.header.speed;
        train.transition_state = saved_train.header.transition_state;
        train.positioned_request_a = saved_train.header.positioned_request_a;
        train.positioned_request_b = saved_train.header.positioned_request_b;
        train.selected_car = c.front_type == c.centre_type ? 0u : 1u;
        for (std::uint32_t i = 0; i < c.maximum_cars; ++i) {
            const auto& saved_car = saved_train.cars[i];
            RideTrainCar car;
            car.type_index = ride_train_car_type(c, i);
            car.index = i;
            const auto& type = c.car_types[car.type_index];
            if (saved_car.persons_a.size() != saved_car.header.person_count_a || saved_car.persons_b.size() != saved_car.header.person_count_b || saved_car.persons_a.size() > type.head_nodes.size() || saved_car.persons_b.size() > type.head_nodes.size()) {
                return false;
            }
            car.flags = saved_car.header.flags;
            car.mesh_slot = saved_car.header.mesh_slot;
            car.passenger_capacity = saved_car.header.passenger_capacity;
            car.persons_a = saved_car.persons_a;
            car.persons_b = saved_car.persons_b;
            train.cars.push_back(std::move(car));
        }
        state.trains.push_back(std::move(train));
    }
    for (const auto& train : state.trains) {
        for (const auto& car : train.cars) {
            const auto& type = c.car_types[car.type_index];
            if (!type.template_id) {
                continue;
            }
            if (!meshes.refresh_car_mesh(car.mesh_slot, type.template_id)) {
                return false;
            }
            if (!(car.flags & car_active)) {
                meshes.unregister_car_mesh(car.mesh_slot);
            }
            meshes.activate_car_animation(car.mesh_slot);
        }
    }
    if (!update_ride_train_layout(state, 0, c.maximum_cars, false)) {
        return false;
    }
    output = std::move(state);
    return true;
}

bool set_ride_train_peep_capacity(RideTrainState& state, std::int32_t desired) {
    const auto& c = state.configuration;
    auto clamped = desired < 0 ? 0u : static_cast<std::uint32_t>(desired);
    clamped = std::min({ clamped, c.head_count_all_trains, c.head_count_per_train });
    return clamped == state.peep_capacity_limit || update_ride_train_layout(state, clamped, 0, true);
}

bool capture_ride_train_state(const RideTrainState& state, CoasterRecordSave& saved) {
    if (state.trains.size() != state.configuration.maximum_trains || !count_fits(state.active_train_count, 65535) || !count_fits(state.peep_capacity_limit, 65535)) {
        return false;
    }
    std::vector<CoasterTrainSave> trains;
    for (const auto& train : state.trains) {
        if (train.cars.size() != state.configuration.maximum_cars || !count_fits(train.car_count, 255) || !count_fits(train.passenger_count, 65535)) {
            return false;
        }
        CoasterTrainSave row;
        row.header = { train.flags, static_cast<std::uint8_t>(train.car_count), train.distance, train.speed, train.transition_state, train.positioned_request_a, train.positioned_request_b, static_cast<std::uint16_t>(train.passenger_count) };
        for (const auto& car : train.cars) {
            if (!count_fits(car.persons_a.size(), 255) || !count_fits(car.persons_b.size(), 255) || !count_fits(car.passenger_capacity, 255)) {
                return false;
            }
            CoasterCarSave out;
            out.header = { car.flags, static_cast<std::uint8_t>(car.persons_a.size()), static_cast<std::uint8_t>(car.persons_b.size()), static_cast<std::uint8_t>(car.passenger_capacity), car.mesh_slot };
            out.persons_a = car.persons_a;
            out.persons_b = car.persons_b;
            row.cars.push_back(std::move(out));
        }
        trains.push_back(std::move(row));
    }
    saved.header.train_count = static_cast<std::uint16_t>(state.active_train_count);
    saved.header.peep_capacity_limit = static_cast<std::uint16_t>(state.peep_capacity_limit);
    saved.trains = std::move(trains);
    return true;
}
} //namespace simcoaster
