#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/mesh_runtime_queries.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
World& base(AdvisorWorld& c) {
    return require_bound(c.simulation.current, "staff Advisor base World");
}

World& primary(AdvisorWorld& c) {
    return require_bound(c.referenced_world(), "staff Advisor primary World");
}

template <class T>
T& record(AdvisorWorld& c, ThingId id) {
    auto* p = c.entities.at(id).payload;
    if (!p)
        throw std::logic_error("staff Advisor missing record");
    return *reinterpret_cast<T*>(p);
}

ParkAnalyserState& analyser(AdvisorWorld& c) {
    return record<ParkAnalyserState>(c, primary(c).saved.park_analyser);
}

const ResearchCatalogDefinition& definition(AdvisorWorld& c, const ParkObjectRuntimeView& o) {
    const auto* d = get_park_object_definition(c, &o);
    if (!d || !d->balance)
        throw std::logic_error("staff Advisor definition");
    return *d;
}

bool enabled(const AdvisorQuery& c) {
    return c.advisor.balance->GeneralAdvisor.UISelectionModeMessageFilter && !c.construction.mode;
}

std::int32_t div(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "staff Advisor division");
}

std::int32_t staff_count(AdvisorWorld& c, ThingKind kind) {
    return count_linked_staff_by_kind(c, std::to_underlying(kind));
}

bool candidate(AdvisorWorld& c, StaffType role) {
    return count_world_staff_candidates(primary(c), role) != 0;
}

std::int64_t elapsed_months(AdvisorWorld& c) {
    return std::bit_cast<std::int64_t>(
               scale_world_ticks_to_filetime_delta(primary(c).calendar, base(c).saved.game_tick))
         / 25920000000000ll;
}

std::int32_t days_since(AdvisorWorld& c, std::uint64_t start) {
    FileTime now{};
    calculate_world_calendar_filetime(c, &base(c).calendar, &now);
    const auto time = static_cast<std::uint64_t>(now.low) | (static_cast<std::uint64_t>(now.high) << 32);
    return fixed::narrow(std::bit_cast<std::int64_t>(time - start) / 864000000000ll);
}

std::int32_t days_since_open(AdvisorWorld& c) {
    return days_since(c, analyser(c).park_last_opened);
}

bool has_capability(const ParkObjectRuntimeView& object, ParkObjectCapabilities flag) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
}

template <class Predicate>
std::int32_t active_object_count(AdvisorWorld& c, Predicate predicate) {
    std::int32_t count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == ThingKind::park_object
            && predicate(*reinterpret_cast<ParkObjectRuntimeView*>(node->payload)))
            ++count;
    return count;
}

std::int32_t poor_ride_count(AdvisorWorld& c, std::int32_t threshold, bool broken_only) {
    std::int32_t count = 0;
    for (auto id = base(c).saved.first_park_object; id;) {
        const auto& object = record<ParkObjectRuntimeView>(c, id);
        if (definition(c, object).which_ui_type == ObjectUiCategory::rides
            && ((!broken_only && static_cast<std::uint8_t>(fixed::truncate_integer(object.repair_state)) < threshold)
                || object.state == ParkObjectState::broken_down))
            ++count;
        id = object.next_park_object;
    }
    return count;
}

std::int32_t ride_count(AdvisorWorld& c) {
    return active_object_count(c, [&](const auto& o) {
        return definition(c, o).which_ui_type == ObjectUiCategory::rides
            && is_special_ride_group_id_unregistered(c, o.base.header.id);
    });
}

std::int32_t litter_percent(AdvisorWorld& c) {
    std::int32_t count = 0, litter = 0;
    for (const auto& cell : base(c).map_cells) {
        const auto type = cell.base.common.type;
        count += type == 1 || type == 3 || type == 9 || cell.litter_selector;
        litter += cell.litter_selector != 0;
    }
    return count ? 100 * litter / count : 0;
}

std::int32_t busy(AdvisorWorld& c) {
    return std::clamp(fixed::word(get_cached_eligible_person_record_count(c, analyser(c), 0)) / 2, 0, 100);
}

std::int32_t unhappy_percent(AdvisorWorld& c) {
    std::int32_t count = 0, unhappy = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == ThingKind::person) {
            const auto& p = *reinterpret_cast<PersonRuntimeView*>(node->payload);
            if (record_passes_grid_predicates(c, &p.base)) {
                ++count;
                unhappy += static_cast<std::uint8_t>(fixed::truncate_integer(p.happiness)) < 30;
            }
        }
    return count ? 100 * unhappy / count : 0;
}

std::int32_t thirsty_or_hungry(AdvisorWorld& c, bool hungry, std::uint8_t threshold) {
    std::int32_t count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == ThingKind::person) {
            const auto& p = *reinterpret_cast<PersonRuntimeView*>(node->payload);
            if (record_passes_grid_predicates(c, &p.base)
                && static_cast<std::uint8_t>(fixed::truncate_integer(hungry ? p.needs.hunger : p.needs.thirst))
                       > threshold)
                ++count;
        }
    return count;
}

std::array<CellId, 2> endpoints(const StaffRuntimeRecord& s) {
    const auto& b = s.area.bounds;
    return {
        static_cast<CellId>(static_cast<std::uint32_t>(b.start_y) * 128u + static_cast<std::uint32_t>(b.start_x) + 1u),
        static_cast<CellId>(static_cast<std::uint32_t>(b.end_y) * 128u + static_cast<std::uint32_t>(b.end_x) + 1u)
    };
}

