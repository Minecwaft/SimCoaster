#include "simcoaster/numeric_word.hpp"
#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
constexpr std::array<std::uint8_t, 121> need_match { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 5, 7, 11, 15, 20, 25, 31, 0, 0, 1, 4, 7, 11, 16, 21, 28, 36, 44, 0, 0, 2, 4, 8, 13, 19, 26, 35, 44, 54, 0, 0, 2, 5, 10, 15, 22, 30, 40, 51, 63, 0, 0, 2, 6, 11, 17, 25, 34, 45, 57, 70, 0, 0, 3, 6, 12, 19, 27, 37, 49, 62, 77, 0, 0, 3, 7, 13, 20, 30, 40, 53, 67, 83, 0, 0, 3, 8, 14, 22, 32, 43, 57, 72, 89, 0, 0, 3, 8, 15, 23, 34, 46, 60, 76, 94, 0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100 };
constexpr std::array<std::uint8_t, 52> relief_match { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

std::uint32_t divide(std::uint32_t a, std::uint32_t b) {
    if (!b) {
        throw std::domain_error("zero guest-choice divisor");
    }
    return a / b;
}

std::int32_t percent(std::int32_t value, std::int32_t modifier) {
    return numeric::product(value, modifier) / 100;
}

WeatherCategory weather(VisitorWorld& c) {
    auto& world = person_detail::primary_world(c);
    return reinterpret_cast<const WeatherRecord*>(&person_detail::map_object(c, world.saved.weather))->type;
}

std::uint32_t match_need(std::int32_t effect, float need) {
    //The original matrix is 11x11. Indices outside that allocation are malformed
    //balance/need data; report them instead of reading neighboring native data.
    return need_match.at((static_cast<std::uint8_t>(effect) / 10u) * 11u + truncate_park_object_condition(need) / 10u);
}
} //namespace

