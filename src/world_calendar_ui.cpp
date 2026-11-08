#include "simcoaster/world_calendar_ui.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
template <class T>
T& record(WorldRuntime& c, ThingId id) {
    auto* p = c.entities.at(id).payload;
    if (!p) {
        throw std::logic_error("calendar UI World record");
    }
    return *reinterpret_cast<T*>(p);
}

ParkAnalyserState& analyser(WorldRuntime& c) {
    const auto* w = c.referenced_world();
    if (!w) {
        throw std::logic_error("calendar UI primary World");
    }
    return record<ParkAnalyserState>(c, w->saved.park_analyser);
}

std::u16string wide(std::string_view input) {
    return { input.begin(), input.end() };
}

std::u16string localized(WorldRuntime& c, const std::vector<std::byte>& table, std::uint32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> strings = {}, std::span<const BfstSignedIntegerArgument> integers = {}) {
    auto text = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, capacity, strings, integers);
    if (!text) {
        throw std::runtime_error("calendar UI BFST: " + text.error().reason);
    }
    return std::move(*text);
}

std::int32_t intervals(StatisticsGraphRange range) {
    match (range) {
        case StatisticsGraphRange::one_year     => return 12;
        case StatisticsGraphRange::three_years  => return 36;
        case StatisticsGraphRange::twelve_years => return 144;
        case _                                  => return 0;
    }
}

std::optional<std::int32_t> sample(const AnalyserDwordHistory144& ring, std::int32_t offset) {
    if (offset >= ring.num_entries || (offset > ring.current_entry && !ring.wrapped)) {
        return {};
    }
    const auto index = offset <= ring.current_entry ? fixed::subtract(ring.current_entry, offset) : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), offset);
    return fixed::word(ring.data.at(static_cast<std::size_t>(index)));
}

struct SeriesBinding {
    std::uint32_t mask, history;
};

constexpr std::array financial_series { SeriesBinding { 1, 0 }, SeriesBinding { 2, 1 }, SeriesBinding { 4, 6 }, SeriesBinding { 8, 7 }, SeriesBinding { 0x100, 10 }, SeriesBinding { 0x10, 8 }, SeriesBinding { 0x20, 9 }, SeriesBinding { 0x200, 11 }, SeriesBinding { 0x40, 3 }, SeriesBinding { 0x400, 12 }, SeriesBinding { 0x800, 13 }, SeriesBinding { 0x80, 4 } };
constexpr std::array park_series { SeriesBinding { 2, 17 }, SeriesBinding { 1, 14 }, SeriesBinding { 4, 20 } };

void refresh_graph(WorldRuntime& c, StatisticsGraphUiState& graph, std::span<const SeriesBinding> bindings, bool include_negative) {
    if (!graph.open) {
        return;
    }
    const auto& a = analyser(c);
    graph.lower = graph.upper = 0;
    graph.divisions = 10;
    graph.series.clear();
    const auto limit = intervals(graph.range);
    for (const auto binding : bindings) {
        if (graph.selected_series_mask & binding.mask) {
            const auto& ring = a.long_histories[binding.history];
            for (int offset = 0; offset < ring.num_entries && offset <= limit; ++offset) {
                const auto value = sample(ring, offset);
                if (!value) {
                    break;
                }
                graph.upper = std::max(graph.upper, *value);
                if (include_negative) {
                    graph.lower = std::min(graph.lower, *value);
                }
            }
            auto& output = graph.series.emplace_back(binding.history);
            const auto available = ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1);
            const auto count = std::min(limit, available);
            std::int32_t previous = 0;
            for (auto offset = count; offset >= 0; --offset) {
                if (const auto value = sample(ring, offset)) {
                    previous = *value;
                }
                output.samples.push_back(previous);
            }
        }
    }
    ++graph.revision;
}

std::int32_t latest(const AnalyserDwordHistory144& ring) {
    if (ring.num_entries <= 0 || (ring.current_entry < 0 && !ring.wrapped)) {
        return 0;
    }
    const auto index = ring.current_entry < 0 ? fixed::add(ring.current_entry, ring.num_entries) : ring.current_entry;
    return fixed::word(ring.data.at(static_cast<std::size_t>(index)));
}
} //namespace

