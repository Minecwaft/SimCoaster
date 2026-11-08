#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
enum class ChallengeMetric {
    eligible_population,
    balloon_holders,
    costume_wearers,
    minimum_staff_happiness,
    maximum_upgrade_level,
    recent_net_profit,
    sideshow_satisfaction,
    visitor_happiness,
    any_staff_strike,
    thought_in_top_three,
    minimum_ride_repair,
    maximum_vertical_special_count,
    has_ingredient,
    has_shop_type,
    balloons_sold,
    costumes_sold,
    lifetime_visitors,
    misbehaving_kids_ever,
    staff_count,
    completed_research_count,
    preferred_research_matches,
    breakdowns_ever,
    maximum_staff_experience
};
std::int32_t read_challenge_metric(MissionWorld&, ChallengeMetric, std::int32_t argument = 0, ThingId* focus = nullptr);
std::int32_t calculate_park_object_customer_satisfaction(const ParkObjectRuntimeView&);
std::int32_t sum_recent_park_object_uses(const ParkObjectRuntimeView&);
std::int32_t sum_recent_park_object_walkaways(const ParkObjectRuntimeView&);
void initialize_challenge_record(ChallengeRecord&, const ChallengeDefinitionParameters&);
ChallengeManagerRecord* initialize_challenge_manager(MissionWorld&, ChallengeManagerRecord*);
void load_challenge_records(ChallengeManagerRecord&, const ChallengeDefinitions&, const ChallengeBalance&);
void validate_challenge_followup_types(ChallengeManagerRecord&);
std::int64_t get_challenge_remaining_days(MissionWorld&, const ChallengeRecord&);
bool DidThisWinIt(MissionWorld&, const ChallengeRecord&, std::uint8_t evaluation_mode);
bool state3_challenge_failure_predicate(const MissionQuery&, const ChallengeRecord&);
void UpdateCurrentValue(MissionWorld&, ChallengeRecord&);
void CalcAvailTime(MissionWorld&, ChallengeRecord&);
void begin_challenge_after_offer_period(MissionWorld&, ChallengeRecord&);
void accept_world_challenge(MissionWorld&, ChallengeManagerRecord&, std::size_t slot);
void process_challenge_success(MissionWorld&, ChallengeRecord&);
void process_challenge_failure(MissionWorld&, ChallengeRecord&);
void EndChallenge(MissionWorld&, ChallengeManagerRecord&, std::size_t slot);
void offer_next_available_challenge(MissionWorld&, ChallengeManagerRecord&);
void award_world_shares(MissionWorld&, std::int32_t);
void adjust_world_share_price(MissionWorld&, std::int32_t percentage);
void update_challenge_manager(MissionWorld&, ChallengeManagerRecord&);
void handle_world_challenge_event(ChallengeManagerRecord&, const WorldEvent&);
} //namespace simcoaster