std::int32_t score_person_park_object_candidate(VisitorWorld& c, PersonRuntimeView& person, ParkObjectRuntimeView& original) {
    if (!is_special_ride_group_id_unregistered(c, original.base.header.id)) {
        return 0;
    }
    if (get_park_object_balance(c, original).Info.GenderSpecific) {
        const auto gender = world_operations::get_person_sprite_gender_flag(c, person) ? 2 : 1;
        if (get_park_object_balance(c, original).Info.GenderSpecific != gender) {
            return 0;
        }
    }
    (void)get_back_of_queue(c, &original);
    const auto queue_cell = get_back_of_queue(c, &original);
    const auto distance_squared = person_detail::squared_cell_distance(person_detail::cell_id(person.base), queue_cell);
    const auto& parameters = c.visitors.needs_balance.PeepInfo;
    const auto thirst_weight = static_cast<std::uint16_t>(parameters.DecisionVarThirstWeight), hunger_weight = static_cast<std::uint16_t>(parameters.DecisionVarHungerWeight);
    const auto distance_weight = static_cast<std::uint16_t>(parameters.DecisionVarDistWeight), illness_weight = static_cast<std::uint16_t>(parameters.DecisionVarIllnessWeight);
    const auto toilet_weight = static_cast<std::uint16_t>(parameters.DecisionVarToiletWeight);
    //Native diagnostics also resolve this definition before the history check.
    (void)get_park_object_definition(c, &original);
    if (const auto previous = person.previous_rides[0]; previous && c.entities.at(previous).payload) {
        if (c.entities.payload<const ParkObjectRuntimeView>(previous)->definition_id == original.definition_id) {
            return 0;
        }
    }
    if (get_park_object_balance(c, original).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::umbrella)) {
        if (person.umbrella_state != PersonUmbrellaState::none) {
            return 0;
        }
        if (weather(c) != WeatherCategory::rain && weather(c) != WeatherCategory::snow && weather(c) != WeatherCategory::storm) {
            return 0;
        }
    }
    if (get_park_object_balance(c, original).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::balloon)) {
        if (person.balloon_script || person.remaining_balloon_life || person.umbrella_state == PersonUmbrellaState::active || person.umbrella_state == PersonUmbrellaState::creation_pending) {
            return 0;
        }
    }
    auto distance_score = 100u - std::min(100u, 100u * distance_squared / 450u);
    if (!c.simulation.region_effects.cells || !queue_cell || queue_cell > map_cell_count) {
        throw std::out_of_range("guest-choice effect cell");
    }
    const auto attraction = static_cast<std::uint32_t>(c.simulation.region_effects.cells[queue_cell - 1u].attraction);
    if (attraction) {
        distance_score /= attraction;
    }
    auto* candidate = &original;
    if (get_park_object_balance(c, *candidate).UsageInfo.IsInfoDesk) {
        if (!get_info_desk_promoted_attraction_id(c, *candidate)) {
            return 0;
        }
        candidate = &person_detail::park_object(c, get_info_desk_promoted_attraction_id(c, *candidate));
    }
    std::uint32_t queue_score = 0, queue_weight = 0;
    if (distance_squared <= 8) {
        auto cells = candidate->queue_size_in_cells;
        if (!cells) {
            cells = 1;
        }
        const auto count = static_cast<std::uint32_t>(get_person_park_object_queue_position(c, candidate, 0));
        queue_weight = numeric::bits(c.visitors.needs_balance.PeepInfo.DecisionVarQueueWeight);
        queue_score = 100u - divide(100u * count, 4u * cells);
    }
    const auto difference = std::abs(int(static_cast<std::uint8_t>(person.preferred_excitement)) - int(get_park_object_excitement(c, *candidate, false)));
    const auto excitement_score = static_cast<std::uint32_t>(100 - 2 * std::min(difference, 50));
    const auto excitement_weight = get_park_object_excitement(c, *candidate, true) ? static_cast<std::uint16_t>(c.visitors.needs_balance.PeepInfo.DecisionVarExcitementWeight) : 0u;
    const auto thirst_score = match_need(get_park_object_balance(c, *candidate).UsageInfo.ThirstEffect, person.needs.thirst);
    const auto hunger_score = match_need(get_park_object_balance(c, *candidate).UsageInfo.HungerEffect, person.needs.hunger);
    const bool relief = (std::to_underlying(candidate->capabilities) & std::to_underlying(ParkObjectCapabilities::provides_relief)) != 0;
    const auto toilet_score = relief ? relief_match[(truncate_park_object_condition(person.needs.toilet_need) + 4u) / 5u] : 0u;
    const auto illness_score = (std::to_underlying(candidate->capabilities) & std::to_underlying(ParkObjectCapabilities::provides_relief)) ? relief_match[(truncate_park_object_condition(person.needs.sickness) + 4u) / 5u] : 0u;
    const auto numerator = thirst_score * thirst_weight + hunger_score * hunger_weight + toilet_score * toilet_weight + illness_score * illness_weight + queue_score * queue_weight + distance_score * distance_weight + excitement_score * excitement_weight;
    const auto denominator = queue_weight + excitement_weight + illness_weight + toilet_weight + hunger_weight + thirst_weight + static_cast<std::uint32_t>(distance_weight);
    auto score = std::bit_cast<std::int32_t>(divide(numerator, denominator));
    if (static_cast<std::uint32_t>(get_park_object_age_in_days(c, *candidate)) <= static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.DecisionVariable1)) {
        score = numeric::product(score, c.visitors.needs_balance.PeepInfo.DecisionVariable2);
    }
    const auto golden_cost = get_park_object_balance(c, *candidate).UsageInfo.GoldenTicketCost;
    if (golden_cost > 0) {
        score = fixed::truncate_integer((static_cast<double>(fixed::add(golden_cost, 1)) * 0.1 + 1.0) * score);
    } else if (get_park_object_balance(c, *candidate).Upgrades[0].CostOfUpgrade) {
        const auto ratio = static_cast<double>(get_park_object_balance(c, *candidate).Upgrades[0].CostOfUpgrade) * static_cast<double>(0.00033333333f);
        if (ratio > 1.0) {
            score = fixed::truncate_integer((ratio * static_cast<double>(0.1f) + 1.0) * score);
        }
    }
    if (weather(c) == WeatherCategory::rain && get_park_object_balance(c, *candidate).WeatherModifier.RainPercentage) {
        score = percent(score, get_park_object_balance(c, *candidate).WeatherModifier.RainPercentage);
    }
    if (weather(c) == WeatherCategory::snow && get_park_object_balance(c, *candidate).WeatherModifier.SnowPercentage) {
        score = percent(score, get_park_object_balance(c, *candidate).WeatherModifier.SnowPercentage);
    }
    if (weather(c) == WeatherCategory::heatwave && get_park_object_balance(c, *candidate).WeatherModifier.HeatWavePercentage) {
        score = percent(score, get_park_object_balance(c, *candidate).WeatherModifier.HeatWavePercentage);
    }
    if (weather(c) == WeatherCategory::storm && get_park_object_balance(c, *candidate).WeatherModifier.StormPercentage) {
        score = percent(score, get_park_object_balance(c, *candidate).WeatherModifier.StormPercentage);
    }
    if (get_park_object_balance(c, *candidate).UsageInfo.AppearanceEffect == std::to_underlying(PersonAppearanceEffect::umbrella) && (person.balloon_script || person.remaining_balloon_life) && person.remaining_balloon_life) {
        const auto used = divide(100u * person.remaining_balloon_life, c.visitors.balloon_life_maximum);
        auto modifier = 100u - used;
        if (used != 100u) {
            if (c.visitors.needs_balance.PeepInfo.DiscardBalloonPctModifier) {
                modifier = modifier * numeric::bits(c.visitors.needs_balance.PeepInfo.DiscardBalloonPctModifier) / 100u;
            }
            score = std::bit_cast<std::int32_t>(numeric::bits(score) * modifier / 100u);
        }
    }
    match (person.generation) {
        case PersonGeneration::child => {
            score = percent(score, get_park_object_balance(c, *candidate).GenerationModifier.KidScorePercentage);
        }
        case PersonGeneration::parent => {
            score = percent(score, get_park_object_balance(c, *candidate).GenerationModifier.ParentScorePercentage);
        }
        case PersonGeneration::grandparent => {
            score = percent(score, get_park_object_balance(c, *candidate).GenerationModifier.GrandparentScorePercentage);
        }
        case _ => {
        }
    } for (std::size_t i = 0; i < 4; ++i) {
        if (person.previous_rides[i] == candidate->base.header.id) {
            score /= static_cast<std::int32_t>(5 - i);
            break;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (person.previous_temporary_rides[i] == candidate->base.header.id) {
            score /= static_cast<std::int32_t>(5 - i);
        }
    }
    return score;
}

