#include "simcoaster/dynamic_texture_balance.hpp"
#include "simcoaster/ride_train.hpp"
#include <limits>

namespace simcoaster {
std::optional<CoasterTrainLimits> coaster_train_limits(const DynamicTextureMeshBalance& balance) {
    if (!balance.car_count || balance.train.uiMaxCars < 0 || balance.coaster.uiMaxTrains < 0) {
        return std::nullopt;
    }
    return CoasterTrainLimits { balance.coaster.uiMaxTrains ? static_cast<std::uint32_t>(balance.coaster.uiMaxTrains) : 1u, balance.train.uiMaxCars ? static_cast<std::uint32_t>(balance.train.uiMaxCars) : 3u };
}

std::optional<RideTrainConfiguration> build_ride_train_configuration(const DynamicTextureMeshBalance& balance, const RideCarMeshLoader& load) {
    const auto limits = coaster_train_limits(balance);
    if (!limits || balance.car_count > balance.cars.size() || balance.coaster.uiTrainReleaseRate < 0) {
        return std::nullopt;
    }
    RideTrainConfiguration c;
    c.maximum_trains = limits->trains;
    c.maximum_cars = limits->cars_per_train;
    c.release_rate_ms = balance.coaster.uiTrainReleaseRate ? static_cast<std::uint32_t>(balance.coaster.uiTrainReleaseRate) : 15000u;
    const auto& coaster = balance.coaster;
    if (coaster.fWinchSpeed != 0) {
        c.winch_speed = coaster.fWinchSpeed;
    }
    if (coaster.fMinSpeed != 0) {
        c.minimum_speed = coaster.fMinSpeed;
    }
    if (coaster.fMaxSpeedAtMinSetting != 0) {
        c.maximum_speed_at_minimum_setting = coaster.fMaxSpeedAtMinSetting;
    }
    if (coaster.fMaxSpeedAtMaxSetting != 0) {
        c.maximum_speed_at_maximum_setting = coaster.fMaxSpeedAtMaxSetting;
    }
    if (coaster.fAccelerationPerHeight != 0) {
        c.acceleration_per_height = coaster.fAccelerationPerHeight;
    }
    if (coaster.fUphillAccelModifier != 0) {
        c.uphill_modifier = coaster.fUphillAccelModifier;
    }
    if (coaster.fDownhillAccelModifier != 0) {
        c.downhill_modifier = coaster.fDownhillAccelModifier;
    }
    if (coaster.fFrictionMultiplier != 0) {
        c.friction_multiplier = coaster.fFrictionMultiplier;
    }
    const auto& train = balance.train;
    if (train.uiFrontCarTypeIndex < 0 || train.uiCentreCarTypeIndex < 0 || train.uiRearCarTypeIndex < 0) {
        return std::nullopt;
    }
    c.front_type = static_cast<std::uint32_t>(train.uiFrontCarTypeIndex);
    c.centre_type = static_cast<std::uint32_t>(train.uiCentreCarTypeIndex);
    c.rear_type = static_cast<std::uint32_t>(train.uiRearCarTypeIndex);
    if (c.front_type >= balance.car_count || c.centre_type >= balance.car_count || c.rear_type >= balance.car_count) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < balance.car_count; ++i) {
        const auto& source = balance.cars[i];
        if (source.uiCarTypeUniqueID <= 0 || !source.pcMeshFilename) {
            return std::nullopt;
        }
        RideCarType car;
        car.unique_id = static_cast<std::uint32_t>(source.uiCarTypeUniqueID);
        car.bend = (source.bBendCar & 1) != 0;
        car.vertical_offset = source.fYOffset;
        car.animation_speed = source.fAnimSpeedMultiplier != 0 ? source.fAnimSpeedMultiplier : 0.1f;
        c.car_types.push_back(std::move(car));
    }
    const auto load_type = [&](std::uint32_t index) {
        const auto& source = balance.cars[index];
        auto model = load(c.car_types[index].unique_id, *source.pcMeshFilename);
        if (!model || !model->template_id) {
            return false;
        }
        auto& car = c.car_types[index];
        car.template_id = model->template_id;
        car.front_extent = model->max_z - model->root_z + source.fFrontDistanceAdd;
        car.rear_extent = source.fRearDistanceAdd - (model->min_z - model->root_z);
        car.head_nodes = std::move(model->head_nodes);
        car.primary_node = model->primary_node;
        return true;
    };
    if (!load_type(c.centre_type) || (c.front_type != c.centre_type && !load_type(c.front_type)) || (c.rear_type != c.centre_type && !load_type(c.rear_type))) {
        return std::nullopt;
    }
    const auto& front = c.car_types[c.front_type];
    const auto& centre = c.car_types[c.centre_type];
    const auto& rear = c.car_types[c.rear_type];
    c.front_spacing = front.rear_extent + centre.front_extent;
    c.standard_spacing = centre.front_extent + centre.rear_extent;
    c.end_spacing = rear.front_extent + centre.rear_extent;
    std::uint64_t heads = 0;
    for (std::uint32_t i = 0; i < c.maximum_cars; ++i) {
        heads += c.car_types[ride_train_car_type(c, i)].head_nodes.size();
    }
    if (heads > std::numeric_limits<std::uint32_t>::max() || heads * c.maximum_trains > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    c.head_count_per_train = static_cast<std::uint32_t>(heads);
    c.head_count_all_trains = static_cast<std::uint32_t>(heads * c.maximum_trains);
    return c;
}
} //namespace simcoaster
