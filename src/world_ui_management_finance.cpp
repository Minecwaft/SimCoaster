#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_ui_management_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char finance_words[]{
#embed "ui_templates/app_finance.bin"
};
constexpr unsigned char loan_words[]{
#embed "ui_templates/app_loans.bin"
};
constexpr unsigned char shares_words[]{
#embed "ui_templates/app_shares.bin"
};

struct FinanceSeries {
    std::uint32_t control, caption, mask, history;
    std::uint16_t part;
    RgbaPixel color;
};

//53C350 color initialization and53D3D0 indicator part order. Repeated
//colors/parts for the final two series are intentional native values.
constexpr std::array financial_series{ FinanceSeries{ 40009, 387, 1, 0, 1, { 255, 255, 0, 255 } },
                                       FinanceSeries{ 40013, 388, 2, 1, 2, { 255, 0, 255, 255 } },
                                       FinanceSeries{ 40019, 389, 4, 6, 3, { 255, 0, 0, 255 } },
                                       FinanceSeries{ 40011, 390, 8, 7, 4, { 0, 0xE0, 255, 255 } },
                                       FinanceSeries{ 40014, 391, 0x100, 10, 5, { 255, 0x80, 0, 255 } },
                                       FinanceSeries{ 40015, 392, 0x10, 8, 6, { 0xC0, 255, 0, 255 } },
                                       FinanceSeries{ 40016, 393, 0x20, 9, 7, { 255, 0x80, 0x80, 255 } },
                                       FinanceSeries{ 40010, 394, 0x200, 11, 8, { 0xA0, 0x40, 255, 255 } },
                                       FinanceSeries{ 40018, 395, 0x40, 3, 9, { 0, 255, 0xC0, 255 } },
                                       FinanceSeries{ 40012, 396, 0x400, 12, 10, { 0, 0x40, 255, 255 } },
                                       FinanceSeries{ 40007, 397, 0x800, 13, 2, { 255, 0, 255, 255 } },
                                       FinanceSeries{ 40017, 398, 0x80, 4, 1, { 255, 255, 0, 255 } } };

std::optional<StatisticsGraphRange> finance_range(std::uint32_t id) {
    match (id) {
        case 40005 or 1611167 => return StatisticsGraphRange::one_year;
        case 40006 or 1611168 => return StatisticsGraphRange::three_years;
        case 40004 or 1611166 => return StatisticsGraphRange::twelve_years;
        case _ => return {};
    }
}
} //namespace

void WorldManagementUiController::State::open_finance() {
    if (page == Page::finance && root)
        return;
    begin(Page::finance);
    root = &parse(finance_words, 40000);
    title(40029, text(386));
    graph_control = &required(root, 40020);
    auto& graph = c.ui.panels.financial_statistics.graph;
    graph.open = true;
    graph_styles.clear();
    for (const auto& series : financial_series) {
        auto& button = required(root, series.control);
        caption(button, text(series.caption), 11);
        button.color = { 0, 0, 0, 255 };
        graph_styles.push_back({ series.history, series.color });
    }
    for (const auto [id, row] :
         std::array<std::pair<std::uint32_t, std::uint32_t>, 3>{ { { 40005, 872 }, { 40006, 873 }, { 40004, 874 } } }) {
        auto& button = required(root, id);
        caption(button, text(row), 11);
        button.color = { 0, 0, 0, 255 };
    }
    ui.set_selected(required(root, 40003),
                    graph.range == StatisticsGraphRange::one_year      ? 40005
                    : graph.range == StatisticsGraphRange::three_years ? 40006
                                                                       : 40004);
    caption(required(root, 40024), text(399), 6, 0);
    required(root, 40024).horizontal_alignment = 1;
    required(root, 40024).text_constraints = { true, true };
    caption(required(root, 40022), text(402), 6, 0);
    caption(required(root, 40025), text(401), 6, 0);
    caption(required(root, 40027), text(400), 6, 0);
    c.ports.ui.refresh_financial_statistics_month(c);
    refresh_finance();
    stage_advisor(159);
    activate();
}

