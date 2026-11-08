#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct ChallengeDefinitionParameters {
    [[=sam::Field{ "Type", sam::Kind::integer }]] std::int32_t Type{};
    [[=sam::Field{ "FollowupType", sam::Kind::integer }]] std::int32_t FollowupType{};
    [[=sam::Field{ "TargetTime", sam::Kind::integer }]] std::int32_t TargetTime{};
    [[=sam::Field{ "TargetVal", sam::Kind::integer }]] std::int32_t TargetVal{};
    [[=sam::Field{ "TargetVal2", sam::Kind::integer }]] std::int32_t TargetVal2{};
    [[=sam::Field{ "TargetObj", sam::Kind::integer }]] std::int32_t TargetObj{};
    [[=sam::Field{ "TargetObj2", sam::Kind::integer }]] std::int32_t TargetObj2{};
    [[=sam::Field{ "TargetStaffType", sam::Kind::integer }]] std::int32_t TargetStaffType{};
    [[=sam::Field{ "CashPrize", sam::Kind::integer }]] std::int32_t CashPrize{};
    [[=sam::Field{ "GTPrize", sam::Kind::integer }]] std::int32_t GTPrize{};
    [[=sam::Field{ "SharePrize", sam::Kind::integer }]] std::int32_t SharePrize{};
    [[=sam::Field{ "SharePriceEffectWon", sam::Kind::integer }]] std::int32_t SharePriceEffectWon{};
    [[=sam::Field{ "SharePriceEffectLost", sam::Kind::integer }]] std::int32_t SharePriceEffectLost{};
    [[=sam::Field{ "CheckAtEndOnly", sam::Kind::integer }]] std::int32_t CheckAtEndOnly{};
    [[=sam::Field{ "Independent", sam::Kind::integer }]] std::int32_t Independent{};
    [[=sam::Field{ "MinDaysTilTrigger", sam::Kind::integer }]] std::int32_t MinDaysTilTrigger{};
    [[=sam::Field{ "MaxDaysTilTrigger", sam::Kind::integer }]] std::int32_t MaxDaysTilTrigger{};
    [[=sam::Field{ "DaysDelay", sam::Kind::integer }]] std::int32_t DaysDelay{};
    [[=sam::Field{ "TriggerData", sam::Kind::integer }]] std::int32_t TriggerData{};
    [[=sam::Field{ "GracePeriodLimit", sam::Kind::integer }]] std::int32_t GracePeriodLimit{};
    [[=sam::Field{ "GraceTargetVal", sam::Kind::integer }]] std::int32_t GraceTargetVal{};
    [[=sam::Field{ "Sender", sam::Kind::integer }]] std::int32_t Sender{};
    [[=sam::Field{ "ScriptIndex", sam::Kind::integer }]] std::int32_t ScriptIndex{};
    [[=sam::Field{ "MissionNeededToActivate", sam::Kind::integer }]] std::int32_t MissionNeededToActivate{};
    [[=sam::Field{ "PromoGiven", sam::Kind::integer }]] std::int32_t PromoGiven{};
};

struct ChallengeDefinitions {
    [[=sam::Rows{ "Challenges" }]] std::array<ChallengeDefinitionParameters, 60> rows;
    [[=sam::Extent{ "Challenges" }]] std::uint32_t count{};
};

struct ChallengeScheduleParameters {
    [[=sam::Field{ "DaysBetweenOffers", sam::Kind::integer }]] std::int32_t DaysBetweenOffers{};
    [[=sam::Field{ "MissionNumberForFirstChallenge",
                   sam::Kind::integer }]] std::int32_t MissionNumberForFirstChallenge{};
    [[=sam::Field{ "ShortTimeLeftWarningAt", sam::Kind::integer }]] std::int32_t ShortTimeLeftWarningAt{};
};

struct LevelChallengeParameters {
    [[=sam::Field{ "ChallengeType", sam::Kind::integer }]] std::int32_t ChallengeType{};
};

struct RankFailureParameters {
    [[=sam::Field{ "Limit", sam::Kind::integer }]] std::int32_t limit{};
};

struct ChallengeBalance {
    [[=sam::Rows{ "FiringLineForRank" }]] std::array<RankFailureParameters, 6> rank_failures{};
    [[=sam::Extent{ "FiringLineForRank" }]] std::uint32_t rank_failure_count{};
    [[=sam::Group{ "Challenges" }]] ChallengeScheduleParameters schedule;
    [[=sam::Rows{ "ChallengesInThisLevel" }]] std::array<LevelChallengeParameters, 20> level_rows;
    [[=sam::Extent{ "ChallengesInThisLevel" }]] std::uint32_t level_row_count{};
};
} //namespace simcoaster
