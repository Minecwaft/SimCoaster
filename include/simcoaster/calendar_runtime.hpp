#pragma once
#include "world_domains_fwd.hpp"
#include "sam_record.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace simcoaster {
struct WorldRuntime;
struct CompanyEmailMessage;
struct MailMessage;
struct ShareFinanceSavedRecord;
enum class SpamCategory : std::int32_t {
    joke = 0,
    joke_response = 1,
    instruction = 2,
    controversy = 3,
    urban_legend = 4,
    brain_teaser = 5
};

struct SpamBalance {
    [[=sam::Field { "SpamEmailsPerYear", sam::Kind::integer }]] std::int32_t SpamEmailsPerYear {};
    [[=sam::Field { "YearsBeforeStarting", sam::Kind::integer }]] std::int32_t YearsBeforeStarting {};
    [[=sam::Field { "TreatJokeResponseDifferently", sam::Kind::integer }]] std::int32_t TreatJokeResponseDifferently {};
    [[=sam::Field { "MaxDaysBeforeJokeResponse", sam::Kind::integer }]] std::int32_t MaxDaysBeforeJokeResponse {};
    [[=sam::Field { "MinDaysBeforeJokeResponse", sam::Kind::integer }]] std::int32_t MinDaysBeforeJokeResponse {};
    [[=sam::Field { "JokeWeighting", sam::Kind::integer }]] std::int32_t JokeWeighting {};
    [[=sam::Field { "StupidInstructionWeighting", sam::Kind::integer }]] std::int32_t StupidInstructionWeighting {};
    [[=sam::Field { "DoorsControversyWeighting", sam::Kind::integer }]] std::int32_t DoorsControversyWeighting {};
    [[=sam::Field { "UrbanLegendWeighting", sam::Kind::integer }]] std::int32_t UrbanLegendWeighting {};
    [[=sam::Field { "BrainTeaserWeighting", sam::Kind::integer }]] std::int32_t BrainTeaserWeighting {};
};

struct SpamScheduleEntry {
    std::int32_t resource {};
    SpamCategory category {};
    bool sent {};
};

struct SpamSchedulerState {
    std::array<SpamScheduleEntry, 17> entries {};
    bool shutdown_disabled {}, enabled {}, initialization_attempted {}, special_text_flag {}, manual_send {};
    std::int32_t current_entry {}, previous_entry {}, schedule_year {}, elapsed_days {}, next_send_day {}, sends_this_year {};
    std::uint32_t year {};
    std::int32_t annual_days = 365, first_day = 7, regular_interval {}, special_interval {}, total_weight {};
    std::array<std::int32_t, 5> cumulative_weights {};
    std::u16string previous_joke_sender, previous_joke_subject;
    SpamSchedulerState();
};

struct AnnualReportPolicy {
    std::int32_t similar_percentage {}, big_change_percentage {};
};

void initialize_spam_schedule(EconomyWorld&);
std::int32_t choose_next_spam_entry(EconomyWorld&);
std::int32_t select_unsent_spam_entry(EconomyWorld&, SpamCategory);
std::int32_t disable_spam_category(EconomyWorld&, SpamCategory);
void configure_spam_interval(EconomyWorld&, std::int32_t days);
void dispatch_spam_email(EconomyWorld&, std::int32_t resource, SpamCategory);
void dispatch_share_report_email(EconomyWorld&, const ShareFinanceSavedRecord&, std::int32_t body, std::int32_t summary, std::int32_t trend);
void format_calendar_company_notice(EconomyWorld&, MailMessage&, CompanyEmailMessage&);
std::int32_t score_share_report_trend(const ShareFinanceSavedRecord&);
std::int32_t select_share_report_summary(const AnnualReportPolicy&, std::int32_t primary, std::int32_t secondary);
} //namespace simcoaster
