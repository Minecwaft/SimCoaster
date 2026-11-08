#include "simcoaster/require_bound.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/weather_runtime.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/world_research.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
using Vec = WorldVector3;
using Seasons = StandardSeasons;

//Each scalar record owns only a subset of Standard.sam. Select its groups
//from the reflected descriptor metadata; retain strict validation inside
//those groups. The original complete balance receiver parses all of them.
template <class T>
std::expected<void, sam::Error> load_groups(T& record, std::string_view source, bool preserve) {
    return sam::parse_integer_record_checked<T, true>(record, source, preserve);
}

WeatherEffectsRuntime& effects(WeatherWorld& c) {
    return require_bound(c.weather.weather_runtime, "Weather presentation runtime is not bound");
}

TextureImagePtr load_texture(WeatherWorld& c, std::string_view path) {
    auto packed_path = std::filesystem::path { std::string { path } };
    packed_path.replace_extension(".fsh");
    auto bytes = c.ports.assets.read_balance_asset(c, packed_path.native_encoded_string());
    if (!bytes) {
        bytes = c.ports.assets.read_balance_asset(c, path);
    }
    if (!bytes) {
        return {};
    }
    const auto data = std::as_bytes(std::span { bytes->data(), bytes->size() });
    auto decoded = data.size() >= 4 && std::string_view { bytes->data(), 4 } == "SHPI" ? decode_fsh_image(data) : decode_tga_image(data);
    if (!decoded) {
        throw std::runtime_error(std::format("{} in {}", describe_texture_error(decoded.error()), path));
    }
    decoded->source_mode = TextureSourceMode::alpha_low;
    return std::make_shared<TextureImage>(std::move(*decoded));
}

double squared(Vec v) {
    return static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] + static_cast<double>(v[2]) * v[2];
}

Vec difference(Vec a, Vec b) {
    for (unsigned k = 0; k < 3; ++k) {
        a[k] -= b[k];
    }
    return a;
}

float wrap_cube(float v) {
    return v > 3 ? v - 6 : v < -3 ? v + 6 : v;
}

std::uint32_t draw(std::uint32_t& seed) {
    return seed = std::rotl(std::rotl(seed * 1299827u, 3) + 918563u, 4);
}

Vec cubic(const WeatherEffectsRuntime& runtime, double phase, double speed) {
    const auto scaled = phase * speed;
    const auto integral = static_cast<std::int32_t>(scaled);
    const auto t = scaled - integral;
    const auto& p = runtime.cubics[static_cast<std::uint32_t>(integral) & 127u];
    Vec result;
    for (unsigned k = 0; k < 3; ++k) {
        result[k] = static_cast<float>(((p.a[k] * t + p.b[k]) * t + p.c[k]) * t + p.d[k]);
    }
    return result;
}

Vec displaced_midpoint(Vec a, Vec b, std::uint32_t& seed) {
    Vec midpoint, delta = difference(b, a);
    for (unsigned k = 0; k < 3; ++k) {
        midpoint[k] = static_cast<float>((static_cast<double>(a[k]) + b[k]) * 0.5);
    }
    const float midpoint_squared = static_cast<float>(squared(midpoint));
    const auto random = [&] {
        return std::bit_cast<std::int32_t>(draw(seed));
    };
    const float first = static_cast<float>(static_cast<double>(random() % 8191) * 0.00012207031);
    float amplitude = static_cast<float>(static_cast<double>(random() % 8191) * 0.00012207031 * first * first);
    if (random() & 1) {
        amplitude = -amplitude;
    }
    (void)random();
    const auto selector = random() % 3;
    unsigned axis = selector == 0 ? 0u : selector == 1 ? 1u : 2u;
    if (delta[axis] == 0) {
        axis = axis == 0 ? (delta[1] != 0 ? 1u : 2u) : axis == 1 ? (delta[2] != 0 ? 2u : 0u) : (delta[0] != 0 ? 0u : 1u);
    }
    Vec perpendicular = midpoint;
    //A zero-length native segment produces NaNs. Keep that result explicit;
    //downstream emission rejects nonfinite geometry instead of recursing forever.
    perpendicular[axis] = static_cast<float>(midpoint[axis] - static_cast<double>(midpoint_squared) / delta[axis]);
    const auto scale = std::sqrt(squared(delta)) * 0.6f * amplitude / std::sqrt(squared(perpendicular));
    Vec result;
    for (unsigned k = 0; k < 3; ++k) {
        result[k] = static_cast<float>(perpendicular[k] * scale + midpoint[k]);
    }
    return result;
}

