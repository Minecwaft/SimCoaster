#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_weather.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t current_month(WeatherWorld& c) {
    FileTime time{};
    std::uint32_t month = 0;
    //Calendar receiver is primary; the date helper reads the base alias's tick.
    calculate_world_calendar_filetime(c, &c.simulation.reference->calendar, &time, nullptr, &month);
    if (month < 1 || month > 12)
        throw std::runtime_error("weather calendar month could not be calculated");
    return static_cast<std::int32_t>(month);
}

void notify(WeatherWorld& c, WorldEvent& event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}
} //namespace

WeatherScheduleEntry* choose_next_world_weather_schedule_entry(WeatherWorld& c, WeatherScheduleEntry* entry) {
    const auto month = current_month(c);
    const auto next_month = month % 12 + 1;
    const auto season = static_cast<std::size_t>(next_month / 3);
    const auto ordinary_length = next_month % 3 ? next_month % 3 : 3;
    const auto elapsed = count_world_calendar_elapsed_365_day_periods(c, &c.simulation.current->calendar);
    auto category = WeatherCategory::code_0;
    if (ordinary_length == 3 && elapsed >= c.weather.weather_balance.years_before_first_freak_weather) {
        //November's native row4 reads beyond Seasons[4]. Express those verified
        //inputs directly, rather than indexing outside a C++ array or inventing
        //a wrap to season0. Parsed extent remains a required loader input.
        const auto chance =
            season < 4
                ? c.weather.weather_season_chances[season]
                : std::array<std::int32_t, 4>{ std::bit_cast<std::int32_t>(c.weather.weather_seasons_parsed_extent),
                                               c.weather.weather_balance.days_of_warning,
                                               c.weather.weather_balance.days_between_changes,
                                               c.weather.weather_balance.years_before_first_freak_weather };
        auto draw = next_crt_random(c) % 100;
        constexpr std::array categories{ WeatherCategory::heatwave,
                                         WeatherCategory::rain,
                                         WeatherCategory::storm,
                                         WeatherCategory::snow };
        for (std::size_t i = 0; i < chance.size(); ++i) {
            if (draw < chance[i]) {
                category = categories[i];
                break;
            }
            draw = numeric::subtract(draw, chance[i]);
        }
    }
    //The chooser uses CRT randomness; apply/update use the separate World RNG.
    const auto draw = next_crt_random(c);
    const auto& effects = c.weather.weather_effects_balance;
    const auto range = match (category) -> std::array<std::int32_t, 2> {
        case WeatherCategory::heatwave => {
            effects.heatwave_low, effects.heatwave_high
        };
        case WeatherCategory::rain => {
            effects.rain_low, effects.rain_high
        };
        case WeatherCategory::storm => {
            effects.lightning_low, effects.lightning_high
        };
        case WeatherCategory::snow => {
            effects.snow_low, effects.snow_high
        };
        case _ => {
            effects.normal_low, effects.normal_high
        };
    };
    const auto width = numeric::subtract(range[1], range[0]);
    if (!width)
        throw std::domain_error("selected weather quality range has zero width");
    entry->length = category == WeatherCategory::code_0 ? ordinary_length : 3;
    entry->month = month + 1; //December intentionally retains native month13.
    entry->quality =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(range[0]) + static_cast<std::uint32_t>(draw % width));
    entry->valid = 1;
    entry->type = category;
    return entry;
}

void handle_world_weather_schedule_event(WeatherWorld& c, WeatherRecord* weather, WorldEvent* event) {
    if (event->type() != 12)
        return;
    const auto month = current_month(c);
    if (month >= weather->pending.month && weather->pending.valid) {
        apply_world_weather_configuration(c, weather, &weather->pending);
        weather->pending.valid = 0;
        WorldEvent changed{
            WeatherChangedEvent{ weather->active.quality, weather->active.length, weather->active.type }
        };
        notify(c, changed);
    }
    //A listener can replace schedules. Keep the captured month, but reread
    //validity and the active endpoint after the type1 broadcast returns.
    if (weather->pending.valid)
        return;
    const auto endpoint = std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(weather->active.length) + static_cast<std::uint32_t>(weather->active.month) - 1u);
    if (month % 12 + 1 < endpoint % 12 + 1)
        return;
    choose_next_world_weather_schedule_entry(c, &weather->pending);
    const auto action = match (weather->pending.type) -> std::int32_t {
        case WeatherCategory::heatwave => 340;
        case WeatherCategory::rain     => 343;
        case WeatherCategory::storm    => 347;
        case WeatherCategory::snow     => 346;
        case _                         => 0;
    };
    if (action) {
        WorldEvent warning{ AdvisorActionEvent{ world_operations::make_advisor_action_candidate(c, action) } };
        notify(c, warning);
    }
}
} //namespace simcoaster
