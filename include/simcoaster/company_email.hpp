#pragma once
#include "world_domains_fwd.hpp"
#include <array>
#include <cstdint>

namespace simcoaster {
struct WorldRuntime;
struct TagTableEntry;
struct StaffRuntimeRecord;
struct ShareFinanceSavedRecord;
enum class SpamCategory : std::int32_t;
//These are the native constructor's message-kind codes. The historical
//GameOverEmailReason alias also serves the two already compiled bank paths.
enum class CompanyEmailMessageKind : std::int32_t {
    spam = 2,
    annual_report = 3,
    objective_complete = 4,
    initial_mission = 5,
    end_game = 6,
    failed_challenges = 7,
    bankruptcy = 8,
    staff_wage_demand = 10,
    staff_wage_variant = 11,
    staff_training_complete = 12,
    staff_training_cancelled = 13,
    promotion = 14,
    debug_promotion = 15
};
using GameOverEmailReason = CompanyEmailMessageKind;
enum class CompanyEmailClass {
    tag_system,
    game_over,
    objective_complete,
    promotion,
    staff,
    spam,
    annual_report,
    debug_promotion
};

struct TagMessageVariant {
    std::int32_t variant_id {}, category_resource_id {}, secondary_resource_id {};
    std::uint16_t variant_code {};
    std::uint8_t available {};
};

inline constexpr std::array<TagMessageVariant, 9> default_tag_system_message_variants { { { 0, 191, 191, 0, 0 }, { 1, 173, 174, 1, 0 }, { 2, 177, 178, 2, 1 }, { 3, 179, 180, 3, 1 }, { 4, 181, 182, 4, 1 }, { 5, 183, 184, 5, 0 }, { 6, 185, 186, 6, 1 }, { 7, 175, 176, 7, 0 }, { 8, 192, 192, 8, 0 } } };

//Natural shared runtime model of the verified concrete CompanyEmail
//classes. concrete_class represents their different native vtables. This is
//not a wire image or an assertion that every CompanyEmail subclass is alike.
struct CompanyEmailMessage {
    CompanyEmailClass concrete_class {};
    TagMessageVariant* selected_variant {};
    CompanyEmailMessageKind reason {}; //Native message-kind code, including5.
    std::int32_t context_mode {};
    std::uint32_t counter_slot {};
    std::int32_t default_resource {};
    std::uint8_t append_signature {};
    std::int32_t tag_id {};
    TagTableEntry* tag_entry {}; //Borrowed; meaningful only for tag-backed mail.
    std::int32_t objective_text_row_index {};
    std::int32_t promotion_rank {}, share_prize {};
    StaffRuntimeRecord* staff_record {};
    std::uint32_t staff_argument {};
    std::int32_t spam_resource {};
    SpamCategory spam_category {};
    const ShareFinanceSavedRecord* annual_report {}; //Borrowed during synchronous event delivery.
    std::int32_t report_body {}, report_summary {}, report_trend {};
};

using GameOverCompanyEmail = CompanyEmailMessage;

CompanyEmailMessage* construct_tag_system_company_email(MissionWorld&, CompanyEmailMessage*, std::int32_t tag_id, std::int32_t message_kind, std::int32_t context_mode);
GameOverCompanyEmail* construct_game_over_company_email(MissionWorld&, GameOverCompanyEmail*, GameOverEmailReason);
bool dispatch_initial_mission_event_once(MissionWorld&);
} //namespace simcoaster
