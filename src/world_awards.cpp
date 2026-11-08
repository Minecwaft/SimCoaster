#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_awards.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
World& world(EconomyWorld& c) {
    return require_bound(c.simulation.current, "park awards require base World");
}

template <class F>
void active(EconomyWorld& c, F&& visit) {
    for (auto* node = c.entities.active_head; node;) {
        auto* h = node->payload;
        if (!h)
            throw std::logic_error("park award active record");
        visit(*h);
        node = c.entities.at(h->id).active_next;
    }
}

template <class F>
void objects(EconomyWorld& c, F&& visit) {
    active(c, [&](ThingHeader& h) {
        if (h.kind == ThingKind::park_object)
            visit(*reinterpret_cast<ParkObjectRuntimeView*>(&h));
    });
}

bool covered(EconomyWorld& c, const MapCell& cell) {
    const auto type = cell.base.common.type;
    return type == 9
        || type == 10
        || type == 4
        || world_track_reference_matches_type_set(c, cell.base.index)
        || type == 21;
}

std::int32_t quotient(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "park award divisor");
}

std::size_t index(ParkAwardType type) {
    const auto value = std::to_underlying(type);
    if (value < 0 || value >= 8)
        throw std::out_of_range("park award type");
    return static_cast<std::size_t>(value);
}
} //namespace

ParkAwardClaim claim_or_replace_global_park_award(ThemeGlobalInfoManager& m, ParkAwardType type, std::int32_t score) {
    const auto i = index(type);
    auto* selected = m.selected_theme_info;
    if (!selected)
        throw std::logic_error("park award selected theme");
    if (m.global_park_award_flags[i]) {
        //A winning park does not improve its stored score. Only another park can
        //take the record; equal scores keep the incumbent.
        if (selected->award_winner_flags[i])
            return ParkAwardClaim::unchanged;
        for (auto& [source, theme] : m.theme_info_records) {
            (void)source;
            if (theme.award_winner_flags[i] && score > theme.award_scores[i]) {
                theme.award_winner_flags[i] = 0;
                selected->award_winner_flags[i] = 1;
                selected->award_scores[i] = score;
                return ParkAwardClaim::transferred;
            }
        }
        return ParkAwardClaim::unchanged;
    }
    m.global_park_award_flags[i] = selected->award_winner_flags[i] = 1;
    selected->award_scores[i] = score;
    return ParkAwardClaim::new_award;
}

ParkAwardCandidate select_coaster_park_award_candidate(EconomyWorld& c, bool speed, ThingId filter) {
    ParkAwardCandidate best{};
    bool found = false;
    objects(c, [&](const ParkObjectRuntimeView& o) {
        if (get_park_object_balance(c, o).Bumper.WhichTrackType != 3
            || (filter && o.definition_id != filter)
            || !o.can_load
            || !o.prebuilt_track_edited
            || !o.been_around_at_least_a_month)
            return;
        if (!c.resources.meshes || !c.resources.meshes->coasters())
            throw std::logic_error("park award coaster metrics owner");
        const auto* binding = c.resources.meshes->coasters()->find(o.mesh_instance_id);
        if (!binding)
            throw std::logic_error("park award coaster mesh binding");
        const auto value = speed ? binding->metrics.maximum_speed : binding->metrics.maximum_y;
        //Native compares the unsigned DWORD retained from the previous truncated
        //metric to the next float. This is numeric state, never a host pointer.
        if (!found || !(static_cast<double>(static_cast<std::uint32_t>(best.score)) >= value)) {
            found = true;
            best = { o.base.header.id, fixed::truncate_integer(value) };
        }
    });
    return best;
}

ParkAwardCandidate select_track_park_award_candidate(EconomyWorld& c, std::int32_t track_type) {
    ParkAwardCandidate best{};
    bool found = false;
    objects(c, [&](const ParkObjectRuntimeView& o) {
        if (!o.can_load)
            return;
        const auto* d = get_park_object_definition(c, &o);
        if (!d
            || d->which_ui_type != ObjectUiCategory::rides
            || get_park_object_balance(c, o).Bumper.WhichTrackType != track_type)
            return;
        const auto metrics = c.ports.entities.collect_track_ride_layout_metrics(c, o.track_ride_handle);
        //Standard.sam explicitly calls GokartSpeed the longest straight section.
        const auto value = track_type == 1 ? metrics.longest_supported_run : metrics.visited_entry_count;
        if (!found || value > best.score) {
            found = true;
            best = { o.base.header.id, value };
        }
    });
    return best;
}

std::int32_t count_park_object_covered_cells(EconomyWorld& c) {
    std::int32_t count = 0;
    for (const auto& cell : world(c).map_cells)
        count += covered(c, cell);
    return count;
}

