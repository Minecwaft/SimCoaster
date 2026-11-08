#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
World& world(EconomyWorld& c) {
    return require_bound(c.simulation.current, "analyser base World");
}

template <class T>
T& record(EconomyWorld& c, ThingId id) {
    auto* h = c.entities.at(id).payload;
    if (!h)
        throw std::logic_error("analyser World record");
    return *reinterpret_cast<T*>(h);
}

template <class F>
void active(EconomyWorld& c, F&& visit) {
    for (auto* row = c.entities.active_head; row;) {
        auto* h = row->payload;
        if (!h)
            throw std::logic_error("analyser active record");
        visit(*h);
        row = c.entities.at(h->id).active_next;
    }
}

template <class T, std::size_t N>
void append(SampleHistory<T, N>& h, T value) {
    h.current_entry = fixed::add(h.current_entry, 1);
    if (h.current_entry >= h.num_entries) {
        h.current_entry = 0;
        h.wrapped = 1;
    }
    h.data.at(static_cast<std::size_t>(h.current_entry)) = value;
}

double sum_recent(const AnalyserDwordHistory50& h, std::int32_t count) {
    count = std::min(count, h.wrapped ? h.num_entries : fixed::add(h.current_entry, 1));
    double sum = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto at = i <= h.current_entry ? fixed::subtract(h.current_entry, i)
                                             : fixed::subtract(fixed::add(h.num_entries, h.current_entry), i);
        sum += h.data.at(static_cast<std::size_t>(at));
    }
    return sum;
}

std::uint64_t now(EconomyWorld& c) {
    FileTime value{};
    calculate_world_calendar_filetime(c, &world(c).calendar, &value);
    return std::uint64_t{ value.low } | (std::uint64_t{ value.high } << 32);
}
} //namespace

double average_eligible_person_happiness(EconomyWorld& c, std::int32_t generation) {
    if (world(c).saved.park_closed)
        return 0;
    std::int32_t count = 0, total = 0;
    active(c, [&](const ThingHeader& h) {
        if (h.kind == ThingKind::person) {
            const auto& p = *reinterpret_cast<const PersonRuntimeView*>(&h);
            if ((generation == 4 || static_cast<std::int32_t>(p.generation) == generation)
                && record_passes_grid_predicates(c, &p.base)) {
                ++count;
                total = fixed::add(total, static_cast<std::uint8_t>(fixed::truncate_integer(p.happiness)));
            }
        }
    });
    return count ? static_cast<double>(total) / count : 0;
}

std::int32_t count_ordinary_park_objects(EconomyWorld& c, ObjectUiCategory category, std::int32_t upgrade) {
    std::int32_t count = 0;
    active(c, [&](const ThingHeader& h) {
        if (h.kind == ThingKind::park_object) {
            const auto& o = *reinterpret_cast<const ParkObjectRuntimeView*>(&h);
            const auto* d = get_park_object_definition(c, &o);
            if (!d)
                throw std::logic_error("analyser object definition");
            count += d->which_ui_type == category
                  && is_special_ride_group_id_unregistered(c, h.id)
                  && (category != ObjectUiCategory::rides || o.current_upgrade >= upgrade);
        }
    });
    return count;
}

std::int32_t get_cached_park_ride_object_count(EconomyWorld& c, ParkAnalyserState& a, std::uint32_t age) {
    auto& cache = a.metrics[1];
    const auto tick = world(c).saved.game_tick;
    if (cache.cache_valid && age >= (tick >> 2) - (cache.last_calculation_tick >> 2))
        return fixed::word(cache.cached_value_bits);
    std::uint32_t count = 0;
    active(c, [&](const ThingHeader& h) {
        if (h.kind == ThingKind::park_object) {
            const auto* d = get_park_object_definition(c, reinterpret_cast<const ParkObjectRuntimeView*>(&h));
            if (!d)
                throw std::logic_error("analyser ride definition");
            count += d->which_ui_type == ObjectUiCategory::rides;
        }
    });
    cache = { tick, count, 1 };
    return fixed::word(count);
}

