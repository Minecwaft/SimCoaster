#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct ParkAwardBalance {
    [[=sam::Field { "CoasterHeight", sam::Kind::integer }]] std::int32_t CoasterHeight {};
    [[=sam::Field { "CoasterSpeed", sam::Kind::integer }]] std::int32_t CoasterSpeed {};
    [[=sam::Field { "Hygiene", sam::Kind::integer }]] std::int32_t Hygiene {};
    [[=sam::Field { "HygieneMinParkSize", sam::Kind::integer }]] std::int32_t HygieneMinParkSize {};
    [[=sam::Field { "Decoration", sam::Kind::integer }]] std::int32_t Decoration {};
    [[=sam::Field { "Security", sam::Kind::integer }]] std::int32_t Security {};
    [[=sam::Field { "Training", sam::Kind::integer }]] std::int32_t Training {};
    [[=sam::Field { "TrainingMinStaff", sam::Kind::integer }]] std::int32_t TrainingMinStaff {};
    [[=sam::Field { "GokartSpeed", sam::Kind::integer }]] std::int32_t GokartSpeed {};
    [[=sam::Field { "WaterLength", sam::Kind::integer }]] std::int32_t WaterLength {};
    //Parsed legacy policy fields; the award evaluator does not read these.
    [[=sam::Field { "MinCellsOwned", sam::Kind::integer }]] std::int32_t MinCellsOwned {};
    [[=sam::Field { "MinCellsCovered", sam::Kind::integer }]] std::int32_t MinCellsCovered {};
};
} //namespace simcoaster
