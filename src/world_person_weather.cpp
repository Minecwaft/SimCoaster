#include "world_person_internal.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"

namespace simcoaster {
namespace {
constexpr std::array<std::uint32_t, 34> litter_by_variant { 9, 12, 11, 10, 1, 1, 2, 2, 3, 3, 4, 4, 6, 6, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6 };

void create_balloon_litter(VisitorWorld& c, const PersonRuntimeView& person, std::uint32_t variant) {
    const auto id = person_detail::cell_id(person.base);
    if (!id || id > map_cell_count) {
        throw std::out_of_range("balloon cell ID");
    }
    initialize_world_litter_record(c, person_detail::base_world(c).map_cells.at(id - 1), static_cast<LitterSelector>(litter_by_variant.at(variant)));
}

void balloon_penalty(VisitorWorld& c, PersonRuntimeView& person) {
    person.happiness = person_detail::meter(static_cast<double>(person.happiness) - static_cast<std::uint8_t>(c.visitors.needs_balance.PeepInfo.MediumHappinessChange));
    person.remaining_balloon_life = 0;
}

bool appearance_blocked(VisitorWorld& c, const PersonRuntimeView& person) {
    if (person.mobile.sprite_under_ride_control || person.mobile.no_floating_things_on_ride) {
        return true;
    }
    const auto id = person_detail::cell_id(person.base);
    const auto* cell = get_world_map_cell_record_by_id(c, id);
    if (!world_track_reference_matches_type_set(c, cell->base.index)) {
        return false;
    }
    //The second probe uses the coordinate-derived ID, independently of the
    //stored reference used by the first classifier.
    return world_cell_blocks_person_weather_appearance(c.simulation.track_cell_records[id - 1]);
}

float weather_effect(const PersonWeatherEffects& row, WeatherCategory weather) {
    return match (weather) -> float {
        case WeatherCategory::heatwave => row.heatwave;
        case WeatherCategory::rain     => row.rain;
        case WeatherCategory::storm    => row.storm;
        case WeatherCategory::snow     => row.snow;
        case _                         => 0.0f;
    };
}
} //namespace

void discard_person_balloon(VisitorWorld& c, PersonRuntimeView& person, bool litter) {
    if (person.balloon_script) {
        world_operations::restart_resource_variant(c, person.balloon_script, { ResourceProgramFamily::balloon_discard, 0 });
        person.balloon_script = 0;
        if (litter) {
            create_balloon_litter(c, person, person.balloon_colour);
        }
    }
    balloon_penalty(c, person);
}

void start_person_weather_appearance_transition(VisitorWorld& c, PersonRuntimeView& person, PersonUmbrellaState next) {
    if (appearance_blocked(c, person)) {
        return;
    }
    const auto response = c.visitors.needs_balance.PeepInfo.SlowestWeatherChangeResponse;
    if (!response) {
        throw std::domain_error("weather response divisor is zero");
    }
    const auto draw = static_cast<std::uint32_t>(next_crt_random(c) % response);
    person.appearance_state_deadline_quarter_ticks = draw;
    if (person.remaining_balloon_life) {
        if (!c.visitors.balloon_life_maximum) {
            throw std::domain_error("maximum balloon life is zero");
        }
        auto fraction = 100u * person.remaining_balloon_life / c.visitors.balloon_life_maximum;
        if (fraction) {
            const auto modifier = static_cast<std::uint32_t>(c.visitors.needs_balance.PeepInfo.DiscardBalloonPctModifier);
            if (modifier) {
                fraction = fraction * modifier / 100u;
            }
            person.appearance_state_deadline_quarter_ticks = draw + fraction * draw / 100u;
        }
    }
    person.appearance_state_deadline_quarter_ticks += person_detail::primary_world(c).saved.game_tick >> 2;
    person.umbrella_state = next;
}

void process_person_weather_appearance_deadline(VisitorWorld& c, PersonRuntimeView& person) {
    if (appearance_blocked(c, person) || person_detail::primary_world(c).saved.game_tick >> 2 < person.appearance_state_deadline_quarter_ticks) {
        return;
    }
    if (person.umbrella_state == PersonUmbrellaState::removal_pending) {
        if (person.umbrella_script) {
            const auto handle = person.umbrella_script;
            person.umbrella_state = PersonUmbrellaState::inactive;
            world_operations::release_resource_pool_handle(c, handle);
            person.umbrella_script = 0;
        }
        return;
    }
    if (person.balloon_script) {
        world_operations::restart_resource_variant(c, person.balloon_script, { ResourceProgramFamily::balloon_discard, 0 });
        const auto colour = person.balloon_colour;
        person.balloon_script = 0;
        create_balloon_litter(c, person, colour);
        balloon_penalty(c, person);
    } else if (person.remaining_balloon_life) {
        balloon_penalty(c, person);
    }
    if (person.umbrella_state != PersonUmbrellaState::none && !person.umbrella_script) {
        set_mobile_entity_purpose_speed(person.mobile, PurposeSpeedMode::normal, person.umbrella_state);
        person.umbrella_script = world_operations::allocate_resource_variant(c, { ResourceProgramFamily::umbrella, 0 }, { 19, 0, person.umbrella_type, 0, 0, 0 });
        person.umbrella_state = PersonUmbrellaState::active;
    }
}

void update_world_person_weather_effects(VisitorWorld& c, PersonRuntimeView& person) {
    const auto elapsed = 4u * ((person_detail::base_world(c).saved.game_tick - person.arrival_date) >> 2);
    const auto delta = scale_world_ticks_to_filetime_delta(person_detail::primary_world(c).calendar, elapsed);
    const auto day = static_cast<std::int32_t>(std::bit_cast<std::int64_t>(delta) / 864000000000ll);
    const auto frequency = c.visitors.needs_balance.PeepInfo.WeatherModifierFrequency;
    if (!frequency) {
        throw std::domain_error("weather modifier frequency is zero");
    }
    if (day % frequency) {
        return;
    }
    person.last_day_checked = static_cast<std::uint32_t>(day);
    const auto id = person_detail::primary_world(c).saved.weather;
    const auto* record = c.entities.at(id).payload;
    if (!record) {
        throw std::logic_error("weather record is required");
    }
    const auto weather = reinterpret_cast<const WeatherRecord*>(record)->type;
    float happiness = 0, hunger = 0, thirst = 0;
    if (weather == WeatherCategory::heatwave || weather == WeatherCategory::rain || weather == WeatherCategory::storm || weather == WeatherCategory::snow) {
        const auto generation = static_cast<std::uint32_t>(person.generation);
        happiness = weather_effect(c.visitors.needs_balance.happiness_weather.at(generation), weather);
        hunger = weather_effect(c.visitors.needs_balance.hunger_weather.at(generation), weather);
        thirst = weather_effect(c.visitors.needs_balance.thirst_weather.at(generation), weather);
    }
    if (weather == WeatherCategory::rain || weather == WeatherCategory::storm || weather == WeatherCategory::snow) {
        match (person.umbrella_state) {
            case PersonUmbrellaState::removal_pending => person.umbrella_state = PersonUmbrellaState::active;
            case PersonUmbrellaState::inactive        => {
                start_person_weather_appearance_transition(c, person, PersonUmbrellaState::creation_pending);
            }
            case PersonUmbrellaState::creation_pending => process_person_weather_appearance_deadline(c, person);
            case PersonUmbrellaState::active           => {
                if (happiness < 0) {
                    happiness = static_cast<float>(static_cast<double>(c.visitors.needs_balance.PeepInfo.UmbrellaModifier) + happiness);
                }
            }
            case _ => {
            }
        }
    } else {
        match (person.umbrella_state) {
            case PersonUmbrellaState::removal_pending  => process_person_weather_appearance_deadline(c, person);
            case PersonUmbrellaState::creation_pending => person.umbrella_state = PersonUmbrellaState::inactive;
            case PersonUmbrellaState::active           => {
                start_person_weather_appearance_transition(c, person, PersonUmbrellaState::removal_pending);
            }
            case _ => {
            }
        }
    }
    person.happiness = person_detail::meter(static_cast<double>(happiness) + person.happiness);
    person.needs.hunger = person_detail::meter(static_cast<double>(hunger) + person.needs.hunger);
    person.needs.thirst = person_detail::meter(static_cast<double>(thirst) + person.needs.thirst);
}
} //namespace simcoaster