std::int32_t calculate_park_path_litter_percentage(EconomyWorld& c) {
    std::int32_t total = 0, litter = 0;
    for (const auto& cell : world(c).map_cells) {
        const auto type = cell.base.common.type;
        total += type == 1 || type == 3 || type == 9 || cell.litter_selector;
        litter += cell.litter_selector != 0;
    }
    return total ? 100 * litter / total : 0;
}

std::int32_t calculate_park_object_and_staff_metric(EconomyWorld& c) {
    const auto objects = [&](ObjectUiCategory category, int weight, int cap, int upgrade = 0) {
        return std::min(cap,
                        weight
                            * count_ordinary_park_objects(c, category, upgrade)
                            / (category == ObjectUiCategory::rides && !upgrade ? 2 : 1));
    };
    auto score = 20 * std::min(1000, fixed::word(count_eligible_person_records(c, 4))) / 1000
               + objects(ObjectUiCategory::rides, 3, 20)
               + objects(ObjectUiCategory::shops, 2, 10)
               + objects(ObjectUiCategory::sideshows, 2, 10)
               + objects(ObjectUiCategory::features, 1, 10)
               + objects(ObjectUiCategory::rides, 1, 10, 2);
    for (auto role :
         { ThingKind::handyman, ThingKind::mechanic, ThingKind::entertainer, ThingKind::guard, ThingKind::researcher })
        score += std::min(4, count_linked_staff_by_kind(c, std::to_underlying(role)));
    return score;
}

void sample_park_analyser_metrics(EconomyWorld& c, ParkAnalyserState& a) {
    auto& h = a.long_histories;
    const auto* price = c.finance.shares_finance.share_price_context.get();
    if (!price || !c.referenced_world())
        throw std::logic_error("analyser finance owners");
    append(h[2], static_cast<std::uint32_t>(price->current_share_price));
    append(h[0], static_cast<std::uint32_t>(record<BankRecord>(c, c.referenced_world()->saved.bank_account).balance));
    for (std::size_t i = 3; i <= 13; ++i) {
        append(h[i], h[i].temporary);
        h[i].temporary = 0;
    }
    append(h[17], h[17].temporary);
    h[17].temporary = 0;
    append(h[14], get_cached_eligible_person_record_count(c, a, 0));
    append(h[15], a.visitors_this_month);
    append(h[16], static_cast<std::uint32_t>(get_cached_park_ride_object_count(c, a, 0)));
    append(h[18], std::bit_cast<std::uint32_t>(static_cast<float>(average_eligible_person_happiness(c))));
    append(h[1], static_cast<std::uint32_t>(world_operations::cached_park_value(c, &a, 0)));
    a.misbehaving_kids = a.misbehaving_kids_that_got_away = a.visitors_this_month = 0;
    const auto& medium = a.medium_history;
    const auto count = std::min(50, medium.wrapped ? medium.num_entries : fixed::add(medium.current_entry, 1));
    append(h[19], static_cast<std::uint32_t>(count > 0 ? fixed::truncate_integer(sum_recent(medium, 50) / count) : 0));
    append(h[20], static_cast<std::uint32_t>(calculate_park_object_and_staff_metric(c)));
}

void handle_park_analyser_world_event(EconomyWorld& c, ParkAnalyserState& a, const WorldEvent& event) {
    match (event.type()) {
        case 7 => {
            ++a.breakdowns_ever;
            return;
        }
        case 12 => {
            sample_park_analyser_metrics(c, a);
            return;
        }
        case 17 => {
            const auto id = event.get<ResearchAvailabilityEvent>().definition_id;
            const auto* d = find_object_definition_by_id(&c.resources.object_catalog, id);
            if (!d)
                throw std::logic_error("analyser research definition");
            const auto category = std::to_underlying(d->which_ui_type);
            if (category < 4)
                append(a.short_histories.at(static_cast<std::size_t>(category)), id);
            return;
        }
        case 19 => {
            if (event.get<Tag19Event>().tag == 3)
                a.park_last_opened = now(c);
            return;
        }
        case 23 => {
            if (event.get<StaffStrikeEvent>().code == 2)
                a.last_strike = now(c);
            return;
        }
        case _ => return;
    }
}
} //namespace simcoaster