struct LightningBuilder {
    const WeatherLightning& bolt;
    std::vector<WeatherLightningPath>& paths;
    std::uint32_t seed, color;
    unsigned maximum_branches;

    void split(Vec a, Vec b, unsigned depth, std::size_t path, unsigned subdivisions = 0) {
        const auto midpoint = displaced_midpoint(b, a, seed);
        if (!std::ranges::all_of(midpoint, [](float f) {
                return std::isfinite(f);
            })) {
            throw std::domain_error("Degenerate lightning subdivision");
        }
        if ((draw(seed) & 511u) < 9u && depth < maximum_branches) {
            auto direction = difference(b, a);
            const auto length = std::sqrt(squared(direction));
            if (length) {
                for (auto& v : direction) {
                    v = static_cast<float>(v / length);
                }
            }
            const float inverse = 1.0f / static_cast<float>(96u * (depth + 1));
            const auto forward = static_cast<double>((draw(seed) & 1023u) + 512u) * inverse * 2.5;
            for (auto& v : direction) {
                v = static_cast<float>(v * forward);
            }
            const float along = static_cast<float>(static_cast<double>((draw(seed) & 1023u) + 512u) * inverse);
            for (unsigned k = 0; k < 3; ++k) {
                direction[k] = static_cast<float>(along * static_cast<double>(bolt.direction[k]) + direction[k]);
            }
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) {
                dot += static_cast<double>(direction[k]) * bolt.direction[k];
            }
            if (dot < 0) {
                for (unsigned k = 0; k < 3; ++k) {
                    direction[k] = static_cast<float>(along * 0.5 * bolt.direction[k] + direction[k] * 0.5);
                }
            }
            Vec endpoint;
            for (unsigned k = 0; k < 3; ++k) {
                endpoint[k] = a[k] + direction[k];
            }
            const auto branch = paths.size();
            paths.push_back({ {}, color, depth + 1 });
            split(endpoint, a, depth + 1, branch);
            paths[branch].points.push_back(endpoint);
        }
        //Disassembly 6EC04A..6EC109: one shared perturbed squared-distance
        //threshold for both halves. The decompiler loses both FPU comparisons.
        const float jitter = static_cast<float>((static_cast<std::int32_t>(draw(seed) & 4095u) - 2048) * 2.0 * 0.00048828125);
        const auto threshold = 6.0 + jitter;
        const auto half = [&](Vec left, Vec right) {
            if (squared(difference(left, right)) >= threshold) {
                if (subdivisions >= 64) {
                    throw std::domain_error("Nonconvergent lightning subdivision");
                }
                split(left, right, depth, path, subdivisions + 1);
            } else {
                paths[path].points.push_back(right);
            }
        };
        half(midpoint, b);
        half(a, midpoint);
    }
};
} //namespace

std::expected<void, sam::Error> load_weather_balance_checked(WeatherWorld& c, std::string_view text, bool preserve) {
    Seasons seasons;
    if (preserve) {
        seasons.extent = c.weather.weather_seasons_parsed_extent;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& row = c.weather.weather_season_chances[i];
            seasons.rows[i] = { row[0], row[1], row[2], row[3] };
        }
    }
    const auto weather = load_groups(c.weather.weather_balance, text, preserve);
    if (!weather) {
        return weather;
    }
    const auto effects = load_groups(c.weather.weather_effects_balance, text, preserve);
    if (!effects) {
        return effects;
    }
    const auto season = load_groups(seasons, text, preserve);
    if (!season) {
        return season;
    }
    c.weather.weather_seasons_parsed_extent = seasons.extent;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& row = seasons.rows[i];
        c.weather.weather_season_chances[i] = { row.heatwave, row.rain, row.storm, row.snow };
    }
    return {};
}

bool load_weather_balance(WeatherWorld& c, std::string_view text, bool preserve) {
    return load_weather_balance_checked(c, text, preserve).has_value();
}

