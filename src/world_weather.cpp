#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_weather.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mail.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator)
        throw std::domain_error("weather ratio has a zero divisor");
    if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)
        throw std::overflow_error("weather ratio is not representable");
    return numerator / denominator;
}

std::int32_t quality_percentage(std::int32_t quality, std::int32_t low, std::int32_t high) {
    const auto range = numeric::subtract(high, low);
    return range ? divide(numeric::product(100, numeric::subtract(quality, low)), range) : 100;
}

void mark_weather_messages(WeatherWorld& c, std::int32_t key) {
    auto* system = c.entities.payload<TagSystemRecord>(c.simulation.reference->saved.tag_system);
    mark_tag_messages_by_primary_key_for_deletion(c, system, key);
}

void set_motion_scale(WeatherWorld& c, float scale) {
    const auto modes = c.weather.wind_effects.precipitation_modes;
    if (modes & 1)
        c.weather.wind_effects.rain_motion_scale = scale;
    if (modes & 2)
        c.weather.wind_effects.snow_motion_scale = scale;
}

WorldVector3 normalized_drift(float x, float z) {
    const auto length = std::sqrt(static_cast<double>(x) * x + static_cast<double>(z) * z + 1.0);
    if (length > 0.0)
        return { static_cast<float>(x / length), static_cast<float>(-1.0 / length), static_cast<float>(z / length) };
    return { x, -1.0f, z };
}

constexpr double radians(double degrees) {
    return degrees * (std::numbers::pi / 180.0);
}
} //namespace

WeatherScheduleEntry* initialize_world_weather_schedule_entry(WeatherScheduleEntry* entry) {
    //Game.exe0x6430E0: supplied initialized storage retains type/quality/padding.
    entry->length = 0;
    entry->month = 0;
    entry->valid = 0;
    return entry;
}

void set_world_precipitation_mode(WeatherWorld& c, WeatherRecord* weather, WeatherPrecipitation mode) {
    weather->precipitation = mode;
    match (mode) {
        case WeatherPrecipitation::rain => {
            c.weather.wind_effects.precipitation_modes = 1;
            if (!weather->rain_sound_handle)
                weather->rain_sound_handle = world_operations::submit_weather_sound(c, WeatherSound::rain, { 0, 0, 0 });
        }
        case WeatherPrecipitation::snow => {
            c.weather.wind_effects.precipitation_modes = 2;
            if (auto handle = weather->rain_sound_handle) {
                world_operations::stop_weather_sound(c, handle);
                weather->rain_sound_handle = 0;
            }
        }
        case WeatherPrecipitation::none => {
            //None leaves the selector and target unchanged; configuration clears
            //its target before calling here. The direct update call relies on this.
            weather->current_drops = 0;
            c.weather.wind_effects.precipitation_amount = 0;
            if (auto handle = weather->rain_sound_handle) {
                world_operations::stop_weather_sound(c, handle);
                weather->rain_sound_handle = 0;
            }
        }
        case _ => ;
    }
}

void configure_wind_effect_vectors(WeatherWorld& c, float direction_degrees, float magnitude) {
    //Game.exe0x6EA9A0 stores this vector before updating the four sky layers.
    const auto angle = radians(direction_degrees);
    c.weather.wind_effects.drift_vector = normalized_drift(static_cast<float>(std::cos(angle) * magnitude),
                                                           static_cast<float>(std::sin(angle) * magnitude));
    const float sky_speed = magnitude * 0.0017f;
    const float sky_direction = direction_degrees + 270.0f;
    const auto sky_velocity = [&](double x_offset, double z_offset) -> WorldVector3 {
        return { static_cast<float>(std::sin(radians(sky_direction + x_offset)) * sky_speed),
                 -1.0f,
                 static_cast<float>(std::cos(radians(sky_direction + z_offset)) * sky_speed) };
    };
    const auto layer0 = sky_velocity(0.0, 0.0);
    world_operations::set_sky_layer_uv_velocity(c, 0, layer0);
    const auto layer1 = sky_velocity(-1.9, -2.3);
    world_operations::set_sky_layer_uv_velocity(c, 1, layer1);
    const auto layer2 = sky_velocity(-1.2, 1.7);
    world_operations::set_sky_layer_uv_velocity(c, 2, layer2);
    const auto layer3 = sky_velocity(1.8, 2.9);
    world_operations::set_sky_layer_uv_velocity(c, 3, layer3);
    //The original then overwrites the same drift vector from layer3, rather
    //than retaining the first normalized horizontal-wind vector.
    c.weather.wind_effects.drift_vector = normalized_drift(layer3[0] * 0.2f, layer3[2] * 0.2f);
}

