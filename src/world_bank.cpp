#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_weather.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_mail.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace simcoaster {
namespace {
std::uint32_t calculate_initial_repayment(const BankLoan* loan) {
    const double months = numeric::bits(loan->repayment_period_in_months);
    if (months == 0)
        return 0;
    const double growth = std::pow(1.0 + numeric::bits(loan->apr_in_percent) / 100.0, months / 24.0);
    const double repayment = numeric::bits(loan->amount_available) * growth / months;
    //Invalid/unrepresentable configurations produce no payment. Check before
    //conversion so infinities, NaNs and oversized terms cannot cause C++ UB.
    if (!std::isfinite(repayment) || repayment < 0 || repayment >= std::ldexp(1.0, 63))
        return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(repayment));
}

void add(std::int32_t& target, std::int32_t value) {
    target = numeric::word(numeric::bits(target) + numeric::bits(value));
}

void subtract(std::int32_t& target, std::int32_t value) {
    target = numeric::word(numeric::bits(target) - numeric::bits(value));
}

ParkAnalyserFinanceView* analyser(EconomyWorld& c, const World& world) {
    return c.entities.payload<ParkAnalyserFinanceView>(world.saved.park_analyser);
}

void history(EconomyWorld& c, unsigned index, std::int32_t value) {
    analyser(c, *c.simulation.reference)->long_histories[index].temporary += numeric::bits(value);
}

std::uint32_t convert_cached_park_value(std::int32_t value) {
    return numeric::bits(value);
}

void dispatch(EconomyWorld& c, WorldEvent* event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        event,
                                        deliver_world_record_event,
                                        nullptr);
}
} //namespace

BankRecord* initialize_world_bank_account_state(EconomyWorld& c, BankRecord* bank) {
    initialize_world_map_object_base(c, &bank->base, 1, static_cast<std::uint32_t>(ThingKind::bank_account));
    bank->balance = 0;
    bank->batch_balance = 0;
    bank->withdrawals_enabled = 0;
    bank->admission_fee = 0;
    bank->last_balance = 0;
    bank->turn_entered_red = 0;
    bank->withdrawals_enabled = 1;
    bank->balance = c.finance.initial_cash;
    bank->batch_balance = 0;
    bank->profit_this_year = 0;
    for (std::size_t i = 0; i < bank->loans.size(); ++i) {
        auto& loan = bank->loans[i];
        auto& offer = c.finance.bank_loan_offers[i];
        loan.lender_name_index = offer.lender_name_index;
        loan.amount_available = offer.amount;
        loan.apr_in_percent = offer.apr_in_percent;
        loan.repayment_period_in_months = offer.repayment_period_in_months;
        loan.loan_bought = 0;
        loan.months_repaid = 0;
        loan.loan_available = numeric::bits(loan.amount_available) <= 10000u;
        loan.monthly_repayment = numeric::word(calculate_initial_repayment(&loan));
    }
    set_world_admission_fee(bank, c.finance.initial_admission_fee);
    for (int category : { 12, 19, 13 })
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &bank->base.header, category);
    return bank;
}

void destroy_heap_payload_event(EconomyWorld& c, HeapPayloadEvent* event) {
    if (event->payload) {
        c.ports.memory.release(Allocation::event_payload, event->payload);
        event->payload = nullptr;
    }
}

std::int64_t* write_scaled_global_clock_delta_qword(EconomyWorld& c,
                                                    CalendarState* calendar,
                                                    std::int64_t* out,
                                                    std::uint32_t start) {
    const auto delta = c.simulation.current->saved.game_tick - start;
    const auto product = std::uint64_t{ delta } * calendar->funny_seconds_per_real_second;
    const auto divided = std::bit_cast<std::int64_t>(product) / 4;
    *out = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(divided) * 10000000u);
    return out;
}

std::int32_t apply_bank_balance_and_profit_delta(EconomyWorld& c, BankRecord* bank, std::int32_t amount) {
    add(bank->balance, amount);
    history(c, 6, amount);
    add(bank->profit_this_year, amount);
    return bank->profit_this_year;
}

std::int32_t charge_world_bank_account_expense(EconomyWorld& c, BankRecord* bank, std::int32_t amount) {
    if (!bank->withdrawals_enabled)
        return 0;
    subtract(bank->balance, amount);
    if (bank->balance < 0 && bank->last_balance >= 0)
        bank->turn_entered_red = numeric::word(c.simulation.current->saved.game_tick);
    bank->last_balance = bank->balance;
    history(c, 3, amount);
    subtract(bank->profit_this_year, amount);
    return bank->profit_this_year;
}

std::int32_t accrue_bank_batch_income(BankRecord* bank, std::int32_t amount) {
    add(bank->batch_balance, amount);
    add(bank->profit_this_year, amount);
    return amount;
}

std::int64_t* write_bank_time_in_red(EconomyWorld& c, BankRecord* bank, std::int64_t* out) {
    if (bank->last_balance < 0)
        return write_scaled_global_clock_delta_qword(c, &c.simulation.current->calendar, out, numeric::bits(bank->turn_entered_red));
    *out = 0;
    return out;
}

int reset_bank_starting_cash(EconomyWorld& c, BankRecord* bank) {
    bank->balance = c.finance.initial_cash;
    bank->batch_balance = 0;
    bank->turn_entered_red = 0;
    return 0;
}

