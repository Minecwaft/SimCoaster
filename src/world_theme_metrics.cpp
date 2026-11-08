#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_theme.hpp"
#include <bit>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
World& require_world(World* world) {
    return require_bound<std::invalid_argument>(world, "null mission metric world");
}

BankRecord& bank_for_world(MissionWorld& c, const World& world) {
    auto* record = c.entities.at(world.saved.bank_account).payload;
    if (!record)
        throw std::invalid_argument("null mission metric bank");
    return *reinterpret_cast<BankRecord*>(record);
}

ThemeGlobalSaveInfo& require_theme_info(ThemeGlobalSaveInfo* info) {
    return require_bound<std::runtime_error>(info, "mission metric theme failed to load");
}

void read_base_analyser(MissionWorld& c) {
    //Both native person-count cases retain this lookup although they ignore
    //its result. A null payload is allowed; the World and metadata index must
    //still be usable. History aggregation independently follows the World
    //reference binding, which may point to a different World pointer slot.
    auto* volatile analyser = c.entities.at(require_world(c.simulation.current).saved.park_analyser).payload;
    (void)analyser;
}
} //namespace

std::int32_t sum_remaining_bank_loan_repayments(const BankRecord* bank) {
    if (!bank)
        throw std::invalid_argument("null repayment bank");
    std::uint32_t remaining = 0;
    for (const auto& loan : bank->loans)
        if (loan.loan_bought)
            remaining += numeric::bits(loan.monthly_repayment) * (numeric::bits(loan.repayment_period_in_months) - numeric::bits(loan.months_repaid));
    return numeric::word(remaining);
}

std::int32_t count_global_park_award_flags(const ThemeGlobalInfoManager* manager) {
    if (!manager)
        throw std::invalid_argument("null award manager");
    std::int32_t count = 0;
    for (const auto flag : manager->global_park_award_flags)
        if (flag)
            ++count;
    return count;
}

std::int32_t read_mission_objective_metric(MissionWorld& c, const MissionObjectiveTrackingEntry* entry) {
    if (!entry)
        throw std::invalid_argument("null mission objective");
    match (entry->objective_kind) {
        case MissionObjectiveMetric::adjusted_bank_positive or MissionObjectiveMetric::adjusted_bank_balance => {
            const bool positive = entry->objective_kind == MissionObjectiveMetric::adjusted_bank_positive;
            const auto& world = require_world(c.simulation.current);
            const auto balance = bank_for_world(c, world).balance;
            const auto remaining = sum_remaining_bank_loan_repayments(&bank_for_world(c, world));
            const auto adjusted = numeric::word(numeric::bits(balance) - numeric::bits(remaining));
            return positive ? static_cast<std::int32_t>(adjusted > 0) : adjusted;
        }
        case MissionObjectiveMetric::total_challenges_completed => {
            std::uint32_t total = 0;
            for (std::uint32_t ordinal = 0; ordinal < 3; ++ordinal) {
                auto& manager = get_or_create_theme_global_info_manager(c);
                auto& info = require_theme_info(get_or_create_theme_global_info_for_ordinal(c, manager, ordinal));
                total += numeric::bits(info.challenges_done);
            }
            return numeric::word(total);
        }
        case MissionObjectiveMetric::park_analyser_growth_percent => {
            read_base_analyser(c);
            return numeric::word(sum_current_park_analyser_history_values_7_to_11(c));
        }
        case MissionObjectiveMetric::eligible_person_count_growth_percent or MissionObjectiveMetric::eligible_person_count => {
            read_base_analyser(c);
            return numeric::word(count_eligible_person_records(c, 4));
        }
        case MissionObjectiveMetric::global_park_award_count => {
            return count_global_park_award_flags(&get_or_create_theme_global_info_manager(c));
        }
        case MissionObjectiveMetric::share_ownership_percent => {
            if (!c.finance.shares_finance.ownership_context)
                throw std::invalid_argument("null shares ownership context");
            const auto owned = c.finance.shares_finance.ownership_context->shares_owned;
            if (!owned)
                return 0;
            const auto numerator = numeric::word(numeric::bits(owned) * 100u);
            const auto denominator = c.finance.company_shares_issued;
            if (!denominator)
                throw std::domain_error("zero company shares issued");
            if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)
                throw std::overflow_error("share ownership percentage division overflow");
            return numerator / denominator;
        }
        case _ => return 0;
    }
}

ThemeGlobalSaveInfo* get_mission_required_challenge_progress(MissionWorld& c,
                                                             std::int32_t* required_count,
                                                             std::int32_t* completed_count,
                                                             std::int32_t* required_theme) {
    if (!required_count || !completed_count || !required_theme)
        throw std::invalid_argument("null challenge progress output");
    *required_theme = get_or_create_theme_global_info_manager(c).mission_research.required_challenges_theme;
    *required_count = get_or_create_theme_global_info_manager(c).mission_research.required_challenges_number;
    //Reload the output: callers can alias required_count and required_theme.
    const auto ordinal = *required_theme;
    auto& manager = get_or_create_theme_global_info_manager(c);
    auto* info = get_or_create_theme_global_info_for_ordinal(c, manager, static_cast<std::uint32_t>(ordinal));
    *completed_count = require_theme_info(info).challenges_done;
    return info;
}

std::int32_t*
get_mission_promotion_progress_values(MissionWorld& c, std::int32_t* mission_number, std::int32_t* current_index) {
    if (!mission_number || !current_index)
        throw std::invalid_argument("null promotion progress output");
    *mission_number = get_or_create_theme_global_info_manager(c).mission_research.promotion_number;
    const auto index = get_or_create_theme_global_info_manager(c).current_promotion_index;
    *current_index = index;
    return current_index;
}
} //namespace simcoaster