void initialize_weather_particles(WeatherWorld& c, WeatherEffectsRuntime& runtime, std::uint32_t tick) {
    for (auto& point : runtime.positions) {
        for (auto& v : point) {
            v = static_cast<float>((static_cast<double>(next_crt_random(c) & 16383) - 8192.0) * 0.00036630037);
        }
    }
    for (std::size_t i = 0; i < 128; ++i) {
        const auto phase = static_cast<double>(i) * 0.0078125;
        runtime.envelope[i] = static_cast<float>((std::cos(phase * 3.1415927) + 1.0) * (0.99998999 - phase) * 0.5);
    }
    const auto perturb = [&] {
        return static_cast<float>(static_cast<double>((next_crt_random(c) & 2047) - 1024) * 0.000032552085);
    };
    for (std::size_t i = 0; i < 1024; ++i) {
        const auto phase = static_cast<double>(next_crt_random(c) % 75499 + 1) * 0.001;
        runtime.phases[i] = static_cast<float>(phase);
        runtime.reciprocal_phase_factors[i] = static_cast<float>(1.0 / (phase + 0.001));
        for (auto& v : runtime.perturbation_vectors[i]) {
            v = perturb();
        }
        runtime.phases[i] = static_cast<float>(static_cast<double>(next_crt_random(c) % 75499 + 1) * 0.001 + static_cast<double>(i));
    }
    std::array<Vec, 128> controls;
    for (auto& point : controls) {
        for (auto& v : point) {
            v = perturb();
        }
    }
    for (std::size_t i = 0; i < 128; ++i) {
        for (unsigned k = 0; k < 3; ++k) {
            const auto p0 = controls[(i + 125) % 128][k], p1 = controls[(i + 126) % 128][k], p2 = controls[(i + 127) % 128][k], p3 = controls[i][k];
            const auto previous = static_cast<double>(p0) - p1;
            const auto a = static_cast<double>(p3) - p2 - previous;
            auto& polynomial = runtime.cubics[i];
            polynomial.a[k] = static_cast<float>(a);
            polynomial.b[k] = static_cast<float>(previous - a);
            polynomial.c[k] = p2 - p0;
            polynomial.d[k] = p1;
        }
    }
    runtime.last_tick = tick;
    runtime.particles_initialized = true;
}

void initialize_weather_sky(WeatherWorld& c, WeatherEffectsRuntime& runtime) {
    const auto directory = c.source.data_directory + "/levels/" + c.source.selected_resource_source_name + "/sky/";
    runtime.sky = load_texture(c, directory + "sky.tga");
    runtime.cylinder = load_texture(c, directory + "sky_cyl.tga");
    const auto rgb = load_texture(c, directory + "sky_rgb.tga");
    for (unsigned row = 0; row < 16; ++row) {
        for (unsigned column = 0; column < 16; ++column) {
            const float x = static_cast<float>(column * static_cast<double>(static_cast<float>(2400.0 * 0.06666667)) - 1200.0);
            const double z = static_cast<double>(row) * static_cast<float>(2400.0 * 0.06666667) - 1200.0;
            const auto index = column * 16u + row;
            runtime.sky_vertices[index] = { x, static_cast<float>(300.0 - std::sqrt(static_cast<double>(x) * x + static_cast<float>(z * z)) * 0.2), static_cast<float>(z) };
            runtime.sky_colors[index] = (65793u * static_cast<std::uint32_t>(next_crt_random(c) & 127)) | 0xFF808080u;
            if (rgb) {
                const auto sx = rgb->width / 16, sy = rgb->height / 16;
                const auto& pixel = rgb->pixels.at(static_cast<std::size_t>(row * sy + sy / 2) * rgb->width + column * sx + sx / 2);
                runtime.sky_colors[index] = (static_cast<std::uint32_t>(pixel[3]) << 24) | (static_cast<std::uint32_t>(pixel[0]) << 16) | (static_cast<std::uint32_t>(pixel[1]) << 8) | pixel[2];
            }
        }
    }
    const double radius = 1200.0 * 0.6f, top = 300.0 - radius * 0.2, bottom = -top;
    constexpr std::array<double, 4> radii { 1.0, 0.98f, 0.96f, 0.84f };
    const std::array heights { top, top - (top - bottom) * 0.25, top - (top - bottom) * 0.5, bottom };
    for (unsigned half = 0; half < 2; ++half) {
        for (unsigned sample = 0; sample < 9; ++sample) {
            for (unsigned band = 0; band < 4; ++band) {
                const auto angle = static_cast<double>(sample * 2 + half * 16) * 3.1415927f * 0.0625;
                runtime.cylinder_vertices[half * 36 + sample * 4 + band] = { static_cast<float>(std::cos(angle) * radius * radii[band]), static_cast<float>(heights[band]), static_cast<float>(std::sin(angle) * radius * radii[band]) };
            }
        }
    }
    runtime.sky_indices.clear();
    const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t d) {
        if (runtime.sky_vertices[a][1] > bottom && runtime.sky_vertices[b][1] > bottom && runtime.sky_vertices[d][1] > bottom) {
            runtime.sky_indices.insert(runtime.sky_indices.end(), { a, b, d });
        }
    };
    for (unsigned row = 0; row < 15; ++row) {
        for (unsigned column = 0; column < 15; ++column) {
            const auto i = row * 16 + column;
            triangle(i + 1, i, i + 17);
            triangle(i, i + 16, i + 17);
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        runtime.sky_velocities[i] = { static_cast<float>(static_cast<double>(i) * 0.00050000002 + 0.00079999998), 0, static_cast<float>(0.000090000001 - static_cast<double>(i) * 0.000029999999) };
    }
}