void
apply_world_weather_configuration(WeatherWorld& c, WeatherRecord* weather, const WeatherScheduleEntry* configuration) {
    const auto month = configuration->month, quality = configuration->quality, length = configuration->length;
    const auto category = configuration->type;
    weather->active.month = month;
    weather->active.quality = quality;
    weather->active.length = length;
    weather->active.valid = 1;
    weather->active.type = category;
    weather->quality = configuration->quality;
    weather->type = weather->active.type;
    const auto precipitation =
        [&](WeatherPrecipitation mode, std::int32_t key, bool snow, float motion_scale, bool lightning) {
            mark_weather_messages(c, key);
            const auto& effects = c.weather.weather_effects_balance;
            const auto percent = snow ? quality_percentage(weather->quality, effects.snow_low, effects.snow_high)
                                      : quality_percentage(weather->quality, effects.rain_low, effects.rain_high);
            set_world_precipitation_mode(c, weather, mode);
            //Reload after sound dispatch; the quality percentage remains the earlier
            //snapshot. The signed upper cap deliberately has no lower zero floor.
            const auto maximum = c.weather.weather_effects_balance.max_raindrops;
            const auto target = divide(numeric::product(maximum, numeric::subtract(100, percent)), 100);
            weather->target_drops = std::min(target, maximum);
            set_motion_scale(c, motion_scale);
            weather->lightning = lightning;
        };
    match (category) {
        case WeatherCategory::rain     => precipitation(WeatherPrecipitation::rain, 65, false, 2.5f, false);
        case WeatherCategory::snow     => precipitation(WeatherPrecipitation::snow, 68, true, 0.8f, false);
        case WeatherCategory::storm    => precipitation(WeatherPrecipitation::rain, 71, false, 4.5f, true);
        case WeatherCategory::heatwave => {
            mark_weather_messages(c, 62);
            weather->lightning = 0;
            weather->target_drops = 0;
            set_world_precipitation_mode(c, weather, WeatherPrecipitation::none);
        }
        case _ => {
            weather->lightning = 0;
            weather->target_drops = 0;
            set_world_precipitation_mode(c, weather, WeatherPrecipitation::none);
        }
    } const auto force = static_cast<float>(static_cast<double>(numeric::subtract(100, weather->quality)) * 0.01f);
    weather->wind_force = std::clamp(force, 0.0f, 1.0f);
    const float previous_direction = weather->wind_direction;
    const auto variation = numeric::bits(next_world_prng_value(*c.simulation.reference)) % 90u;
    float direction = static_cast<float>(static_cast<double>(variation) + previous_direction - 45.0);
    if (direction < 0.0f)
        direction = std::abs(direction);
    direction = std::fmod(direction, 360.0f);
    weather->wind_direction = direction;
    configure_wind_effect_vectors(c, direction, weather->wind_force);
    //Reload the current category after the wind integration calls. Unknown
    //category values follow the original default path, which hides the icon.
    const auto icon = match (weather->type) -> WeatherCategory {
        case WeatherCategory::heatwave => WeatherCategory::heatwave;
        case WeatherCategory::rain     => WeatherCategory::rain;
        case WeatherCategory::storm    => WeatherCategory::storm;
        case WeatherCategory::snow     => WeatherCategory::snow;
        case _                         => WeatherCategory::code_0;
    };
    world_operations::set_weather_icon(c, icon);
}

WeatherRecord* initialize_world_weather_state(WeatherWorld& c, WeatherRecord* weather) {
    initialize_world_map_object_base(c, &weather->base, 1, static_cast<std::uint32_t>(ThingKind::weather));
    initialize_world_weather_schedule_entry(&weather->pending);
    initialize_world_weather_schedule_entry(&weather->active);
    weather->wind_direction = 0.0f;
    register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &weather->base.header, 12);
    weather->pending.valid = 0;
    weather->active.month = -1;
    weather->active.quality = 80;
    weather->active.length = 0;
    weather->active.valid = 1;
    weather->active.type = WeatherCategory::code_0;
    apply_world_weather_configuration(c, weather, &weather->active);
    weather->overridden = 0;
    weather->target_drops = 0;
    weather->current_drops = 0;
    weather->lightning = 0;
    weather->thunder_countdown = 0;
    weather->lightning_countdown = 0;
    weather->lightning_count_this_period = 0;
    weather->rain_sound_handle = 0;
    weather->wind_direction = 90.0f;
    weather->wind_force = 0.5f;
    //These final record wind values do not trigger another vector update.
    world_operations::load_weather_effect_textures(c);
    c.weather.wind_effects.flash_pending = false;
    return weather;
}

void destroy_world_weather_state(WeatherWorld& c, WeatherRecord* weather) {
    unlink_world_used_thing_map_and_event_lists(c, &weather->base.header);
    world_operations::release_weather_effect_textures(c);
}
} //namespace simcoaster
