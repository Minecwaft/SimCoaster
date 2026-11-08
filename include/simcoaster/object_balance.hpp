#pragma once
#include "sam_record.hpp"

//Recovered from all212 object_definition_balance_descriptors rows in Game.exe.
//Names, ranges and capacities are schema identities. Host storage owns strings
//and grids and uses natural x64 layout; it contains no original pointer slots.
namespace simcoaster::object_balance {
struct Info {
    [[=sam::Field { "Id", sam::Kind::nonnegative_integer }]] std::int32_t Id {};
    [[=sam::Field { "Type", sam::Kind::nonnegative_integer }]] std::int32_t Type {};
    [[=sam::Field { "Name", sam::Kind::string }]] std::optional<std::string> Name {};
    [[=sam::Field { "Shape", sam::GridSymbols::placement }]] std::unique_ptr<DefinitionGrid> Shape {};
    [[=sam::Field { "Hoarding", sam::GridSymbols::hoarding }]] std::unique_ptr<DefinitionGrid> Hoarding {};
    [[=sam::Field { "EngineMapOffsetOverrideX", sam::Kind::nonnegative_integer }]] std::int32_t EngineMapOffsetOverrideX {};
    [[=sam::Field { "EngineMapOffsetOverrideY", sam::Kind::nonnegative_integer }]] std::int32_t EngineMapOffsetOverrideY {};
    [[=sam::Field { "EngineFootprintWidthOverride", sam::Kind::nonnegative_integer }]] std::int32_t EngineFootprintWidthOverride {};
    [[=sam::Field { "EngineFootprintHeightOverride", sam::Kind::nonnegative_integer }]] std::int32_t EngineFootprintHeightOverride {};
    [[=sam::Field { "MapOffsetX", sam::Kind::nonnegative_integer }]] std::int32_t MapOffsetX {};
    [[=sam::Field { "MapOffsetY", sam::Kind::nonnegative_integer }]] std::int32_t MapOffsetY {};
    [[=sam::Field { "DontApplyOffset", 0, 2 }]] std::int32_t DontApplyOffset {};
    [[=sam::Field { "IsChoosable", 0, 2 }]] std::int32_t IsChoosable {};
    [[=sam::Field { "GotoExitMode", 0, 2 }]] std::int32_t GotoExitMode {};
    [[=sam::Field { "IsUsable", 0, 2 }]] std::int32_t IsUsable {};
    [[=sam::Field { "IsObstacle", 0, 2 }]] std::int32_t IsObstacle {};
    [[=sam::Field { "IsSupportingObstacle", 0, 2 }]] std::int32_t IsSupportingObstacle {};
    [[=sam::Field { "IsOutsideFeature", 0, 2 }]] std::int32_t IsOutsideFeature {};
    [[=sam::Field { "GateTo", 0, 4 }]] std::int32_t GateTo {};
    [[=sam::Field { "IsSpecial", 0, 2 }]] std::int32_t IsSpecial {};
    [[=sam::Field { "IsCameraPath", 0, 2 }]] std::int32_t IsCameraPath {};
    [[=sam::Field { "HasQueue", 0, 2 }]] std::int32_t HasQueue {};
    [[=sam::Field { "CanBreakDown", 0, 2 }]] std::int32_t CanBreakDown {};
    [[=sam::Field { "RunsContinuously", 0, 2 }]] std::int32_t RunsContinuously {};
    [[=sam::Field { "WhichUIType", 0, 6 }]] std::int32_t WhichUIType {};
    [[=sam::Field { "DontDeformBase", 0, 2 }]] std::int32_t DontDeformBase {};
    [[=sam::Field { "GenderSpecific", 0, 3 }]] std::int32_t GenderSpecific {};
    [[=sam::Field { "OverwritePriority", sam::Kind::nonnegative_integer }]] std::int32_t OverwritePriority {};
    [[=sam::Field { "AttractionValue", sam::Kind::nonnegative_integer }]] std::int32_t AttractionValue {};
    [[=sam::Field { "NewAttractionDecayTime", 1, 1000 }]] std::int32_t NewAttractionDecayTime {};
    [[=sam::Field { "ExpectedPrice", sam::Kind::nonnegative_integer }]] std::int32_t ExpectedPrice {};
    [[=sam::Field { "WorkingLife", sam::Kind::nonnegative_integer }]] std::int32_t WorkingLife {};
    [[=sam::Field { "CreateParticleEffect", 0, 500 }]] std::int32_t CreateParticleEffect {};
    [[=sam::Field { "DestroyParticleEffect", 0, 500 }]] std::int32_t DestroyParticleEffect {};
    [[=sam::Field { "RepairParticleEffect", 0, 500 }]] std::int32_t RepairParticleEffect {};
    [[=sam::Field { "UpgradeParticleEffect", 0, 500 }]] std::int32_t UpgradeParticleEffect {};
    [[=sam::Field { "DurationUnit", sam::Kind::nonnegative_integer }]] std::int32_t DurationUnit {};
    [[=sam::Field { "UsesCars", sam::Kind::nonnegative_integer }]] std::int32_t UsesCars {};
    [[=sam::Field { "PreviewAnimType", sam::Kind::string }]] std::optional<std::string> PreviewAnimType {};
    [[=sam::Field { "PreviewAnimNum", sam::Kind::nonnegative_integer }]] std::int32_t PreviewAnimNum {};
    [[=sam::Field { "FirstPersonSprites", 0, 2 }]] std::int32_t FirstPersonSprites {};
    [[=sam::Field { "DoHeadProcessing", 0, 2 }]] std::int32_t DoHeadProcessing {};
    [[=sam::Field { "TurnOffAnimatingTextures", 0, 2 }]] std::int32_t TurnOffAnimatingTextures {};
    [[=sam::Field { "TurnOffScrollingTextures", 0, 2 }]] std::int32_t TurnOffScrollingTextures {};
    [[=sam::Field { "RideTypeStringIndex", sam::Kind::nonnegative_integer }]] std::int32_t RideTypeStringIndex {};
    [[=sam::Field { "DownloadedRideNumber", 0, 256 }]] std::int32_t DownloadedRideNumber {};
    [[=sam::Field { "IsPreviewRide", 0, 2 }]] std::int32_t IsPreviewRide {};
    [[=sam::Field { "IsPhotoBooth", 0, 2 }]] std::int32_t IsPhotoBooth {};
    [[=sam::Field { "IsFoodAndDrinkShop", 0, 2 }]] std::int32_t IsFoodAndDrinkShop {};
    [[=sam::Field { "NoOrientationLine", 0, 2 }]] std::int32_t NoOrientationLine {};
};

struct DefaultCoaster {
    [[=sam::Field { "AddDefaultPylons", 0, 2 }]] std::int32_t AddDefaultPylons {};
    [[=sam::Field { "CoasterPlacementShape", sam::GridSymbols::placement }]] std::unique_ptr<DefinitionGrid> CoasterPlacementShape {};
    [[=sam::Field { "OffsetX", sam::Kind::integer }]] std::int32_t OffsetX {};
    [[=sam::Field { "OffsetY", sam::Kind::integer }]] std::int32_t OffsetY {};
    [[=sam::Field { "PlacementOffsetX", sam::Kind::integer }]] std::int32_t PlacementOffsetX {};
    [[=sam::Field { "PlacementOffsetY", sam::Kind::integer }]] std::int32_t PlacementOffsetY {};
};

struct WeatherModifier {
    [[=sam::Field { "SnowPercentage", sam::Kind::integer }]] std::int32_t SnowPercentage {};
    [[=sam::Field { "RainPercentage", sam::Kind::integer }]] std::int32_t RainPercentage {};
    [[=sam::Field { "StormPercentage", sam::Kind::integer }]] std::int32_t StormPercentage {};
    [[=sam::Field { "HeatWavePercentage", sam::Kind::integer }]] std::int32_t HeatWavePercentage {};
    [[=sam::Field { "ClosedDuringHeatWave", 0, 2 }]] std::int32_t ClosedDuringHeatWave {};
    [[=sam::Field { "ClosedDuringRain", 0, 2 }]] std::int32_t ClosedDuringRain {};
    [[=sam::Field { "ClosedDuringStorm", 0, 2 }]] std::int32_t ClosedDuringStorm {};
    [[=sam::Field { "ClosedDuringSnow", 0, 2 }]] std::int32_t ClosedDuringSnow {};
};

struct GenerationModifier {
    [[=sam::Field { "KidScorePercentage", sam::Kind::integer }]] std::int32_t KidScorePercentage {};
    [[=sam::Field { "ParentScorePercentage", sam::Kind::integer }]] std::int32_t ParentScorePercentage {};
    [[=sam::Field { "GrandparentScorePercentage", sam::Kind::integer }]] std::int32_t GrandparentScorePercentage {};
};

struct Obstacle {
    [[=sam::Field { "AfterShape", sam::GridSymbols::placement }]] std::unique_ptr<DefinitionGrid> AfterShape {};
    [[=sam::Field { "ShapeOffsetX", sam::Kind::nonnegative_integer }]] std::int32_t ShapeOffsetX {};
    [[=sam::Field { "ShapeOffsetY", sam::Kind::nonnegative_integer }]] std::int32_t ShapeOffsetY {};
    [[=sam::Field { "EntranceX", sam::Kind::nonnegative_integer }]] std::int32_t EntranceX {};
    [[=sam::Field { "EntranceY", sam::Kind::nonnegative_integer }]] std::int32_t EntranceY {};
    [[=sam::Field { "DeleteAfterUse", 0, 2 }]] std::int32_t DeleteAfterUse {};
    [[=sam::Field { "ReplaceWithID", sam::Kind::nonnegative_integer }]] std::int32_t ReplaceWithID {};
    [[=sam::Field { "OpenedAutomaticly", 0, 2 }]] std::int32_t OpenedAutomaticly {};
};

struct Prebuilt {
    [[=sam::Field { "IsPrebuiltCoaster", 0, 2 }]] std::int32_t IsPrebuiltCoaster {};
    [[=sam::Field { "PrebuiltCoasterBase", sam::Kind::nonnegative_integer }]] std::int32_t PrebuiltCoasterBase {};
    [[=sam::Field { "PlacementOffsetX", sam::Kind::integer }]] std::int32_t PlacementOffsetX {};
    [[=sam::Field { "PlacementOffsetY", sam::Kind::integer }]] std::int32_t PlacementOffsetY {};
};

struct Bumper {
    [[=sam::Field { "WhichTrackType", 0, 4 }]] std::int32_t WhichTrackType {};
    [[=sam::Field { "BumperType", sam::Kind::integer }]] std::int32_t BumperType {};
    [[=sam::Field { "NorthXAdjust", sam::Kind::integer }]] std::int32_t NorthXAdjust {};
    [[=sam::Field { "NorthYAdjust", sam::Kind::integer }]] std::int32_t NorthYAdjust {};
    [[=sam::Field { "EastXAdjust", sam::Kind::integer }]] std::int32_t EastXAdjust {};
    [[=sam::Field { "EastYAdjust", sam::Kind::integer }]] std::int32_t EastYAdjust {};
    [[=sam::Field { "SouthXAdjust", sam::Kind::integer }]] std::int32_t SouthXAdjust {};
    [[=sam::Field { "SouthYAdjust", sam::Kind::integer }]] std::int32_t SouthYAdjust {};
    [[=sam::Field { "WestXAdjust", sam::Kind::integer }]] std::int32_t WestXAdjust {};
    [[=sam::Field { "WestYAdjust", sam::Kind::integer }]] std::int32_t WestYAdjust {};
};

struct UsageInfo {
    [[=sam::Field { "GoldenTicketCost", sam::Kind::nonnegative_integer }]] std::int32_t GoldenTicketCost {};
    [[=sam::Field { "Length", sam::Kind::nonnegative_integer }]] std::int32_t Length {};
    [[=sam::Field { "Width", sam::Kind::nonnegative_integer }]] std::int32_t Width {};
    [[=sam::Field { "Height", sam::Kind::nonnegative_integer }]] std::int32_t Height {};
    [[=sam::Field { "EntryCellStandPosX", sam::Kind::scalar }]] float EntryCellStandPosX {};
    [[=sam::Field { "EntryCellStandPosY", sam::Kind::scalar }]] float EntryCellStandPosY {};
    [[=sam::Field { "ExitCellAppearPosX", sam::Kind::scalar }]] float ExitCellAppearPosX {};
    [[=sam::Field { "ExitCellAppearPosY", sam::Kind::scalar }]] float ExitCellAppearPosY {};
    [[=sam::Field { "InitPricePerUse", sam::Kind::nonnegative_integer }]] std::int32_t InitPricePerUse {};
    [[=sam::Field { "FatigueEffect", sam::Kind::nonnegative_integer }]] std::int32_t FatigueEffect {};
    [[=sam::Field { "InitChanceOfLoosing", 0, 100 }]] std::int32_t InitChanceOfLoosing {};
    [[=sam::Field { "ProvidesSecurity", 0, 2 }]] std::int32_t ProvidesSecurity {};
    [[=sam::Field { "ProvidesRelief", 0, 2 }]] std::int32_t ProvidesRelief {};
    [[=sam::Field { "HoldsLitter", 0, 2 }]] std::int32_t HoldsLitter {};
    [[=sam::Field { "RequiresWatering", 0, 2 }]] std::int32_t RequiresWatering {};
    [[=sam::Field { "ChillsYouOut", 0, 2 }]] std::int32_t ChillsYouOut {};
    [[=sam::Field { "ThinkingSpace", 0, 2 }]] std::int32_t ThinkingSpace {};
    [[=sam::Field { "TrainingRoom", 0, 2 }]] std::int32_t TrainingRoom {};
    [[=sam::Field { "SingleInstance", 0, 2 }]] std::int32_t SingleInstance {};
    [[=sam::Field { "RequiresTeleport", 0, 2 }]] std::int32_t RequiresTeleport {};
    [[=sam::Field { "RideHandlesSprite", 0, 2 }]] std::int32_t RideHandlesSprite {};
    [[=sam::Field { "NonWinnableSideshow", 0, 2 }]] std::int32_t NonWinnableSideshow {};
    [[=sam::Field { "IsFireworks", 0, 2 }]] std::int32_t IsFireworks {};
    [[=sam::Field { "ISIndoors", 0, 2 }]] std::int32_t ISIndoors {};
    [[=sam::Field { "CannotRide", 0, 2 }]] std::int32_t CannotRide {};
    [[=sam::Field { "ConstrainCamera", 0, 2 }]] std::int32_t ConstrainCamera {};
    [[=sam::Field { "PukeReductionLevel", sam::Kind::nonnegative_integer }]] std::int32_t PukeReductionLevel {};
    [[=sam::Field { "MinCapacity", sam::Kind::nonnegative_integer }]] std::int32_t MinCapacity {};
    [[=sam::Field { "MaxCapacity", sam::Kind::nonnegative_integer }]] std::int32_t MaxCapacity {};
    [[=sam::Field { "MinDuration", sam::Kind::nonnegative_integer }]] std::int32_t MinDuration {};
    [[=sam::Field { "MaxDuration", sam::Kind::nonnegative_integer }]] std::int32_t MaxDuration {};
    [[=sam::Field { "MinSpeed", sam::Kind::nonnegative_integer }]] std::int32_t MinSpeed {};
    [[=sam::Field { "MaxSpeed", sam::Kind::nonnegative_integer }]] std::int32_t MaxSpeed {};
    [[=sam::Field { "ExcitementLevel", 0, 101 }]] std::int32_t ExcitementLevel {};
    [[=sam::Field { "InitCostOfGoods", sam::Kind::nonnegative_integer }]] std::int32_t InitCostOfGoods {};
    [[=sam::Field { "ThirstEffect", sam::Kind::integer }]] std::int32_t ThirstEffect {};
    [[=sam::Field { "HungerEffect", sam::Kind::integer }]] std::int32_t HungerEffect {};
    [[=sam::Field { "VomitEffect", sam::Kind::integer }]] std::int32_t VomitEffect {};
    [[=sam::Field { "HappinessEffect", sam::Kind::integer }]] std::int32_t HappinessEffect {};
    [[=sam::Field { "LitterEffect", sam::Kind::integer }]] std::int32_t LitterEffect {};
    [[=sam::Field { "ToiletEffect", sam::Kind::integer }]] std::int32_t ToiletEffect {};
    [[=sam::Field { "SpecialIngredient", 0, 6 }]] std::int32_t SpecialIngredient {};
    [[=sam::Field { "AppearanceEffect", 0, 4 }]] std::int32_t AppearanceEffect {};
    [[=sam::Field { "InitPrizeValue", sam::Kind::nonnegative_integer }]] std::int32_t InitPrizeValue {};
    [[=sam::Field { "ShopType", 0, 9 }]] std::int32_t ShopType {};
    [[=sam::Field { "SaltEffect", sam::Kind::integer }]] std::int32_t SaltEffect {};
    [[=sam::Field { "FatEffect", sam::Kind::integer }]] std::int32_t FatEffect {};
    [[=sam::Field { "IceEffect", sam::Kind::integer }]] std::int32_t IceEffect {};
    [[=sam::Field { "SugarEffect", sam::Kind::integer }]] std::int32_t SugarEffect {};
    [[=sam::Field { "ExciteFactor", 50, 200 }]] std::int32_t ExciteFactor {};
    [[=sam::Field { "RipOffOK", sam::Kind::nonnegative_integer }]] std::int32_t RipOffOK {};
    [[=sam::Field { "NumSimultAnims", sam::Kind::nonnegative_integer }]] std::int32_t NumSimultAnims {};
    [[=sam::Field { "IsInfoDesk", 0, 2 }]] std::int32_t IsInfoDesk {};
    [[=sam::Field { "IsEffectorFeature", sam::Kind::nonnegative_integer }]] std::int32_t IsEffectorFeature {};
};

struct Research {
    [[=sam::Field { "LinkedWith", sam::Kind::nonnegative_integer }]] std::int32_t LinkedWith {};
    [[=sam::Field { "Group", 0, 255 }]] std::int32_t Group {};
    [[=sam::Field { "Category", 0, 7 }]] std::int32_t Category {};
    [[=sam::Field { "Type", 0, 15 }]] std::int32_t Type {};
};

struct RStaff {
    [[=sam::Field { "SType", sam::Kind::nonnegative_integer }]] std::int32_t SType {};
    [[=sam::Field { "SA", sam::Kind::nonnegative_integer }]] std::int32_t SA {};
};

struct Upgrades {
    [[=sam::Field { "CostOfResearch", sam::Kind::nonnegative_integer }]] std::int32_t CostOfResearch {};
    [[=sam::Field { "CashPerBrainPoint", sam::Kind::nonnegative_integer }]] std::int32_t CashPerBrainPoint {};
    [[=sam::Field { "EstimatedPriceVariance", 0, 100 }]] std::int32_t EstimatedPriceVariance {};
    [[=sam::Field { "ScrapValueYear1", sam::Kind::nonnegative_integer }]] std::int32_t ScrapValueYear1 {};
    [[=sam::Field { "ScrapValueYear2", sam::Kind::nonnegative_integer }]] std::int32_t ScrapValueYear2 {};
    [[=sam::Field { "ScrapValueYear3", sam::Kind::nonnegative_integer }]] std::int32_t ScrapValueYear3 {};
    [[=sam::Field { "ScrapValueYear4", sam::Kind::nonnegative_integer }]] std::int32_t ScrapValueYear4 {};
    [[=sam::Field { "InitCapacity", sam::Kind::nonnegative_integer }]] std::int32_t InitCapacity {};
    [[=sam::Field { "RedLineCapacity", sam::Kind::nonnegative_integer }]] std::int32_t RedLineCapacity {};
    [[=sam::Field { "InitDuration", sam::Kind::nonnegative_integer }]] std::int32_t InitDuration {};
    [[=sam::Field { "RedLineDuration", sam::Kind::nonnegative_integer }]] std::int32_t RedLineDuration {};
    [[=sam::Field { "InitSpeed", sam::Kind::nonnegative_integer }]] std::int32_t InitSpeed {};
    [[=sam::Field { "RedLineSpeed", sam::Kind::nonnegative_integer }]] std::int32_t RedLineSpeed {};
    [[=sam::Field { "WearRate", sam::Kind::nonnegative_integer }]] std::int32_t WearRate {};
    [[=sam::Field { "QueueWaitTimeConstant", sam::Kind::scalar }]] float QueueWaitTimeConstant {};
    [[=sam::Field { "CostOfUpgrade", sam::Kind::nonnegative_integer }]] std::int32_t CostOfUpgrade {};
    [[=sam::Field { "DurationOfUpgrade", sam::Kind::nonnegative_integer }]] std::int32_t DurationOfUpgrade {};
    [[=sam::Field { "RunningCost", sam::Kind::integer }]] std::int32_t RunningCost {};
};

struct SupplementalMeshes {
    [[=sam::Field { "FileName", sam::Kind::string }]] std::optional<std::string> FileName {};
};

struct Attraction {
    [[=sam::Field { "NewBonus", sam::Kind::nonnegative_integer }]] std::int32_t NewBonus {};
};

struct SkillSet {
    [[=sam::Field { "Num", sam::Kind::nonnegative_integer }]] std::int32_t Num {};
};

struct SignTextures {
    [[=sam::Field { "FileName", sam::Kind::string }]] std::optional<std::string> FileName {};
};

struct RideBreakDown {
    [[=sam::Field { "Imminent", sam::Kind::scalar }]] float Imminent {};
    [[=sam::Field { "WarnPlayer", sam::Kind::scalar }]] float WarnPlayer {};
};

struct AddOn {
    [[=sam::Field { "UpgradesAnyRide", 0, 2 }]] std::int32_t UpgradesAnyRide {};
    [[=sam::Field { "UpgradesId", sam::Kind::nonnegative_integer }]] std::int32_t UpgradesId {};
    [[=sam::Field { "UpgradeType", sam::Kind::nonnegative_integer }]] std::int32_t UpgradeType {};
    [[=sam::Field { "UpgradeDistance", sam::Kind::nonnegative_integer }]] std::int32_t UpgradeDistance {};
};
} //namespace simcoaster::object_balance

