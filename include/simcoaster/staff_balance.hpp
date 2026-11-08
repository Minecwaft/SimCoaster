#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct StaffPoolParameters {
    [[=sam::Field{ "ChanceToGetGreatHandymen", 0, 100 }]] std::int32_t ChanceToGetGreatHandymen{};
    [[=sam::Field{ "ChanceToGetGreatMechanics", 0, 100 }]] std::int32_t ChanceToGetGreatMechanics{};
    [[=sam::Field{ "ChanceToGetGreatEntertainers", 0, 100 }]] std::int32_t ChanceToGetGreatEntertainers{};
    [[=sam::Field{ "ChanceToGetGreatGuards", 0, 100 }]] std::int32_t ChanceToGetGreatGuards{};
    [[=sam::Field{ "ChanceToGetGreatResearchers", 0, 100 }]] std::int32_t ChanceToGetGreatResearchers{};
    [[=sam::Field{ "AvgGradeOfHandymen", 0, 4 }]] std::int32_t AvgGradeOfHandymen{};
    [[=sam::Field{ "AvgGradeOfMechanics", 0, 4 }]] std::int32_t AvgGradeOfMechanics{};
    [[=sam::Field{ "AvgGradeOfEntertainers", 0, 4 }]] std::int32_t AvgGradeOfEntertainers{};
    [[=sam::Field{ "AvgGradeOfGuards", 0, 4 }]] std::int32_t AvgGradeOfGuards{};
    [[=sam::Field{ "AvgGradeOfResearchers", 0, 4 }]] std::int32_t AvgGradeOfResearchers{};
    [[=sam::Field{ "AvgExperienceOfHandymen", sam::Kind::integer }]] std::int32_t AvgExperienceOfHandymen{};
    [[=sam::Field{ "AvgExperienceOfMechanics", sam::Kind::integer }]] std::int32_t AvgExperienceOfMechanics{};
    [[=sam::Field{ "AvgExperienceOfEntertainers", sam::Kind::integer }]] std::int32_t AvgExperienceOfEntertainers{};
    [[=sam::Field{ "AvgExperienceOfGuards", sam::Kind::integer }]] std::int32_t AvgExperienceOfGuards{};
    [[=sam::Field{ "AvgExperienceOfResearchers", sam::Kind::integer }]] std::int32_t AvgExperienceOfResearchers{};
    [[=sam::Field{ "BeginningNumberOfHandymen",
                   sam::Kind::nonnegative_integer }]] std::int32_t BeginningNumberOfHandymen{};
    [[=sam::Field{ "BeginningNumberOfMechanics",
                   sam::Kind::nonnegative_integer }]] std::int32_t BeginningNumberOfMechanics{};
    [[=sam::Field{ "BeginningNumberOfEntertainers",
                   sam::Kind::nonnegative_integer }]] std::int32_t BeginningNumberOfEntertainers{};
    [[=sam::Field{ "BeginningNumberOfGuards", sam::Kind::nonnegative_integer }]] std::int32_t BeginningNumberOfGuards{};
    [[=sam::Field{ "BeginningNumberOfResearchers",
                   sam::Kind::nonnegative_integer }]] std::int32_t BeginningNumberOfResearchers{};
    [[=sam::Field{ "MaxHandymen", sam::Kind::nonnegative_integer }]] std::int32_t MaxHandymen{};
    [[=sam::Field{ "MaxMechanics", sam::Kind::nonnegative_integer }]] std::int32_t MaxMechanics{};
    [[=sam::Field{ "MaxEntertainers", sam::Kind::nonnegative_integer }]] std::int32_t MaxEntertainers{};
    [[=sam::Field{ "MaxGuards", sam::Kind::nonnegative_integer }]] std::int32_t MaxGuards{};
    [[=sam::Field{ "MaxResearchers", sam::Kind::nonnegative_integer }]] std::int32_t MaxResearchers{};
    [[=sam::Field{ "MaxGardeners", sam::Kind::nonnegative_integer }]] std::int32_t MaxGardeners{};
    [[=sam::Field{ "MinHandymenInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinHandymenInPool{};
    [[=sam::Field{ "MinMechanicsInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinMechanicsInPool{};
    [[=sam::Field{ "MinEntertainersInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinEntertainersInPool{};
    [[=sam::Field{ "MinGuardsInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinGuardsInPool{};
    [[=sam::Field{ "MinResearchersInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinResearchersInPool{};
    [[=sam::Field{ "MinGardenersInPool", sam::Kind::nonnegative_integer }]] std::int32_t MinGardenersInPool{};
    [[=sam::Field{ "MaxHandymenInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxHandymenInPark{};
    [[=sam::Field{ "MaxMechanicsInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxMechanicsInPark{};
    [[=sam::Field{ "MaxEntertainersInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxEntertainersInPark{};
    [[=sam::Field{ "MaxGuardsInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxGuardsInPark{};
    [[=sam::Field{ "MaxResearchersInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxResearchersInPark{};
    [[=sam::Field{ "MaxGardenersInPark", sam::Kind::nonnegative_integer }]] std::int32_t MaxGardenersInPark{};
    [[=sam::Field{ "BaseCostPerStaff", sam::Kind::nonnegative_integer }]] std::int32_t BaseCostPerStaff{};
    [[=sam::Field{ "CostPerQualityLevel", sam::Kind::nonnegative_integer }]] std::int32_t CostPerQualityLevel{};
    [[=sam::Field{ "TimeBetweenStaffUpdates", sam::Kind::nonnegative_integer }]] std::int32_t TimeBetweenStaffUpdates{};
    [[=sam::Field{ "MaxNumberOfStaffPerUpdate",
                   sam::Kind::nonnegative_integer }]] std::int32_t MaxNumberOfStaffPerUpdate{};
    [[=sam::Field{ "StaffTimeoutTime", sam::Kind::nonnegative_integer }]] std::int32_t StaffTimeoutTime{};
    [[=sam::Field{ "MinExperience", sam::Kind::nonnegative_integer }]] std::int32_t MinExperience{};
    [[=sam::Field{ "AvgWageDifference", sam::Kind::nonnegative_integer }]] std::int32_t AvgWageDifference{};
};

struct AllStaffParameters {
    [[=sam::Field{ "RestLevel", 0, 100 }]] std::int32_t RestLevel{};
    [[=sam::Field{ "HappyHitCosNoRestArea", sam::Kind::nonnegative_integer }]] std::int32_t HappyHitCosNoRestArea{};
    [[=sam::Field{ "HappyHitCosRoomInaccessible",
                   sam::Kind::nonnegative_integer }]] std::int32_t HappyHitCosRoomInaccessible{};
    [[=sam::Field{ "MaxExperience", sam::Kind::integer }]] std::int32_t MaxExperience{};
    [[=sam::Field{ "UnHappyWageVal", sam::Kind::integer }]] std::int32_t UnHappyWageVal{};
    [[=sam::Field{ "HappyWageVal", sam::Kind::integer }]] std::int32_t HappyWageVal{};
    [[=sam::Field{ "LowestWageHappiness", sam::Kind::integer }]] std::int32_t LowestWageHappiness{};
    [[=sam::Field{ "MinRecuperationRate", sam::Kind::scalar }]] float MinRecuperationRate{};
    [[=sam::Field{ "MaxRecuperationRate", sam::Kind::scalar }]] float MaxRecuperationRate{};
    [[=sam::Field{ "MinHappinessRecuperationRate", sam::Kind::scalar }]] float MinHappinessRecuperationRate{};
    [[=sam::Field{ "MaxHappinessRecuperationRate", sam::Kind::scalar }]] float MaxHappinessRecuperationRate{};
    [[=sam::Field{ "MinWage", sam::Kind::integer }]] std::int32_t MinWage{};
    [[=sam::Field{ "MaxWage", sam::Kind::integer }]] std::int32_t MaxWage{};
    [[=sam::Field{ "WageStep", sam::Kind::integer }]] std::int32_t WageStep{};
    [[=sam::Field{ "PercentageForBadWageDecision", sam::Kind::integer }]] std::int32_t PercentageForBadWageDecision{};
    [[=sam::Field{ "MotivationFactor", sam::Kind::integer }]] std::int32_t MotivationFactor{};
    [[=sam::Field{ "StaffSlackingEnergy", sam::Kind::integer }]] std::int32_t StaffSlackingEnergy{};
    [[=sam::Field{ "MinDistanceForSlacking", sam::Kind::integer }]] std::int32_t MinDistanceForSlacking{};
    [[=sam::Field{ "StaffRoomCapacity", sam::Kind::integer }]] std::int32_t StaffRoomCapacity{};
    [[=sam::Field{ "MaxStaffPerStaffRoom", sam::Kind::integer }]] std::int32_t MaxStaffPerStaffRoom{};
    [[=sam::Field{ "MinStaffPerStaffRoom", sam::Kind::integer }]] std::int32_t MinStaffPerStaffRoom{};
    [[=sam::Field{ "MaxSpeedDifference", sam::Kind::integer }]] std::int32_t MaxSpeedDifference{};
    [[=sam::Field{ "TrainingExtraExperience", sam::Kind::integer }]] std::int32_t TrainingExtraExperience{};
    [[=sam::Field{ "MinExperienceForTraining", sam::Kind::integer }]] std::int32_t MinExperienceForTraining{};
    [[=sam::Field{ "SkillIncrement", sam::Kind::integer }]] std::int32_t SkillIncrement{};
    [[=sam::Field{ "ProgessUpperLimit", sam::Kind::integer }]] std::int32_t ProgessUpperLimit{};
};

struct StaffRoleDetectionParameters {
    [[=sam::Field{ "MinWorkDuration", sam::Kind::integer }]] std::int32_t MinWorkDuration{};
    [[=sam::Field{ "MaxWorkDuration", sam::Kind::integer }]] std::int32_t MaxWorkDuration{};
    [[=sam::Field{ "MinDetectionRange", sam::Kind::integer }]] std::int32_t MinDetectionRange{};
    [[=sam::Field{ "MaxDetectionRange", sam::Kind::integer }]] std::int32_t MaxDetectionRange{};
    [[=sam::Field{ "MaxWage", sam::Kind::integer }]] std::int32_t MaxWage{};
};

struct StaffEntertainerParameters {
    [[=sam::Field{ "MinWorkDuration", sam::Kind::integer }]] std::int32_t MinWorkDuration{};
    [[=sam::Field{ "MaxWorkDuration", sam::Kind::integer }]] std::int32_t MaxWorkDuration{};
    [[=sam::Field{ "MinActiveRange", sam::Kind::integer }]] std::int32_t MinActiveRange{};
    [[=sam::Field{ "MaxActiveRange", sam::Kind::integer }]] std::int32_t MaxActiveRange{};
    [[=sam::Field{ "MaxWage", sam::Kind::integer }]] std::int32_t MaxWage{};
};

struct StaffResearcherParameters {
    [[=sam::Field{ "MinWorkDuration", sam::Kind::integer }]] std::int32_t MinWorkDuration{};
    [[=sam::Field{ "MaxWorkDuration", sam::Kind::integer }]] std::int32_t MaxWorkDuration{};
    [[=sam::Field{ "MinResearchAbility", sam::Kind::integer }]] std::int32_t MinResearchAbility{};
    [[=sam::Field{ "MaxResearchAbility", sam::Kind::integer }]] std::int32_t MaxResearchAbility{};
    [[=sam::Field{ "MaxWage", sam::Kind::integer }]] std::int32_t MaxWage{};
};

struct StaffMechanicParameters {
    [[=sam::Field{ "MinWorkDuration", sam::Kind::integer }]] std::int32_t MinWorkDuration{};
    [[=sam::Field{ "MaxWorkDuration", sam::Kind::integer }]] std::int32_t MaxWorkDuration{};
    [[=sam::Field{ "MaxWage", sam::Kind::integer }]] std::int32_t MaxWage{};
};

struct StaffStrikeParameters {
    [[=sam::Field{ "MinMonthsForStrike", sam::Kind::integer }]] std::int32_t MinMonthsForStrike{};
    [[=sam::Field{ "MinStaffOfTypeForStrike", sam::Kind::integer }]] std::int32_t MinStaffOfTypeForStrike{};
    [[=sam::Field{ "EnergyPercentToStrike", sam::Kind::integer }]] std::int32_t EnergyPercentToStrike{};
    [[=sam::Field{ "HappyPercentToStrike", sam::Kind::integer }]] std::int32_t HappyPercentToStrike{};
    [[=sam::Field{ "MotivationPercentToStrike", sam::Kind::integer }]] std::int32_t MotivationPercentToStrike{};
};

struct StaffReleasePoint {
    [[=sam::Field{ "PosX", sam::Kind::nonnegative_integer }]] std::int32_t PosX{};
    [[=sam::Field{ "PosY", sam::Kind::nonnegative_integer }]] std::int32_t PosY{};
};

struct StaffAgencyParameters {
    [[=sam::Field{ "RequiredPromotionLevel", sam::Kind::integer }]] std::int32_t RequiredPromotionLevel{};
    [[=sam::Field{ "MotivationGainModifier", sam::Kind::integer }]] std::int32_t MotivationGainModifier{};
    [[=sam::Field{ "MotivationLossModifier", sam::Kind::integer }]] std::int32_t MotivationLossModifier{};
    [[=sam::Field{ "RecuperationModifier", sam::Kind::integer }]] std::int32_t RecuperationModifier{};
    [[=sam::Field{ "HappinessRecuperationModifier", sam::Kind::integer }]] std::int32_t HappinessRecuperationModifier{};
    [[=sam::Field{ "InitialWageModifier", sam::Kind::integer }]] std::int32_t InitialWageModifier{};
    [[=sam::Field{ "InitialExperienceModifier", sam::Kind::integer }]] std::int32_t InitialExperienceModifier{};
    [[=sam::Field{ "ExperienceGainModifier", sam::Kind::integer }]] std::int32_t ExperienceGainModifier{};
    [[=sam::Field{ "TrainingExperienceGainModifier",
                   sam::Kind::integer }]] std::int32_t TrainingExperienceGainModifier{};
};

struct StaffGradeExperienceParameters {
    [[=sam::Field{ "ExperiencePerMonth", sam::Kind::nonnegative_integer }]] std::int32_t ExperiencePerMonth{};
};

struct EntertainerGradeParameters {
    [[=sam::Field{ "HappinessEffectOnCell", sam::Kind::nonnegative_integer }]] std::int32_t HappinessEffectOnCell{};
    [[=sam::Field{ "ExperiencePerMonth", sam::Kind::nonnegative_integer }]] std::int32_t ExperiencePerMonth{};
};

struct ResearcherGradeParameters {
    [[=sam::Field{ "WorkDuration", sam::Kind::nonnegative_integer }]] std::int32_t WorkDuration{};
    [[=sam::Field{ "ExperiencePerMonth", sam::Kind::nonnegative_integer }]] std::int32_t ExperiencePerMonth{};
    [[=sam::Field{ "NoRRoomHappyHit", sam::Kind::nonnegative_integer }]] std::int32_t NoRRoomHappyHit{};
};

struct StaffRecruitmentBalance {
    [[=sam::Rows{ "KidRelease" }]] std::array<StaffReleasePoint, 2> KidRelease;
    [[=sam::Extent{ "KidRelease" }]] std::uint32_t kid_release_count{};
    [[=sam::Group{ "Strike" }]] StaffStrikeParameters Strike;
    [[=sam::Group{ "StaffPoolInfo" }]] StaffPoolParameters pool;
    [[=sam::Rows{ "StaffAgencies" }]] std::array<StaffAgencyParameters, 4> agencies;
    [[=sam::Extent{ "StaffAgencies" }]] std::uint32_t agency_count{};
    [[=sam::Group{ "AllStaffConstants" }]] AllStaffParameters all;
    [[=sam::Group{ "Handyman" }]] StaffRoleDetectionParameters Handyman;
    [[=sam::Group{ "Mechanic" }]] StaffMechanicParameters Mechanic;
    [[=sam::Group{ "Entertainer" }]] StaffEntertainerParameters Entertainer;
    [[=sam::Group{ "Guard" }]] StaffRoleDetectionParameters Guard;
    [[=sam::Group{ "Researcher" }]] StaffResearcherParameters Researcher;
    [[=sam::Group{ "Gardener" }]] StaffRoleDetectionParameters Gardener;
    [[=sam::Rows{ "HandymanConstsPerGrade" }]] std::array<StaffGradeExperienceParameters, 5> handyman_grades;
    [[=sam::Extent{ "HandymanConstsPerGrade" }]] std::uint32_t handyman_grade_count{};
    [[=sam::Rows{ "MechanicConstsPerGrade" }]] std::array<StaffGradeExperienceParameters, 5> mechanic_grades;
    [[=sam::Extent{ "MechanicConstsPerGrade" }]] std::uint32_t mechanic_grade_count{};
    [[=sam::Rows{ "EntertainerConstsPerGrade" }]] std::array<EntertainerGradeParameters, 5> entertainer_grades;
    [[=sam::Extent{ "EntertainerConstsPerGrade" }]] std::uint32_t entertainer_grade_count{};
    [[=sam::Rows{ "GuardConstsPerGrade" }]] std::array<StaffGradeExperienceParameters, 5> guard_grades;
    [[=sam::Extent{ "GuardConstsPerGrade" }]] std::uint32_t guard_grade_count{};
    [[=sam::Rows{ "ResearcherConstsPerGrade" }]] std::array<ResearcherGradeParameters, 5> researcher_grades;
    [[=sam::Extent{ "ResearcherConstsPerGrade" }]] std::uint32_t researcher_grade_count{};
};

struct StaffRecruitmentRuntime {
    StaffRecruitmentBalance balance{};
    std::array<std::vector<std::u16string>, 6> names;
    std::int32_t selected_candidate = -1;
    std::uint32_t changed_slots{}; //Host view-model notifications for the hiring list.
};
} //namespace simcoaster
