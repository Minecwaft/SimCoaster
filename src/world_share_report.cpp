#include "simcoaster/numeric_word.hpp"
#include "simcoaster/calendar_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
std::int32_t divide(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "annual report division");
}

std::int32_t valid_count(const AnalyserDwordHistory144& ring) {
    return ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1);
}

double sum(const AnalyserDwordHistory144& ring, std::int32_t maximum) {
    const auto count = std::min(maximum, valid_count(ring));
    double result = 0;
    for (int offset = 0; offset < count; ++offset) {
        if (offset >= ring.num_entries || (offset > ring.current_entry && !ring.wrapped))
            continue;
        const auto index = offset <= ring.current_entry
                             ? fixed::subtract(ring.current_entry, offset)
                             : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), offset);
        result += fixed::word(ring.data.at(static_cast<std::size_t>(index)));
    }
    return result;
}

std::int32_t percent_change(std::int32_t now, std::int32_t prior) {
    if (!prior)
        return now > 0 ? 100 : now < 0 ? -100 : 0;
    if (now == prior)
        return 0;
    return divide(numeric::product(100, fixed::subtract(now, prior)), fixed::absolute(prior));
}
} //namespace

std::int32_t score_share_report_trend(const ShareFinanceSavedRecord& report) {
    const auto& delta = report.year_over_year;
    const auto sign = [](auto value) {
        return (value > 0) - (value < 0);
    };
    return std::clamp(sign(delta.income_change)
                          + sign(delta.profit_change)
                          + sign(delta.park_value_change)
                          + sign(delta.visitor_change)
                          + sign(delta.rating_difference),
                      -3,
                      3);
}

std::int32_t select_share_report_summary(const AnnualReportPolicy& p, std::int32_t primary, std::int32_t secondary) {
    if (!primary && !secondary)
        return 281;
    if (primary < fixed::negate(p.similar_percentage) || primary > p.similar_percentage)
        return primary < 0 ? (secondary < 0 ? 289 : 287) : (secondary < 0 ? 286 : 288);
    return secondary < 0 ? 284 + (secondary <= fixed::negate(p.big_change_percentage))
                         : 282 + (secondary >= p.big_change_percentage);
}

void
world_operations::builtin::send_annual_shares_report(WorldRuntime& c, std::uint32_t year, int source, int maximum) {
    if (!c.advisor.presentation.mode_initialized) {
        c.advisor.presentation.mode_initialized = true;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3 || source > 2 || year < 2000)
        return;
    if (source < 0)
        throw std::out_of_range("negative annual report theme ordinal");
    auto& slot = c.finance.shares_finance.record_slots.at(static_cast<std::size_t>(source));
    if (slot && year <= slot->report_year)
        return;
    auto prior = std::move(slot);
    slot = std::make_unique<ShareFinanceSavedRecord>();
    auto& report = *slot;
    report.report_year = year;
    const auto* w = c.referenced_world();
    if (!w)
        throw std::logic_error("annual report primary World");
    auto* payload = c.entities.at(w->saved.park_analyser).payload;
    if (!payload)
        throw std::logic_error("annual report analyser");
    const auto& a = *reinterpret_cast<ParkAnalyserState*>(payload);
    report.history_sum_a = sum(a.long_histories[6], maximum);
    report.history_difference = fixed::truncate_integer(report.history_sum_a - sum(a.long_histories[3], maximum));
    report.history_sum_b = sum(a.long_histories[1], maximum);
    report.history_sum = fixed::truncate_integer(sum(a.long_histories[15], maximum));
    const auto count = std::min(maximum, valid_count(a.long_histories[20]));
    report.history_average = count > 0 ? fixed::truncate_integer(sum(a.long_histories[20], count) / count) : 0;
    auto body = 263, summary = 280, trend = 0;
    if (prior) {
        auto& delta = report.year_over_year;
        delta.income_change = percent_change(fixed::truncate_integer(report.history_sum_a),
                                             fixed::truncate_integer(prior->history_sum_a));
        delta.profit_change = percent_change(report.history_difference, prior->history_difference);
        delta.park_value_change = percent_change(fixed::truncate_integer(report.history_sum_b),
                                                 fixed::truncate_integer(prior->history_sum_b));
        delta.visitor_change = percent_change(report.history_sum, prior->history_sum);
        delta.rating_difference = fixed::subtract(report.history_average, prior->history_average);
        prior.reset();
        trend = score_share_report_trend(report);
        auto* price = c.finance.shares_finance.share_price_context.get();
        if (!price)
            throw std::logic_error("annual report share-price context");
        if (price->current_share_price && trend) {
            const auto next = fixed::add(price->current_share_price, divide(numeric::product(trend, price->current_share_price), 100));
            if (next > 0)
                price->current_share_price = next;
            world_operations::mark_shares_panel_refresh(c);
        }
        body = 267 - trend;
        summary = select_share_report_summary(c.finance.annual_report_policy,
                                              fixed::truncate_integer(delta.income_change),
                                              delta.profit_change);
    }
    dispatch_share_report_email(c, report, body, summary, trend);
    const auto candidate = world_operations::make_advisor_action_candidate(c, 458);
    auto* advisor = c.entities.at(w->saved.advisor).payload;
    if (!advisor)
        throw std::logic_error("annual report Advisor");
    world_operations::queue_advisor_action_candidate(c, reinterpret_cast<AdvisorRecord*>(advisor), candidate);
}
} //namespace simcoaster
