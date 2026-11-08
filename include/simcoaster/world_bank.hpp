#pragma once
#include "world_domains_fwd.hpp"
#include "world_events.hpp"

namespace simcoaster {
//Loan construction uses ordinary C++ floating-point arithmetic for the
//recovered growth/payment formula; numerical work is independent of x87 state.
//Cached park valuation, visitor accounting and the UI mail consumer remain
//explicit Services dependencies. The concrete email constructors are compiled.
enum class BankTag19 : std::uint32_t {
    insolvency = 2,
    reset_starting_cash = 10
};
BankRecord* initialize_world_bank_account_state(EconomyWorld&, BankRecord*);
void destroy_heap_payload_event(EconomyWorld&, HeapPayloadEvent*);
void dispatch_world_used_thing_event(WorldRuntime&, ThingHeader*, WorldEvent*);
void handle_world_bank_account_event(EconomyWorld&, BankRecord*, WorldEvent*);
std::int64_t* write_scaled_global_clock_delta_qword(EconomyWorld&, CalendarState*, std::int64_t*, std::uint32_t start_tick);
std::int32_t apply_bank_balance_and_profit_delta(EconomyWorld&, BankRecord*, std::int32_t amount);
std::int32_t charge_world_bank_account_expense(EconomyWorld&, BankRecord*, std::int32_t amount);
std::int32_t accrue_bank_batch_income(BankRecord*, std::int32_t amount);
std::int64_t* write_bank_time_in_red(EconomyWorld&, BankRecord*, std::int64_t*);
int reset_bank_starting_cash(EconomyWorld&, BankRecord*);
void update_bank_account_month(EconomyWorld&, BankRecord*);
void set_world_admission_fee(BankRecord*, std::int32_t);
void record_park_admission_income(EconomyWorld&, BankRecord*);
int apply_new_bank_loan(EconomyWorld&, BankRecord*, int index);
int apply_bank_loan_repayment(EconomyWorld&, BankRecord*, int index);
BankLoan* refresh_loan_offer_availability_and_copy_record(EconomyWorld&, BankRecord*, BankLoan* out, int index);
int has_available_unpurchased_bank_loan(EconomyWorld&, BankRecord*);
} //namespace simcoaster
