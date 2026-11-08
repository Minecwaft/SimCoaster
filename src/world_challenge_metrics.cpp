#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
template <class F>
void active(MissionWorld& c, F&& visit) {
    for (auto* entry = c.entities.active_head; entry;) {
        auto* record = entry->payload;
        if (!record)
            throw std::logic_error("null active challenge record");
        if (!visit(*record))
            return;
        entry = c.entities.at(record->id).active_next;
    }
}

template <class T>
T& record(MissionWorld& c, ThingId id) {
    auto* value = c.entities.at(id).payload;
    if (!value)
        throw std::logic_error("missing challenge World manager");
    return *reinterpret_cast<T*>(value);
}

World& referenced(MissionWorld& c) {
    if (!c.referenced_world())
        throw std::logic_error("challenge primary World is not bound");
    return *c.referenced_world();
}

ParkAnalyserState& analyser(MissionWorld& c) {
    return record<ParkAnalyserState>(c, referenced(c).saved.park_analyser);
}

template <class T, std::size_t N>
double history_sum(const SampleHistory<T, N>& ring, std::int32_t maximum) {
    const auto count = std::min(maximum, ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1));
    double sum = 0;
    for (std::int32_t i = 0; i < count; ++i)
        if (i < ring.num_entries && (i <= ring.current_entry || ring.wrapped)) {
            const auto index = i <= ring.current_entry
                                 ? ring.current_entry - i
                                 : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), i);
            if (index < 0 || static_cast<std::size_t>(index) >= ring.data.size())
                throw std::out_of_range("challenge history sample");
            sum += ring.data[static_cast<std::size_t>(index)];
        }
    return sum;
}
} //namespace

std::int32_t sum_recent_park_object_uses(const ParkObjectRuntimeView& object) {
    return object.history ? fixed::truncate_integer(history_sum(object.history->dword_history_30[2], 30)) : 0;
}

std::int32_t sum_recent_park_object_walkaways(const ParkObjectRuntimeView& object) {
    return object.history ? fixed::truncate_integer(history_sum(object.history->dword_history_30[3], 30)) : 0;
}

std::int32_t calculate_park_object_customer_satisfaction(const ParkObjectRuntimeView& object) {
    const auto uses = sum_recent_park_object_uses(object);
    if (!uses)
        return 0;
    const auto percentage = [&](std::size_t series) {
        return fixed::truncate_integer(history_sum(object.history->dword_history_30[series], 30) * 100.0 / uses);
    };
    const auto value = fixed::subtract(
        fixed::add(percentage(6), fixed::narrow(std::int64_t{ 2 } * fixed::subtract(percentage(5), percentage(8)))),
        percentage(7));
    return fixed::add(50, value / 4);
}