void enqueue_weather_lightning(WeatherEffectsRuntime& runtime, Vec ground, Vec sky, std::int32_t duration, std::uint32_t seed) {
    if (runtime.lightning.size() >= 16) {
        return;
    }
    auto direction = difference(ground, sky);
    const auto length = std::sqrt(squared(direction));
    if (length) {
        for (auto& v : direction) {
            v = static_cast<float>(v / length);
        }
    }
    runtime.lightning.push_back({ ground, sky, direction, duration, seed });
}

void release_weather_sky_objects(WeatherEffectsRuntime& runtime) {
    runtime.frame.reset();
    runtime.sky_objects.clear();
    runtime.sky_objects_initialized = false;
}

void initialize_weather_sky_objects(WeatherWorld& c, WeatherEffectsRuntime& runtime) {
    const auto count = c.balance.standard_supplement.sky_count.n;
    if (count < 0 || static_cast<std::size_t>(count) > c.balance.standard_supplement.sky_objects.size()) {
        throw std::runtime_error("NumSkyObjects.n exceeds the four configured sky records");
    }
    if (!count || !runtime.sky_detail || runtime.sky_objects_initialized) {
        return;
    }
    std::vector<WeatherSkyObject> loaded;
    loaded.reserve(static_cast<std::size_t>(count));
    const auto directory = c.source.data_directory + "/levels/" + c.source.selected_resource_source_name + "/sky/";
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& config = c.balance.standard_supplement.sky_objects[static_cast<std::size_t>(i)];
        if (!config.filename || config.filename->empty()) {
            throw std::runtime_error(std::format("SkyObjects[{}].Filename is empty", i));
        }
        const auto path = directory + config.filename.value_or("");
        auto texture = load_texture(c, path);
        if (!texture) {
            throw std::runtime_error(std::format("Cannot load sky object texture: {}", path));
        }
        loaded.push_back({ { config.x, config.y, config.z }, config.size, config.front_flag != 0, std::move(texture) });
    }
    //Publish only the complete set. Failure releases the temporary live entries;
    //cleanup never searches inactive/null native registry slots.
    runtime.sky_objects = std::move(loaded);
    runtime.sky_objects_initialized = true;
}

