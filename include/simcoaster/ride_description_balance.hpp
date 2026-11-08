#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct RideDescriptionInfo {
    [[=sam::Field { "PositionOverideX", sam::Kind::integer }]] std::int32_t PositionOverideX {};
    [[=sam::Field { "PositionOverideY", sam::Kind::integer }]] std::int32_t PositionOverideY {};
    [[=sam::Field { "StartX", sam::Kind::integer }]] std::int32_t StartX {};
    [[=sam::Field { "StartY", sam::Kind::integer }]] std::int32_t StartY {};
    [[=sam::Field { "StartRad", sam::Kind::integer }]] std::int32_t StartRad {};
    [[=sam::Field { "TotalRad", sam::Kind::integer }]] std::int32_t TotalRad {};
};

struct RideDescriptionCollisionObject {
    [[=sam::Field { "X", sam::Kind::integer }]] std::int32_t X {};
    [[=sam::Field { "Y", sam::Kind::integer }]] std::int32_t Y {};
    [[=sam::Field { "Rad", sam::Kind::integer }]] std::int32_t Rad {};
};

struct RideDescriptionBuoy {
    [[=sam::Field { "X", sam::Kind::integer }]] std::int32_t X {};
    [[=sam::Field { "Y", sam::Kind::integer }]] std::int32_t Y {};
};

//The executable keeps one shared receiver and overlays each qualifying
//Bumper.sam without resetting fields. Entries end at an X value of 512.
struct RideDescriptionBalance {
    [[=sam::Group { "Info" }]] RideDescriptionInfo Info;
    [[=sam::Rows { "ColObjects" }]] std::array<RideDescriptionCollisionObject, 64> ColObjects {};
    [[=sam::Extent { "ColObjects" }]] std::uint32_t collision_count {};
    [[=sam::Rows { "Buoys" }]] std::array<RideDescriptionBuoy, 64> Buoys {};
    [[=sam::Extent { "Buoys" }]] std::uint32_t buoy_count {};
};
} //namespace simcoaster
