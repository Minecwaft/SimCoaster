#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct FixedItemParameters {
    [[=sam::Field { "NumTicketBooths", sam::Kind::nonnegative_integer }]] std::int32_t NumTicketBooths;
    [[=sam::Field { "BusStopAPosX", sam::Kind::nonnegative_integer }]] std::int32_t BusStopAPosX;
    [[=sam::Field { "BusStopAPosY", sam::Kind::nonnegative_integer }]] std::int32_t BusStopAPosY;
    [[=sam::Field { "BusStopBPosX", sam::Kind::nonnegative_integer }]] std::int32_t BusStopBPosX;
    [[=sam::Field { "BusStopBPosY", sam::Kind::nonnegative_integer }]] std::int32_t BusStopBPosY;
    [[=sam::Field { "BusStopCPosX", sam::Kind::nonnegative_integer }]] std::int32_t BusStopCPosX;
    [[=sam::Field { "BusStopCPosY", sam::Kind::nonnegative_integer }]] std::int32_t BusStopCPosY;
    [[=sam::Field { "EntranceAPosX", sam::Kind::nonnegative_integer }]] std::int32_t EntranceAPosX;
    [[=sam::Field { "EntranceAPosY", sam::Kind::nonnegative_integer }]] std::int32_t EntranceAPosY;
    [[=sam::Field { "EntranceBPosX", sam::Kind::nonnegative_integer }]] std::int32_t EntranceBPosX;
    [[=sam::Field { "EntranceBPosY", sam::Kind::nonnegative_integer }]] std::int32_t EntranceBPosY;
    [[=sam::Field { "CrossingParkSideAPosX", sam::Kind::nonnegative_integer }]] std::int32_t CrossingParkSideAPosX;
    [[=sam::Field { "CrossingParkSideAPosY", sam::Kind::nonnegative_integer }]] std::int32_t CrossingParkSideAPosY;
    [[=sam::Field { "CrossingParkSideBPosX", sam::Kind::nonnegative_integer }]] std::int32_t CrossingParkSideBPosX;
    [[=sam::Field { "CrossingParkSideBPosY", sam::Kind::nonnegative_integer }]] std::int32_t CrossingParkSideBPosY;
    [[=sam::Field { "CrossingBSSideAPosX", sam::Kind::nonnegative_integer }]] std::int32_t CrossingBSSideAPosX;
    [[=sam::Field { "CrossingBSSideAPosY", sam::Kind::nonnegative_integer }]] std::int32_t CrossingBSSideAPosY;
    [[=sam::Field { "CrossingBSSideBPosX", sam::Kind::nonnegative_integer }]] std::int32_t CrossingBSSideBPosX;
    [[=sam::Field { "CrossingBSSideBPosY", sam::Kind::nonnegative_integer }]] std::int32_t CrossingBSSideBPosY;
    [[=sam::Field { "StrikeAreaStartX", sam::Kind::nonnegative_integer }]] std::int32_t StrikeAreaStartX;
    [[=sam::Field { "StrikeAreaStartY", sam::Kind::nonnegative_integer }]] std::int32_t StrikeAreaStartY;
    [[=sam::Field { "StrikeAreaSizeX", sam::Kind::nonnegative_integer }]] std::int32_t StrikeAreaSizeX;
    [[=sam::Field { "StrikeAreaSizeY", sam::Kind::nonnegative_integer }]] std::int32_t StrikeAreaSizeY;
};

struct TicketBoothParameters {
    [[=sam::Field { "PosX", sam::Kind::nonnegative_integer }]] std::int32_t PosX;
    [[=sam::Field { "PosY", sam::Kind::nonnegative_integer }]] std::int32_t PosY;
    [[=sam::Field { "Angle", sam::Kind::nonnegative_integer }]] std::int32_t Angle;
};

struct GenericUmbrellaParameters {
    [[=sam::Field { "ScalingFactor", sam::Kind::scalar }]] float ScalingFactor;
    [[=sam::Field { "XAdjustCamera", sam::Kind::scalar }]] float XAdjustCamera;
    [[=sam::Field { "YAdjustCamera", sam::Kind::scalar }]] float YAdjustCamera;
    [[=sam::Field { "ZAdjustCamera", sam::Kind::scalar }]] float ZAdjustCamera;
};

