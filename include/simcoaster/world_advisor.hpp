#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
//Native554CB0 acknowledges a valid context-menu request for future action495
//scoring. This does not cancel an already queued Advisor message.
void mark_advisor_context_menu_used(AdvisorWorld&);
//Native503A40: explicit Help bypasses response history, retaining policy gates.
void queue_world_advisor_help_action(AdvisorWorld&, std::int32_t action);
bool can_queue_advisor_action_candidate(AdvisorWorld&, const AdvisorRecord&, std::int32_t action, bool bypass);
bool queue_advisor_action_candidate(AdvisorWorld&, AdvisorRecord&, const AdvisorQueueEntry&);
bool cancel_advisor_action_by_id(AdvisorWorld&, AdvisorRecord&, std::int32_t action);
void stop_advisor_speech_response(AdvisorWorld&);
enum class AdvisorStopTargetAction {
    parameter_request,
    recycle
};
//Native716BF0: UI interruption, without the completion-only idle vocal.
void stop_active_advisor_response(AdvisorWorld&, AdvisorStopTargetAction);
std::int32_t select_runtime_behavior_candidate(AdvisorWorld&, AdvisorRecord&, std::int32_t steps);
int find_highest_score_queued_candidate(AdvisorWorld&, const AdvisorRecord&);
std::int32_t select_advisor_response_variant(AdvisorWorld&, const AdvisorQueueEntry&);
std::int32_t choose_advisor_response_variant(World&, std::int32_t action, std::int32_t count, AdvisorVariantMode, std::int32_t previous);
bool play_queued_advisor_response(AdvisorWorld&, AdvisorQueueEntry&, std::int32_t variant, std::uint32_t& duration);
void update_advisor_action_queue(AdvisorWorld&, AdvisorRecord&);
void receive_world_advisor_message(AdvisorWorld&, AdvisorRecord&, const WorldEvent&);
std::int32_t count_people_waiting_at_ticket_booths(AdvisorWorld&);
std::optional<std::int32_t> evaluate_advisor_staff_and_facility_score(AdvisorWorld&, std::int32_t action);
std::optional<std::int32_t> evaluate_advisor_trade_score(AdvisorWorld&, std::int32_t action);
std::optional<std::int32_t> evaluate_advisor_staff_decision_score(AdvisorWorld&, std::int32_t action);
std::optional<std::int32_t> evaluate_advisor_park_runtime_score(AdvisorWorld&, std::int32_t action);
std::optional<std::int32_t> evaluate_advisor_pricing_score(AdvisorWorld&, std::int32_t action);
} //namespace simcoaster