std::int32_t custom_patrols(AdvisorWorld& c, ThingKind kind) {
    std::int32_t count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == kind) {
            const auto corners = endpoints(*reinterpret_cast<StaffRuntimeRecord*>(node->payload));
            count += corners[0] != 1 || corners[1] != map_cell_count;
        }
    return count;
}

std::int32_t catalog_definitions(AdvisorWorld& c, ObjectUiCategory category, bool built, bool exclude_ticket = true) {
    std::int32_t count = 0;
    for (auto* d = first_object_catalog_entry(&c.resources.object_catalog); d;
         d = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (d->which_ui_type != category)
            continue;
        if (!d->balance)
            throw std::logic_error("staff Advisor catalog balance");
        const auto* control = find_world_object_control(base(c).object_controls, d->id);
        if (!control)
            throw std::logic_error("staff Advisor object control");
        if ((built ? fixed::word(control->live_instance_count) > 0 : control->available != 0)
            && (!exclude_ticket || !d->balance->UsageInfo.GoldenTicketCost))
            ++count;
    }
    return count;
}

std::int32_t ride_definitions(AdvisorWorld& c, bool built) {
    return catalog_definitions(c, ObjectUiCategory::rides, built);
}

std::int32_t category_count(AdvisorWorld& c, ObjectUiCategory category) {
    return active_object_count(c, [&](const auto& o) {
        return definition(c, o).which_ui_type == category && is_special_ride_group_id_unregistered(c, o.base.header.id);
    });
}

std::int32_t prebuilt_count(AdvisorWorld& c) {
    return active_object_count(c, [&](const auto& o) {
        const auto& d = definition(c, o);
        return d.which_track_type == 3
            && (d.is_special != 1 || is_special_ride_group_id_unregistered(c, o.base.header.id));
    });
}

std::int32_t capability_count(AdvisorWorld& c, ParkObjectCapabilities flag) {
    return active_object_count(c, [&](const auto& o) {
        return has_capability(o, flag);
    });
}

//FLD / __ftol followed by an AL compare or low-byte mask. The old
//untyped header-pointer decompilation incorrectly displayed a DWORD read.
std::uint8_t truncated_byte(float value) {
    return static_cast<std::uint8_t>(fixed::truncate_integer(value));
}

std::int32_t need_count(AdvisorWorld& c, float PersonNeeds::* member, std::uint8_t threshold) {
    int count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == ThingKind::person) {
            const auto& p = *reinterpret_cast<const PersonRuntimeView*>(node->payload);
            count += record_passes_grid_predicates(c, &p.base) && truncated_byte(p.needs.*member) > threshold;
        }
    return count;
}

struct ReliefMetric {
    ThingId selected{};
    std::int32_t average{};
};

ReliefMetric relief_metric(AdvisorWorld& c) {
    ReliefMetric result;
    int count = 0, total = 0, maximum = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next)
        if (node->payload->kind == ThingKind::park_object) {
            const auto& o = *reinterpret_cast<const ParkObjectRuntimeView*>(node->payload);
            if (!has_capability(o, ParkObjectCapabilities::provides_relief))
                continue;
            const auto value = 100 - truncated_byte(o.repair_state);
            if (!result.selected || value > maximum) {
                result.selected = o.base.header.id;
                maximum = value;
            }
            ++count;
            total = fixed::add(total, value);
        }
    result.average = count ? div(total, count) : 0;
    return result;
}

int count_relief_and_stamp(AdvisorWorld& c) {
    const auto count = capability_count(c, ParkObjectCapabilities::provides_relief);
    analyser(c).metrics[3] = { base(c).saved.game_tick, static_cast<std::uint32_t>(count), 1 };
    return count;
}

std::optional<std::uint8_t> admission_satisfaction(AdvisorWorld& c) {
    const auto& h = analyser(c).byte_histories[0];
    const auto count = h.wrapped ? h.num_entries : fixed::add(h.current_entry, 1);
    if (count < 50)
        return {};
    double total = 0;
    for (int offset = 0; offset < 50; ++offset)
        if (offset < h.num_entries && (offset <= h.current_entry || h.wrapped)) {
            const auto index =
                offset <= h.current_entry ? h.current_entry - offset : h.num_entries + h.current_entry - offset;
            total += h.data.at(static_cast<std::size_t>(index));
        }
    return static_cast<std::uint8_t>(fixed::truncate_integer(total / 50));
}

std::uint32_t repayment(const BankLoan& loan) {
    return loan.loan_bought ? static_cast<std::uint32_t>(loan.monthly_repayment)
                                  * (static_cast<std::uint32_t>(loan.repayment_period_in_months)
                                     - static_cast<std::uint32_t>(loan.months_repaid))
                            : 0u;
}
} //namespace

