#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
WeatherScheduleEntry* initialize_world_weather_schedule_entry(WeatherScheduleEntry*);
WeatherRecord* initialize_world_weather_state(WeatherWorld&, WeatherRecord*);
void apply_world_weather_configuration(WeatherWorld&, WeatherRecord*, const WeatherScheduleEntry*);
void set_world_precipitation_mode(WeatherWorld&, WeatherRecord*, WeatherPrecipitation);
void configure_wind_effect_vectors(WeatherWorld&, float direction_degrees, float magnitude);
void destroy_world_weather_state(WeatherWorld&, WeatherRecord*);
void set_rain_ambient_volume(WeatherWorld&, std::int32_t volume);
void make_weather_lightning_endpoints(WeatherWorld&, WorldVector3& ground, WorldVector3& sky);
void update_world_weather_state(WeatherWorld&, WeatherRecord*);
WeatherScheduleEntry* choose_next_world_weather_schedule_entry(WeatherWorld&, WeatherScheduleEntry*);
void handle_world_weather_schedule_event(WeatherWorld&, WeatherRecord*, WorldEvent*);
} //namespace simcoaster