void WorldManagementUiController::State::refresh_finance() {
    if (page != Page::finance || !root)
        return;
    const auto& model = c.ui.panels.financial_statistics;
    for (const auto& series : financial_series) {
        auto& button = required(root, series.control);
        const bool selected = (model.graph.selected_series_mask & series.mask) != 0;
        ui.set_value(button, static_cast<std::int32_t>((button.visual_flags & ~1u) | (selected ? 1u : 0u)));
        ui.set_attachment_part(required(&button, 40008), selected ? series.part : 0);
    }
    scalar(40023, model.balance, true);
    required(root, 40023).color = model.balance_color;
    scalar(40026, model.expenditure, true);
    scalar(40028, model.income, true);
    for (const auto id : { 40023u, 40026u, 40028u })
        required(root, id).horizontal_alignment = 2;
}

void WorldManagementUiController::State::open_loans() {
    if (c.mode.game == GameMode::code_2 || (page == Page::loans && root))
        return;
    begin(Page::loans);
    root = &parse(loan_words, 41000);
    auto& model = c.ui.panels.loans;
    model.open = true;
    list_control = &required(root, 41004);
    constexpr std::array headings{ 407u, 409u, 408u, 410u, 411u, 412u };
    constexpr std::array formats{
        UiListCellFormat::text,           UiListCellFormat::signed_currency, UiListCellFormat::signed_integer,
        UiListCellFormat::signed_integer, UiListCellFormat::signed_currency, UiListCellFormat::signed_currency
    };
    list(*list_control, headings, formats, loan_sort);
    for (const auto [id, row] :
         std::array<std::pair<std::uint32_t, std::uint32_t>, 2>{ { { 41006, 406 }, { 41007, 405 } } }) {
        auto& tab = required(root, id);
        caption(tab, text(row), 6);
        for (std::size_t style = 0; style < 3; ++style)
            tab.button_text_colors[style] = { 255, 255, 255, 255 };
    }
    ui.set_selected(required(root, 41005), model.view == LoanView::offers ? 41006 : 41007);
    ui.set_enabled(required(root, 41002), false);
    rebuild_loan_management_rows(c);
    loan_revision = 0;
    refresh_loans();
    stage_advisor(436);
    activate();
}

void WorldManagementUiController::State::refresh_loans() {
    if (page != Page::loans || !root)
        return;
    const auto& model = c.ui.panels.loans;
    if (loan_revision == model.revision)
        return;
    loan_revision = model.revision;
    const bool offers = model.view == LoanView::offers;
    title(41008, text(offers ? 403 : 404));
    caption(required(list_control, 17), text(offers ? 409 : 413), 8);
    caption(required(list_control, 18), text(offers ? 408 : 414), 8);
    caption(required(list_control, 21), text(offers ? 412 : 415), 8);
    required(root, 41002).help_index = offers ? 98 : 99;
    std::vector<UiListRow> rows;
    for (const auto& row : model.rows)
        rows.push_back(
            { row.key,
              { row.lender, row.amount, row.months, row.annual_rate, row.monthly_payment, row.total_repayment } });
    ui.set_list_rows(*list_control, std::move(rows), true, false);
    ui.set_enabled(required(root, 41002), ui.selected_list_identity(*list_control).has_value());
}

void WorldManagementUiController::State::loan_action(std::int32_t key) {
    if (key == 0 || key > 8 || key < -8)
        return;
    const auto index = key > 0 ? key - 1 : -1 - key;
    BankLoan loan{};
    refresh_loan_offer_availability_and_copy_record(c, &bank(), &loan, index);
    const auto serial = ui.lifetime_serial(root);
    const auto valid = [this, serial] {
        return page == Page::loans && root && ui.lifetime_serial(root) == serial;
    };
    if (key > 0) {
        if (loan.loan_bought || !loan.loan_available)
            return;
        const std::array args{ BfstSignedIntegerArgument{ 18, loan.amount_available } };
        message(text(420), text(421, args), [this, index, valid] {
            if (!valid())
                return;
            apply_new_bank_loan(c, &bank(), index);
            rebuild_loan_management_rows(c);
            refresh_loans();
        });
    } else {
        if (!loan.loan_bought)
            return;
        const auto total = fixed::narrow(static_cast<std::int64_t>(loan.monthly_repayment)
                                         * fixed::subtract(loan.repayment_period_in_months, loan.months_repaid));
        const std::array args{ BfstSignedIntegerArgument{ 18, total } };
        //Native549300 deliberately requires strictly more than the amount.
        if (bank().balance <= total) {
            message(text(418), text(419, args));
            return;
        }
        message(text(416), text(417, args), [this, index, valid] {
            if (!valid())
                return;
            apply_bank_loan_repayment(c, &bank(), index);
            rebuild_loan_management_rows(c);
            refresh_loans();
        });
    }
}