std::optional<std::int32_t> evaluate_advisor_staff_decision_score(AdvisorWorld& c, std::int32_t action) {
    const auto& policy = *c.advisor.balance;
    if (action == 139) {
        if (elapsed_months(c) < 3)
            return 0;
        int count = 0, tired = 0;
        for (auto* node = c.entities.active_head; node; node = node->active_next)
            if (is_staff_record_kind(node->payload->kind)) {
                ++count;
                tired +=
                    is_world_staff_too_tired_for_task(c, *reinterpret_cast<const StaffRuntimeRecord*>(node->payload));
            }
        return count && div(numeric::product(100, tired), count) > policy.NeedStaffRoom.PctOfStaffTired ? policy.NeedStaffRoom.Score : 0;
    }
    if (action == 79 || action == 524) {
        if (action == 79 && is_first_hour_of_gameplay(c))
            return 0;
        if (primary(c).saved.park_closed || count_relief_and_stamp(c))
            return 0;
        const auto count = fixed::word(get_cached_eligible_person_record_count(c, analyser(c), 4));
        const auto needy = need_count(c,
                                      &PersonNeeds::toilet_need,
                                      static_cast<std::uint8_t>(policy.FeaturesNoToilets.NeedingMoreThan));
        return count && div(numeric::product(100, needy), count) > policy.FeaturesNoToilets.MorePeopleThanThisNeeding ? (action == 79 ? policy.FeaturesNoToilets.Score : policy.New.ADVISORSTUTBUILDTOILETS) : 0;
    }
    if (action == 81) {
        //Native discards the pure average-queue query and always returns zero.
        return 0;
    }
    if (action >= 82 && action <= 85) {
        if (action <= 83 && !enabled(c))
            return 0;
        if (primary(c).saved.park_closed)
            return 0;
        const auto handymen = staff_count(c, ThingKind::handyman);
        if ((action == 82 || action == 84) ? handymen > 0 : !handymen)
            return 0;
        const auto metric = relief_metric(c);
        c.advisor.behavior.callback_thing_id = metric.selected;
        if (!metric.selected)
            return 0;
        const auto value = metric.average;
        if (action == 82)
            return value >= policy.FeaturesDirtyToilets1.FilthierThan && value <= policy.FeaturesDirtyToilets3.FilthierThan ? numeric::product(value, policy.FeaturesDirtyToilets1.PointsPerFilthPct) : 0;
        //Action83 also uses group1's multiplier in the executable.
        if (action == 83)
            return value >= policy.FeaturesDirtyToilets2.FilthierThan && value <= policy.FeaturesDirtyToilets4.FilthierThan ? numeric::product(value, policy.FeaturesDirtyToilets1.PointsPerFilthPct) : 0;
        return action == 84 ? (value >= policy.FeaturesDirtyToilets3.FilthierThan ? numeric::product(value, policy.FeaturesDirtyToilets3.PointsPerFilthPct) : 0) : (value >= policy.FeaturesDirtyToilets4.FilthierThan ? numeric::product(value, policy.FeaturesDirtyToilets4.PointsPerFilthPct) : 0);
    }
    if (action == 86 || action == 87 || action == 536) {
        if (action == 87 && (!enabled(c) || is_first_hour_of_gameplay(c)))
            return 0;
        if (action == 536 && !is_first_hour_of_gameplay(c))
            return 0;
        const auto& bank = record<BankRecord>(c, primary(c).saved.bank_account);
        if (action == 86 && !bank.admission_fee)
            return 0;
        const auto satisfaction = admission_satisfaction(c);
        if (!satisfaction)
            return 0;
        if (action == 86)
            return *satisfaction < policy.PricingTicketTooExpensive.TicketSatisfactionUnder
                     ? policy.PricingTicketTooExpensive.Score
                     : 0;
        if (action == 87)
            return *satisfaction > policy.PricingTicketTooCheap.TicketSatisfactionOver
                     ? policy.PricingTicketTooCheap.Score
                     : 0;
        return static_cast<std::uint32_t>(*satisfaction) >= static_cast<std::uint32_t>(numeric::product(2, bank.admission_fee)) ? policy.New.ADVISORSTUTENTRYPRICE : 0;
    }
    if (action == 93)
        return fixed::word(analyser(c).misbehaving_kids_that_got_away) > policy.GeneralLaxSecurity.MoreThanThisGotAway ? numeric::product(policy.GeneralLaxSecurity.ScorePerGetAway, fixed::word(analyser(c).misbehaving_kids_that_got_away)) : 0;
    if (action == 395) {
        return fixed::word(get_or_create_theme_global_info_manager(c).history[395].play_count) <= 1 ? numeric::product(2, *evaluate_advisor_staff_decision_score(c, 98)) : 0;
    }
    if (action >= 96 && action <= 101) {
        if (action <= 97 && !enabled(c))
            return 0;
        auto& bank = record<BankRecord>(c, primary(c).saved.bank_account);
        if (action == 96)
            return bank.balance > policy.WealthHigh.MoreMoneyThan
                        && std::ranges::none_of(bank.loans,
                                                [](const auto& loan) {
                                                    return loan.loan_bought != 0;
                                                })
                     ? policy.WealthHigh.Score
                     : 0;
        if (action == 97) {
            const auto remaining =
                static_cast<float>(fixed::subtract(bank.balance, sum_remaining_bank_loan_repayments(&bank)));
            std::optional<std::uint32_t> minimum;
            for (const auto& loan : bank.loans)
                if (const auto value = repayment(loan); value && (!minimum || value < *minimum))
                    minimum = value;
            return minimum && static_cast<double>(*minimum) * policy.WealthHighRepayLoan.Margin < remaining
                     ? policy.WealthHighRepayLoan.Score
                     : 0;
        }
        if (bank.balance >= 0)
            return 0;
        const auto available = has_available_unpurchased_bank_loan(c, &bank);
        if (action == 98)
            return available ? policy.TakeLoan.Score : 0;
        if (available)
            return 0;
        std::int64_t elapsed{};
        write_bank_time_in_red(c, &bank, &elapsed);
        //All three call signed __alldiv and compare the quotient's lowDWORD.
        const auto months = fixed::narrow(elapsed / 25920000000000ll);
        if (action == 99)
            return months < 3 ? policy.InTheRedSixMonths.Score : 0;
        if (action == 100)
            return months >= 3 && months < 5 ? policy.InTheRedThreeMonths.Score : 0;
        return static_cast<std::uint32_t>(months) == 5 ? policy.InTheRedMonthLeft.Score : 0;
    }
    if (action == 525 || action == 526 || action == 527 || action == 531 || action == 532 || action == 540) {
        if (!is_first_hour_of_gameplay(c))
            return 0;
        auto& manager = get_or_create_theme_global_info_manager(c);
        if (action == 540)
            return 10000;
        if (action == 525)
            return !manager.history[525].is_active
                        && !capability_count(c, ParkObjectCapabilities::requires_watering)
                        && ride_definitions(c, true)
                        && !primary(c).saved.park_closed
                        && need_count(c, &PersonNeeds::sickness, 50) >= 5
                     ? policy.New.ADVISORSTUTBUILDPLANTS
                     : 0;
        if (action == 526)
            return !staff_count(c, ThingKind::gardener)
                        && candidate(c, StaffType::gardener)
                        && capability_count(c, ParkObjectCapabilities::requires_watering)
                     ? policy.New.ADVISORSTUTHIREGARDENER
                     : 0;
        if (action == 527)
            return !manager.history[527].is_active && !is_advisor_action_active(c, 483) && can_proceed_with_current_objective(c) ? numeric::product(2, policy.Challenge.Score) : 0;
        if (!capability_count(c, ParkObjectCapabilities::thinking_space))
            return 0;
        if (action == 531)
            return !staff_count(c, ThingKind::researcher) && candidate(c, StaffType::researcher) ? 10000 : 0;
        if (!manager.research_lab_action_532_eligible || manager.history[532].is_active)
            return 0;
        const auto& lab = record<ResearchLabRecord>(c, primary(c).saved.research_lab);
        if (research_slot_has_unfinished_project(&lab, ResearchWorkSlot::primary)
            || research_slot_has_unfinished_project(&lab, ResearchWorkSlot::secondary)
            || fixed::word(lab.num_projects) <= 0
            || !primary(c).saved.first_researcher)
            return 0;
        c.advisor.behavior.callback_thing_id = lab.base.header.id;
        return policy.ResearchNoProject.Score;
    }
    if (action == 530)
        return !get_or_create_theme_global_info_manager(c).history[530].is_active
                    && !capability_count(c, ParkObjectCapabilities::thinking_space)
                    && get_or_create_theme_global_info_manager(c).mission_research.info_number == 1
                 ? 10000
                 : 0;
    if (action == 535) {
        const auto status = primary(c).saved.mission_controller ? query_mission_obstacle_research_status(c).status
                                                                : MissionResearchStatus::disabled;
        const auto& lab = record<ResearchLabRecord>(c, primary(c).saved.research_lab);
        const auto project = find_unfinished_obstacle_research(&lab);
        return project >= 0
                    && get_or_create_theme_global_info_manager(c).mission_number == 1
                    && !get_or_create_theme_global_info_manager(c).history[535].is_active
                    && capability_count(c, ParkObjectCapabilities::thinking_space)
                    && status == MissionResearchStatus::incomplete
                    && !get_world_research_project_progress_percent(&lab, project)
                 ? policy.New.ADVISORSTUTRESEARCHEXPANSION
                 : 0;
    }
    if (action >= 376 && action <= 378) {
        if (action == 378 && !enabled(c))
            return 0;
        if (fixed::word(get_or_create_theme_global_info_manager(c).history[static_cast<std::size_t>(action)].play_count)
            > 1)
            return 0;
        const auto target = action == 376 ? 5 : action == 377 ? 8 : 11;
        return numeric::product(2, *evaluate_advisor_staff_decision_score(c, target));
    }
    if (action == 386 || action == 388 || action == 116)
        return staff_count(c, ThingKind::guard) ? 10000 : 0;
    if (action == 537 || action == 539) {
        if (action == 537 && !is_first_hour_of_gameplay(c))
            return 0;
        const auto status = query_mission_obstacle_research_status(c);
        auto& manager = get_or_create_theme_global_info_manager(c);
        if (action == 537) {
            if (manager.mission_research.info_number != 1
                || status.status != MissionResearchStatus::complete
                || manager.history[537].is_active)
                return 0;
            if (!manager.selected_theme_info)
                throw std::logic_error("training Advisor selected theme");
            const auto* info =
                get_or_create_theme_global_info_for_ordinal(c, manager, manager.selected_theme_info->source_ordinal);
            if (!info || info->source_ordinal != 1)
                return 0;
            return active_object_count(c,
                                       [](const auto& o) {
                                           return has_capability(o, ParkObjectCapabilities::training_room);
                                       })
                     ? 0
                     : c.advisor.balance->TrainingRoomBuild.Score;
        }
        if (manager.history[539].is_active
            || manager.mission_number != 1
            || status.status != MissionResearchStatus::complete
            || can_proceed_with_current_objective(c)
            || !active_object_count(c,
                                    [](const auto& o) {
                                        return has_capability(o, ParkObjectCapabilities::training_room);
                                    })
            || !staff_count(c, ThingKind::gardener))
            return 0;
        for (auto* node = c.entities.active_head; node; node = node->active_next)
            if (node->payload->kind == ThingKind::gardener && !node->payload->retirement_pending) {
                const auto& s = *reinterpret_cast<const StaffRuntimeRecord*>(node->payload);
                if (classify_staff_activity(s) == StaffActivityStatus::training
                    || s.first_known_ability == ThemeAbility::water_drainage)
                    return 0;
            }
        return 10000;
    }
    if (action >= 73 && action <= 78) {
        const auto& p = *c.advisor.balance;
        const auto category = action <= 75 ? ObjectUiCategory::sideshows : ObjectUiCategory::features;
        const auto available = action == 76 ? catalog_definitions(c, category, false, false) : 0;
        if (!enabled(c) || ((action == 74 || action == 75) && is_first_hour_of_gameplay(c)))
            return 0;
        if (action == 73 || action == 77) {
            if (!staff_count(c, ThingKind::researcher))
                return 0;
            const auto built = catalog_definitions(c, category, true);
            if (action == 73 && !active_object_count(c, [](const auto& o) {
                    return has_capability(o, ParkObjectCapabilities::thinking_space);
                }))
                return 0;
            if (!built
                || !count_unfinished_research_projects(&record<ResearchLabRecord>(c, base(c).saved.research_lab),
                                                       action == 73 ? ResearchCategoryFilter::sideshows
                                                                    : ResearchCategoryFilter::features))
                return 0;
            if (action == 73
                && estimate_research_category_output(c,
                                                     &record<ResearchLabRecord>(c, primary(c).saved.research_lab),
                                                     ResearchCategory::sideshows))
                return 0;
            return built == catalog_definitions(c, category, false, false)
                     ? (action == 73 ? p.SideshowsResearchMore.Score : p.FeaturesResearchMore.Score)
                     : 0;
        }
        if (action == 76) {
            if (!available
                || days_since(c, analyser(c).last_object_built) < p.WaitingTimes.DaysForObjects
                || category_count(c, category) < p.FeaturesPoorSelection.MoreFeaturesThan)
                return 0;
            return static_cast<std::uint32_t>(numeric::product(100, catalog_definitions(c, category, true))) / static_cast<std::uint32_t>(available) < static_cast<std::uint32_t>(p.FeaturesPoorSelection.PoorerThan) ? p.FeaturesPoorSelection.Score : 0;
        }
        if (action != 75
            && !catalog_definitions(c, action == 74 ? ObjectUiCategory::shops : ObjectUiCategory::sideshows, true))
            return 0;
        const auto built = catalog_definitions(c, category, true);
        if (action == 75 && !built)
            return 0;
        if (days_since(c, analyser(c).last_object_built) < p.WaitingTimes.DaysForObjects)
            return 0;
        if (action == 75)
            return static_cast<double>(ride_definitions(c, true)) * p.SideshowsBuildMoreSideshows.SideshowsPerRideRatio
                             > built
                        && record<BankRecord>(c, primary(c).saved.bank_account).balance
                               > p.SideshowsBuildMoreSideshows.WealthMoreThan
                     ? p.SideshowsBuildMoreSideshows.Score
                     : 0;
        if (built)
            return 0;
        const auto days =
            fixed::narrow(std::bit_cast<std::int64_t>(
                              scale_world_ticks_to_filetime_delta(primary(c).calendar, base(c).saved.game_tick))
                          / 864000000000ll);
        return action == 74 ? fixed::add(p.SideshowsBuildSideshows.Score, numeric::product(p.SideshowsBuildSideshows.AdditionalPerDay, days)) : fixed::add(p.FeaturesBuildFeatures.Score, numeric::product(p.FeaturesBuildFeatures.AdditionalPerDay, days));
    }
    if (action == 542) {
        const auto& played = get_or_create_theme_global_info_manager(c).history[604];
        if (!played.is_active || (base(c).saved.game_tick >> 2) - (played.last_played_world_tick >> 2) <= 60)
            return 0;
        return record<ChallengeManagerRecord>(c, primary(c).saved.challenge_manager).records[3].status
                    == ChallengeStatus::available
                 ? 10000
                 : 0;
    }
    if (action >= 68 && action <= 72) {
        const auto& p = *c.advisor.balance;
        const auto category = action == 72 ? ObjectUiCategory::sideshows : ObjectUiCategory::shops;
        //The sideshow callback performs this catalog scan before its UI gate.
        const auto available = action == 72 ? catalog_definitions(c, category, false, false) : 0;
        if (!enabled(c))
            return 0;
        if (action == 69) {
            if (!staff_count(c, ThingKind::researcher))
                return 0;
            const auto built = catalog_definitions(c, category, true);
            if (!active_object_count(c,
                                     [](const auto& o) {
                                         return has_capability(o, ParkObjectCapabilities::thinking_space);
                                     })
                || !built)
                return 0;
            const auto& lab = record<ResearchLabRecord>(c, base(c).saved.research_lab);
            if (!count_unfinished_research_projects(&lab, ResearchCategoryFilter::shops)
                || estimate_research_category_output(c,
                                                     &record<ResearchLabRecord>(c, primary(c).saved.research_lab),
                                                     ResearchCategory::shops))
                return 0;
            return built == catalog_definitions(c, category, false, false) ? p.ShopsResearchMore.Score : 0;
        }
        const auto built = catalog_definitions(c, category, true);
        if (action == 68 || action == 72) {
            const auto choices = action == 72 ? available : catalog_definitions(c, category, false, false);
            if (!choices || days_since(c, analyser(c).last_object_built) < p.WaitingTimes.DaysForObjects)
                return 0;
            const auto minimum =
                action == 72 ? p.SideshowsPoorSelection.MoreSideshowsThan : p.ShopsPoorSelection.MoreShopsThan;
            const auto threshold = action == 72 ? p.SideshowsPoorSelection.PoorerThan : p.ShopsPoorSelection.PoorerThan;
            return category_count(c, category) >= minimum && static_cast<std::uint32_t>(numeric::product(100, built)) / static_cast<std::uint32_t>(choices) < static_cast<std::uint32_t>(threshold) ? (action == 72 ? p.SideshowsPoorSelection.Score : p.ShopsPoorSelection.Score) : 0;
        }
        if (action == 70 && !ride_definitions(c, true))
            return 0;
        if (action == 71 && !built)
            return 0;
        if (days_since(c, analyser(c).last_object_built) < p.WaitingTimes.DaysForObjects)
            return 0;
        if (action == 70) {
            if (built)
                return 0;
            const auto days =
                fixed::narrow(std::bit_cast<std::int64_t>(
                                  scale_world_ticks_to_filetime_delta(primary(c).calendar, base(c).saved.game_tick))
                              / 864000000000ll);
            return fixed::add(p.ShopsBuildShops.Score, numeric::product(p.ShopsBuildShops.AdditionalPerDay, days));
        }
        //Both sides count shops in the binary, despite the SAM's
        //ShopsPerRideRatio.
        return static_cast<double>(catalog_definitions(c, category, true)) * p.ShopsBuildMoreShops.ShopsPerRideRatio
                         > built
                    && record<BankRecord>(c, primary(c).saved.bank_account).balance
                           > p.ShopsBuildMoreShops.WealthMoreThan
                 ? p.ShopsBuildMoreShops.Score
                 : 0;
    }
    if (action == 512 || action == 514 || action == 515 || action == 517 || action == 518) {
        if (!is_first_hour_of_gameplay(c))
            return 0;
        const auto& p = *c.advisor.balance;
        if (action == 512)
            return !prebuilt_count(c)
                        && category_count(c, ObjectUiCategory::rides) >= 2
                        && category_count(c, ObjectUiCategory::shops) >= 1
                     ? p.New.ADVISORSTUTBUILDCOASTER
                     : 0;
        if (action == 518)
            return catalog_definitions(c, ObjectUiCategory::shops, true)
                        && ride_definitions(c, true)
                        && prebuilt_count(c)
                        && !catalog_definitions(c, ObjectUiCategory::sideshows, true)
                        && !c.staff.interaction.cursor_presentation
                     ? p.New.ADVISORSTUTBUILDSIDESHOW
                     : 0;
        const auto role = action == 514 ? StaffType::mechanic
                        : action == 515 ? StaffType::handyman
                                        : StaffType::entertainer;
        const auto kind = action == 514 ? ThingKind::mechanic
                        : action == 515 ? ThingKind::handyman
                                        : ThingKind::entertainer;
        if (staff_count(c, kind) || !candidate(c, role))
            return 0;
        if (action == 514)
            return numeric::product(p.New.ADVISORSTUTHIREMECHANIC, poor_ride_count(c, p.StaffHireMechanics1.PoorerStateThan, false));
        if (action == 517)
            return !get_or_create_theme_global_info_manager(c).history[517].is_active && unhappy_percent(c) > 20
                     ? p.New.ADVISORSTUTHIREENTERTAINER
                     : 0;
        std::uint32_t litter = 0;
        auto& grids = analyser(c).grid;
        for (std::size_t i = grids.size(); i; --i)
            litter += static_cast<std::uint32_t>(count_cached_analyser_litter_cells(c, grids[i - 1], 1));
        return fixed::word(litter) > 1 ? p.New.ADVISORSTUTHIREHANDYMAN : 0;
    }
    if (action == 508 || action == 511 || action == 626) {
        if (action != 626 ? !is_first_hour_of_gameplay(c) : !enabled(c))
            return 0;
        for (auto* node = c.entities.active_head; node; node = node->active_next) {
            if (node->payload->kind != ThingKind::park_object)
                continue;
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(node->payload);
            const auto& d = definition(c, object);
            if (action != 626) {
                if (d.which_ui_type != (action == 508 ? ObjectUiCategory::shops : ObjectUiCategory::rides)
                    || has_clear_entry_exit_path(c, object)
                    || (action == 511 && (!object.can_load || c.ui.panels.selected_record == &object.base.header)))
                    continue;
                c.advisor.behavior.callback_thing_id = object.base.header.id;
                c.advisor.behavior.callback_secondary = object.definition_id;
            } else {
                if (d.is_special == 1 || d.which_track_type != 3 || object.prebuilt_track_edited)
                    continue;
                const auto* mesh = world_operations::find_mesh_runtime_opening_state(c, object.mesh_instance_id);
                if (mesh && (mesh->placement_flags & 1u) && !(mesh->placement_flags & 2u))
                    continue;
            }
            return 10000;
        }
        return 0;
    }
    if (action < 3 || action > 27)
        return {};
    const auto& p = *c.advisor.balance;
    if (action == 3 || action == 4) {
        if (!enabled(c))
            return 0;
        return action == 3 ? numeric::product(p.VisitorsThirsty.ScorePerThirstyPerson, thirsty_or_hungry(c, false, static_cast<std::uint8_t>(p.VisitorsThirsty.ThirstierThan)))
                           : numeric::product(p.VisitorsHungry.ScorePerHungryPerson, thirsty_or_hungry(c, true, static_cast<std::uint8_t>(p.VisitorsHungry.HungrierThan)));
    }
    if (action == 24 || action == 25)
        return 0; //Actual constant-zero native bodies.
    if (action == 5 || action == 6) {
        if (is_first_hour_of_gameplay(c)
            || (action == 5 && !enabled(c))
            || staff_count(c, ThingKind::mechanic) > 0
            || !candidate(c, StaffType::mechanic))
            return 0;
        return action == 5 ? numeric::product(p.StaffHireMechanics1.ScorePerWornRide, poor_ride_count(c, p.StaffHireMechanics1.PoorerStateThan, false)) : numeric::product(p.StaffHireMechanics2.ScorePerBrokenRide, poor_ride_count(c, 0, true));
    }
    if (action == 7) {
        if (is_first_hour_of_gameplay(c) || !enabled(c) || days_since_open(c) < p.WaitingTimes.DaysForStaff)
            return 0;
        const auto extra = fixed::subtract(ride_count(c), numeric::product(p.StaffHireMechanics3.IdealNumberOfRidesPerMechanic, staff_count(c, ThingKind::mechanic)));
        return extra > 0 && candidate(c, StaffType::mechanic) ? numeric::product(extra, p.StaffHireMechanics3.ScorePerExtraRide) : 0;
    }
    if (action == 8 || action == 9) {
        if (primary(c).saved.park_closed
            || staff_count(c, ThingKind::handyman) > 0
            || (action == 8 && !enabled(c))
            || !candidate(c, StaffType::handyman))
            return 0;
        const auto litter = litter_percent(c);
        if (action == 8)
            return litter <= p.StaffHireHandymen1.LitterPointCutoffHigh ? fixed::subtract(numeric::product(litter, p.StaffHireHandymen1.ScorePerLitterPoint), p.StaffHireHandymen1.LitterPointCutoffLow) : 0;
        return litter >= p.StaffHireHandymen2.LitterPointCutoffLow ? fixed::subtract(numeric::product(litter, p.StaffHireHandymen2.ScorePerLitterPoint), p.StaffHireHandymen2.LitterPointCutoffLow) : 0;
    }
    if (action == 10) {
        if (!enabled(c)
            || primary(c).saved.park_closed
            || !staff_count(c, ThingKind::handyman)
            || !candidate(c, StaffType::handyman))
            return 0;
        const auto cells = count_park_developed_cells(c, analyser(c), 0);
        if (cells < p.StaffHireHandymen3.MinCellsForConsideration)
            return 0;
        const auto extra = fixed::subtract(cells, numeric::product(p.StaffHireHandymen3.IdealDevelopedCellsPerHandyman, staff_count(c, ThingKind::handyman)));
        return extra > 0 ? numeric::product(extra, p.StaffHireHandymen3.ScorePerExtra10Cells) : 0; //No /10 in the executable.
    }
    if (action == 11 || action == 12 || action == 13 || action == 14) {
        const auto role = action == 14 ? StaffType::entertainer : StaffType::guard;
        const auto kind = action == 14 ? ThingKind::entertainer : ThingKind::guard;
        if (is_first_hour_of_gameplay(c) || (action != 12 && !enabled(c)) || primary(c).saved.park_closed)
            return 0;
        if (action == 11 && days_since_open(c) < p.WaitingTimes.DaysForStaff)
            return 0;
        if (action != 13 && staff_count(c, kind) > 0)
            return 0;
        if (!candidate(c, role))
            return 0;
        if (action == 11)
            return numeric::product(p.StaffHireGuards1.ScorePerExtraBusyPoint, fixed::subtract(busy(c), p.StaffHireGuards1.AtLeastThisBusy));
        if (action == 12)
            return fixed::add(p.StaffHireGuards2.Score, numeric::product(p.StaffHireGuards2.AdditionalPerMisbehavingKid, fixed::word(analyser(c).misbehaving_kids)));
        if (action == 14)
            return unhappy_percent(c) > 30 ? numeric::product(p.StaffHireEntertainers1.ScorePerExtraBusyPoint, fixed::subtract(busy(c), p.StaffHireEntertainers1.AtLeastThisBusy)) : 0;
        if (days_since_open(c) < p.WaitingTimes.DaysForStaff)
            return 0;
        const auto cells = count_park_developed_cells(c, analyser(c), 0);
        if (cells < p.StaffHireGuards3.MinCellsForConsideration)
            return 0;
        const auto extra = fixed::subtract(cells, numeric::product(p.StaffHireGuards3.IdealDevelopedCellsPerGuard, staff_count(c, ThingKind::guard)));
        return extra > 0 ? numeric::product(extra, p.StaffHireGuards3.ScorePerExtra10Cells) : 0;
    }
    if (action >= 15 && action <= 17) {
        if (!active_object_count(c,
                                 [](const auto& object) {
                                     return has_capability(object, ParkObjectCapabilities::thinking_space);
                                 })
            || !enabled(c)
            || primary(c).saved.park_closed)
            return 0;
        if (action == 17) {
            const auto& lab = record<ResearchLabRecord>(c, primary(c).saved.research_lab);
            if (!research_slot_has_unfinished_project(&lab, ResearchWorkSlot::primary)
                && !research_slot_has_unfinished_project(&lab, ResearchWorkSlot::secondary))
                return 0;
            const auto capacity = get_researcher_ability_capacity(c);
            if (!capacity
                || elapsed_months(c) < p.StaffHireResearchers3.MonthsIntoGameForConsideration
                || !staff_count(c, ThingKind::researcher)
                || !count_world_staff_candidates(base(c), StaffType::researcher)
                || !candidate(c, StaffType::researcher))
                return 0;
            const auto percent = 100u * static_cast<std::uint32_t>(sum_researcher_ability_by_experience(c)) / capacity;
            return percent < static_cast<std::uint32_t>(p.StaffHireResearchers3.SlowerThan)
                     ? p.StaffHireResearchers3.Score
                     : 0;
        }
        if (staff_count(c, ThingKind::researcher) > 0
            || !count_unfinished_research_projects(&record<ResearchLabRecord>(c, base(c).saved.research_lab),
                                                   ResearchCategoryFilter::rides))
            return 0;
        if (action == 15) {
            const auto available = ride_definitions(c, false);
            if (!available)
                return 0;
            const auto built = ride_definitions(c, true);
            if (built == available || !candidate(c, StaffType::researcher))
                return 0;
            const auto percent = div(numeric::product(100, built), available);
            return percent > p.StaffHireResearchers1.Most ? numeric::product(p.StaffHireResearchers1.PointsPerExtraPct, fixed::subtract(percent, p.StaffHireResearchers1.Most)) : 0;
        }
        if (!candidate(c, StaffType::researcher))
            return 0;
        return ride_definitions(c, true) == ride_definitions(c, false) ? p.StaffHireResearchers2.Score : 0;
    }
    if (action == 22) {
        return enabled(c)
                    && staff_count(c, ThingKind::guard)
                    && fixed::word(analyser(c).misbehaving_kids_that_got_away)
                           > p.StaffGuardBuildCamera.AcceptableTrouble
                 ? p.StaffGuardBuildCamera.Score
                 : 0;
    }
    if (!enabled(c))
        return 0;
    const auto area_score = [&](ThingKind role, const auto& parameters) {
        if (elapsed_months(c) < parameters.MonthsIntoGameForConsideration)
            return 0;
        const auto count = staff_count(c, role);
        if (count < parameters.AtLeastThisMany)
            return 0;
        const auto percent = div(numeric::product(100, custom_patrols(c, role)), count);
        return percent <= parameters.DontBotherIfPatrolledUp ? numeric::product(fixed::subtract(100, percent), parameters.ScorePerUnpatrolledPct) : 0;
    };
    match (action) {
        case 18 => return area_score(ThingKind::mechanic, p.StaffSetMechanicArea);
        case 20 => return area_score(ThingKind::handyman, p.StaffSetHandymanArea);
        case 23 => return area_score(ThingKind::guard, p.StaffSetGuardArea);
        case 26 => return area_score(ThingKind::entertainer, p.StaffSetEntertainerArea);
        case _ => {}
    }
    const auto coverage_score = [&](ThingKind role,
                                    StaffCoverageCellFilter filter,
                                    const auto& gate,
                                    const auto& parameters,
                                    bool require_custom) {
        if (elapsed_months(c) < gate.MonthsIntoGameForConsideration)
            return 0;
        const auto count = staff_count(c, role);
        if (require_custom ? (count < gate.AtLeastThisMany || !custom_patrols(c, role)) : !count)
            return 0;
        const auto coverage = calculate_world_staff_patrol_coverage(c, role, filter);
        const auto score = numeric::product(parameters.ScorePerNonCoveredPercent, fixed::subtract(100, coverage));
        return score < parameters.Score ? 0 : score;
    };
    match (action) {
        case 19 => {
            return coverage_score(ThingKind::mechanic,
                                  StaffCoverageCellFilter::object_entries,
                                  p.StaffSetMechanicArea,
                                  p.StaffMechanicCoverage,
                                  true);
        }
        case 21 => {
            return coverage_score(ThingKind::handyman,
                                  StaffCoverageCellFilter::paths_and_queues,
                                  p.StaffSetHandymanArea,
                                  p.StaffHandymanCoverage,
                                  false);
        }
        case 27 => {
            return coverage_score(ThingKind::entertainer,
                                  StaffCoverageCellFilter::developed,
                                  p.StaffSetEntertainerArea,
                                  p.StaffEntertainerCoverage,
                                  false);
        }
        case _ => return {};
    }
}
} //namespace simcoaster