void update_weather_effects(WeatherWorld& c, WeatherEffectsRuntime& runtime, std::uint32_t tick, bool freeze_motion, bool turbulent_snow) {
    if (!runtime.particles_initialized) {
        throw std::logic_error("Weather particles were not initialized");
    }
    const auto frame_ms = std::bit_cast<std::int32_t>(tick - runtime.last_tick);
    const auto elapsed = std::min(frame_ms, 500);
    runtime.last_tick = tick;
    const float seconds = freeze_motion ? 0.0f : static_cast<float>(static_cast<double>(elapsed) * 0.001);
    auto frame = std::make_shared<WeatherFrame>();
    frame->flash = std::exchange(c.weather.wind_effects.flash_pending, false);
    frame->sky_layers = runtime.sky_layers;
    frame->sky_detail = runtime.sky_detail;
    frame->sky_objects = runtime.sky_objects;
    frame->sky_shadow = runtime.sky_shadow;
    frame->modes = c.weather.wind_effects.precipitation_modes;
    frame->drift = c.weather.wind_effects.drift_vector;
    frame->camera_flags = c.view.flags;
    frame->diffuse_mask = runtime.diffuse_mask;
    frame->rain = runtime.rain;
    frame->snow = runtime.snow;
    frame->lightning = runtime.bolt;
    frame->sky = runtime.sky;
    frame->cylinder = runtime.cylinder;
    frame->sky_vertices = runtime.sky_vertices;
    frame->sky_colors = runtime.sky_colors;
    frame->sky_indices = runtime.sky_indices;
    frame->cylinder_vertices = runtime.cylinder_vertices;
    //Sky uses the full frame interval and rejects jumps outside +/-1 second.
    const auto sky_unrounded = static_cast<double>(frame_ms) * 0.001 * 25.0;
    const float sky_step = freeze_motion || sky_unrounded > 25 || sky_unrounded < -25 ? 0.0f : static_cast<float>(sky_unrounded);
    if (runtime.sky && runtime.cylinder) {
        for (std::size_t i = 0; i < 4; ++i) {
            for (unsigned axis : { 0u, 2u }) {
                auto& value = runtime.sky_offsets[i][axis];
                value = static_cast<float>(sky_step * static_cast<double>(runtime.sky_velocities[i][axis]) + value);
                if (value > 1 || value < -1) {
                    value -= std::trunc(value);
                }
            }
        }
    }
    frame->sky_offsets = runtime.sky_offsets;
    for (std::size_t i = runtime.lightning.size(); i-- > 0;) {
        auto& bolt = runtime.lightning[i];
        bolt.remaining_ms = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(bolt.remaining_ms) - static_cast<std::uint32_t>(elapsed));
        if (bolt.remaining_ms <= 0) {
            bolt = runtime.lightning.back();
            runtime.lightning.pop_back();
            continue;
        }
        std::uint32_t color = 0xFFFFFFFFu, seed = bolt.seed;
        unsigned branches = 0;
        if (seed) {
            const double saw = static_cast<double>(bolt.remaining_ms % 500) * 0.0020000001;
            const auto intensity = saw >= 0.89999998 ? 192 - static_cast<std::int32_t>((1 - saw) * 10 * -64) : 255 - static_cast<std::int32_t>((0.89999998 - saw) * 1.11 * 64);
            const auto brightness = static_cast<std::uint32_t>(intensity + (next_crt_random(c) & 63));
            color = brightness | ((brightness | (((brightness & 255u) | 0xFFFFFF00u) << 8)) << 8);
            branches = 4;
        } else {
            seed = draw(runtime.visual_seed);
        }
        const auto path = frame->bolts.size();
        frame->bolts.push_back({ {}, color, 0 });
        LightningBuilder builder { bolt, frame->bolts, seed, color, branches };
        builder.split(bolt.sky, bolt.ground, 0, path);
        frame->bolts[path].points.push_back(bolt.sky);
    }
    const auto amount = c.weather.wind_effects.precipitation_amount;
    if (amount < 0 || amount > 1024) {
        throw std::out_of_range("Weather drop count exceeds the native 1024-particle capacity");
    }
    if (amount) {
        const auto travel = static_cast<double>(frame->modes & 1u ? c.weather.wind_effects.rain_motion_scale : c.weather.wind_effects.snow_motion_scale) * seconds;
        for (unsigned k = 0; k < 3; ++k) {
            runtime.motion_offset[k] = wrap_cube(static_cast<float>(runtime.motion_offset[k] + travel * frame->drift[k] * (k == 1 ? 1.0 : -1.0)));
        }
        frame->offset = runtime.motion_offset;
        frame->drops.reserve(static_cast<std::size_t>(amount));
        std::array<Vec, 2> global {};
        std::array<Vec, 16> local {};
        if (!(frame->modes & 1u) && turbulent_snow) {
            const auto delta = static_cast<double>(seconds) * 5.0;
            for (std::size_t i = 0; i < 2; ++i) {
                global[i] = cubic(runtime, delta + runtime.phases[i], 0.07692308);
                for (unsigned k = 0; k < 3; ++k) {
                    global[i][k] = k == 0 ? global[i][k] * 13 + frame->drift[k] : global[i][k] * 13 * frame->drift[k];
                }
            }
            for (std::size_t i = 0; i < 16; ++i) {
                local[i] = cubic(runtime, delta + runtime.phases[i], 0.14285715);
                for (unsigned k = 0; k < 3; ++k) {
                    local[i][k] = local[i][k] * 6 + global[i & 1u][k];
                }
            }
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(amount); ++i) {
            auto p = runtime.positions[i];
            if (!(frame->modes & 1u) && (frame->modes & 2u)) {
                if (turbulent_snow) {
                    runtime.phases[i] = static_cast<float>(runtime.phases[i] + seconds * 5.0);
                    const auto noise = cubic(runtime, runtime.phases[i], 1.0);
                    for (unsigned k = 0; k < 3; ++k) {
                        p[k] = static_cast<float>(p[k] + noise[k] * 0.40000001 + local[i & 15u][k]);
                    }
                } else {
                    for (auto& v : p) {
                        v = static_cast<float>(v + static_cast<double>((next_crt_random(c) & 31) - 16) * 0.00048828125);
                    }
                }
            }
            frame->drops.push_back(p);
        }
    }
    runtime.frame = std::move(frame);
}