void WorldManagementUiController::State::open_shares() {
    //Native consumes two CRT draws even if an existing panel prevents create.
    RgbaPixel color{ 0, 0, 0, 255 };
    color[static_cast<std::size_t>(next_crt_random(c) % 3)] = 255;
    color[static_cast<std::size_t>(next_crt_random(c) % 3)] = 255;
    if (page == Page::shares && root) {
        graph_styles = { { 2, color } };
        return;
    }
    if (c.mode.game == GameMode::code_3)
        return;
    begin(Page::shares);
    root = &parse(shares_words, 1611161);
    graph_control = &required(root, 1611164);
    ui.set_primary_callback(*graph_control, [this](UiControl& sender, const UiEvent& event) {
        if (!closing && page == Page::shares && event.type == 21) {
            //581850 resets the graph range when its surface is initialized.
            shares_graph.range = StatisticsGraphRange::one_year;
            refresh_shares_graph();
        }
        return ui.secondary(sender, event);
    });
    graph_styles = { { 2, color } };
    shares_graph.open = true;
    shares_graph.range = StatisticsGraphRange::one_year;
    shares_graph.selected_series_mask = 1;
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 7>{ { { 1611167, 872 },
                                                                                          { 1611168, 873 },
                                                                                          { 1611166, 874 },
                                                                                          { 1611175, 466 },
                                                                                          { 1611174, 467 },
                                                                                          { 1611171, 468 },
                                                                                          { 1611177, 471 } } }) {
        auto& control = required(root, id);
        caption(control, text(row), id < 1611170 ? 11 : 6, id < 1611170 ? 1 : 0);
        if (id < 1611170)
            control.color = { 0, 0, 0, 255 };
    }
    ui.set_selected(required(root, 1611165), 1611167);
    refresh_shares();
    stage_advisor(440);
    activate();
}

void WorldManagementUiController::State::refresh_shares_graph() {
    if (!root || page != Page::shares)
        return;
    auto& graph = shares_graph;
    graph.lower = 0;
    graph.upper = 1000;
    graph.divisions = 10;
    graph.series.clear();
    const auto limit = graph.range == StatisticsGraphRange::one_year    ? 12
                     : graph.range == StatisticsGraphRange::three_years ? 36
                                                                        : 144;
    const auto& ring = analyser().long_histories[2];
    const auto sample = [&](std::int32_t offset) -> std::optional<std::int32_t> {
        if (offset >= ring.num_entries || (offset > ring.current_entry && !ring.wrapped))
            return {};
        const auto index = offset <= ring.current_entry
                             ? fixed::subtract(ring.current_entry, offset)
                             : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), offset);
        return fixed::word(ring.data.at(static_cast<std::size_t>(index)));
    };
    for (int i = 0; i < ring.num_entries && i <= limit; ++i) {
        const auto value = sample(i);
        if (!value)
            break;
        graph.lower = std::min(graph.lower, *value);
        graph.upper = std::max(graph.upper, *value);
    }
    auto& series = graph.series.emplace_back(2);
    const auto count = std::min(limit, ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1));
    std::int32_t previous{};
    for (auto offset = count; offset >= 0; --offset) {
        if (auto value = sample(offset))
            previous = *value;
        series.samples.push_back(previous);
    }
    ++graph.revision;
}