std::int32_t read_challenge_metric(MissionWorld& c, ChallengeMetric metric, std::int32_t argument, ThingId* focus) {
    if (focus)
        *focus = 0;
    if (metric == ChallengeMetric::eligible_population)
        return fixed::word(get_cached_eligible_person_record_count(c, analyser(c), 0));
    if (metric == ChallengeMetric::recent_net_profit) {
        auto& a = analyser(c);
        return fixed::truncate_integer(history_sum(a.long_histories[6], 1) - history_sum(a.long_histories[3], 1));
    }
    if (metric == ChallengeMetric::balloons_sold)
        return fixed::word(analyser(c).balloons_sold_ever);
    if (metric == ChallengeMetric::costumes_sold)
        return fixed::word(analyser(c).costumes_sold_ever);
    if (metric == ChallengeMetric::lifetime_visitors)
        return fixed::word(analyser(c).lifetime_visitors);
    if (metric == ChallengeMetric::misbehaving_kids_ever)
        return fixed::word(analyser(c).misbehaving_kids_ever);
    if (metric == ChallengeMetric::breakdowns_ever)
        return fixed::word(analyser(c).breakdowns_ever);
    if (metric == ChallengeMetric::any_staff_strike) {
        const auto& hq = record<StaffHqRecord>(c, referenced(c).saved.staff_hq);
        return std::ranges::any_of(hq.strikes, [](const StaffStrikeState& strike) {
            return strike.strike_active != 0;
        });
    }
    if (metric == ChallengeMetric::completed_research_count || metric == ChallengeMetric::preferred_research_matches) {
        const auto& lab = record<ResearchLabRecord>(c, referenced(c).saved.research_lab);
        if (metric == ChallengeMetric::preferred_research_matches) {
            const auto index =
                lab.primary_current_project >= 0 ? lab.primary_current_project : lab.secondary_current_project;
            return index >= 0
                && lab.projects.at(static_cast<std::size_t>(index)).object_definition_id
                       == static_cast<std::uint16_t>(argument);
        }
        if (lab.num_projects > lab.projects.size())
            throw std::out_of_range("challenge research count");
        std::int32_t count = 0;
        for (std::size_t i = lab.num_projects; i > 0; --i) {
            const auto& project = lab.projects[i - 1];
            if ((argument == 7 || std::to_underlying(project.category) == argument)
                && project.research_done >= project.cost)
                ++count;
        }
        return count;
    }
    if (metric == ChallengeMetric::thought_in_top_three) {
        std::array<std::uint32_t, 31> counts{};
        active(c, [&](ThingHeader& header) {
            if (header.kind == ThingKind::person) {
                const auto& person = *reinterpret_cast<PersonRuntimeView*>(&header);
                if (record_passes_grid_predicates(c, &person.base)) {
                    const auto thought = static_cast<std::size_t>(person.mobile.thought_history.last_thought);
                    if (thought >= counts.size())
                        throw std::out_of_range("challenge thought category");
                    ++counts[thought];
                }
            }
            return true;
        });
        counts[0] = 0;
        std::array<std::size_t, 3> top{ 0, 1, 2 };
        for (std::size_t thought = 3; thought < counts.size(); ++thought) {
            std::int32_t best = 0;
            std::optional<std::size_t> slot;
            for (std::size_t i = 0; i < top.size(); ++i) {
                const auto difference = fixed::word(counts[thought] - counts[top[i]]);
                if (difference > best) {
                    best = difference;
                    slot = i;
                }
            }
            if (slot)
                top[*slot] = thought;
        }
        return std::ranges::any_of(top, [&](std::size_t thought) {
            return static_cast<std::int32_t>(counts[thought] ? thought : 31) == argument;
        });
    }
    if (metric == ChallengeMetric::visitor_happiness
        && (!c.simulation.current || c.simulation.current->saved.park_closed))
        return 0;
    std::int32_t value =
        (metric == ChallengeMetric::minimum_staff_happiness || metric == ChallengeMetric::minimum_ride_repair) ? 100
        : metric == ChallengeMetric::maximum_upgrade_level                                                     ? -1
                                                                                                               : 0;
    std::uint32_t population = 0;
    bool selected = false;
    active(c, [&](ThingHeader& header) {
        if (header.kind == ThingKind::person) {
            const auto& person = *reinterpret_cast<PersonRuntimeView*>(&header);
            if (record_passes_grid_predicates(c, &person.base)) {
                if (metric == ChallengeMetric::balloon_holders
                    && (person.remaining_balloon_life || person.balloon_script))
                    ++value;
                if (metric == ChallengeMetric::costume_wearers && person.mobile.sprite_category == 6)
                    ++value;
                if (metric == ChallengeMetric::visitor_happiness) {
                    value = fixed::add(value, truncate_park_object_condition(person.happiness));
                    ++population;
                }
            }
        }
        if (is_staff_record_kind(header.kind)) {
            if (metric == ChallengeMetric::staff_count)
                ++value;
            if (metric == ChallengeMetric::maximum_staff_experience && std::to_underlying(header.kind) == argument)
                value = std::max(value, fixed::word(reinterpret_cast<StaffRuntimeRecord*>(&header)->experience));
            if (metric == ChallengeMetric::minimum_staff_happiness) {
                const auto score =
                    truncate_park_object_condition(reinterpret_cast<StaffRuntimeRecord*>(&header)->happiness_score);
                if (score < value) {
                    value = score;
                    if (focus)
                        *focus = header.id;
                }
            }
        }
        if (header.kind == ThingKind::park_object) {
            auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(&header);
            if (metric == ChallengeMetric::maximum_upgrade_level
                && object.definition_id == static_cast<std::uint16_t>(argument))
                value = std::max(value, static_cast<std::int32_t>(object.current_upgrade));
            if (metric == ChallengeMetric::has_ingredient || metric == ChallengeMetric::has_shop_type) {
                const auto& usage = get_park_object_balance(c, object).UsageInfo;
                if ((metric == ChallengeMetric::has_ingredient ? usage.SpecialIngredient : usage.ShopType)
                    == argument) {
                    value = 1;
                    return false;
                }
            }
            if (metric == ChallengeMetric::minimum_ride_repair || metric == ChallengeMetric::sideshow_satisfaction) {
                const auto* definition = get_park_object_definition(c, &object);
                if (!definition)
                    throw std::logic_error("challenge object definition");
                if (metric == ChallengeMetric::minimum_ride_repair
                    && definition->which_ui_type == ObjectUiCategory::rides) {
                    const auto score = truncate_park_object_condition(object.repair_state);
                    if (!selected || score < value) {
                        selected = true;
                        value = score;
                        if (focus)
                            *focus = header.id;
                    }
                }
                if (metric == ChallengeMetric::sideshow_satisfaction
                    && definition->which_ui_type == ObjectUiCategory::sideshows
                    && fixed::word(static_cast<std::uint32_t>(get_park_object_age_in_days(c, object))) > 30
                    && sum_recent_park_object_uses(object)) {
                    value = fixed::add(value, calculate_park_object_customer_satisfaction(object));
                    ++population;
                }
            }
            if (metric == ChallengeMetric::maximum_vertical_special_count
                && get_park_object_balance(c, object).Bumper.WhichTrackType == 3) {
                const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object.mesh_instance_id);
                if (opening
                    && !opening->geometry_peer_reference_count
                    && (!argument || object.definition_id == static_cast<std::uint16_t>(argument))) {
                    if (!c.resources.meshes || !c.resources.meshes->coasters())
                        throw std::logic_error("challenge coaster profiles are not bound");
                    const auto& bindings = c.resources.meshes->coasters()->bindings;
                    const auto binding =
                        std::ranges::find(bindings, object.mesh_instance_id, &WorldCoasterBinding::mesh_slot);
                    if (binding == bindings.end())
                        throw std::logic_error("challenge coaster binding is missing");
                    //A runtime that has not built its track yet retains the constructor's
                    //zero metric; only profile generation publishes a new cached count.
                    const auto count = binding->track ? binding->track->profile.vertical_special_count : 0u;
                    if (!selected || count > static_cast<std::uint32_t>(value)) {
                        selected = true;
                        value = fixed::word(count);
                        if (focus)
                            *focus = header.id;
                    }
                }
            }
        }
        return true;
    });
    if (metric == ChallengeMetric::visitor_happiness)
        return population ? fixed::truncate_integer(static_cast<double>(value) / population) : 0;
    if (metric == ChallengeMetric::sideshow_satisfaction)
        return population ? value / static_cast<std::int32_t>(population) : 0;
    return value;
}
} //namespace simcoaster
