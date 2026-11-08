#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_weather.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator)
        throw std::domain_error("weather volume has a zero divisor");
    if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)
        throw std::overflow_error("weather volume is not representable");
    return numerator / denominator;
}

std::uint32_t world_draw(WeatherWorld& c) {
    return numeric::bits(next_world_prng_value(*c.simulation.reference));
}

std::int32_t remainder(std::uint32_t numerator, std::int32_t denominator) {
    const auto divisor = numeric::bits(denominator);
    if (!divisor)
        throw std::domain_error("weather random range is zero");
    return numeric::word(numerator % divisor);
}

std::int64_t thunder_coordinate(float value) {
    const auto coordinate = std::trunc(static_cast<double>(value));
    if (!std::isfinite(coordinate))
        throw std::domain_error("weather sound coordinate is not finite");
    const auto limit = std::ldexp(1.0, 63);
    if (coordinate < -limit || coordinate >= limit)
        throw std::overflow_error("weather sound coordinate is not representable");
    return static_cast<std::int64_t>(coordinate);
}

WeatherCategory icon_category(WeatherCategory category) {
    return match (category) -> WeatherCategory {
        case WeatherCategory::heatwave => WeatherCategory::heatwave;
        case WeatherCategory::rain     => WeatherCategory::rain;
        case WeatherCategory::storm    => WeatherCategory::storm;
        case WeatherCategory::snow     => WeatherCategory::snow;
        case _                         => WeatherCategory::code_0;
    };
}
} //namespace

void set_rain_ambient_volume(WeatherWorld& c, std::int32_t volume) {
    //Game.exe0x657200 owns a separate global handle from WeatherRecord's rain
    //request. Operation8 replaces that global with its returned handle.
    if (volume > 0) {
        const auto bounded = std::min(volume, 100);
        auto handle = c.weather.rain_intensity_sound_handle;
        if (!handle) {
            handle = world_operations::submit_weather_sound(c, WeatherSound::rain, { 0, 0, 0 });
            c.weather.rain_intensity_sound_handle = handle;
        }
        c.weather.rain_intensity_sound_handle = world_operations::set_weather_sound_volume(c, handle, bounded);
    } else if (auto handle = c.weather.rain_intensity_sound_handle) {
        //The native stop path intentionally does not clear the global handle.
        world_operations::stop_weather_sound(c, handle);
    }
}

void make_weather_lightning_endpoints(WeatherWorld& c, WorldVector3& ground, WorldVector3& sky) {
    //Game.exe0x642550: four World PRNG draws, each retaining thirty bits.
    //Dimensions are snapshotted before the first draw and stored as floats.
    const float width = static_cast<float>(static_cast<double>(c.weather.map_heightfield_width) * 10.0);
    const float height = static_cast<float>(static_cast<double>(c.weather.map_heightfield_height) * 10.0);
    const auto unit = [&] {
        return static_cast<double>(world_draw(c) & 1073741823u) / 1073741824.0;
    };
    const auto ground_x = unit();
    ground[1] = 0.0f;
    ground[0] = static_cast<float>(ground_x * width);
    ground[2] = static_cast<float>(unit() * height);
    const auto sky_x = unit();
    sky[1] = 300.0f;
    sky[0] = static_cast<float>(sky_x * 100.0 + ground[0] - 50.0);
    sky[2] = static_cast<float>(unit() * 100.0 + ground[2] - 50.0);
    //Sky coordinates use the unclamped ground point. Upper-then-lower checks
    //also define the native result when a supplied map dimension is zero.
    if (ground[0] > width)
        ground[0] = width;
    if (ground[0] < 1.0f)
        ground[0] = 1.0f;
    if (ground[2] > height)
        ground[2] = height;
    if (ground[2] < 1.0f)
        ground[2] = 1.0f;
}