void WorldManagementUiController::State::refresh_shares() {
    if (!root || page != Page::shares)
        return;
    c.finance.shares_panel_refresh_pending = false;
    refresh_shares_graph();
    const auto& ownership = c.finance.shares_finance.ownership_context;
    const auto& price = c.finance.shares_finance.share_price_context;
    if (!ownership || !price)
        throw std::logic_error("Shares UI finance contexts absent");
    auto& theme = get_or_create_theme_global_info_manager(c);
    const auto promotion =
        localized_text(c.messages.mail_language.promotions, static_cast<std::uint32_t>(theme.current_promotion_index));
    const std::array args{ BfstWideStringArgument{ 0, ownership->current_profile_name },
                           BfstWideStringArgument{ 12, promotion } };
    auto details = text(469, {}, args);
    //581F22/896A30: formatter capacity 254 permits 253 text code units plus NUL.
    details.resize(std::min<std::size_t>(details.size(), 253));
    caption(required(root, 1611178), details, 7, 0, 1);
    required(root, 1611178).color = { 0, 0, 150, 255 };
    const std::array percentage{ BfstSignedIntegerArgument{ 21, fixed::absolute(ownership->ownership_percentage) } };
    auto explanation = text(470, percentage);
    explanation.resize(std::min<std::size_t>(explanation.size(), 253));
    caption(required(root, 1611181), explanation, 7, 0, 1);
    required(root, 1611181).color = { 0, 0, 150, 255 };
    const auto company =
        price->company_title.empty() ? localized_text(c.messages.mail_language.tag_system, 172) : price->company_title;
    title(1611179, company);
    scalar(1611170, price->current_share_price, true);
    scalar(1611173, ownership->shares_owned);
    const auto portfolio =
        price->current_share_price == -1 || ownership->shares_owned == -1
            ? -1
            : fixed::narrow(static_cast<std::int64_t>(price->current_share_price) * ownership->shares_owned);
    scalar(1611172, portfolio, true);
    scalar(1611176, theme.golden_ticket_balance);
}

std::intptr_t WorldManagementUiController::State::finance_event(UiControl& sender, const UiEvent& e) {
    if (page == Page::finance) {
        auto& graph = c.ui.panels.financial_statistics.graph;
        if (e.type == 256) {
            if (e.id == 40001 || e.id == 4294967294u || e.id == 4294967295u) {
                close();
                return 0;
            }
            if (e.id == 40030) {
                help_advisor(159);
                return 0;
            }
            if (auto row = std::ranges::find(financial_series, e.id, &FinanceSeries::control);
                row != financial_series.end()) {
                auto mask = graph.selected_series_mask & (row->mask & 3u ? 0xFFFFF003u : 0xFFFFFFFCu);
                graph.selected_series_mask = (mask & row->mask) ? mask & ~row->mask : mask | row->mask;
                c.ports.ui.refresh_financial_statistics_month(c);
                refresh_finance();
                return 0;
            }
        }
        if (e.type == 257)
            if (auto range = finance_range(e.id)) {
                graph.range = *range;
                if (e.value == 1)
                    c.ports.ui.refresh_financial_statistics_month(c);
                return 0;
            }
    } else if (page == Page::loans) {
        if (e.type == 1030) {
            loan_sort = ui.list_sort_key(*list_control);
            return 0;
        }
        if (e.type == 257 && e.value == 1 && (e.id == 41006 || e.id == 41007)) {
            c.ui.panels.loans.view = e.id == 41006 ? LoanView::offers : LoanView::outstanding;
            ui.set_enabled(required(root, 41002), false);
            ui.clear_list_rows(*list_control, false);
            rebuild_loan_management_rows(c);
            refresh_loans();
            return 0;
        }
        if ((e.type == 1024 || e.type == 1025) && e.id == 41004) {
            const auto index = static_cast<std::size_t>(e.value);
            ui.set_enabled(required(root, 41002), e.value >= 0 && index < list_control->rows.size());
            if (e.type == 1024 && e.value >= 0 && index < list_control->rows.size())
                loan_action(static_cast<std::int32_t>(list_control->rows[index].identity));
            return 0;
        }
        if (e.type == 256) {
            if (e.id == 41001 || e.id == 4294967294u) {
                close();
                return 0;
            }
            if (e.id == 41009) {
                help_advisor(436);
                return 0;
            }
            if (e.id == 41002)
                if (auto key = ui.selected_list_identity(*list_control))
                    loan_action(static_cast<std::int32_t>(*key));
        }
    } else if (page == Page::shares) {
        if (e.type == 30 && c.finance.shares_panel_refresh_pending)
            refresh_shares();
        if (e.type == 256 || e.type == 257)
            if (auto range = finance_range(e.id)) {
                shares_graph.range = *range;
                refresh_shares_graph();
                return 0;
            }
        if (e.type == 256) {
            if (e.id == 1611162 || e.id == 4294967294u || e.id == 4294967295u) {
                close();
                return 0;
            }
            if (e.id == 1611180) {
                help_advisor(440);
                return 0;
            }
        }
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