void select_person_park_object_destination_by_score(VisitorWorld& c, PersonRuntimeView& person) {
    std::int32_t best = 0;
    person.major_destination = 0;
    auto id = person_detail::base_world(c).saved.first_park_object;
    std::size_t visited = 0;
    while (id) {
        if (visited++ >= thing_slot_count) {
            throw std::length_error("cyclic guest-choice object list");
        }
        auto& object = person_detail::park_object(c, id);
        if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::is_choosable)) && get_back_of_queue(c, &object)) {
            const auto score = score_person_park_object_candidate(c, person, object);
            if (static_cast<std::uint32_t>(score) >= 10u && (score > best || (score == best && (person_detail::base_world(c).saved.game_tick & 1u))) && is_choosable_park_object_with_eligible_queue(c, object) && try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, get_back_of_queue(c, &object))) {
                best = score;
                person.major_destination = id;
            }
        }
        id = object.next_park_object;
    }
}

void reselect_person_ride_target_from_nearby_cells(VisitorWorld& c, PersonRuntimeView& person) {
    if (!person.major_destination) {
        return;
    }
    const auto origin = person_detail::cell_id(person.base), back = get_back_of_queue(c, &person_detail::park_object(c, person.major_destination));
    if (!back) {
        return;
    }
    const auto unpack = [](CellId id) {
        const auto value = static_cast<std::uint32_t>(id) - 1u;
        return WorldCellCoordinates { static_cast<std::int32_t>(value & 127u), static_cast<std::int32_t>(value >> 7) };
    };
    const auto point = unpack(origin), queue = unpack(back);
    const auto maximum = person_detail::squared_cell_distance(origin, back);
    ThingId best = 0;
    std::int32_t best_score = 0;
    if (!c.simulation.map_cell_records) {
        throw std::logic_error("guest target reselection has no map cells");
    }
    for (int dx = -2; dx < 2; ++dx) {
        for (int dy = -2; dy < 2; ++dy) {
            const auto x = point.x + dx, y = point.y + dy;
            if (static_cast<std::uint32_t>(x) >= 128 || static_cast<std::uint32_t>(y) >= 128) {
                continue;
            }
            const auto delta_x = static_cast<std::uint32_t>(x - queue.x), delta_y = static_cast<std::uint32_t>(y - queue.y);
            if (fixed::word(delta_x * delta_x + delta_y * delta_y) >= fixed::word(maximum)) {
                continue;
            }
            auto id = static_cast<ThingId>(c.simulation.map_cell_records[static_cast<std::size_t>(y * 128 + x)].who);
            std::size_t visited = 0;
            while (id) {
                if (visited++ >= thing_slot_count) {
                    throw std::length_error("cyclic guest target reselection occupants");
                }
                auto& base = person_detail::map_object(c, id);
                if (base.header.kind == ThingKind::park_object && id != person.major_destination) {
                    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(&base);
                    if (is_choosable_park_object_with_eligible_queue(c, object)) {
                        const auto score = score_person_park_object_candidate(c, person, object);
                        if (score > 0 && (score > best_score || (score == best_score && (person_detail::base_world(c).saved.game_tick & 1u)))) {
                            best = id;
                            best_score = score;
                        }
                    }
                }
                id = base.map_child;
            }
        }
    }
    if (!best) {
        return;
    }
    const auto old_top_left = person_detail::park_object(c, person.major_destination).top_left, new_top_left = person_detail::park_object(c, best).top_left;
    const auto old_distance = calculate_world_grid_path_length_between_cells(c, old_top_left, person_detail::cell_id(person.base), person.mobile.navigation.navigation_mode);
    //The original second query starts at the old target, not at the Person.
    const auto candidate_distance = calculate_world_grid_path_length_between_cells(c, old_top_left, new_top_left, person.mobile.navigation.navigation_mode);
    if (old_distance != -1 && candidate_distance != -1 && old_distance > candidate_distance) {
        const auto entry = person_detail::park_object(c, best).entry_cell;
        person.saved_major_destination = person.major_destination;
        person.major_destination = best;
        (void)try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, entry);
    }
}
} //namespace simcoaster
