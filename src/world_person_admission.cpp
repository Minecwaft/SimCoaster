#include "world_person_internal.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_queue.hpp"

namespace simcoaster {
namespace {
BankRecord& bank(VisitorWorld& c) {
    auto* payload = c.entities.at(person_detail::base_world(c).saved.bank_account).payload;
    if (!payload) {
        throw std::logic_error("admission bank is missing");
    }
    return *reinterpret_cast<BankRecord*>(payload);
}

ParkAnalyserState& analyser(VisitorWorld& c) {
    auto* payload = c.entities.at(person_detail::base_world(c).saved.park_analyser).payload;
    if (!payload) {
        throw std::logic_error("admission analyser is missing");
    }
    return *reinterpret_cast<ParkAnalyserState*>(payload);
}

void leave_expensive_park(VisitorWorld& c, PersonRuntimeView& person) {
    if (!try_set_mobile_entity_path_to_park_exit(c, person)) {
        person.mobile.navigation.navigation_mode = 1;
        (void)try_set_mobile_entity_path_to_park_exit(c, person);
    }
    world_operations::enter_world_person_state(c, &person, PersonState::walk_to_exit);
    person.exit_level = 0;
}
} //namespace

AdmissionFeeBand classify_park_admission_fee_value(VisitorWorld& c) {
    auto& opinion_analyser = analyser(c);
    const auto attraction = static_cast<std::uint32_t>(calculate_park_attraction_value(c));
    const auto base = static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.ExcitementToCostDivisor);
    const auto denominator = base + next_world_prng_value(person_detail::base_world(c)) % ((base >> 1) + 1u);
    if (!denominator) {
        throw std::domain_error("zero admission attraction divisor");
    }
    const auto scaled = attraction / denominator, fee = static_cast<std::uint32_t>(bank(c).admission_fee);
    const auto minimum = static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.MinimumEntryFee), reference = minimum + scaled;
    if (reference <= minimum && fee <= minimum) {
        return AdmissionFeeBand::about_right;
    }
    const auto& balance = c.visitors.needs_balance.PeepInfo;
    AdmissionFeeBand band;
    std::int32_t sample;
    //Unordered comparisons take the same branches as "less" and "less/equal".
    if (!(static_cast<double>(fee) >= static_cast<double>(reference) * balance.AveragePriceMultiplier)) {
        if (!(static_cast<double>(fee) > static_cast<double>(reference) * balance.CheapPriceMultiplier)) {
            band = AdmissionFeeBand::too_low;
            sample = balance.OpinionForTooLow;
        } else {
            band = AdmissionFeeBand::about_right;
            sample = balance.OpinionForAboutRight;
        }
    } else if (!(static_cast<double>(fee) >= static_cast<double>(reference) * balance.ExpensivePriceMultiplier)) {
        band = AdmissionFeeBand::too_high;
        sample = balance.OpinionForTooHigh;
    } else {
        band = AdmissionFeeBand::much_too_high;
        sample = balance.OpinionForMuchTooHigh;
    }
    append_park_analyser_byte_history(opinion_analyser, 0, static_cast<std::uint8_t>(sample));
    return band;
}

void update_person_admission_fee_response(VisitorWorld& c, PersonRuntimeView& person) {
    if (person.park_opening_waiting_time) {
        --person.park_opening_waiting_time;
        return;
    }
    const auto band = classify_park_admission_fee_value(c);
    match (band) {
        case AdmissionFeeBand::much_too_high => {
            if (!try_set_mobile_entity_path_to_park_exit(c, person)) {
                person.mobile.navigation.navigation_mode = 1;
                (void)try_set_mobile_entity_path_to_park_exit(c, person);
            }
            world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, MobileThoughtCode::sideshow_loss, false);
            world_operations::enter_world_person_state(c, &person, PersonState::walk_to_exit);
            person.exit_level = 0;
            return;
        }
        case AdmissionFeeBand::too_high => {
            person.park_opening_waiting_time = next_world_prng_value(person_detail::base_world(c)) % 50u + 50u;
            world_operations::set_mobile_entity_thought_bubble(c, &person.mobile.thought_history, MobileThoughtCode::sideshow_loss, false);
            decrease_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::medium);
            if (!truncate_park_object_condition(person.happiness)) {
                leave_expensive_park(c, person);
            }
            return;
        }
        case AdmissionFeeBand::too_low or AdmissionFeeBand::about_right => {
            if (band == AdmissionFeeBand::too_low) {
                increase_person_happiness_by_configured_amount(c, &person, HappinessChangeSize::medium);
            }
            record_park_admission_income(c, &bank(c));
            person.paid_admission = 1;
            person.cash_spent += static_cast<std::uint32_t>(bank(c).admission_fee);
            world_operations::enter_world_person_state(c, &person, PersonState::wait_for_park_open);
            return;
        }
        case _ => {
        }
    }
}
} //namespace simcoaster