namespace simcoaster {
struct ObjectDefinitionBalance {
    [[=sam::Group { "Info" }]] object_balance::Info Info;
    [[=sam::Group { "DefaultCoaster" }]] object_balance::DefaultCoaster DefaultCoaster;
    [[=sam::Group { "WeatherModifier" }]] object_balance::WeatherModifier WeatherModifier;
    [[=sam::Group { "GenerationModifier" }]] object_balance::GenerationModifier GenerationModifier;
    [[=sam::Group { "Obstacle" }]] object_balance::Obstacle Obstacle;
    [[=sam::Group { "Prebuilt" }]] object_balance::Prebuilt Prebuilt;
    [[=sam::Group { "Bumper" }]] object_balance::Bumper Bumper;
    [[=sam::Group { "UsageInfo" }]] object_balance::UsageInfo UsageInfo;
    [[=sam::Group { "Research" }]] object_balance::Research Research;
    [[=sam::Rows { "RStaff" }]] std::array<object_balance::RStaff, 2> RStaff {};
    [[=sam::Extent { "RStaff" }]] std::uint32_t RStaffCount {};
    [[=sam::Rows { "Upgrades" }]] std::array<object_balance::Upgrades, 3> Upgrades {};
    [[=sam::Extent { "Upgrades" }]] std::uint32_t UpgradesCount {};
    [[=sam::Rows { "SupplementalMeshes" }]] std::array<object_balance::SupplementalMeshes, 8> SupplementalMeshes {};
    [[=sam::Extent { "SupplementalMeshes" }]] std::uint32_t SupplementalMeshesCount {};
    [[=sam::Rows { "Attraction" }]] std::array<object_balance::Attraction, 3> Attraction {};
    [[=sam::Extent { "Attraction" }]] std::uint32_t AttractionCount {};
    [[=sam::Rows { "SkillSet" }]] std::array<object_balance::SkillSet, 14> SkillSet {};
    [[=sam::Extent { "SkillSet" }]] std::uint32_t SkillSetCount {};
    [[=sam::Rows { "SignTextures" }]] std::array<object_balance::SignTextures, 2> SignTextures {};
    [[=sam::Extent { "SignTextures" }]] std::uint32_t SignTexturesCount {};
    [[=sam::Group { "RideBreakDown" }]] object_balance::RideBreakDown RideBreakDown;
    [[=sam::Group { "AddOn" }]] object_balance::AddOn AddOn;
};
} //namespace simcoaster
