#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct LitterBinCreationParameters {
    [[=sam::Field{ "Capacity", sam::Kind::integer }]] std::int32_t Capacity{};
    [[=sam::Field{ "Dirty", sam::Kind::integer }]] std::int32_t Dirty{};
    [[=sam::Field{ "FireRisk", sam::Kind::integer }]] std::int32_t FireRisk{};
};

struct VegetationCreationParameters {
    [[=sam::Field{ "TimeBetweenWaterings", sam::Kind::integer }]] std::int32_t TimeBetweenWaterings{};
    [[=sam::Field{ "TestAnimation", sam::Kind::integer }]] std::int32_t TestAnimation{};
    [[=sam::Field{ "HeatwaveWither", sam::Kind::scalar }]] float HeatwaveWither{};
    [[=sam::Field{ "RainWither", sam::Kind::scalar }]] float RainWither{};
    [[=sam::Field{ "StormWither", sam::Kind::scalar }]] float StormWither{};
    [[=sam::Field{ "SnowWither", sam::Kind::scalar }]] float SnowWither{};
};

struct ParkObjectCreationBalance {
    [[=sam::Group{ "LitterBin" }]] LitterBinCreationParameters LitterBin;
    [[=sam::Group{ "Vegetation" }]] VegetationCreationParameters Vegetation;
};

std::expected<void, sam::Error>
load_park_object_creation_balance(ParkObjectCreationBalance&, std::string_view, bool preserve = false);
} //namespace simcoaster