void update_bank_account_month(EconomyWorld& c, BankRecord* bank) {
    const auto income = bank->batch_balance;
    add(bank->balance, income);
    history(c, 6, income);
    add(bank->profit_this_year, income);
    bank->batch_balance = 0;
    for (auto& loan : bank->loans)
        if (loan.loan_bought) {
            if (bank->withdrawals_enabled)
                charge_world_bank_account_expense(c, bank, loan.monthly_repayment);
            add(loan.months_repaid, 1);
            const auto term = numeric::bits(loan.repayment_period_in_months);
            if (term) {
                //Native DIV is unsigned; retain DWORD wrap before division and addition.
                const auto interest = (numeric::bits(loan.monthly_repayment) * term - numeric::bits(loan.amount_available)) / term;
                add(bank->profit_this_year, numeric::word(numeric::bits(loan.monthly_repayment) - interest));
            }
            if (loan.months_repaid == loan.repayment_period_in_months) {
                loan.loan_bought = 0;
                loan.months_repaid = 0;
            }
        }
    if (bank->balance < 0) {
        std::int64_t elapsed = 0;
        if (bank->last_balance < 0)
            write_scaled_global_clock_delta_qword(c, &c.simulation.current->calendar, &elapsed, numeric::bits(bank->turn_entered_red));
        if (elapsed / 25920000000000LL >= 6) {
            WorldEvent event{ Tag19Event{ static_cast<std::uint32_t>(BankTag19::insolvency) } };
            dispatch(c, &event);
        }
    }
}

void set_world_admission_fee(BankRecord* bank, std::int32_t fee) {
    bank->admission_fee = fee;
}

void record_park_admission_income(EconomyWorld& c, BankRecord* bank) {
    const auto initial = bank->admission_fee;
    add(bank->balance, initial);
    history(c, 6, initial);
    add(bank->profit_this_year, initial);
    history(c, 7, bank->admission_fee);
    const auto notification_fee = bank->admission_fee;
    auto* receiver = analyser(c, *c.simulation.reference);
    world_operations::record_visitor_admission(c, receiver, notification_fee);
}

int apply_new_bank_loan(EconomyWorld& c, BankRecord* bank, int index) {
    auto& loan = bank->loans[index];
    if (loan.loan_bought)
        return 0;
    const auto amount = loan.amount_available;
    apply_bank_balance_and_profit_delta(c, bank, amount);
    subtract(bank->profit_this_year, loan.amount_available);
    loan.loan_bought = 1;
    return 1; //No availability check or months-repaid reset.
}

int apply_bank_loan_repayment(EconomyWorld& c, BankRecord* bank, int index) {
    auto& loan = bank->loans[index];
    if (!loan.loan_bought)
        return 0;
    const auto remaining = numeric::bits(loan.monthly_repayment) * (numeric::bits(loan.repayment_period_in_months) - numeric::bits(loan.months_repaid));
    if (numeric::bits(bank->balance) < remaining) {
        return 0; //Unsigned comparison, including negative balances.
    }
    if (bank->withdrawals_enabled)
        charge_world_bank_account_expense(c, bank, numeric::word(remaining));
    loan.loan_bought = 0;
    loan.months_repaid = 0;
    const auto term = numeric::bits(loan.repayment_period_in_months);
    if (term) {
        const auto interest = (numeric::bits(loan.monthly_repayment) * term - numeric::bits(loan.amount_available)) / term;
        add(bank->profit_this_year, numeric::word(remaining - term * interest));
    }
    return 1;
}

BankLoan* refresh_loan_offer_availability_and_copy_record(EconomyWorld& c, BankRecord* bank, BankLoan* out, int index) {
    std::uint32_t remaining = 0;
    for (auto& loan : bank->loans)
        if (loan.loan_bought)
            remaining += numeric::bits(loan.monthly_repayment) * (numeric::bits(loan.repayment_period_in_months) - numeric::bits(loan.months_repaid));
    auto* receiver = analyser(c, *c.simulation.current);
    const auto capacity = numeric::word(convert_cached_park_value(world_operations::cached_park_value(c, receiver, 20)) - remaining);
    auto& loan = bank->loans[index];
    loan.loan_available = loan.amount_available <= capacity;
    *out = loan;
    return out;
}

int has_available_unpurchased_bank_loan(EconomyWorld& c, BankRecord* bank) {
    BankLoan loan;
    for (int i = 0; i < 8; ++i) {
        refresh_loan_offer_availability_and_copy_record(c, bank, &loan, i);
        if (!loan.loan_bought && loan.loan_available)
            return 1;
    }
    return 0;
}

void handle_world_bank_account_event(EconomyWorld& c, BankRecord* bank, WorldEvent* event) {
    const auto type = event->type();
    if (type == 12) {
        update_bank_account_month(c, bank);
        return;
    }
    if (type == 13) {
        bank->profit_this_year = 0;
        return;
    }
    if (type != 19)
        return;
    const auto tag = static_cast<BankTag19>(event->get<Tag19Event>().tag);
    if (tag == BankTag19::insolvency
        && c.simulation.current->saved.state != WorldState::transition
        && !c.messages.congratulations_popup) {
        auto* raw_email = static_cast<GameOverCompanyEmail*>(
            c.ports.memory.allocate(Allocation::event_payload, sizeof(GameOverCompanyEmail)));
        GameOverCompanyEmail* email;
        try {
            email = raw_email ? construct_game_over_company_email(c,
                                                                  std::construct_at(raw_email),
                                                                  GameOverEmailReason::bankruptcy)
                              : nullptr;
        } catch (...) {
            c.ports.memory.release(Allocation::event_payload, raw_email);
            throw;
        }
        dispatch_heap_payload_event(c, email);
    } else if (tag == BankTag19::reset_starting_cash)
        reset_bank_starting_cash(c, bank);
}
} //namespace simcoaster