struct PersonUmbrellaParameters {
    [[=sam::Field { "X", sam::Kind::scalar }]] float X;
    [[=sam::Field { "Y", sam::Kind::scalar }]] float Y;
    [[=sam::Field { "Z", sam::Kind::scalar }]] float Z;
    [[=sam::Field { "Projection", sam::Kind::scalar }]] float Projection;
    [[=sam::Field { "HandPosDistance", sam::Kind::scalar }]] float HandPosDistance;
    [[=sam::Field { "HandPosAngle", sam::Kind::scalar }]] float HandPosAngle;
    [[=sam::Field { "FPX", sam::Kind::scalar }]] float FPX;
    [[=sam::Field { "FPY", sam::Kind::scalar }]] float FPY;
    [[=sam::Field { "FPZ", sam::Kind::scalar }]] float FPZ;
    [[=sam::Field { "FPProjection", sam::Kind::scalar }]] float FPProjection;
    [[=sam::Field { "FPCentreOffsetX", sam::Kind::scalar }]] float FPCentreOffsetX;
    [[=sam::Field { "FPCentreOffsetZ", sam::Kind::scalar }]] float FPCentreOffsetZ;
    [[=sam::Field { "FPHandPosDistance", sam::Kind::scalar }]] float FPHandPosDistance;
    [[=sam::Field { "FPHandPosAngle", sam::Kind::scalar }]] float FPHandPosAngle;
};

struct PersonMapParameters {
    [[=sam::Field { "HeightfieldXStart", sam::Kind::integer }]] std::int32_t HeightfieldXStart;
    [[=sam::Field { "HeightfieldYStart", sam::Kind::integer }]] std::int32_t HeightfieldYStart;
    [[=sam::Field { "HeightfieldWidth", sam::Kind::integer }]] std::int32_t HeightfieldWidth;
    [[=sam::Field { "HeightfieldHeight", sam::Kind::integer }]] std::int32_t HeightfieldHeight;
    [[=sam::Field { "FixedItemOriginX", sam::Kind::integer }]] std::int32_t FixedItemOriginX;
    [[=sam::Field { "FixedItemOriginY", sam::Kind::integer }]] std::int32_t FixedItemOriginY;
};

struct ArrivalParameters {
    [[=sam::Field { "MinPeople", sam::Kind::nonnegative_integer }]] std::int32_t MinPeople;
    [[=sam::Field { "TimeBetweenArrivals", sam::Kind::nonnegative_integer }]] std::int32_t TimeBetweenArrivals;
    [[=sam::Field { "FixedRate", sam::Kind::integer }]] std::int32_t FixedRate;
    [[=sam::Field { "NewParkBonus", sam::Kind::integer }]] std::int32_t NewParkBonus;
    [[=sam::Field { "PointsPerVisitor", sam::Kind::nonnegative_integer }]] std::int32_t PointsPerVisitor;
    [[=sam::Field { "RainPercentageModifier", sam::Kind::integer }]] std::int32_t RainPercentageModifier;
    [[=sam::Field { "StormPercentageModifier", sam::Kind::integer }]] std::int32_t StormPercentageModifier;
    [[=sam::Field { "SnowPercentageModifier", sam::Kind::integer }]] std::int32_t SnowPercentageModifier;
    [[=sam::Field { "HeatwavePercentageModifier", sam::Kind::integer }]] std::int32_t HeatwavePercentageModifier;
};

struct ArrivalBalanceFile {
    [[=sam::Group { "Arrival" }]] ArrivalParameters Arrival;
};

struct PersonEnvironmentBalance {
    [[=sam::Group { "FixedItemInfo" }]] FixedItemParameters FixedItemInfo;
    [[=sam::Rows { "TicketBooth" }]] std::array<TicketBoothParameters, 3> TicketBooth;
    [[=sam::Extent { "TicketBooth" }]] std::uint32_t TicketBoothCount;
    [[=sam::Group { "GenericUmbrellaModifiers" }]] GenericUmbrellaParameters GenericUmbrellaModifiers;
    [[=sam::Rows { "PersonSpriteUmbrellaModifiers" }]] std::array<PersonUmbrellaParameters, 12> PersonSpriteUmbrellaModifiers;
    [[=sam::Extent { "PersonSpriteUmbrellaModifiers" }]] std::uint32_t PersonSpriteUmbrellaModifiersCount;
    [[=sam::Group { "MapInfo" }]] PersonMapParameters MapInfo;
};

std::expected<void, sam::Error> load_person_environment_balance(PersonEnvironmentBalance&, std::string_view, bool preserve = false);
std::expected<void, sam::Error> load_arrival_balance(ArrivalParameters&, std::string_view, bool preserve = false);

struct MobileRenderPose {
    WorldVector3 position;
    std::uint32_t angle;
};

struct MobileRenderState {
    WorldVector3 camera_position {};
    float camera_zoom = 40;
    std::uint32_t balloon_phase {};
    float balloon_phase_fraction {};
};
} //namespace simcoaster
