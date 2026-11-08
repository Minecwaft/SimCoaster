#pragma once
#include "advisor_policy.hpp"

namespace simcoaster {
//Recovered original SAM group/field names, kinds, bounds and
//row capacities. Layout follows native x64; no x86 offsets.
//Regenerate with tools/recover_advisor_catalog.py.
struct AdvisorGroupTimesParameters {
    [[=sam::Field{ "MinTimeForMessage", sam::Kind::nonnegative_integer }]] std::int32_t MinTimeForMessage{};
    [[=sam::Field{ "MinTimeUntilFirstMessage",
                   sam::Kind::nonnegative_integer }]] std::int32_t MinTimeUntilFirstMessage{};
};

struct AdvisorPricingParameters {
    [[=sam::Field{ "TooExpensive", sam::Kind::scalar }]] float TooExpensive{};
    [[=sam::Field{ "TooCheap", sam::Kind::scalar }]] float TooCheap{};
    [[=sam::Field{ "TooExpensiveScore", sam::Kind::integer }]] std::int32_t TooExpensiveScore{};
    [[=sam::Field{ "TooCheapScore", sam::Kind::integer }]] std::int32_t TooCheapScore{};
    [[=sam::Field{ "MonthsTilFirst", sam::Kind::integer }]] std::int32_t MonthsTilFirst{};
    [[=sam::Field{ "PercentageChanceToHappen", sam::Kind::integer }]] std::int32_t PercentageChanceToHappen{};
};

struct AdvisorBreakdownParameters {
    [[=sam::Field{ "MonthsTilFirst", sam::Kind::integer }]] std::int32_t MonthsTilFirst{};
    [[=sam::Field{ "PercentageChanceToHappen", sam::Kind::integer }]] std::int32_t PercentageChanceToHappen{};
};

struct AdvisorLotsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorWelcomeParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorOpenParkParameters {
    [[=sam::Field{ "ScorePerWaitingPerson", sam::Kind::integer }]] std::int32_t ScorePerWaitingPerson{};
};

struct AdvisorCloseParkParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorVisitorsThirstyParameters {
    [[=sam::Field{ "ScorePerThirstyPerson", sam::Kind::integer }]] std::int32_t ScorePerThirstyPerson{};
    [[=sam::Field{ "ThirstierThan", 0, 100 }]] std::int32_t ThirstierThan{};
};

struct AdvisorVisitorsHungryParameters {
    [[=sam::Field{ "ScorePerHungryPerson", sam::Kind::integer }]] std::int32_t ScorePerHungryPerson{};
    [[=sam::Field{ "HungrierThan", 0, 100 }]] std::int32_t HungrierThan{};
};

struct AdvisorStaffHireMechanics1Parameters {
    [[=sam::Field{ "ScorePerWornRide", sam::Kind::integer }]] std::int32_t ScorePerWornRide{};
    [[=sam::Field{ "PoorerStateThan", 0, 100 }]] std::int32_t PoorerStateThan{};
};

struct AdvisorStaffHireMechanics2Parameters {
    [[=sam::Field{ "ScorePerBrokenRide", sam::Kind::integer }]] std::int32_t ScorePerBrokenRide{};
};

struct AdvisorStaffHireMechanics3Parameters {
    [[=sam::Field{ "IdealNumberOfRidesPerMechanic", sam::Kind::integer }]] std::int32_t IdealNumberOfRidesPerMechanic{};
    [[=sam::Field{ "ScorePerExtraRide", sam::Kind::integer }]] std::int32_t ScorePerExtraRide{};
};

struct AdvisorStaffHireHandymen1Parameters {
    [[=sam::Field{ "ScorePerLitterPoint", sam::Kind::integer }]] std::int32_t ScorePerLitterPoint{};
    [[=sam::Field{ "LitterPointCutoffLow", 0, 100 }]] std::int32_t LitterPointCutoffLow{};
    [[=sam::Field{ "LitterPointCutoffHigh", 0, 100 }]] std::int32_t LitterPointCutoffHigh{};
};

struct AdvisorStaffHireHandymen2Parameters {
    [[=sam::Field{ "ScorePerLitterPoint", sam::Kind::integer }]] std::int32_t ScorePerLitterPoint{};
    [[=sam::Field{ "LitterPointCutoffLow", 0, 100 }]] std::int32_t LitterPointCutoffLow{};
};

struct AdvisorStaffHireHandymen3Parameters {
    [[=sam::Field{ "IdealDevelopedCellsPerHandyman",
                   sam::Kind::integer }]] std::int32_t IdealDevelopedCellsPerHandyman{};
    [[=sam::Field{ "ScorePerExtra10Cells", sam::Kind::integer }]] std::int32_t ScorePerExtra10Cells{};
    [[=sam::Field{ "MinCellsForConsideration", sam::Kind::integer }]] std::int32_t MinCellsForConsideration{};
};

struct AdvisorStaffHireGuards1Parameters {
    [[=sam::Field{ "AtLeastThisBusy", sam::Kind::integer }]] std::int32_t AtLeastThisBusy{};
    [[=sam::Field{ "ScorePerExtraBusyPoint", sam::Kind::integer }]] std::int32_t ScorePerExtraBusyPoint{};
};

struct AdvisorStaffHireGuards2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AdditionalPerMisbehavingKid", sam::Kind::integer }]] std::int32_t AdditionalPerMisbehavingKid{};
};

struct AdvisorStaffHireGuards3Parameters {
    [[=sam::Field{ "IdealDevelopedCellsPerGuard", sam::Kind::integer }]] std::int32_t IdealDevelopedCellsPerGuard{};
    [[=sam::Field{ "ScorePerExtra10Cells", sam::Kind::integer }]] std::int32_t ScorePerExtra10Cells{};
    [[=sam::Field{ "MinCellsForConsideration", sam::Kind::integer }]] std::int32_t MinCellsForConsideration{};
};

struct AdvisorStaffHireEntertainers1Parameters {
    [[=sam::Field{ "AtLeastThisBusy", sam::Kind::integer }]] std::int32_t AtLeastThisBusy{};
    [[=sam::Field{ "ScorePerExtraBusyPoint", sam::Kind::integer }]] std::int32_t ScorePerExtraBusyPoint{};
};

struct AdvisorStaffHireResearchers1Parameters {
    [[=sam::Field{ "Most", 0, 100 }]] std::int32_t Most{};
    [[=sam::Field{ "PointsPerExtraPct", sam::Kind::integer }]] std::int32_t PointsPerExtraPct{};
};

struct AdvisorStaffHireResearchers2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHireResearchers3Parameters {
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::nonnegative_integer }]] std::int32_t MonthsIntoGameForConsideration{};
    [[=sam::Field{ "SlowerThan", 0, 100 }]] std::int32_t SlowerThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffSetMechanicAreaParameters {
    [[=sam::Field{ "AtLeastThisMany", sam::Kind::integer }]] std::int32_t AtLeastThisMany{};
    [[=sam::Field{ "DontBotherIfPatrolledUp", 0, 100 }]] std::int32_t DontBotherIfPatrolledUp{};
    [[=sam::Field{ "ScorePerUnpatrolledPct", sam::Kind::integer }]] std::int32_t ScorePerUnpatrolledPct{};
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::integer }]] std::int32_t MonthsIntoGameForConsideration{};
};

struct AdvisorStaffMechanicCoverageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "ScorePerNonCoveredPercent", sam::Kind::integer }]] std::int32_t ScorePerNonCoveredPercent{};
};

struct AdvisorStaffSetHandymanAreaParameters {
    [[=sam::Field{ "AtLeastThisMany", sam::Kind::integer }]] std::int32_t AtLeastThisMany{};
    [[=sam::Field{ "DontBotherIfPatrolledUp", 0, 100 }]] std::int32_t DontBotherIfPatrolledUp{};
    [[=sam::Field{ "ScorePerUnpatrolledPct", sam::Kind::integer }]] std::int32_t ScorePerUnpatrolledPct{};
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::integer }]] std::int32_t MonthsIntoGameForConsideration{};
};

struct AdvisorStaffHandymanCoverageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "ScorePerNonCoveredPercent", sam::Kind::integer }]] std::int32_t ScorePerNonCoveredPercent{};
};

struct AdvisorStaffGuardBuildCameraParameters {
    [[=sam::Field{ "AcceptableTrouble", sam::Kind::nonnegative_integer }]] std::int32_t AcceptableTrouble{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffSetGuardAreaParameters {
    [[=sam::Field{ "AtLeastThisMany", sam::Kind::integer }]] std::int32_t AtLeastThisMany{};
    [[=sam::Field{ "DontBotherIfPatrolledUp", 0, 100 }]] std::int32_t DontBotherIfPatrolledUp{};
    [[=sam::Field{ "ScorePerUnpatrolledPct", sam::Kind::integer }]] std::int32_t ScorePerUnpatrolledPct{};
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::integer }]] std::int32_t MonthsIntoGameForConsideration{};
};

struct AdvisorStaffGuardCoverageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "ScorePerNonCoveredPercent", sam::Kind::integer }]] std::int32_t ScorePerNonCoveredPercent{};
};