std::int32_t world_operations::builtin::submit_weather_sound(WorldRuntime& c, WeatherSound kind, const std::array<std::int64_t, 3>& position) {
    const auto narrow = [](std::int64_t v) {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(v));
    };
    return std::bit_cast<std::int32_t>(submit_positioned_template_request(c, 0, c.resources.ambient_catalog_slot, kind == WeatherSound::rain ? 0x21u : 0x20u, narrow(position[0]), narrow(position[1]), narrow(position[2])));
}

void world_operations::builtin::stop_weather_sound(WorldRuntime& c, std::int32_t handle) {
    c.ports.resources.submit_resource_target_parameter_request(c, std::bit_cast<std::uint32_t>(handle));
}

std::int32_t world_operations::builtin::set_weather_sound_volume(WorldRuntime& c, std::int32_t handle, std::int32_t volume) {
    return std::bit_cast<std::int32_t>(dispatch_resource_manager_operation(c, std::bit_cast<std::uint32_t>(handle), 8, volume));
}

void world_operations::builtin::set_weather_icon(WorldRuntime& c, WeatherCategory category) {
    const auto part = match (category) -> std::int32_t {
        case WeatherCategory::heatwave => 0;
        case WeatherCategory::rain     => 1;
        case WeatherCategory::storm    => 3;
        case WeatherCategory::snow     => 2;
        case _                         => -1;
    };
    c.weather.weather_hud.visible = part >= 0;
    if (part >= 0) {
        c.weather.weather_hud.attachment_part = part;
        c.weather.weather_hud.help_query = 343 + part;
    }
}

void world_operations::builtin::set_sky_layer_uv_velocity(WorldRuntime& c, unsigned layer, const Vec& velocity) {
    effects(c).sky_velocities.at(layer) = velocity;
}

void world_operations::builtin::load_weather_effect_textures(WorldRuntime& c) {
    auto& runtime = effects(c);
    const auto base = c.source.data_directory + "/generic/weather/";
    if (!runtime.snow) {
        runtime.snow = load_texture(c, base + "snow.tga");
    }
    if (!runtime.bolt) {
        runtime.bolt = load_texture(c, base + "lightning.tga");
    }
    if (!runtime.rain) {
        runtime.rain = load_texture(c, base + "Raindrop.tga");
    }
    c.weather.wind_effects.flash_pending = false;
}

void world_operations::builtin::release_weather_effect_textures(WorldRuntime& c) {
    auto& runtime = effects(c);
    runtime.snow.reset();
    runtime.bolt.reset();
    runtime.rain.reset();
    runtime.frame.reset();
}

//Original visual seeds came from RDTSC and are not saved. Use an independent
//reproducible visual stream; never consume the World or shared CRT stream here.
void world_operations::builtin::trigger_weather_lightning(WorldRuntime& c, const Vec& ground, const Vec& sky, std::int32_t duration) {
    auto& runtime = effects(c);
    enqueue_weather_lightning(runtime, ground, sky, duration, draw(runtime.visual_seed) | 1u);
}
} //namespace simcoaster