std::int32_t calculate_park_decoration_percentage(EconomyWorld& c) {
    const auto area = count_park_object_covered_cells(c);
    std::int32_t decorated = 0;
    objects(c, [&](const ParkObjectRuntimeView& o) {
        const auto* d = get_park_object_definition(c, &o);
        if (!d)
            throw std::logic_error("decoration object definition");
        if (d->which_ui_type != ObjectUiCategory::features)
            return;
        const auto* shape = get_park_object_balance(c, o).Info.Shape.get();
        if (!shape)
            throw std::logic_error("decoration shape");
        decorated = fixed::add(decorated, numeric::product(shape->width, shape->height));
    });
    //The original tests the numerator, not the denominator. Report invalid
    //nonzero decoration/zero-area state instead of inventing a percentage.
    return decorated ? quotient(numeric::product(100, decorated), area) : 0;
}

std::int32_t calculate_secured_park_cell_percentage(EconomyWorld& c) {
    std::int32_t total = 0, secured = 0;
    auto& w = world(c);
    for (std::size_t i = 0; i < w.map_cells.size(); ++i) {
        const auto type = w.map_cells[i].base.common.type;
        if (type != 2 && type != 7 && type != 30) {
            ++total;
            secured += w.region_cells[i].security > 0;
        }
    }
    return total ? 100 * secured / total : 0;
}

std::int32_t calculate_staff_training_percentage(EconomyWorld& c) {
    std::int32_t count = 0, trained = 0;
    active(c, [&](const ThingHeader& h) {
        if (is_staff_record_kind(h.kind)) {
            ++count;
            trained += reinterpret_cast<const StaffRuntimeRecord*>(&h)->first_known_ability != ThemeAbility::none;
        }
    });
    return count ? 100 * trained / count : 0;
}

void notify_park_award(EconomyWorld& c, ParkAwardType type, ParkAwardClaim status) {
    constexpr std::array won{ 191, 314, 315, 316, 317, 318, 192, 193 };
    constexpr std::array transferred{ 195, 322, 323, 324, 325, 326, 196, 197 };
    const auto i = index(type);
    if (status == ParkAwardClaim::new_award || status == ParkAwardClaim::transferred) {
        const auto action = (status == ParkAwardClaim::new_award ? won : transferred)[i];
        AdvisorQueueEntry candidate{};
        init_advisor_action_candidate(c, candidate, action, 0, 0, static_cast<std::uint32_t>(-1), 0, -1);
        WorldEvent event{ AdvisorActionEvent{ candidate } };
        dispatch_world_event_to_subscribers(c,
                                            get_world_event_subscription_registry(c),
                                            &event,
                                            deliver_world_record_event,
                                            nullptr);
    }
    auto& panel = c.ui.panels.park_awards;
    panel.highlighted_type = std::to_underlying(type);
    if (panel.open) {
        panel.highlight_visible = true;
        panel.highlight_started_ms = c.ports.clock.get_ui_clock_time_ms(c);
        ++panel.revision;
    }
}

bool notify_new_park_award(EconomyWorld& c, ParkAwardType type, std::int32_t score) {
    const auto claim = claim_or_replace_global_park_award(get_or_create_theme_global_info_manager(c), type, score);
    if (claim == ParkAwardClaim::unchanged)
        return false;
    notify_park_award(c, type, claim);
    return true;
}

void evaluate_world_park_awards(EconomyWorld& c) {
    const auto& p = c.balance.award_balance;
    const auto award = [&](ParkAwardType type, std::int32_t score, std::int32_t threshold) {
        return score >= threshold && notify_new_park_award(c, type, score);
    };
    if (award(ParkAwardType::coaster_height, select_coaster_park_award_candidate(c, false).score, p.CoasterHeight))
        return;
    if (award(ParkAwardType::coaster_speed, select_coaster_park_award_candidate(c, true).score, p.CoasterSpeed))
        return;
    const auto hygiene = fixed::subtract(100, calculate_park_path_litter_percentage(c));
    if (hygiene >= p.Hygiene
        && count_park_object_covered_cells(c) >= p.HygieneMinParkSize
        && notify_new_park_award(c, ParkAwardType::hygiene, hygiene))
        return;
    if (award(ParkAwardType::decoration, calculate_park_decoration_percentage(c), p.Decoration))
        return;
    const auto patrol = calculate_world_staff_patrol_coverage(c, ThingKind::guard, StaffCoverageCellFilter::land);
    if (award(ParkAwardType::security, fixed::add(patrol, calculate_secured_park_cell_percentage(c)), p.Security))
        return;
    const auto training = calculate_staff_training_percentage(c);
    std::int32_t staff = 0;
    active(c, [&](const ThingHeader& h) {
        staff += is_staff_record_kind(h.kind);
    });
    if (training >= p.Training
        && staff >= p.TrainingMinStaff
        && notify_new_park_award(c, ParkAwardType::training, training))
        return;
    if (award(ParkAwardType::gokart_speed, select_track_park_award_candidate(c, 1).score, p.GokartSpeed))
        return;
    (void)award(ParkAwardType::water_length, select_track_park_award_candidate(c, 2).score, p.WaterLength);
}

void world_operations::builtin::evaluate_awards(WorldRuntime& c, void*) {
    evaluate_world_park_awards(c);
}

std::uint32_t ClockPort::get_ui_clock_time_ms(WorldRuntime& c) {
    if (c.resources.meshes)
        return static_cast<std::uint32_t>(c.resources.meshes->clocks.unscaled.count());
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} //namespace simcoaster
