#include "advisor_score_context.hpp"
#include "simcoaster/mesh_runtime_queries.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"

namespace simcoaster {
namespace {
using namespace advisor_detail;

std::int64_t elapsed(AdvisorWorld& c, std::int64_t period) {
    return std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(primary(c).calendar, base(c).saved.game_tick)) / period;
}

int staff_count(AdvisorWorld& c) {
    int n = 0;
    active(c, [&](const auto& h) {
        n += is_staff_record_kind(h.kind);
    });
    return n;
}

int capability_count(AdvisorWorld& c, ParkObjectCapabilities f) {
    int n = 0;
    objects(c, [&](const auto& o) {
        n += has(o, f);
    });
    return n;
}

int category_count(AdvisorWorld& c, ObjectUiCategory category) {
    return count_ordinary_park_objects(c, category);
}

int catalog_count(AdvisorWorld& c, ObjectUiCategory category, bool built, bool exclude_ticket) {
    int n = 0;
    for (auto* d = first_object_catalog_entry(&c.resources.object_catalog); d; d = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (d->which_ui_type == category) {
            const auto* row = find_world_object_control(base(c).object_controls, d->id);
            if (!row || !d->balance) {
                throw std::logic_error("Advisor catalog control");
            }
            n += (built ? fixed::word(row->live_instance_count) > 0 : row->available != 0) && (!exclude_ticket || !d->balance->UsageInfo.GoldenTicketCost);
        }
    }
    return n;
}

int days_since_object(AdvisorWorld& c) {
    FileTime t {};
    calculate_world_calendar_filetime(c, &base(c).calendar, &t);
    const auto now = std::uint64_t { t.low } | (std::uint64_t { t.high } << 32);
    return fixed::narrow(std::bit_cast<std::int64_t>(now - analyser(c).last_object_built) / 864000000000ll);
}

double happy_people(AdvisorWorld& c, int generation = 4) {
    return average_eligible_person_happiness(c, generation);
}

double happy_staff(AdvisorWorld& c) {
    int count = 0, total = 0;
    active(c, [&](const auto& h) {
        if (is_staff_record_kind(h.kind)) {
            ++count;
            total = fixed::add(total, byte(reinterpret_cast<const StaffRuntimeRecord*>(&h)->happiness_score));
        }
    });
    return count ? static_cast<double>(total) / count : 0;
}

int average(const AnalyserDwordHistory144& h, int maximum, int& count) {
    count = std::min(maximum, available(h));
    return count > 0 ? fixed::truncate_integer(sum(h,
                                                   maximum,
                                                   [](auto v) {
                                                       return fixed::word(v);
                                                   })
                                               / count)
                     : 0;
}

ThingId select_unconnected_queue(AdvisorWorld& c) {
    ThingId selected = 0;
    objects(c, [&](auto& o) {
        if (!selected && has(o, ParkObjectCapabilities::is_choosable) && has(o, ParkObjectCapabilities::has_queue) && !has_usable_entry_approach(c, &o) && !park_object_definition_is_marked_special(c, &o) && c.construction.park_object != &o) {
            selected = o.base.header.id;
        }
    });
    return selected;
}

int script(AdvisorWorld& c, const ParkObjectRuntimeView& o, int slot) {
    return world_operations::script_slot(c, world_operations::find_script(c, o.script_id), slot);
}

struct PlantSelection {
    int dying {}, dead {};
    ThingId driest {}, first_dead {};
};

PlantSelection plants(AdvisorWorld& c) {
    PlantSelection result;
    int lowest = 100;
    linked_objects(c, [&](const auto& o) {
        if (!has(o, ParkObjectCapabilities::requires_watering)) {
            return;
        }
        if (script(c, o, 0) == 2) {
            ++result.dying;
            const auto condition = byte(o.repair_state);
            if (condition < lowest) {
                lowest = condition;
                result.driest = o.base.header.id;
            }
        }
        if (script(c, o, 4)) {
            ++result.dead;
            if (!result.first_dead) {
                result.first_dead = o.base.header.id;
            }
        }
    });
    return result;
}

ThingId blocked_exit_ride(AdvisorWorld& c) {
    ThingId selected = 0;
    active(c, [&](ThingHeader& h) {
        if (selected || (h.kind != ThingKind::person && !is_staff_record_kind(h.kind) && h.kind != ThingKind::chat_presence)) {
            return;
        }
        if (h.kind == ThingKind::chat_presence) {
            throw std::logic_error("Online presence mobile runtime is not reconstructed");
        }
        auto& map = *reinterpret_cast<MapObjectBase*>(&h);
        auto& mobile = h.kind == ThingKind::person ? reinterpret_cast<PersonRuntimeView*>(&h)->mobile : reinterpret_cast<StaffRuntimeRecord*>(&h)->mobile;
        if (next_world_grid_change_serial(c) < mobile.stranded_time) {
            mobile.stranded_time = 0;
        }
        if (!mobile.stranded_time) {
            return;
        }
        const auto id = static_cast<CellId>((static_cast<std::uint32_t>(map.y >> 8) << 7) + (map.x >> 8) + 1u);
        const auto& entry = base(c).map_cells.at(id - 1).base;
        if (entry.common.type != 1) {
            return;
        }
        const auto* exit = resolve_world_cell_from_selector_flag_byte(c, &entry, entry.common.direction);
        if (!exit || exit->base.common.type != 10) {
            return;
        }
        const auto object = find_first_park_object_id_in_map_cell(c, &base(c).map_cells.at(exit->base.common.parent_id - 1));
        if (object && definition(c, record<ParkObjectRuntimeView>(c, object)).which_ui_type == ObjectUiCategory::rides) {
            selected = object;
        }
    });
    return selected;
}

ThingId scariest_ride(AdvisorWorld& c) {
    ThingId selected = 0;
    int best = 0;
    objects(c, [&](auto& o) {
        if (definition(c, o).which_ui_type == ObjectUiCategory::rides) {
            const auto metric = get_park_object_excitement(c, o, false);
            if (!selected || metric > best) {
                selected = o.base.header.id;
                best = metric;
            }
        }
    });
    return selected;
}

int average_ride_excitement(AdvisorWorld& c) {
    auto& cache = analyser(c).metrics[5];
    const auto tick = base(c).saved.game_tick;
    if (cache.cache_valid && 10u >= (tick >> 2) - (cache.last_calculation_tick >> 2)) {
        return fixed::word(cache.cached_value_bits);
    }
    int count = 0, total = 0;
    objects(c, [&](auto& o) {
        if (definition(c, o).which_ui_type == ObjectUiCategory::rides) {
            ++count;
            total = fixed::add(total, get_park_object_excitement(c, o, false));
        }
    });
    const auto value = count ? quotient(total, count) : 0;
    cache = { tick, static_cast<std::uint32_t>(value), 1 };
    return value;
}

int staffroom_coverage(AdvisorWorld& c, int radius) {
    for (auto& cell : base(c).map_cells) {
        cell.staff_patrol_coverage = 0;
    }
    objects(c, [&](const auto& o) {
        if (!has(o, ParkObjectCapabilities::chills_you_out)) {
            return;
        }
        const auto* shape = definition(c, o).balance->Info.Shape.get();
        if (!shape) {
            throw std::logic_error("Advisor staffroom shape");
        }
        const auto x = static_cast<int>(o.base.x >> 8), y = static_cast<int>(o.base.y >> 8);
        const auto left = std::max(0, fixed::subtract(x, radius)), top = std::max(0, fixed::subtract(y, radius));
        const auto right = std::min(127, fixed::add(fixed::add(x, radius), shape->width)), bottom = std::min(127, fixed::add(fixed::add(y, radius), shape->height));
        //Native clamps the exclusive ends to127, omitting the outermost
        //row/column.
        for (int row = top; row < bottom; ++row) {
            for (int col = left; col < right; ++col) {
                base(c).map_cells.at(static_cast<std::size_t>(row) * 128 + col).staff_patrol_coverage = 1;
            }
        }
    });
    //MOV ECX,offset sub_5B80B0 / TEST ECX,ECX, not a call. The
    //uncovered numerator never increments, despite the coverage writes above.
    return 0;
}

bool available_feature(AdvisorWorld& c, bool photo) {
    for (auto* d = first_object_catalog_entry(&c.resources.object_catalog); d; d = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (!d->balance) {
            throw std::logic_error("Advisor facility definition");
        }
        if (photo ? d->balance->Info.IsPhotoBooth != 0 : d->balance->UsageInfo.IsInfoDesk == 1) {
            const auto* control = find_world_object_control(primary(c).object_controls, d->id);
            if (!control) {
                throw std::logic_error("Advisor facility control");
            }
            if (photo) {
                return control->available != 0;
            }
            if (control->available) {
                return true;
            }
        }
    }
    return false;
}

int count_info_or_photo(AdvisorWorld& c, bool photo) {
    int count = 0;
    objects(c, [&](const auto& o) {
        const auto& b = *definition(c, o).balance;
        count += photo ? b.Info.IsPhotoBooth == 1 : b.UsageInfo.IsInfoDesk == 1;
    });
    return count;
}

int photo_tutorial(AdvisorWorld& c) {
    if (!enabled(c) || primary(c).saved.park_closed) {
        return 0;
    }
    const auto available = available_feature(c, true);
    int mechanics = 0;
    active(c, [&](const auto& h) {
        if (h.kind == ThingKind::mechanic && !h.retirement_pending && reinterpret_cast<const StaffRuntimeRecord*>(&h)->first_known_ability == ThemeAbility::ride_upgrading) {
            ++mechanics;
        }
    });
    return available && mechanics && category_count(c, ObjectUiCategory::rides) > 5 && !count_info_or_photo(c, true) ? c.advisor.balance->New.ADVISORFEATURESNOPHOTOKIOSKS : 0;
}
enum class TutorialGate : std::size_t {
    view_rotate,
    view_zoom,
    item_rotate,
    compass,
    right_click,
    paths,
    score
};

bool gate(AdvisorWorld& c, TutorialGate which) {
    return get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[std::to_underlying(which)] != 0;
}

int delegated(AdvisorWorld& c, int action) {
    if (auto s = evaluate_advisor_park_runtime_score(c, action)) {
        return *s;
    }
    if (auto s = evaluate_advisor_staff_and_facility_score(c, action)) {
        return *s;
    }
    if (auto s = evaluate_advisor_staff_decision_score(c, action)) {
        return *s;
    }
    throw std::logic_error("Advisor reminder target " + std::to_string(action));
}
} //namespace