struct AdvisorStaffSetEntertainerAreaParameters {
    [[=sam::Field{ "AtLeastThisMany", sam::Kind::integer }]] std::int32_t AtLeastThisMany{};
    [[=sam::Field{ "DontBotherIfPatrolledUp", 0, 100 }]] std::int32_t DontBotherIfPatrolledUp{};
    [[=sam::Field{ "ScorePerUnpatrolledPct", sam::Kind::integer }]] std::int32_t ScorePerUnpatrolledPct{};
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::integer }]] std::int32_t MonthsIntoGameForConsideration{};
};

struct AdvisorStaffEntertainerCoverageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "ScorePerNonCoveredPercent", sam::Kind::integer }]] std::int32_t ScorePerNonCoveredPercent{};
};

struct AdvisorStaffTrainMechanicsParameters {
    [[=sam::Field{ "WorseThan", 0, 4 }]] std::int32_t WorseThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffTrainHandymenParameters {
    [[=sam::Field{ "WorseThan", 0, 4 }]] std::int32_t WorseThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffTrainGuardsParameters {
    [[=sam::Field{ "WorseThan", 0, 4 }]] std::int32_t WorseThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffTrainEntertainersParameters {
    [[=sam::Field{ "WorseThan", 0, 4 }]] std::int32_t WorseThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffTrainResearchersParameters {
    [[=sam::Field{ "WorseThan", 0, 4 }]] std::int32_t WorseThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyHandymenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyGuardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyEntertainersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffUnhappyGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyHandymenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyGuardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyEntertainersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffVeryUnhappyGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingHandymenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingGuardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingEntertainersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffStrikingGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierHandymenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierGuardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierEntertainersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffHappierGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadHandymenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadGuardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadEntertainersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStrikeEndBadGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesPoorSelectionParameters {
    [[=sam::Field{ "PoorerThan", 0, 100 }]] std::int32_t PoorerThan{};
    [[=sam::Field{ "MoreRidesThan", sam::Kind::integer }]] std::int32_t MoreRidesThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesResearchMoreParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesBuildRidesParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AdditionalPerDay", sam::Kind::integer }]] std::int32_t AdditionalPerDay{};
};

struct AdvisorRidesNoUpgradesParameters {
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::nonnegative_integer }]] std::int32_t MonthsIntoGameForConsideration{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesMoreUpgradesParameters {
    [[=sam::Field{ "LessAvailableThan", sam::Kind::integer }]] std::int32_t LessAvailableThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesBreakdownImminentParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesBreakdownNoMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesBreakdownBusyMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesBreakdownMechanicOnItParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorShopsPoorSelectionParameters {
    [[=sam::Field{ "PoorerThan", 0, 100 }]] std::int32_t PoorerThan{};
    [[=sam::Field{ "MoreShopsThan", sam::Kind::integer }]] std::int32_t MoreShopsThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorShopsResearchMoreParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorShopsBuildShopsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AdditionalPerDay", sam::Kind::integer }]] std::int32_t AdditionalPerDay{};
};

struct AdvisorShopsBuildMoreShopsParameters {
    [[=sam::Field{ "ShopsPerRideRatio", sam::Kind::scalar }]] float ShopsPerRideRatio{};
    [[=sam::Field{ "WealthMoreThan", sam::Kind::integer }]] std::int32_t WealthMoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorSideshowsPoorSelectionParameters {
    [[=sam::Field{ "PoorerThan", 0, 100 }]] std::int32_t PoorerThan{};
    [[=sam::Field{ "MoreSideshowsThan", sam::Kind::integer }]] std::int32_t MoreSideshowsThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorSideshowsResearchMoreParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorSideshowsBuildSideshowsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AdditionalPerDay", sam::Kind::integer }]] std::int32_t AdditionalPerDay{};
};

struct AdvisorSideshowsBuildMoreSideshowsParameters {
    [[=sam::Field{ "SideshowsPerRideRatio", sam::Kind::scalar }]] float SideshowsPerRideRatio{};
    [[=sam::Field{ "WealthMoreThan", sam::Kind::integer }]] std::int32_t WealthMoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesPoorSelectionParameters {
    [[=sam::Field{ "PoorerThan", 0, 100 }]] std::int32_t PoorerThan{};
    [[=sam::Field{ "MoreFeaturesThan", sam::Kind::integer }]] std::int32_t MoreFeaturesThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesResearchMoreParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesBuildFeaturesParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AdditionalPerDay", sam::Kind::integer }]] std::int32_t AdditionalPerDay{};
};

struct AdvisorFeaturesNoToiletsParameters {
    [[=sam::Field{ "NeedingMoreThan", 0, 101 }]] std::int32_t NeedingMoreThan{};
    [[=sam::Field{ "MorePeopleThanThisNeeding", 0, 101 }]] std::int32_t MorePeopleThanThisNeeding{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesFarToToiletsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesNotEnoughToiletsParameters {
    [[=sam::Field{ "AverageQueueLongerThan", sam::Kind::scalar }]] float AverageQueueLongerThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFeaturesDirtyToilets1Parameters {
    [[=sam::Field{ "FilthierThan", sam::Kind::integer }]] std::int32_t FilthierThan{};
    [[=sam::Field{ "PointsPerFilthPct", sam::Kind::integer }]] std::int32_t PointsPerFilthPct{};
};

struct AdvisorFeaturesDirtyToilets2Parameters {
    [[=sam::Field{ "FilthierThan", sam::Kind::integer }]] std::int32_t FilthierThan{};
    [[=sam::Field{ "PointsPerFilthPct", sam::Kind::integer }]] std::int32_t PointsPerFilthPct{};
};

struct AdvisorFeaturesDirtyToilets3Parameters {
    [[=sam::Field{ "FilthierThan", sam::Kind::integer }]] std::int32_t FilthierThan{};
    [[=sam::Field{ "PointsPerFilthPct", sam::Kind::integer }]] std::int32_t PointsPerFilthPct{};
};

struct AdvisorFeaturesDirtyToilets4Parameters {
    [[=sam::Field{ "FilthierThan", sam::Kind::integer }]] std::int32_t FilthierThan{};
    [[=sam::Field{ "PointsPerFilthPct", sam::Kind::integer }]] std::int32_t PointsPerFilthPct{};
};

struct AdvisorPricingTicketTooExpensiveParameters {
    [[=sam::Field{ "TicketSatisfactionUnder", sam::Kind::integer }]] std::int32_t TicketSatisfactionUnder{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPricingTicketTooCheapParameters {
    [[=sam::Field{ "TicketSatisfactionOver", sam::Kind::integer }]] std::int32_t TicketSatisfactionOver{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchRideResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchAddonResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchShopResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchSideshowResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchFeatureResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGeneralLaxSecurityParameters {
    [[=sam::Field{ "MoreThanThisGotAway", sam::Kind::integer }]] std::int32_t MoreThanThisGotAway{};
    [[=sam::Field{ "ScorePerGetAway", sam::Kind::integer }]] std::int32_t ScorePerGetAway{};
};

struct AdvisorGeneralWonGoldenTicketParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGeneralRecievedEmailParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorAwardGivenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorAwardMovedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorParkAwardWonParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorParkAwardMovedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorHelpMessageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "ContextHelpScore", sam::Kind::integer }]] std::int32_t ContextHelpScore{};
};

struct AdvisorWealthHighParameters {
    [[=sam::Field{ "MoreMoneyThan", sam::Kind::integer }]] std::int32_t MoreMoneyThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorWealthHighRepayLoanParameters {
    [[=sam::Field{ "Margin", sam::Kind::scalar }]] float Margin{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTakeLoanParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInTheRedSixMonthsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInTheRedThreeMonthsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInTheRedMonthLeftParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorBankruptedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorWagesHighParameters {
    [[=sam::Field{ "WagesTimesHigherThanIncome", sam::Kind::scalar }]] float WagesTimesHigherThanIncome{};
    [[=sam::Field{ "AverageOverMonths", sam::Kind::integer }]] std::int32_t AverageOverMonths{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsHandymanParameters {
    [[=sam::Field{ "FudgeFactor", sam::Kind::integer }]] std::int32_t FudgeFactor{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsEntertainersParameters {
    [[=sam::Field{ "FudgeFactor", sam::Kind::integer }]] std::int32_t FudgeFactor{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsMechanicsParameters {
    [[=sam::Field{ "FudgeFactor", sam::Kind::integer }]] std::int32_t FudgeFactor{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsSecurityParameters {
    [[=sam::Field{ "FudgeFactor", sam::Kind::integer }]] std::int32_t FudgeFactor{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsResearchersParameters {
    [[=sam::Field{ "FudgeFactor", sam::Kind::integer }]] std::int32_t FudgeFactor{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorQueueUnconnectedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxHandymenParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxHandymenV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxEntertainersParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxEntertainersV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxMechanicsParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxMechanicsV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxGuardsParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxGuardsV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxResearchersParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxResearchersV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorHypeNewparkParameters {
    [[=sam::Field{ "MaxMonths", sam::Kind::integer }]] std::int32_t MaxMonths{};
    [[=sam::Field{ "DroppedPercent", sam::Kind::integer }]] std::int32_t DroppedPercent{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankStinkbombParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankLitterdropParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankBalloonburstParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorParkNowOpenParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorParkNowClosedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorUpgradeHireMechanicParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorUpgradeStikingMechanicsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNewUpgradeNoMechanicParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNewResearchGroupRideParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNewResearchGroupShopParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNewResearchGroupSideshowParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNewResearchGroupFeatureParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorBusArrivedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPlaneArrivedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorBoatArrivedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorBigBusloadArrivedParameters {
    [[=sam::Field{ "HowBigIsBig", sam::Kind::integer }]] std::int32_t HowBigIsBig{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorVisitorsTooScaredParameters {
    [[=sam::Field{ "AverageScarierThan", sam::Kind::integer }]] std::int32_t AverageScarierThan{};
    [[=sam::Field{ "MinimumRides", sam::Kind::integer }]] std::int32_t MinimumRides{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorOnlineNewItemParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorOnlineMessageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratStaffHappyParameters {
    [[=sam::Field{ "MinimumStaff", sam::Kind::integer }]] std::int32_t MinimumStaff{};
    [[=sam::Field{ "HappierThan", sam::Kind::integer }]] std::int32_t HappierThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratVisitorsHappyParameters {
    [[=sam::Field{ "MinimumVisitors", sam::Kind::integer }]] std::int32_t MinimumVisitors{};
    [[=sam::Field{ "HappierThan", sam::Kind::integer }]] std::int32_t HappierThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratSecurityTrainedParameters {
    [[=sam::Field{ "MinimumGuards", sam::Kind::integer }]] std::int32_t MinimumGuards{};
    [[=sam::Field{ "BetterThan", sam::Kind::integer }]] std::int32_t BetterThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratResearchersTrainedParameters {
    [[=sam::Field{ "MinimumResearchers", sam::Kind::integer }]] std::int32_t MinimumResearchers{};
    [[=sam::Field{ "BetterThan", sam::Kind::integer }]] std::int32_t BetterThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratHandymenTrainedParameters {
    [[=sam::Field{ "MinimumHandymen", sam::Kind::integer }]] std::int32_t MinimumHandymen{};
    [[=sam::Field{ "BetterThan", sam::Kind::integer }]] std::int32_t BetterThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratEntertainersTrainedParameters {
    [[=sam::Field{ "MinimumEntertainers", sam::Kind::integer }]] std::int32_t MinimumEntertainers{};
    [[=sam::Field{ "BetterThan", sam::Kind::integer }]] std::int32_t BetterThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratMechanicsTrainedParameters {
    [[=sam::Field{ "MinimumMechanics", sam::Kind::integer }]] std::int32_t MinimumMechanics{};
    [[=sam::Field{ "BetterThan", sam::Kind::integer }]] std::int32_t BetterThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratPublishParkParameters {
    [[=sam::Field{ "ValueBetterThan", sam::Kind::integer }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "HappinessGreaterThan", sam::Kind::integer }]] std::int32_t HappinessGreaterThan{};
    [[=sam::Field{ "VisitorsMoreThan", sam::Kind::integer }]] std::int32_t VisitorsMoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratSendPostcardParameters {
    [[=sam::Field{ "ValueBetterThan", sam::Kind::integer }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "HappinessGreaterThan", sam::Kind::integer }]] std::int32_t HappinessGreaterThan{};
    [[=sam::Field{ "VisitorsMoreThan", sam::Kind::integer }]] std::int32_t VisitorsMoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratBalloonsParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratCostumesParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratAllCompleteParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratAllCompleteLeavingParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorNeedStaffRoomParameters {
    [[=sam::Field{ "PctOfStaffTired", 0, 101 }]] std::int32_t PctOfStaffTired{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorHireStaffGeneralParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "MinVisitorsEver", sam::Kind::integer }]] std::int32_t MinVisitorsEver{};
};

struct AdvisorSetPatrolsGeneralParameters {
    [[=sam::Field{ "MinStaffForMessage", sam::Kind::integer }]] std::int32_t MinStaffForMessage{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTrainStaffGeneralParameters {
    [[=sam::Field{ "MinStaffForMessage", sam::Kind::integer }]] std::int32_t MinStaffForMessage{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorFoodPoorValueParameters {
    [[=sam::Field{ "ValueWorseThan", 0, 101 }]] std::int32_t ValueWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorShopsPoorValueParameters {
    [[=sam::Field{ "ValueWorseThan", 0, 101 }]] std::int32_t ValueWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorRestaurantPoorValueParameters {
    [[=sam::Field{ "ValueWorseThan", 0, 101 }]] std::int32_t ValueWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorDrinksPoorValueParameters {
    [[=sam::Field{ "ValueWorseThan", 0, 101 }]] std::int32_t ValueWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorSideshowsPoorValueParameters {
    [[=sam::Field{ "ValueWorseThan", 0, 101 }]] std::int32_t ValueWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorFoodPoorSatisfactionParameters {
    [[=sam::Field{ "SatisfactionWorseThan", 0, 101 }]] std::int32_t SatisfactionWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorShopsPoorSatisfactionParameters {
    [[=sam::Field{ "SatisfactionWorseThan", 0, 101 }]] std::int32_t SatisfactionWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorRestaurantPoorSatisfactionParameters {
    [[=sam::Field{ "SatisfactionWorseThan", 0, 101 }]] std::int32_t SatisfactionWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorDrinksPoorSatisfactionParameters {
    [[=sam::Field{ "SatisfactionWorseThan", 0, 101 }]] std::int32_t SatisfactionWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorSideshowsPoorSatisfactionParameters {
    [[=sam::Field{ "SatisfactionWorseThan", 0, 101 }]] std::int32_t SatisfactionWorseThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorFoodGreatValueParameters {
    [[=sam::Field{ "ValueBetterThan", 0, 101 }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorShopsGreatValueParameters {
    [[=sam::Field{ "ValueBetterThan", 0, 101 }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorRestaurantGreatValueParameters {
    [[=sam::Field{ "ValueBetterThan", 0, 101 }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorDrinksGreatValueParameters {
    [[=sam::Field{ "ValueBetterThan", 0, 101 }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorSideshowsGreatValueParameters {
    [[=sam::Field{ "ValueBetterThan", 0, 101 }]] std::int32_t ValueBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorFoodGreatSatisfactionParameters {
    [[=sam::Field{ "SatisfactionBetterThan", 0, 101 }]] std::int32_t SatisfactionBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorShopsGreatSatisfactionParameters {
    [[=sam::Field{ "SatisfactionBetterThan", 0, 101 }]] std::int32_t SatisfactionBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorRestaurantGreatSatisfactionParameters {
    [[=sam::Field{ "SatisfactionBetterThan", 0, 101 }]] std::int32_t SatisfactionBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorDrinksGreatSatisfactionParameters {
    [[=sam::Field{ "SatisfactionBetterThan", 0, 101 }]] std::int32_t SatisfactionBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorSideshowsGreatSatisfactionParameters {
    [[=sam::Field{ "SatisfactionBetterThan", 0, 101 }]] std::int32_t SatisfactionBetterThan{};
    [[=sam::Field{ "ScorePerPoint", sam::Kind::integer }]] std::int32_t ScorePerPoint{};
};

struct AdvisorRideCondemnedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRidesClosedParameters {
    [[=sam::Field{ "AtLeastThisManyRides", sam::Kind::nonnegative_integer }]] std::int32_t AtLeastThisManyRides{};
    [[=sam::Field{ "MoreThanThisPercentClosed", 0, 101 }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AllScore", sam::Kind::integer }]] std::int32_t AllScore{};
};

struct AdvisorShopsClosedParameters {
    [[=sam::Field{ "MoreThanThisPercentClosed", 0, 101 }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AllScore", sam::Kind::integer }]] std::int32_t AllScore{};
};

struct AdvisorSideshowsClosedParameters {
    [[=sam::Field{ "MoreThanThisPercentClosed", 0, 101 }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AllScore", sam::Kind::integer }]] std::int32_t AllScore{};
};

struct AdvisorToiletsClosedParameters {
    [[=sam::Field{ "MoreThanThisPercentClosed", 0, 101 }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
    [[=sam::Field{ "AllScore", sam::Kind::integer }]] std::int32_t AllScore{};
    [[=sam::Field{ "AllFScore", sam::Kind::integer }]] std::int32_t AllFScore{};
    [[=sam::Field{ "AllMScore", sam::Kind::integer }]] std::int32_t AllMScore{};
};

struct AdvisorStaffroomClosedParameters {
    [[=sam::Field{ "MoreThanThisPercentClosed", 0, 101 }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsOfPranksParameters {
    [[=sam::Field{ "MorePrankstersThan", sam::Kind::integer }]] std::int32_t MorePrankstersThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorRideExitBlockedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorWonFirstGoldenTicketParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketNearToFirstOneParameters {
    [[=sam::Field{ "PeopleMultiplier", sam::Kind::scalar }]] float PeopleMultiplier{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketCluesForRemainderParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketNearToXPeepsParameters {
    [[=sam::Field{ "PeopleMultiplier", sam::Kind::scalar }]] float PeopleMultiplier{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketNearToXHappinessParameters {
    [[=sam::Field{ "HappinessMultiplier", sam::Kind::scalar }]] float HappinessMultiplier{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketNearToXProfitParameters {
    [[=sam::Field{ "ProfitMultiplier", sam::Kind::scalar }]] float ProfitMultiplier{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTutorialUIPanelParameters {
    [[=sam::Field{ "AfterTime", sam::Kind::nonnegative_integer }]] std::int32_t AfterTime{};
    [[=sam::Field{ "CutoffTime", sam::Kind::nonnegative_integer }]] std::int32_t CutoffTime{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTutorialExplainTooltipsParameters {
    [[=sam::Field{ "AfterTime", sam::Kind::nonnegative_integer }]] std::int32_t AfterTime{};
    [[=sam::Field{ "CutoffTime", sam::Kind::nonnegative_integer }]] std::int32_t CutoffTime{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGoldTicketExplanationParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrebuiltParkParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAllResearchedAParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAllResearchedBParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAllResearchedCParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAmountVisitorsAParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAmountVisitorsBParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorGTAmountVisitorsCParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorChallengeParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorWaitingTimesParameters {
    [[=sam::Field{ "DaysForStaff", sam::Kind::nonnegative_integer }]] std::int32_t DaysForStaff{};
    [[=sam::Field{ "DaysForObjects", sam::Kind::nonnegative_integer }]] std::int32_t DaysForObjects{};
};

struct AdvisorPlantDieParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPhotoBoothsParameters {
    [[=sam::Field{ "MinimumNumber", sam::Kind::nonnegative_integer }]] std::int32_t MinimumNumber{};
    [[=sam::Field{ "PercentageClosed", sam::Kind::nonnegative_integer }]] std::int32_t PercentageClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInfoKiosksParameters {
    [[=sam::Field{ "MinimumNumber", sam::Kind::nonnegative_integer }]] std::int32_t MinimumNumber{};
    [[=sam::Field{ "PercentageClosed", sam::Kind::nonnegative_integer }]] std::int32_t PercentageClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInfoKiosksAllClosedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMaxGardenersV2Parameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffroomsClosedParameters {
    [[=sam::Field{ "MoreThanThisNumber", sam::Kind::integer }]] std::int32_t MoreThanThisNumber{};
    [[=sam::Field{ "MoreThanThisPercentClosed", sam::Kind::integer }]] std::int32_t MoreThanThisPercentClosed{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffroomsAllClosedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffroomsNotEnoughParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTrainingRoomBuildParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorTrainingRoomClosedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffroomsTooFarParameters {
    [[=sam::Field{ "StaffroomRadius", sam::Kind::integer }]] std::int32_t StaffroomRadius{};
    [[=sam::Field{ "MoreThanThisPercentNotCovered", sam::Kind::integer }]] std::int32_t MoreThanThisPercentNotCovered{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchBuildAndHireParameters {
    [[=sam::Field{ "RidesLeftToBuild", sam::Kind::integer }]] std::int32_t RidesLeftToBuild{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorMessagesUnreadParameters {
    [[=sam::Field{ "NumberOfUnreadMessages", sam::Kind::integer }]] std::int32_t NumberOfUnreadMessages{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorVisitWebsiteParameters {
    [[=sam::Field{ "MonthsIntoGameForConsideration",
                   sam::Kind::integer }]] std::int32_t MonthsIntoGameForConsideration{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorIdleResearchersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchNoProjectParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorInfoKioskBuildParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffSetGardenerAreaParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorStaffGardenerCoverageParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPlantsDyingNoGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPlantsDyingBusyGardenersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPlantsLotsdyingNoGardenersParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPlantsLotsdyingBusyGardenersParameters {
    [[=sam::Field{ "HigherPctThanThis", sam::Kind::integer }]] std::int32_t HigherPctThanThis{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorResearchFacilityResearchedParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorLotsGardenersParameters {
    [[=sam::Field{ "MinimumNumberOfGardeners", sam::Kind::integer }]] std::int32_t MinimumNumberOfGardeners{};
    [[=sam::Field{ "Ratio", sam::Kind::integer }]] std::int32_t Ratio{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankToiletParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankBinfireParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankBinfireNoFirefightersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankPlantfireParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankPlantfireNoFirefightersParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankRumoursParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankWhingeingParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorPrankNoguardsParameters {
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratKidsHappyParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratAdultsHappyParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCongratOAPsHappyParameters {
    [[=sam::Field{ "MoreThan", sam::Kind::integer }]] std::int32_t MoreThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorVisitorsGeneralUnhappyParameters {
    [[=sam::Field{ "LessThan", sam::Kind::integer }]] std::int32_t LessThan{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorAdultsGeneralUnhappyParameters {
    [[=sam::Field{ "Maximum", sam::Kind::integer }]] std::int32_t Maximum{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorOAPsGeneralUnhappyParameters {
    [[=sam::Field{ "Maximum", sam::Kind::integer }]] std::int32_t Maximum{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorKidsGeneralUnhappyParameters {
    [[=sam::Field{ "Maximum", sam::Kind::integer }]] std::int32_t Maximum{};
    [[=sam::Field{ "Score", sam::Kind::integer }]] std::int32_t Score{};
};

struct AdvisorCamcorderTutorialParameters {
    [[=sam::Field{ "WalkTutorialMoreRidesThan", sam::Kind::integer }]] std::int32_t WalkTutorialMoreRidesThan{};
    [[=sam::Field{ "RideTutorialMoreRidesThan", sam::Kind::integer }]] std::int32_t RideTutorialMoreRidesThan{};
};

struct AdvisorHelpImASpazParameters {
    [[=sam::Field{ "DaysTilViewRotateMsg", sam::Kind::integer }]] std::int32_t DaysTilViewRotateMsg{};
    [[=sam::Field{ "DaysTilViewZoomMsg", sam::Kind::integer }]] std::int32_t DaysTilViewZoomMsg{};
    [[=sam::Field{ "ItemsTilItemRotateMsg", sam::Kind::integer }]] std::int32_t ItemsTilItemRotateMsg{};
    [[=sam::Field{ "DaysTilCompassMsg", sam::Kind::integer }]] std::int32_t DaysTilCompassMsg{};
    [[=sam::Field{ "MinsTilNotRightClickedMsg", sam::Kind::integer }]] std::int32_t MinsTilNotRightClickedMsg{};
};

struct AdvisorNewParameters {
    [[=sam::Field{ "ADVISORFEATURESNOPHOTOKIOSKS", sam::Kind::integer }]] std::int32_t ADVISORFEATURESNOPHOTOKIOSKS{};
    [[=sam::Field{ "ADVISORNOTRIGHTCLICKED", sam::Kind::integer }]] std::int32_t ADVISORNOTRIGHTCLICKED{};
    [[=sam::Field{ "ADVISORSTUTBUYSHOPS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUYSHOPS{};
    [[=sam::Field{ "ADVISORSTUTBUILDRIDE", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDRIDE{};
    [[=sam::Field{ "ADVISORSTUTBUILDCOASTER", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDCOASTER{};
    [[=sam::Field{ "ADVISORSTUTHIREMECHANIC", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREMECHANIC{};
    [[=sam::Field{ "ADVISORSTUTHIREHANDYMAN", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREHANDYMAN{};
    [[=sam::Field{ "ADVISORSTUTHIREENTERTAINER", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREENTERTAINER{};
    [[=sam::Field{ "ADVISORSTUTBUILDSIDESHOW", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDSIDESHOW{};
    [[=sam::Field{ "ADVISORSTUTWELLDONE1", sam::Kind::integer }]] std::int32_t ADVISORSTUTWELLDONE1{};
    [[=sam::Field{ "ADVISORSTUTVISITORINFO", sam::Kind::integer }]] std::int32_t ADVISORSTUTVISITORINFO{};
    [[=sam::Field{ "ADVISORSTUTREADEMAILS", sam::Kind::integer }]] std::int32_t ADVISORSTUTREADEMAILS{};
    [[=sam::Field{ "ADVISORSTUTARROWKEYS", sam::Kind::integer }]] std::int32_t ADVISORSTUTARROWKEYS{};
    [[=sam::Field{ "ADVISORSTUTBUILDSTAFFROOM", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDSTAFFROOM{};
    [[=sam::Field{ "ADVISORSTUTBUILDTOILETS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDTOILETS{};
    [[=sam::Field{ "ADVISORSTUTBUILDPLANTS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDPLANTS{};
    [[=sam::Field{ "ADVISORSTUTHIREGARDENER", sam::Kind::integer }]] std::int32_t ADVISORSTUTHIREGARDENER{};
    [[=sam::Field{ "ADVISORSTUTRESEARCHEXPANSION", sam::Kind::integer }]] std::int32_t ADVISORSTUTRESEARCHEXPANSION{};
    [[=sam::Field{ "ADVISORSTUTENTRYPRICE", sam::Kind::integer }]] std::int32_t ADVISORSTUTENTRYPRICE{};
    [[=sam::Field{ "ADVISORSTUTWELLDONE1NUMVIS", sam::Kind::integer }]] std::int32_t ADVISORSTUTWELLDONE1NUMVIS{};
    [[=sam::Field{ "ADVISORSTUTVISITORINFONUMVIS", sam::Kind::integer }]] std::int32_t ADVISORSTUTVISITORINFONUMVIS{};
    [[=sam::Field{ "ADVISORSTUTENTRYPRICEMINRIDES", sam::Kind::integer }]] std::int32_t ADVISORSTUTENTRYPRICEMINRIDES{};
    [[=sam::Field{ "ADVISORSTUTBUILDPATHS", sam::Kind::integer }]] std::int32_t ADVISORSTUTBUILDPATHS{};
};

struct AdvisorBalance {
    [[=sam::Group{ "GeneralAdvisor" }]] AdvisorGeneralPolicy GeneralAdvisor{};
    [[=sam::Rows{ "MessageGroups" }]] std::array<AdvisorMessageGroupPolicy, 10> MessageGroups{};
    [[=sam::Extent{ "MessageGroups" }]] std::uint32_t MessageGroups_count{};
    [[=sam::Rows{ "GroupTimes" }]] std::array<AdvisorGroupTimesParameters, 6> GroupTimes{};
    [[=sam::Extent{ "GroupTimes" }]] std::uint32_t GroupTimes_count{};
    [[=sam::Rows{ "Pricing" }]] std::array<AdvisorPricingParameters, 6> Pricing{};
    [[=sam::Extent{ "Pricing" }]] std::uint32_t Pricing_count{};
    [[=sam::Group{ "Breakdown" }]] AdvisorBreakdownParameters Breakdown{};
    [[=sam::Group{ "Lots" }]] AdvisorLotsParameters Lots{};
    [[=sam::Group{ "Welcome" }]] AdvisorWelcomeParameters Welcome{};
    [[=sam::Group{ "OpenPark" }]] AdvisorOpenParkParameters OpenPark{};
    [[=sam::Group{ "ClosePark" }]] AdvisorCloseParkParameters ClosePark{};
    [[=sam::Group{ "VisitorsThirsty" }]] AdvisorVisitorsThirstyParameters VisitorsThirsty{};
    [[=sam::Group{ "VisitorsHungry" }]] AdvisorVisitorsHungryParameters VisitorsHungry{};
    [[=sam::Group{ "StaffHireMechanics1" }]] AdvisorStaffHireMechanics1Parameters StaffHireMechanics1{};
    [[=sam::Group{ "StaffHireMechanics2" }]] AdvisorStaffHireMechanics2Parameters StaffHireMechanics2{};
    [[=sam::Group{ "StaffHireMechanics3" }]] AdvisorStaffHireMechanics3Parameters StaffHireMechanics3{};
    [[=sam::Group{ "StaffHireHandymen1" }]] AdvisorStaffHireHandymen1Parameters StaffHireHandymen1{};
    [[=sam::Group{ "StaffHireHandymen2" }]] AdvisorStaffHireHandymen2Parameters StaffHireHandymen2{};
    [[=sam::Group{ "StaffHireHandymen3" }]] AdvisorStaffHireHandymen3Parameters StaffHireHandymen3{};
    [[=sam::Group{ "StaffHireGuards1" }]] AdvisorStaffHireGuards1Parameters StaffHireGuards1{};
    [[=sam::Group{ "StaffHireGuards2" }]] AdvisorStaffHireGuards2Parameters StaffHireGuards2{};
    [[=sam::Group{ "StaffHireGuards3" }]] AdvisorStaffHireGuards3Parameters StaffHireGuards3{};
    [[=sam::Group{ "StaffHireEntertainers1" }]] AdvisorStaffHireEntertainers1Parameters StaffHireEntertainers1{};
    [[=sam::Group{ "StaffHireResearchers1" }]] AdvisorStaffHireResearchers1Parameters StaffHireResearchers1{};
    [[=sam::Group{ "StaffHireResearchers2" }]] AdvisorStaffHireResearchers2Parameters StaffHireResearchers2{};
    [[=sam::Group{ "StaffHireResearchers3" }]] AdvisorStaffHireResearchers3Parameters StaffHireResearchers3{};
    [[=sam::Group{ "StaffSetMechanicArea" }]] AdvisorStaffSetMechanicAreaParameters StaffSetMechanicArea{};
    [[=sam::Group{ "StaffMechanicCoverage" }]] AdvisorStaffMechanicCoverageParameters StaffMechanicCoverage{};
    [[=sam::Group{ "StaffSetHandymanArea" }]] AdvisorStaffSetHandymanAreaParameters StaffSetHandymanArea{};
    [[=sam::Group{ "StaffHandymanCoverage" }]] AdvisorStaffHandymanCoverageParameters StaffHandymanCoverage{};
    [[=sam::Group{ "StaffGuardBuildCamera" }]] AdvisorStaffGuardBuildCameraParameters StaffGuardBuildCamera{};
    [[=sam::Group{ "StaffSetGuardArea" }]] AdvisorStaffSetGuardAreaParameters StaffSetGuardArea{};
    [[=sam::Group{ "StaffGuardCoverage" }]] AdvisorStaffGuardCoverageParameters StaffGuardCoverage{};
    [[=sam::Group{ "StaffSetEntertainerArea" }]] AdvisorStaffSetEntertainerAreaParameters StaffSetEntertainerArea{};
    [[=sam::Group{ "StaffEntertainerCoverage" }]] AdvisorStaffEntertainerCoverageParameters StaffEntertainerCoverage{};
    [[=sam::Group{ "StaffTrainMechanics" }]] AdvisorStaffTrainMechanicsParameters StaffTrainMechanics{};
    [[=sam::Group{ "StaffTrainHandymen" }]] AdvisorStaffTrainHandymenParameters StaffTrainHandymen{};
    [[=sam::Group{ "StaffTrainGuards" }]] AdvisorStaffTrainGuardsParameters StaffTrainGuards{};
    [[=sam::Group{ "StaffTrainEntertainers" }]] AdvisorStaffTrainEntertainersParameters StaffTrainEntertainers{};
    [[=sam::Group{ "StaffTrainResearchers" }]] AdvisorStaffTrainResearchersParameters StaffTrainResearchers{};
    [[=sam::Group{ "StaffUnhappyMechanics" }]] AdvisorStaffUnhappyMechanicsParameters StaffUnhappyMechanics{};
    [[=sam::Group{ "StaffUnhappyHandymen" }]] AdvisorStaffUnhappyHandymenParameters StaffUnhappyHandymen{};
    [[=sam::Group{ "StaffUnhappyGuards" }]] AdvisorStaffUnhappyGuardsParameters StaffUnhappyGuards{};
    [[=sam::Group{ "StaffUnhappyEntertainers" }]] AdvisorStaffUnhappyEntertainersParameters StaffUnhappyEntertainers{};
    [[=sam::Group{ "StaffUnhappyResearchers" }]] AdvisorStaffUnhappyResearchersParameters StaffUnhappyResearchers{};
    [[=sam::Group{ "StaffUnhappyGardeners" }]] AdvisorStaffUnhappyGardenersParameters StaffUnhappyGardeners{};
    [[=sam::Group{
        "StaffVeryUnhappyMechanics" }]] AdvisorStaffVeryUnhappyMechanicsParameters StaffVeryUnhappyMechanics{};
    [[=sam::Group{ "StaffVeryUnhappyHandymen" }]] AdvisorStaffVeryUnhappyHandymenParameters StaffVeryUnhappyHandymen{};
    [[=sam::Group{ "StaffVeryUnhappyGuards" }]] AdvisorStaffVeryUnhappyGuardsParameters StaffVeryUnhappyGuards{};
    [[=sam::Group{
        "StaffVeryUnhappyEntertainers" }]] AdvisorStaffVeryUnhappyEntertainersParameters StaffVeryUnhappyEntertainers{};
    [[=sam::Group{
        "StaffVeryUnhappyResearchers" }]] AdvisorStaffVeryUnhappyResearchersParameters StaffVeryUnhappyResearchers{};
    [[=sam::Group{
        "StaffVeryUnhappyGardeners" }]] AdvisorStaffVeryUnhappyGardenersParameters StaffVeryUnhappyGardeners{};
    [[=sam::Group{ "StaffStrikingMechanics" }]] AdvisorStaffStrikingMechanicsParameters StaffStrikingMechanics{};
    [[=sam::Group{ "StaffStrikingHandymen" }]] AdvisorStaffStrikingHandymenParameters StaffStrikingHandymen{};
    [[=sam::Group{ "StaffStrikingGuards" }]] AdvisorStaffStrikingGuardsParameters StaffStrikingGuards{};
    [[=sam::Group{
        "StaffStrikingEntertainers" }]] AdvisorStaffStrikingEntertainersParameters StaffStrikingEntertainers{};
    [[=sam::Group{ "StaffStrikingResearchers" }]] AdvisorStaffStrikingResearchersParameters StaffStrikingResearchers{};
    [[=sam::Group{ "StaffStrikingGardeners" }]] AdvisorStaffStrikingGardenersParameters StaffStrikingGardeners{};
    [[=sam::Group{ "StaffHappierMechanics" }]] AdvisorStaffHappierMechanicsParameters StaffHappierMechanics{};
    [[=sam::Group{ "StaffHappierHandymen" }]] AdvisorStaffHappierHandymenParameters StaffHappierHandymen{};
    [[=sam::Group{ "StaffHappierGuards" }]] AdvisorStaffHappierGuardsParameters StaffHappierGuards{};
    [[=sam::Group{ "StaffHappierEntertainers" }]] AdvisorStaffHappierEntertainersParameters StaffHappierEntertainers{};
    [[=sam::Group{ "StaffHappierResearchers" }]] AdvisorStaffHappierResearchersParameters StaffHappierResearchers{};
    [[=sam::Group{ "StaffHappierGardeners" }]] AdvisorStaffHappierGardenersParameters StaffHappierGardeners{};
    [[=sam::Group{ "StrikeEndBadMechanics" }]] AdvisorStrikeEndBadMechanicsParameters StrikeEndBadMechanics{};
    [[=sam::Group{ "StrikeEndBadHandymen" }]] AdvisorStrikeEndBadHandymenParameters StrikeEndBadHandymen{};
    [[=sam::Group{ "StrikeEndBadGuards" }]] AdvisorStrikeEndBadGuardsParameters StrikeEndBadGuards{};
    [[=sam::Group{ "StrikeEndBadEntertainers" }]] AdvisorStrikeEndBadEntertainersParameters StrikeEndBadEntertainers{};
    [[=sam::Group{ "StrikeEndBadResearchers" }]] AdvisorStrikeEndBadResearchersParameters StrikeEndBadResearchers{};
    [[=sam::Group{ "StrikeEndBadGardeners" }]] AdvisorStrikeEndBadGardenersParameters StrikeEndBadGardeners{};
    [[=sam::Group{ "RidesPoorSelection" }]] AdvisorRidesPoorSelectionParameters RidesPoorSelection{};
    [[=sam::Group{ "RidesResearchMore" }]] AdvisorRidesResearchMoreParameters RidesResearchMore{};
    [[=sam::Group{ "RidesBuildRides" }]] AdvisorRidesBuildRidesParameters RidesBuildRides{};
    [[=sam::Group{ "RidesNoUpgrades" }]] AdvisorRidesNoUpgradesParameters RidesNoUpgrades{};
    [[=sam::Group{ "RidesMoreUpgrades" }]] AdvisorRidesMoreUpgradesParameters RidesMoreUpgrades{};
    [[=sam::Group{ "RidesBreakdownImminent" }]] AdvisorRidesBreakdownImminentParameters RidesBreakdownImminent{};
    [[=sam::Group{
        "RidesBreakdownNoMechanics" }]] AdvisorRidesBreakdownNoMechanicsParameters RidesBreakdownNoMechanics{};
    [[=sam::Group{
        "RidesBreakdownBusyMechanics" }]] AdvisorRidesBreakdownBusyMechanicsParameters RidesBreakdownBusyMechanics{};
    [[=sam::Group{
        "RidesBreakdownMechanicOnIt" }]] AdvisorRidesBreakdownMechanicOnItParameters RidesBreakdownMechanicOnIt{};
    [[=sam::Group{ "ShopsPoorSelection" }]] AdvisorShopsPoorSelectionParameters ShopsPoorSelection{};
    [[=sam::Group{ "ShopsResearchMore" }]] AdvisorShopsResearchMoreParameters ShopsResearchMore{};
    [[=sam::Group{ "ShopsBuildShops" }]] AdvisorShopsBuildShopsParameters ShopsBuildShops{};
    [[=sam::Group{ "ShopsBuildMoreShops" }]] AdvisorShopsBuildMoreShopsParameters ShopsBuildMoreShops{};
    [[=sam::Group{ "SideshowsPoorSelection" }]] AdvisorSideshowsPoorSelectionParameters SideshowsPoorSelection{};
    [[=sam::Group{ "SideshowsResearchMore" }]] AdvisorSideshowsResearchMoreParameters SideshowsResearchMore{};
    [[=sam::Group{ "SideshowsBuildSideshows" }]] AdvisorSideshowsBuildSideshowsParameters SideshowsBuildSideshows{};
    [[=sam::Group{
        "SideshowsBuildMoreSideshows" }]] AdvisorSideshowsBuildMoreSideshowsParameters SideshowsBuildMoreSideshows{};
    [[=sam::Group{ "FeaturesPoorSelection" }]] AdvisorFeaturesPoorSelectionParameters FeaturesPoorSelection{};
    [[=sam::Group{ "FeaturesResearchMore" }]] AdvisorFeaturesResearchMoreParameters FeaturesResearchMore{};
    [[=sam::Group{ "FeaturesBuildFeatures" }]] AdvisorFeaturesBuildFeaturesParameters FeaturesBuildFeatures{};
    [[=sam::Group{ "FeaturesNoToilets" }]] AdvisorFeaturesNoToiletsParameters FeaturesNoToilets{};
    [[=sam::Group{ "FeaturesFarToToilets" }]] AdvisorFeaturesFarToToiletsParameters FeaturesFarToToilets{};
    [[=sam::Group{ "FeaturesNotEnoughToilets" }]] AdvisorFeaturesNotEnoughToiletsParameters FeaturesNotEnoughToilets{};
    [[=sam::Group{ "FeaturesDirtyToilets1" }]] AdvisorFeaturesDirtyToilets1Parameters FeaturesDirtyToilets1{};
    [[=sam::Group{ "FeaturesDirtyToilets2" }]] AdvisorFeaturesDirtyToilets2Parameters FeaturesDirtyToilets2{};
    [[=sam::Group{ "FeaturesDirtyToilets3" }]] AdvisorFeaturesDirtyToilets3Parameters FeaturesDirtyToilets3{};
    [[=sam::Group{ "FeaturesDirtyToilets4" }]] AdvisorFeaturesDirtyToilets4Parameters FeaturesDirtyToilets4{};
    [[=sam::Group{
        "PricingTicketTooExpensive" }]] AdvisorPricingTicketTooExpensiveParameters PricingTicketTooExpensive{};
    [[=sam::Group{ "PricingTicketTooCheap" }]] AdvisorPricingTicketTooCheapParameters PricingTicketTooCheap{};
    [[=sam::Group{ "ResearchRideResearched" }]] AdvisorResearchRideResearchedParameters ResearchRideResearched{};
    [[=sam::Group{ "ResearchAddonResearched" }]] AdvisorResearchAddonResearchedParameters ResearchAddonResearched{};
    [[=sam::Group{ "ResearchShopResearched" }]] AdvisorResearchShopResearchedParameters ResearchShopResearched{};
    [[=sam::Group{
        "ResearchSideshowResearched" }]] AdvisorResearchSideshowResearchedParameters ResearchSideshowResearched{};
    [[=sam::Group{
        "ResearchFeatureResearched" }]] AdvisorResearchFeatureResearchedParameters ResearchFeatureResearched{};
    [[=sam::Group{ "GeneralLaxSecurity" }]] AdvisorGeneralLaxSecurityParameters GeneralLaxSecurity{};
    [[=sam::Group{ "GeneralWonGoldenTicket" }]] AdvisorGeneralWonGoldenTicketParameters GeneralWonGoldenTicket{};
    [[=sam::Group{ "GeneralRecievedEmail" }]] AdvisorGeneralRecievedEmailParameters GeneralRecievedEmail{};
    [[=sam::Group{ "AwardGiven" }]] AdvisorAwardGivenParameters AwardGiven{};
    [[=sam::Group{ "AwardMoved" }]] AdvisorAwardMovedParameters AwardMoved{};
    [[=sam::Group{ "ParkAwardWon" }]] AdvisorParkAwardWonParameters ParkAwardWon{};
    [[=sam::Group{ "ParkAwardMoved" }]] AdvisorParkAwardMovedParameters ParkAwardMoved{};
    [[=sam::Group{ "HelpMessage" }]] AdvisorHelpMessageParameters HelpMessage{};
    [[=sam::Group{ "WealthHigh" }]] AdvisorWealthHighParameters WealthHigh{};
    [[=sam::Group{ "WealthHighRepayLoan" }]] AdvisorWealthHighRepayLoanParameters WealthHighRepayLoan{};
    [[=sam::Group{ "TakeLoan" }]] AdvisorTakeLoanParameters TakeLoan{};
    [[=sam::Group{ "InTheRedSixMonths" }]] AdvisorInTheRedSixMonthsParameters InTheRedSixMonths{};
    [[=sam::Group{ "InTheRedThreeMonths" }]] AdvisorInTheRedThreeMonthsParameters InTheRedThreeMonths{};
    [[=sam::Group{ "InTheRedMonthLeft" }]] AdvisorInTheRedMonthLeftParameters InTheRedMonthLeft{};
    [[=sam::Group{ "Bankrupted" }]] AdvisorBankruptedParameters Bankrupted{};
    [[=sam::Group{ "WagesHigh" }]] AdvisorWagesHighParameters WagesHigh{};
    [[=sam::Group{ "LotsHandyman" }]] AdvisorLotsHandymanParameters LotsHandyman{};
    [[=sam::Group{ "LotsEntertainers" }]] AdvisorLotsEntertainersParameters LotsEntertainers{};
    [[=sam::Group{ "LotsMechanics" }]] AdvisorLotsMechanicsParameters LotsMechanics{};
    [[=sam::Group{ "LotsSecurity" }]] AdvisorLotsSecurityParameters LotsSecurity{};
    [[=sam::Group{ "LotsResearchers" }]] AdvisorLotsResearchersParameters LotsResearchers{};
    [[=sam::Group{ "QueueUnconnected" }]] AdvisorQueueUnconnectedParameters QueueUnconnected{};
    [[=sam::Group{ "MaxHandymen" }]] AdvisorMaxHandymenParameters MaxHandymen{};
    [[=sam::Group{ "MaxHandymenV2" }]] AdvisorMaxHandymenV2Parameters MaxHandymenV2{};
    [[=sam::Group{ "MaxEntertainers" }]] AdvisorMaxEntertainersParameters MaxEntertainers{};
    [[=sam::Group{ "MaxEntertainersV2" }]] AdvisorMaxEntertainersV2Parameters MaxEntertainersV2{};
    [[=sam::Group{ "MaxMechanics" }]] AdvisorMaxMechanicsParameters MaxMechanics{};
    [[=sam::Group{ "MaxMechanicsV2" }]] AdvisorMaxMechanicsV2Parameters MaxMechanicsV2{};
    [[=sam::Group{ "MaxGuards" }]] AdvisorMaxGuardsParameters MaxGuards{};
    [[=sam::Group{ "MaxGuardsV2" }]] AdvisorMaxGuardsV2Parameters MaxGuardsV2{};
    [[=sam::Group{ "MaxResearchers" }]] AdvisorMaxResearchersParameters MaxResearchers{};
    [[=sam::Group{ "MaxResearchersV2" }]] AdvisorMaxResearchersV2Parameters MaxResearchersV2{};
    [[=sam::Group{ "HypeNewpark" }]] AdvisorHypeNewparkParameters HypeNewpark{};
    [[=sam::Group{ "PrankStinkbomb" }]] AdvisorPrankStinkbombParameters PrankStinkbomb{};
    [[=sam::Group{ "PrankLitterdrop" }]] AdvisorPrankLitterdropParameters PrankLitterdrop{};
    [[=sam::Group{ "PrankBalloonburst" }]] AdvisorPrankBalloonburstParameters PrankBalloonburst{};
    [[=sam::Group{ "ParkNowOpen" }]] AdvisorParkNowOpenParameters ParkNowOpen{};
    [[=sam::Group{ "ParkNowClosed" }]] AdvisorParkNowClosedParameters ParkNowClosed{};
    [[=sam::Group{ "UpgradeHireMechanic" }]] AdvisorUpgradeHireMechanicParameters UpgradeHireMechanic{};
    [[=sam::Group{ "UpgradeStikingMechanics" }]] AdvisorUpgradeStikingMechanicsParameters UpgradeStikingMechanics{};
    [[=sam::Group{ "NewUpgradeNoMechanic" }]] AdvisorNewUpgradeNoMechanicParameters NewUpgradeNoMechanic{};
    [[=sam::Group{ "NewResearchGroupRide" }]] AdvisorNewResearchGroupRideParameters NewResearchGroupRide{};
    [[=sam::Group{ "NewResearchGroupShop" }]] AdvisorNewResearchGroupShopParameters NewResearchGroupShop{};
    [[=sam::Group{ "NewResearchGroupSideshow" }]] AdvisorNewResearchGroupSideshowParameters NewResearchGroupSideshow{};
    [[=sam::Group{ "NewResearchGroupFeature" }]] AdvisorNewResearchGroupFeatureParameters NewResearchGroupFeature{};
    [[=sam::Group{ "BusArrived" }]] AdvisorBusArrivedParameters BusArrived{};
    [[=sam::Group{ "PlaneArrived" }]] AdvisorPlaneArrivedParameters PlaneArrived{};
    [[=sam::Group{ "BoatArrived" }]] AdvisorBoatArrivedParameters BoatArrived{};
    [[=sam::Group{ "BigBusloadArrived" }]] AdvisorBigBusloadArrivedParameters BigBusloadArrived{};
    [[=sam::Group{ "VisitorsTooScared" }]] AdvisorVisitorsTooScaredParameters VisitorsTooScared{};
    [[=sam::Group{ "OnlineNewItem" }]] AdvisorOnlineNewItemParameters OnlineNewItem{};
    [[=sam::Group{ "OnlineMessage" }]] AdvisorOnlineMessageParameters OnlineMessage{};
    [[=sam::Group{ "CongratStaffHappy" }]] AdvisorCongratStaffHappyParameters CongratStaffHappy{};
    [[=sam::Group{ "CongratVisitorsHappy" }]] AdvisorCongratVisitorsHappyParameters CongratVisitorsHappy{};
    [[=sam::Group{ "CongratSecurityTrained" }]] AdvisorCongratSecurityTrainedParameters CongratSecurityTrained{};
    [[=sam::Group{
        "CongratResearchersTrained" }]] AdvisorCongratResearchersTrainedParameters CongratResearchersTrained{};
    [[=sam::Group{ "CongratHandymenTrained" }]] AdvisorCongratHandymenTrainedParameters CongratHandymenTrained{};
    [[=sam::Group{
        "CongratEntertainersTrained" }]] AdvisorCongratEntertainersTrainedParameters CongratEntertainersTrained{};
    [[=sam::Group{ "CongratMechanicsTrained" }]] AdvisorCongratMechanicsTrainedParameters CongratMechanicsTrained{};
    [[=sam::Group{ "CongratPublishPark" }]] AdvisorCongratPublishParkParameters CongratPublishPark{};
    [[=sam::Group{ "CongratSendPostcard" }]] AdvisorCongratSendPostcardParameters CongratSendPostcard{};
    [[=sam::Group{ "CongratBalloons" }]] AdvisorCongratBalloonsParameters CongratBalloons{};
    [[=sam::Group{ "CongratCostumes" }]] AdvisorCongratCostumesParameters CongratCostumes{};
    [[=sam::Group{ "CongratAllComplete" }]] AdvisorCongratAllCompleteParameters CongratAllComplete{};
    [[=sam::Group{
        "CongratAllCompleteLeaving" }]] AdvisorCongratAllCompleteLeavingParameters CongratAllCompleteLeaving{};
    [[=sam::Group{ "NeedStaffRoom" }]] AdvisorNeedStaffRoomParameters NeedStaffRoom{};
    [[=sam::Group{ "HireStaffGeneral" }]] AdvisorHireStaffGeneralParameters HireStaffGeneral{};
    [[=sam::Group{ "SetPatrolsGeneral" }]] AdvisorSetPatrolsGeneralParameters SetPatrolsGeneral{};
    [[=sam::Group{ "TrainStaffGeneral" }]] AdvisorTrainStaffGeneralParameters TrainStaffGeneral{};
    [[=sam::Group{ "FoodPoorValue" }]] AdvisorFoodPoorValueParameters FoodPoorValue{};
    [[=sam::Group{ "ShopsPoorValue" }]] AdvisorShopsPoorValueParameters ShopsPoorValue{};
    [[=sam::Group{ "RestaurantPoorValue" }]] AdvisorRestaurantPoorValueParameters RestaurantPoorValue{};
    [[=sam::Group{ "DrinksPoorValue" }]] AdvisorDrinksPoorValueParameters DrinksPoorValue{};
    [[=sam::Group{ "SideshowsPoorValue" }]] AdvisorSideshowsPoorValueParameters SideshowsPoorValue{};
    [[=sam::Group{ "FoodPoorSatisfaction" }]] AdvisorFoodPoorSatisfactionParameters FoodPoorSatisfaction{};
    [[=sam::Group{ "ShopsPoorSatisfaction" }]] AdvisorShopsPoorSatisfactionParameters ShopsPoorSatisfaction{};
    [[=sam::Group{
        "RestaurantPoorSatisfaction" }]] AdvisorRestaurantPoorSatisfactionParameters RestaurantPoorSatisfaction{};
    [[=sam::Group{ "DrinksPoorSatisfaction" }]] AdvisorDrinksPoorSatisfactionParameters DrinksPoorSatisfaction{};
    [[=sam::Group{
        "SideshowsPoorSatisfaction" }]] AdvisorSideshowsPoorSatisfactionParameters SideshowsPoorSatisfaction{};
    [[=sam::Group{ "FoodGreatValue" }]] AdvisorFoodGreatValueParameters FoodGreatValue{};
    [[=sam::Group{ "ShopsGreatValue" }]] AdvisorShopsGreatValueParameters ShopsGreatValue{};
    [[=sam::Group{ "RestaurantGreatValue" }]] AdvisorRestaurantGreatValueParameters RestaurantGreatValue{};
    [[=sam::Group{ "DrinksGreatValue" }]] AdvisorDrinksGreatValueParameters DrinksGreatValue{};
    [[=sam::Group{ "SideshowsGreatValue" }]] AdvisorSideshowsGreatValueParameters SideshowsGreatValue{};
    [[=sam::Group{ "FoodGreatSatisfaction" }]] AdvisorFoodGreatSatisfactionParameters FoodGreatSatisfaction{};
    [[=sam::Group{ "ShopsGreatSatisfaction" }]] AdvisorShopsGreatSatisfactionParameters ShopsGreatSatisfaction{};
    [[=sam::Group{
        "RestaurantGreatSatisfaction" }]] AdvisorRestaurantGreatSatisfactionParameters RestaurantGreatSatisfaction{};
    [[=sam::Group{ "DrinksGreatSatisfaction" }]] AdvisorDrinksGreatSatisfactionParameters DrinksGreatSatisfaction{};
    [[=sam::Group{
        "SideshowsGreatSatisfaction" }]] AdvisorSideshowsGreatSatisfactionParameters SideshowsGreatSatisfaction{};
    [[=sam::Group{ "RideCondemned" }]] AdvisorRideCondemnedParameters RideCondemned{};
    [[=sam::Group{ "RidesClosed" }]] AdvisorRidesClosedParameters RidesClosed{};
    [[=sam::Group{ "ShopsClosed" }]] AdvisorShopsClosedParameters ShopsClosed{};
    [[=sam::Group{ "SideshowsClosed" }]] AdvisorSideshowsClosedParameters SideshowsClosed{};
    [[=sam::Group{ "ToiletsClosed" }]] AdvisorToiletsClosedParameters ToiletsClosed{};
    [[=sam::Group{ "StaffroomClosed" }]] AdvisorStaffroomClosedParameters StaffroomClosed{};
    [[=sam::Group{ "LotsOfPranks" }]] AdvisorLotsOfPranksParameters LotsOfPranks{};
    [[=sam::Group{ "RideExitBlocked" }]] AdvisorRideExitBlockedParameters RideExitBlocked{};
    [[=sam::Group{ "WonFirstGoldenTicket" }]] AdvisorWonFirstGoldenTicketParameters WonFirstGoldenTicket{};
    [[=sam::Group{ "GoldTicketNearToFirstOne" }]] AdvisorGoldTicketNearToFirstOneParameters GoldTicketNearToFirstOne{};
    [[=sam::Group{
        "GoldTicketCluesForRemainder" }]] AdvisorGoldTicketCluesForRemainderParameters GoldTicketCluesForRemainder{};
    [[=sam::Group{ "GoldTicketNearToXPeeps" }]] AdvisorGoldTicketNearToXPeepsParameters GoldTicketNearToXPeeps{};
    [[=sam::Group{
        "GoldTicketNearToXHappiness" }]] AdvisorGoldTicketNearToXHappinessParameters GoldTicketNearToXHappiness{};
    [[=sam::Group{ "GoldTicketNearToXProfit" }]] AdvisorGoldTicketNearToXProfitParameters GoldTicketNearToXProfit{};
    [[=sam::Group{ "TutorialUIPanel" }]] AdvisorTutorialUIPanelParameters TutorialUIPanel{};
    [[=sam::Group{ "TutorialExplainTooltips" }]] AdvisorTutorialExplainTooltipsParameters TutorialExplainTooltips{};
    [[=sam::Group{ "GoldTicketExplanation" }]] AdvisorGoldTicketExplanationParameters GoldTicketExplanation{};
    [[=sam::Group{ "PrebuiltPark" }]] AdvisorPrebuiltParkParameters PrebuiltPark{};
    [[=sam::Group{ "GTAllResearchedA" }]] AdvisorGTAllResearchedAParameters GTAllResearchedA{};
    [[=sam::Group{ "GTAllResearchedB" }]] AdvisorGTAllResearchedBParameters GTAllResearchedB{};
    [[=sam::Group{ "GTAllResearchedC" }]] AdvisorGTAllResearchedCParameters GTAllResearchedC{};
    [[=sam::Group{ "GTAmountVisitorsA" }]] AdvisorGTAmountVisitorsAParameters GTAmountVisitorsA{};
    [[=sam::Group{ "GTAmountVisitorsB" }]] AdvisorGTAmountVisitorsBParameters GTAmountVisitorsB{};
    [[=sam::Group{ "GTAmountVisitorsC" }]] AdvisorGTAmountVisitorsCParameters GTAmountVisitorsC{};
    [[=sam::Group{ "Challenge" }]] AdvisorChallengeParameters Challenge{};
    [[=sam::Group{ "WaitingTimes" }]] AdvisorWaitingTimesParameters WaitingTimes{};
    [[=sam::Group{ "PlantDie" }]] AdvisorPlantDieParameters PlantDie{};
    [[=sam::Group{ "PhotoBooths" }]] AdvisorPhotoBoothsParameters PhotoBooths{};
    [[=sam::Group{ "InfoKiosks" }]] AdvisorInfoKiosksParameters InfoKiosks{};
    [[=sam::Group{ "InfoKiosksAllClosed" }]] AdvisorInfoKiosksAllClosedParameters InfoKiosksAllClosed{};
    [[=sam::Group{ "MaxGardenersV2" }]] AdvisorMaxGardenersV2Parameters MaxGardenersV2{};
    [[=sam::Group{ "StaffroomsClosed" }]] AdvisorStaffroomsClosedParameters StaffroomsClosed{};
    [[=sam::Group{ "StaffroomsAllClosed" }]] AdvisorStaffroomsAllClosedParameters StaffroomsAllClosed{};
    [[=sam::Group{ "StaffroomsNotEnough" }]] AdvisorStaffroomsNotEnoughParameters StaffroomsNotEnough{};
    [[=sam::Group{ "TrainingRoomBuild" }]] AdvisorTrainingRoomBuildParameters TrainingRoomBuild{};
    [[=sam::Group{ "TrainingRoomClosed" }]] AdvisorTrainingRoomClosedParameters TrainingRoomClosed{};
    [[=sam::Group{ "StaffroomsTooFar" }]] AdvisorStaffroomsTooFarParameters StaffroomsTooFar{};
    [[=sam::Group{ "ResearchBuildAndHire" }]] AdvisorResearchBuildAndHireParameters ResearchBuildAndHire{};
    [[=sam::Group{ "MessagesUnread" }]] AdvisorMessagesUnreadParameters MessagesUnread{};
    [[=sam::Group{ "VisitWebsite" }]] AdvisorVisitWebsiteParameters VisitWebsite{};
    [[=sam::Group{ "IdleResearchers" }]] AdvisorIdleResearchersParameters IdleResearchers{};
    [[=sam::Group{ "ResearchNoProject" }]] AdvisorResearchNoProjectParameters ResearchNoProject{};
    [[=sam::Group{ "InfoKioskBuild" }]] AdvisorInfoKioskBuildParameters InfoKioskBuild{};
    [[=sam::Group{ "StaffSetGardenerArea" }]] AdvisorStaffSetGardenerAreaParameters StaffSetGardenerArea{};
    [[=sam::Group{ "StaffGardenerCoverage" }]] AdvisorStaffGardenerCoverageParameters StaffGardenerCoverage{};
    [[=sam::Group{ "PlantsDyingNoGardeners" }]] AdvisorPlantsDyingNoGardenersParameters PlantsDyingNoGardeners{};
    [[=sam::Group{ "PlantsDyingBusyGardeners" }]] AdvisorPlantsDyingBusyGardenersParameters PlantsDyingBusyGardeners{};
    [[=sam::Group{
        "PlantsLotsdyingNoGardeners" }]] AdvisorPlantsLotsdyingNoGardenersParameters PlantsLotsdyingNoGardeners{};
    [[=sam::Group{
        "PlantsLotsdyingBusyGardeners" }]] AdvisorPlantsLotsdyingBusyGardenersParameters PlantsLotsdyingBusyGardeners{};
    [[=sam::Group{
        "ResearchFacilityResearched" }]] AdvisorResearchFacilityResearchedParameters ResearchFacilityResearched{};
    [[=sam::Group{ "LotsGardeners" }]] AdvisorLotsGardenersParameters LotsGardeners{};
    [[=sam::Group{ "PrankToilet" }]] AdvisorPrankToiletParameters PrankToilet{};
    [[=sam::Group{ "PrankBinfire" }]] AdvisorPrankBinfireParameters PrankBinfire{};
    [[=sam::Group{
        "PrankBinfireNoFirefighters" }]] AdvisorPrankBinfireNoFirefightersParameters PrankBinfireNoFirefighters{};
    [[=sam::Group{ "PrankPlantfire" }]] AdvisorPrankPlantfireParameters PrankPlantfire{};
    [[=sam::Group{
        "PrankPlantfireNoFirefighters" }]] AdvisorPrankPlantfireNoFirefightersParameters PrankPlantfireNoFirefighters{};
    [[=sam::Group{ "PrankRumours" }]] AdvisorPrankRumoursParameters PrankRumours{};
    [[=sam::Group{ "PrankWhingeing" }]] AdvisorPrankWhingeingParameters PrankWhingeing{};
    [[=sam::Group{ "PrankNoguards" }]] AdvisorPrankNoguardsParameters PrankNoguards{};
    [[=sam::Group{ "CongratKidsHappy" }]] AdvisorCongratKidsHappyParameters CongratKidsHappy{};
    [[=sam::Group{ "CongratAdultsHappy" }]] AdvisorCongratAdultsHappyParameters CongratAdultsHappy{};
    [[=sam::Group{ "CongratOAPsHappy" }]] AdvisorCongratOAPsHappyParameters CongratOAPsHappy{};
    [[=sam::Group{ "VisitorsGeneralUnhappy" }]] AdvisorVisitorsGeneralUnhappyParameters VisitorsGeneralUnhappy{};
    [[=sam::Group{ "AdultsGeneralUnhappy" }]] AdvisorAdultsGeneralUnhappyParameters AdultsGeneralUnhappy{};
    [[=sam::Group{ "OAPsGeneralUnhappy" }]] AdvisorOAPsGeneralUnhappyParameters OAPsGeneralUnhappy{};
    [[=sam::Group{ "KidsGeneralUnhappy" }]] AdvisorKidsGeneralUnhappyParameters KidsGeneralUnhappy{};
    [[=sam::Group{ "CamcorderTutorial" }]] AdvisorCamcorderTutorialParameters CamcorderTutorial{};
    [[=sam::Group{ "HelpImASpaz" }]] AdvisorHelpImASpazParameters HelpImASpaz{};
    [[=sam::Group{ "New" }]] AdvisorNewParameters New{};
};
} //namespace simcoaster