void update_world_weather_state(WeatherWorld& c, WeatherRecord* weather) {
    const auto target = weather->target_drops;
    if (target >= 0) {
        const auto current = weather->current_drops;
        const auto speed = c.weather.weather_balance.speed_of_change;
        if (numeric::absolute(numeric::subtract(current, target)) < speed) {
            //A positive target inside the deadband does not snap current to target.
            if (!target) {
                set_world_precipitation_mode(c, weather, WeatherPrecipitation::none);
                set_rain_ambient_volume(c, 0);
            }
        } else {
            const auto changed = current >= target ? numeric::subtract(current, speed) : numeric::add(current, speed);
            weather->current_drops = changed;
            const auto maximum = c.weather.weather_effects_balance.max_raindrops;
            c.weather.wind_effects.precipitation_amount = std::min(std::max(changed, 0), maximum);
            const auto volume = divide(numeric::product(100, weather->current_drops), c.weather.weather_effects_balance.max_raindrops);
            set_rain_ambient_volume(c, volume);
        }
        world_operations::set_weather_icon(c, icon_category(weather->type));
    }

    //Icon/audio integration can change the live weather state before this test.
    if (weather->lightning) {
        weather->lightning_countdown = numeric::subtract(weather->lightning_countdown, c.weather.weather_effects_balance.lightning_countdown_speed);
        if (weather->lightning_countdown < 0) {
            WorldVector3 ground{}, sky{};
            make_weather_lightning_endpoints(c, ground, sky);
            world_operations::trigger_weather_lightning(c,
                                                        ground,
                                                        sky,
                                                        c.weather.weather_effects_balance.lightning_time);
            if (weather->thunder_countdown <= 0) {
                const auto delay_base = std::max(numeric::subtract(c.weather.weather_effects_balance.thunder_starts_this_far_away, weather->lightning_count_this_period), 0);
                const auto draw = world_draw(c);
                const auto random_delay = remainder(draw, c.weather.weather_effects_balance.thunder_randomiser);
                weather->last_lightning_location[0] = sky[0];
                weather->thunder_countdown = numeric::add(delay_base, random_delay);
                weather->last_lightning_location[1] = sky[1];
                weather->last_lightning_location[2] = sky[2];
            }
            weather->lightning_count_this_period = numeric::add(weather->lightning_count_this_period, 1);
            const auto draw = world_draw(c);
            weather->lightning_countdown = remainder(draw, c.weather.weather_effects_balance.lightning_countdown_from);
        }
    } else
        weather->lightning_count_this_period = 0;

    weather->thunder_countdown = numeric::subtract(weather->thunder_countdown, 1);
    if (!weather->thunder_countdown) {
        //Ordinary bounded integer conversion replaces the original x87 helper.
        //Keep its z/y/x conversion order; no host pointer or ABI spill is involved.
        const auto z = thunder_coordinate(weather->last_lightning_location[2]);
        const auto y = thunder_coordinate(weather->last_lightning_location[1]);
        const auto x = thunder_coordinate(weather->last_lightning_location[0]);
        world_operations::submit_weather_sound(c, WeatherSound::thunder, { x, y, z });
    }

    if (c.weather.weather_refresh_requested == 1) {
        weather->current_drops = 0;
        const auto saved_target = weather->target_drops;
        const WeatherScheduleEntry saved{ .month = weather->active.month,
                                          .type = weather->active.type,
                                          .quality = weather->active.quality,
                                          .length = weather->active.length,
                                          .valid = 1,
                                          .padding = {} };
        const WeatherScheduleEntry reset{ .month = 0,
                                          .type = WeatherCategory::code_0,
                                          .quality = 80,
                                          .length = 2,
                                          .valid = 1,
                                          .padding = {} };
        apply_world_weather_configuration(c, weather, &reset);
        c.weather.weather_refresh_requested = 0;
        apply_world_weather_configuration(c, weather, &saved);
        weather->target_drops = saved_target;
    }
}
} //namespace simcoaster