void calendar_ui::refresh_calendar_date_label(WorldRuntime& c, CalendarReportFields report) {
    auto& label = c.ui.panels.calendar_header;
    if (!label.present) {
        return;
    }
    if (label.debug_counts) {
        label.text = wide("P:" + std::to_string(fixed::word(c.visitors.count)) + " O:" + std::to_string(fixed::word(c.attractions.park_object_record_count)));
    } else {
        const auto month = localized(c, c.messages.mail_language.months, static_cast<std::uint32_t>(report.zero_based_month), 512);
        const auto year = fixed::add(report.year_since_1900, 1900) % 100;
        auto digits = std::to_string(year);
        if (year >= 0 && year < 10) {
            digits.insert(digits.begin(), '0');
        }
        const auto year_text = wide(digits);
        const std::array strings { BfstWideStringArgument { 42, month }, BfstWideStringArgument { 43, year_text } };
        const std::array integers { BfstSignedIntegerArgument { 41, report.day_of_month } };
        label.text = localized(c, c.messages.mail_language.ui_text, 956, 16383, strings, integers);
        label.color = { 0xE6u, 0xE6u, 0xFFu, 0xFFu };
        label.centered = label.vertically_centered = true;
    }
    ++label.revision;
}

void calendar_ui::refresh_park_statistics_month(WorldRuntime& c) {
    refresh_graph(c, c.ui.panels.park_statistics, park_series, false);
}

void calendar_ui::refresh_financial_statistics_month(WorldRuntime& c) {
    auto& panel = c.ui.panels.financial_statistics;
    if (!panel.graph.open) {
        return;
    }
    refresh_graph(c, panel.graph, financial_series, true);
    panel.income = latest(analyser(c).long_histories[6]);
    panel.expenditure = latest(analyser(c).long_histories[3]);
    panel.balance = fixed::subtract(panel.income, panel.expenditure);
    panel.balance_color = panel.balance < 0 ? std::array<std::uint8_t, 4> { 255, 0, 0, 255 } : std::array<std::uint8_t, 4> { 255, 255, 255, 255 };
}

void rebuild_loan_management_rows(WorldRuntime& c) {
    auto& panel = c.ui.panels.loans;
    panel.rows.clear();
    const auto* w = c.referenced_world();
    if (!w) {
        throw std::logic_error("loan UI primary World");
    }
    for (std::size_t i = 0; i < 8; ++i) {
        BankLoan loan {};
        refresh_loan_offer_availability_and_copy_record(c, &record<BankRecord>(c, w->saved.bank_account), &loan, static_cast<int>(i));
        if (panel.view == LoanView::offers ? !loan.loan_bought && loan.loan_available : panel.view == LoanView::outstanding && loan.loan_bought) {
            LoanManagementRow row;
            row.key = panel.view == LoanView::offers ? static_cast<int>(i) + 1 : -1 - static_cast<int>(i);
            row.lender = localized(c, c.messages.mail_language.lender_names, static_cast<std::uint32_t>(loan.lender_name_index), 16383);
            row.amount = loan.amount_available;
            row.annual_rate = loan.apr_in_percent;
            row.monthly_payment = loan.monthly_repayment;
            row.months = panel.view == LoanView::offers ? loan.repayment_period_in_months : fixed::subtract(loan.repayment_period_in_months, loan.months_repaid);
            row.total_repayment = fixed::word(static_cast<std::uint32_t>(row.months) * static_cast<std::uint32_t>(row.monthly_payment));
            panel.rows.push_back(std::move(row));
        }
    }
    ++panel.revision;
}

void calendar_ui::refresh_outstanding_loans_month(WorldRuntime& c) {
    if (c.ui.panels.loans.open && c.ui.panels.loans.view == LoanView::outstanding) {
        rebuild_loan_management_rows(c);
    }
}
} //namespace simcoaster