std::optional<std::int32_t> evaluate_advisor_park_runtime_score(AdvisorWorld& c, std::int32_t action) {
    using namespace advisor_detail;
    const auto& p = *c.advisor.balance;
    //Native action247 counts the active used-thing list (5B3EC0), rather than
    //the saved park-object chain. Closed Staff Rooms still qualify, and the
    //callback has no UI-selection, FirstHour, training, or staffing gate.
    if (action == 247) {
        return capability_count(c, ParkObjectCapabilities::chills_you_out) ? p.HelpMessage.Score : 0;
    }
    if (action == 286 || action == 287 || action == 389) {
        int burning = 0, firefighters = 0;
        linked_objects(c, [&](const auto& o) {
            burning += has(o, ParkObjectCapabilities::holds_litter) && script(c, o, 5) != 0;
        });
        active(c, [&](const auto& h) {
            if (h.kind == ThingKind::handyman && !h.retirement_pending && reinterpret_cast<const StaffRuntimeRecord*>(&h)->first_known_ability == ThemeAbility::firefighting) {
                ++firefighters;
            }
        });
        const auto guards = count_linked_staff_by_kind(c, std::to_underlying(ThingKind::guard));
        return burning && guards && (action == 287 ? !firefighters : firefighters) ? 10000 : 0;
    }
    if (action == 289) {
        //Native 0x5B4770 counts RequiresWatering objects with nonzero RSSE slot12.
        //The callback tests the entire gardener roster, regardless of ability.
        int burning = 0;
        linked_objects(c, [&](const auto& o) {
            burning += has(o, ParkObjectCapabilities::requires_watering) && script(c, o, 12) != 0;
        });
        return burning && !count_linked_staff_by_kind(c, std::to_underlying(ThingKind::gardener)) && count_linked_staff_by_kind(c, std::to_underlying(ThingKind::guard)) ? p.PrankPlantfireNoFirefighters.Score : 0;
    }
    if (action == 285 || action == 288 || action == 290 || action == 291 || action == 390 || action == 391 || action == 392) {
        return count_linked_staff_by_kind(c, std::to_underlying(ThingKind::guard)) ? 10000 : 0;
    }

    struct Reminder {
        int action, target;
        bool ui;
    };

    constexpr std::array reminders { Reminder { 379, 59, true }, Reminder { 382, 86, false }, Reminder { 384, 103, false }, Reminder { 385, 109, false }, Reminder { 394, 135, true }, Reminder { 397, 141, true }, Reminder { 398, 225, false }, Reminder { 400, -1, false }, Reminder { 401, -1, false }, Reminder { 402, -1, false }, Reminder { 403, 279, false }, Reminder { 404, 281, false }, Reminder { 405, 300, false }, Reminder { 406, 332, true }, Reminder { 407, 334, true } };
    if (const auto it = std::ranges::find(reminders, action, &Reminder::action); it != reminders.end()) {
        if (it->ui && !enabled(c)) {
            return 0;
        }
        if (action == 382 && !record<BankRecord>(c, primary(c).saved.bank_account).admission_fee) {
            return 0;
        }
        if (fixed::word(history(c, action).play_count) > 1) {
            return 0;
        }
        return it->target < 0 ? 0 : product(2, delegated(c, it->target));
    }
    if (action == 383) {
        if (!enabled(c) || fixed::word(history(c, 383).play_count) > 1) {
            return 0;
        }
        const auto& h = analyser(c).byte_histories[0];
        if (available(h) < 50) {
            return 0;
        }
        const auto value = fixed::truncate_integer(sum(h,
                                                       50,
                                                       [](auto v) {
                                                           return v;
                                                       })
                                                   / 50);
        return static_cast<std::uint8_t>(value) > p.PricingTicketTooCheap.TicketSatisfactionOver ? product(2, p.PricingTicketTooCheap.Score) : 0;
    }
    if (action == 58 || action == 59) {
        if (!enabled(c)) {
            return 0;
        }
        if (action == 58) {
            const auto choices = catalog_count(c, ObjectUiCategory::rides, false, true);
            if (!choices || days_since_object(c) < p.WaitingTimes.DaysForObjects || category_count(c, ObjectUiCategory::rides) < p.RidesPoorSelection.MoreRidesThan) {
                return 0;
            }
            return static_cast<std::uint32_t>(product(100, catalog_count(c, ObjectUiCategory::rides, true, true))) / static_cast<std::uint32_t>(choices) < static_cast<std::uint32_t>(p.RidesPoorSelection.PoorerThan) ? p.RidesPoorSelection.Score : 0;
        }
        if (!count_linked_staff_by_kind(c, std::to_underlying(ThingKind::researcher))) {
            return 0;
        }
        const auto built = catalog_count(c, ObjectUiCategory::rides, true, true);
        if (!built) {
            return 0;
        }
        const auto& lab = record<ResearchLabRecord>(c, base(c).saved.research_lab);
        return count_unfinished_research_projects(&lab, ResearchCategoryFilter::rides) && !estimate_research_category_output(c, &record<ResearchLabRecord>(c, primary(c).saved.research_lab), ResearchCategory::rides) && built == catalog_count(c, ObjectUiCategory::rides, false, true) ? p.RidesResearchMore.Score : 0;
    }
    if (action == 103) {
        if (elapsed(c, 25920000000000ll) < 3) {
            return 0;
        }
        int n = 0, m = 0;
        const auto wages = average(analyser(c).long_histories[4], p.WagesHigh.AverageOverMonths, n), income = average(analyser(c).long_histories[6], p.WagesHigh.AverageOverMonths, m);
        return n && m && static_cast<double>(income) * p.WagesHigh.WagesTimesHigherThanIncome < wages ? p.WagesHigh.Score : 0;
    }
    if (action == 109) {
        return select(c, select_unconnected_queue(c)) ? p.QueueUnconnected.Score : 0;
    }
    if (action == 115 || action == 131) {
        if (!enabled(c)) {
            return 0;
        }
        const auto& h = analyser(c).tiny_history;
        if (available(h) < (action == 115 ? 2 : 1)) {
            return 0;
        }
        if (action == 115 && fixed::narrow(elapsed(c, 25920000000000ll)) >= p.HypeNewpark.MaxMonths) {
            return 0;
        }
        const auto now = sample(h, 0);
        if (!now) {
            return 0;
        }
        if (action == 131) {
            if (fixed::word(*now) <= p.BigBusloadArrived.HowBigIsBig) {
                return 0;
            }
            c.advisor.behavior.callback_secondary = get_bus_stop_cell(c.visitors.environment, BusStopSelector::b);
            return p.BigBusloadArrived.Score;
        }
        const auto prior = sample(h, 1);
        if (!prior) {
            return 0;
        }
        const auto a = fixed::word(*now), b = fixed::word(*prior);
        return b && a < b && fixed::subtract(100, quotient(product(100, a), b)) > p.HypeNewpark.DroppedPercent ? p.HypeNewpark.Score : 0;
    }
    if (action == 132) {
        if (primary(c).saved.park_closed || category_count(c, ObjectUiCategory::rides) < p.VisitorsTooScared.MinimumRides || average_ride_excitement(c) <= p.VisitorsTooScared.AverageScarierThan) {
            return 0;
        }
        c.advisor.behavior.callback_thing_id = scariest_ride(c);
        return p.VisitorsTooScared.Score;
    }
    if (action == 133) {
        return staff_count(c) >= p.CongratStaffHappy.MinimumStaff && !(static_cast<double>(p.CongratStaffHappy.HappierThan) > happy_staff(c)) ? p.CongratStaffHappy.Score : 0;
    }
    if (action == 134) {
        return fixed::word(count_eligible_person_records(c, 4)) >= p.CongratVisitorsHappy.MinimumVisitors && !(static_cast<double>(p.CongratVisitorsHappy.HappierThan) > happy_people(c)) ? p.CongratVisitorsHappy.Score : 0;
    }
    if (action == 135) {
        return enabled(c) && world_operations::cached_park_value(c, &analyser(c), 10) > p.CongratSendPostcard.ValueBetterThan && fixed::word(count_eligible_person_records(c, 4)) > p.CongratSendPostcard.VisitorsMoreThan && static_cast<double>(p.CongratSendPostcard.HappinessGreaterThan) < happy_people(c) ? p.CongratSendPostcard.Score : 0;
    }
    if (action == 136 || action == 137) {
        int count = 0;
        active(c, [&](const auto& h) {
            if (h.kind == ThingKind::person) {
                const auto& person = *reinterpret_cast<const PersonRuntimeView*>(&h);
                if (record_passes_grid_predicates(c, &person.base)) {
                    count += action == 136 ? (person.balloon_script || person.remaining_balloon_life) : person.mobile.sprite_category == 6;
                }
            }
        });
        return action == 136 ? (count > p.CongratBalloons.MoreThan ? p.CongratBalloons.Score : 0) : (count > p.CongratCostumes.MoreThan ? p.CongratCostumes.Score : 0);
    }
    if (action == 140) {
        return enabled(c) && !primary(c).saved.park_closed && fixed::word(analyser(c).lifetime_visitors) >= p.HireStaffGeneral.MinVisitorsEver && !staff_count(c) ? p.HireStaffGeneral.Score : 0;
    }
    if (action == 225) {
        return select(c, blocked_exit_ride(c)) ? p.RideExitBlocked.Score : 0;
    }
    if (action >= 279 && action <= 282) {
        const auto gardeners = count_linked_staff_by_kind(c, std::to_underlying(ThingKind::gardener));
        const bool busy = action == 280 || action == 282;
        if (busy ? !gardeners : gardeners > 0) {
            return 0;
        }
        if (busy) {
            bool available = false;
            active(c, [&](const auto& h) {
                if (h.kind == ThingKind::gardener && !h.retirement_pending && classify_staff_activity(*reinterpret_cast<const StaffRuntimeRecord*>(&h)) != StaffActivityStatus::working) {
                    available = true;
                }
            });
            if (available) {
                return 0;
            }
        }
        const auto result = plants(c);
        if (action <= 280 ? result.dying != 1 : result.dying <= 1) {
            return 0;
        }
        if (!select(c, result.driest)) {
            return 0;
        }
        if (action == 279) {
            return p.PlantsDyingNoGardeners.Score;
        }
        if (action == 280) {
            return p.PlantsDyingBusyGardeners.Score;
        }
        return action == 281 ? p.PlantsLotsdyingNoGardeners.Score : p.PlantsLotsdyingBusyGardeners.Score;
    }
    if (action >= 293 && action <= 299) {
        const auto generation = action <= 295 ? action - 293 : action == 296 ? 4 : action - 297;
        if (action <= 295) {
            const auto value = static_cast<float>(happy_people(c, generation));
            const auto count = fixed::word(count_eligible_person_records(c, generation));
            if (action == 293) {
                return count > 30 && !(static_cast<double>(p.CongratKidsHappy.MoreThan) >= value) ? p.CongratKidsHappy.Score : 0;
            }
            if (action == 294) {
                return count > 20 && !(static_cast<double>(p.CongratAdultsHappy.MoreThan) >= value) ? p.CongratAdultsHappy.Score : 0;
            }
            return count > 10 && !(static_cast<double>(p.CongratOAPsHappy.MoreThan) >= value) ? p.CongratOAPsHappy.Score : 0;
        }
        if (!enabled(c) || primary(c).saved.park_closed || fixed::word(count_eligible_person_records(c, generation)) <= 0) {
            return 0;
        }
        const auto value = happy_people(c, generation);
        if (action == 296) {
            return p.VisitorsGeneralUnhappy.LessThan > value ? p.VisitorsGeneralUnhappy.Score : 0;
        }
        if (action == 297) {
            return p.KidsGeneralUnhappy.Maximum > value ? p.KidsGeneralUnhappy.Score : 0;
        }
        if (action == 298) {
            return p.AdultsGeneralUnhappy.Maximum > value ? p.AdultsGeneralUnhappy.Score : 0;
        }
        return p.OAPsGeneralUnhappy.Maximum > value ? p.OAPsGeneralUnhappy.Score : 0;
    }
    if (action == 300) {
        const auto result = plants(c);
        return result.dead && select(c, result.first_dead) ? p.PlantDie.Score : 0;
    }
    if (action == 330) {
        return enabled(c) && staffroom_coverage(c, p.StaffroomsTooFar.StaffroomRadius) > p.StaffroomsTooFar.MoreThanThisPercentNotCovered ? p.StaffroomsTooFar.Score : 0;
    }
    if (action == 331) {
        return !is_first_hour_of_gameplay(c) && enabled(c) && staff_count(c) > product(capability_count(c, ParkObjectCapabilities::chills_you_out), c.staff.recruitment.balance.all.MaxStaffPerStaffRoom) ? p.StaffroomsNotEnough.Score : 0;
    }
    if (action == 332 || action == 333) {
        if (!enabled(c) || (action == 333 && is_first_hour_of_gameplay(c)) || !capability_count(c, ParkObjectCapabilities::thinking_space)) {
            return 0;
        }
        const auto& lab = record<ResearchLabRecord>(c, primary(c).saved.research_lab);
        const bool working = research_slot_has_unfinished_project(&lab, ResearchWorkSlot::primary) || research_slot_has_unfinished_project(&lab, ResearchWorkSlot::secondary);
        if (action == 333) {
            if (working || fixed::word(lab.num_projects) <= 0 || !primary(c).saved.first_researcher) {
                return 0;
            }
            c.advisor.behavior.callback_thing_id = lab.base.header.id;
            return p.ResearchNoProject.Score;
        }
        if (!working) {
            return 0;
        }
        int assigned = 0;
        for (auto id : lab.secondary_staff) {
            assigned += id != 0;
        }
        for (auto id : lab.primary_staff) {
            if (id && c.entities.at(id).payload->kind == ThingKind::researcher) {
                ++assigned;
            }
        }
        return count_linked_staff_by_kind(c, std::to_underlying(ThingKind::researcher)) > assigned ? p.IdleResearchers.Score : 0;
    }
    if (action == 334) {
        return enabled(c) && available_feature(c, false) && !count_info_or_photo(c, false) ? p.InfoKioskBuild.Score : 0;
    }
    if (action == 335) {
        if (!enabled(c) || get_or_create_theme_global_info_manager(c).mission_research.info_number < 1) {
            return 0;
        }
        const bool space = capability_count(c, ParkObjectCapabilities::thinking_space) != 0;
        const auto built = catalog_count(c, ObjectUiCategory::rides, true, true);
        return built && count_unfinished_research_projects(&record<ResearchLabRecord>(c, base(c).saved.research_lab), ResearchCategoryFilter::rides) && !space && fixed::subtract(catalog_count(c, ObjectUiCategory::rides, false, true), built) <= p.ResearchBuildAndHire.RidesLeftToBuild ? p.ResearchBuildAndHire.Score : 0;
    }
    if (action == 337) {
        return enabled(c) && elapsed(c, 25920000000000ll) > p.VisitWebsite.MonthsIntoGameForConsideration && !history(c, 337).is_active ? p.VisitWebsite.Score : 0;
    }
    if (action == 413 || action == 414 || action == 415) {
        if (!enabled(c) || history(c, action).is_active) {
            return 0;
        }
        if (action == 415) {
            return gate(c, TutorialGate::item_rotate) && category_count(c, ObjectUiCategory::rides) > p.HelpImASpaz.ItemsTilItemRotateMsg ? 10000 : 0;
        }
        return gate(c, action == 413 ? TutorialGate::view_rotate : TutorialGate::view_zoom) && elapsed(c, 864000000000ll) > (action == 413 ? p.HelpImASpaz.DaysTilViewRotateMsg : p.HelpImASpaz.DaysTilViewZoomMsg) ? 10000 : 0;
    }
    if (action == 421 || action == 495) {
        if (!enabled(c)) {
            return 0;
        }
        bool flagged = true;
        if (action == 421) {
            const auto id = c.staff.interaction.cursor.cell;
            flagged = id && (primary(c).map_cells.at(id - 1).base.common.flags & 0xC0u);
        }
        const auto minutes = c.timing.first_hour_time / 60u;
        return fixed::word(history(c, action).play_count) < 2 && gate(c, action == 421 ? TutorialGate::compass : TutorialGate::right_click) && minutes > static_cast<std::uint32_t>(action == 421 ? p.HelpImASpaz.DaysTilCompassMsg : p.HelpImASpaz.MinsTilNotRightClickedMsg) && flagged ? (action == 421 ? 10000 : p.New.ADVISORNOTRIGHTCLICKED) : 0;
    }
    if (action == 501) {
        return photo_tutorial(c);
    }
    if (action == 528) {
        const auto& h = history(c, 527);
        return is_first_hour_of_gameplay(c) && h.is_active && (base(c).saved.game_tick >> 2) - (h.last_played_world_tick >> 2) > 600u && !is_advisor_action_active(c, 483) && can_proceed_with_current_objective(c) ? product(2, p.Challenge.Score) : 0;
    }
    if (action == 533) {
        return 0; //Native discards the pure first-hour query.
    }
    if (action == 624) {
        return enabled(c) && fixed::word(history(c, 624).play_count) <= 1 ? photo_tutorial(c) : 0;
    }
    if (action == 632) {
        return fixed::word(history(c, 406).play_count) < 2 && gate(c, TutorialGate::score) && elapsed(c, 864000000000ll) <= 3 ? p.HelpMessage.Score : 0;
    }
    if (action == 633) {
        if (!enabled(c)) {
            return 0;
        }
        (void)history(c, 406);
        if (!gate(c, TutorialGate::score)) {
            return 0;
        }
        const auto& challenges = record<ChallengeManagerRecord>(c, primary(c).saved.challenge_manager).records;
        return std::ranges::any_of(challenges,
                                   [](const auto& r) {
                                       return r.status == ChallengeStatus::preparing || r.status == ChallengeStatus::active;
                                   })
                 ? p.HelpMessage.Score
                 : 0;
    }
    if (action == 635) {
        if (!enabled(c)) {
            return 0;
        }
        ThingId selected = 0;
        objects(c, [&](const auto& o) {
            if (selected || definition(c, o).which_track_type != 3) {
                return;
            }
            const auto* mesh = world_operations::find_mesh_runtime_opening_state(c, o.mesh_instance_id);
            if (mesh && (mesh->placement_flags & 1u) && !(mesh->placement_flags & 2u) && mesh->geometry_peer_reference_count) {
                selected = o.base.header.id;
            }
        });
        return select(c, selected) ? 10000 : 0;
    }
    return {};
}
} //namespace simcoaster
