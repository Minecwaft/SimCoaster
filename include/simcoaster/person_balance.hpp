#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct PeepInfoParameters {
    [[=sam::Field{ "ExitLevel", sam::Kind::nonnegative_integer }]] std::int32_t ExitLevel;
    [[=sam::Field{ "ExitLevelVar", sam::Kind::nonnegative_integer }]] std::int32_t ExitLevelVar;
    [[=sam::Field{ "StartingCashVarPc", 0, 100 }]] std::int32_t StartingCashVarPc;
    [[=sam::Field{ "SmallHappinessChange", sam::Kind::nonnegative_integer }]] std::int32_t SmallHappinessChange;
    [[=sam::Field{ "MediumHappinessChange", sam::Kind::nonnegative_integer }]] std::int32_t MediumHappinessChange;
    [[=sam::Field{ "BigHappinessChange", sam::Kind::nonnegative_integer }]] std::int32_t BigHappinessChange;
    [[=sam::Field{ "RideVomitDivisor", sam::Kind::nonnegative_integer }]] std::int32_t RideVomitDivisor;
    [[=sam::Field{ "ToiletDesparate", sam::Kind::nonnegative_integer }]] std::int32_t ToiletDesparate;
    [[=sam::Field{ "VomitCapacity", sam::Kind::nonnegative_integer }]] std::int32_t VomitCapacity;
    [[=sam::Field{ "ExcitementToCostDivisor", sam::Kind::nonnegative_integer }]] std::int32_t ExcitementToCostDivisor;
    [[=sam::Field{ "MinimumEntryFee", sam::Kind::nonnegative_integer }]] std::int32_t MinimumEntryFee;
    [[=sam::Field{ "CheapPriceMultiplier", sam::Kind::scalar }]] float CheapPriceMultiplier;
    [[=sam::Field{ "AveragePriceMultiplier", sam::Kind::scalar }]] float AveragePriceMultiplier;
    [[=sam::Field{ "ExpensivePriceMultiplier", sam::Kind::scalar }]] float ExpensivePriceMultiplier;
    [[=sam::Field{ "DecisionVariable1", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable1;
    [[=sam::Field{ "DecisionVariable2", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable2;
    [[=sam::Field{ "DecisionVariable3", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable3;
    [[=sam::Field{ "DecisionVariable4", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable4;
    [[=sam::Field{ "DecisionVariable5", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable5;
    [[=sam::Field{ "DecisionVariable6", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable6;
    [[=sam::Field{ "DecisionVariable7", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable7;
    [[=sam::Field{ "DecisionVariable8", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVariable8;
    [[=sam::Field{ "DecisionVarDistWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarDistWeight;
    [[=sam::Field{ "DecisionVarQueueWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarQueueWeight;
    [[=sam::Field{ "DecisionVarExcitementWeight",
                   sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarExcitementWeight;
    [[=sam::Field{ "DecisionVarThirstWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarThirstWeight;
    [[=sam::Field{ "DecisionVarHungerWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarHungerWeight;
    [[=sam::Field{ "DecisionVarToiletWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarToiletWeight;
    [[=sam::Field{ "DecisionVarIllnessWeight", sam::Kind::nonnegative_integer }]] std::int32_t DecisionVarIllnessWeight;
    [[=sam::Field{ "SatisfactionWeightForPhoto",
                   sam::Kind::nonnegative_integer }]] std::int32_t SatisfactionWeightForPhoto;
    [[=sam::Field{ "DistanceWeightForPhoto", sam::Kind::nonnegative_integer }]] std::int32_t DistanceWeightForPhoto;
    [[=sam::Field{ "PhotoThreshold", sam::Kind::nonnegative_integer }]] std::int32_t PhotoThreshold;
    [[=sam::Field{ "OpinionForMuchTooHigh", 0, 101 }]] std::int32_t OpinionForMuchTooHigh;
    [[=sam::Field{ "OpinionForTooHigh", 0, 101 }]] std::int32_t OpinionForTooHigh;
    [[=sam::Field{ "OpinionForTooLow", 0, 101 }]] std::int32_t OpinionForTooLow;
    [[=sam::Field{ "OpinionForAboutRight", 0, 101 }]] std::int32_t OpinionForAboutRight;
    [[=sam::Field{ "PrankeryLikelihood", 0, 101 }]] std::int32_t PrankeryLikelihood;
    [[=sam::Field{ "StinkbombLikelihood", 0, 101 }]] std::int32_t StinkbombLikelihood;
    [[=sam::Field{ "PrankHappinessThreshold", 0, 100 }]] std::int32_t PrankHappinessThreshold;
    [[=sam::Field{ "PrankLikelihood", 0, 100 }]] std::int32_t PrankLikelihood;
    [[=sam::Field{ "WhingeDuration", sam::Kind::integer }]] std::int32_t WhingeDuration;
    [[=sam::Field{ "ViciousRumoursArea", sam::Kind::integer }]] std::int32_t ViciousRumoursArea;
    [[=sam::Field{ "WhingeToStaffArea", sam::Kind::integer }]] std::int32_t WhingeToStaffArea;
    [[=sam::Field{ "PcOfGoodPriceIsCheap", 0, 101 }]] std::int32_t PcOfGoodPriceIsCheap;
    [[=sam::Field{ "HappyDiffForVGood", sam::Kind::integer }]] std::int32_t HappyDiffForVGood;
    [[=sam::Field{ "HappyDiffForGood", sam::Kind::integer }]] std::int32_t HappyDiffForGood;
    [[=sam::Field{ "HappyDiffForPoor", sam::Kind::integer }]] std::int32_t HappyDiffForPoor;
    [[=sam::Field{ "HappyDiffForVPoor", sam::Kind::integer }]] std::int32_t HappyDiffForVPoor;
    [[=sam::Field{ "PerfectExcitementMatch", 0, 101 }]] std::int32_t PerfectExcitementMatch;
    [[=sam::Field{ "GoodExcitementMatch", 0, 101 }]] std::int32_t GoodExcitementMatch;
    [[=sam::Field{ "OKExcitementMatch", 0, 101 }]] std::int32_t OKExcitementMatch;
    [[=sam::Field{ "PerfectRide", sam::Kind::nonnegative_integer }]] std::int32_t PerfectRide;
    [[=sam::Field{ "GoodRide", sam::Kind::nonnegative_integer }]] std::int32_t GoodRide;
    [[=sam::Field{ "OKRide", sam::Kind::nonnegative_integer }]] std::int32_t OKRide;
    [[=sam::Field{ "ComplainAboutThisQuality", sam::Kind::integer }]] std::int32_t ComplainAboutThisQuality;
    [[=sam::Field{ "ComplainAboutThisPortion", sam::Kind::integer }]] std::int32_t ComplainAboutThisPortion;
    [[=sam::Field{ "CheapPrizeFactor", sam::Kind::scalar }]] float CheapPrizeFactor;
    [[=sam::Field{ "PercentageOfKids", sam::Kind::integer }]] std::int32_t PercentageOfKids;
    [[=sam::Field{ "PercentageOfParents", sam::Kind::integer }]] std::int32_t PercentageOfParents;
    [[=sam::Field{ "PercentageOfGrandparents", sam::Kind::integer }]] std::int32_t PercentageOfGrandparents;
    [[=sam::Field{ "UmbrellaModifier", sam::Kind::integer }]] std::int32_t UmbrellaModifier;
    [[=sam::Field{ "WeatherModifierFrequency", sam::Kind::integer }]] std::int32_t WeatherModifierFrequency;
    [[=sam::Field{ "SlowestWeatherChangeResponse", sam::Kind::integer }]] std::int32_t SlowestWeatherChangeResponse;
    [[=sam::Field{ "DiscardBalloonPctModifier", sam::Kind::integer }]] std::int32_t DiscardBalloonPctModifier;
    [[=sam::Field{ "ThoughtBubbleHungry", 0, 101 }]] std::int32_t ThoughtBubbleHungry;
    [[=sam::Field{ "ThoughtBubbleThirsty", 0, 101 }]] std::int32_t ThoughtBubbleThirsty;
    [[=sam::Field{ "ThoughtBubbleNeedTheLoo", 0, 101 }]] std::int32_t ThoughtBubbleNeedTheLoo;
    [[=sam::Field{ "ThoughtBubbleSick", 0, 101 }]] std::int32_t ThoughtBubbleSick;
    [[=sam::Field{ "ThoughtBubbleMess", sam::Kind::scalar }]] float ThoughtBubbleMess;
    [[=sam::Field{ "ThoughtBubbleHappy", 0, 101 }]] std::int32_t ThoughtBubbleHappy;
    [[=sam::Field{ "ThoughtBubbleUnhappy", 0, 101 }]] std::int32_t ThoughtBubbleUnhappy;
};

struct PersonWeatherEffects {
    [[=sam::Field{ "Heatwave", sam::Kind::scalar }]] float heatwave;
    [[=sam::Field{ "Rain", sam::Kind::scalar }]] float rain;
    [[=sam::Field{ "Storm", sam::Kind::scalar }]] float storm;
    [[=sam::Field{ "Snow", sam::Kind::scalar }]] float snow;
};

struct PersonBalance {
    [[=sam::Group{ "PeepInfo" }]] PeepInfoParameters PeepInfo;
    [[=sam::Rows{ "PeepHappinessWeatherEffects" }]] std::array<PersonWeatherEffects, 3> happiness_weather;
    [[=sam::Extent{ "PeepHappinessWeatherEffects" }]] std::uint32_t happiness_extent;
    [[=sam::Rows{ "PeepThirstWeatherEffects" }]] std::array<PersonWeatherEffects, 3> thirst_weather;
    [[=sam::Extent{ "PeepThirstWeatherEffects" }]] std::uint32_t thirst_extent;
    [[=sam::Rows{ "PeepHungerWeatherEffects" }]] std::array<PersonWeatherEffects, 3> hunger_weather;
    [[=sam::Extent{ "PeepHungerWeatherEffects" }]] std::uint32_t hunger_extent;
};

std::expected<void, sam::Error> load_person_balance(PersonBalance&, std::string_view text, bool preserve = false);

struct PersonTypeParameters {
    [[=sam::Field{ "PreferredExcitement", 0, 100 }]] std::int32_t PreferredExcitement;
    [[=sam::Field{ "StartingCash", sam::Kind::nonnegative_integer }]] std::int32_t StartingCash;
    [[=sam::Field{ "BoredomThreshold", 0, 100 }]] std::int32_t BoredomThreshold;
    [[=sam::Field{ "Generation", sam::Kind::integer }]] std::int32_t Generation;
    [[=sam::Field{ "SpeedMultiplier", sam::Kind::scalar }]] float SpeedMultiplier;
};

struct SpriteTestParameters {
    [[=sam::Field{ "OverrideGeneration", sam::Kind::integer }]] std::int32_t OverrideGeneration;
    [[=sam::Field{ "Generation", sam::Kind::integer }]] std::int32_t Generation;
    [[=sam::Field{ "OverrideSpriteID", sam::Kind::integer }]] std::int32_t OverrideSpriteID;
    [[=sam::Field{ "KidSpriteID", sam::Kind::integer }]] std::int32_t KidSpriteID;
    [[=sam::Field{ "ParentSpriteID", sam::Kind::integer }]] std::int32_t ParentSpriteID;
    [[=sam::Field{ "GrandparentSpriteID", sam::Kind::integer }]] std::int32_t GrandparentSpriteID;
};

struct PersonCreationBalance {
    [[=sam::Rows{ "PeepTypes" }]] std::array<PersonTypeParameters, 24> PeepTypes;
    [[=sam::Extent{ "PeepTypes" }]] std::uint32_t PeepTypesCount;
    [[=sam::Group{ "SpriteTest" }]] SpriteTestParameters SpriteTest;
};

std::expected<void, sam::Error>
load_person_creation_balance(PersonCreationBalance&, std::string_view text, bool preserve = false);
} //namespace simcoaster
