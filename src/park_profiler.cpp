#include "simcoaster/park_profiler.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/mesh_renderer.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <numeric>

namespace simcoaster {
namespace {
inline constexpr auto counter_members = std::define_static_array(std::meta::nonstatic_data_members_of(^^FrameCounters, std::meta::access_context::current()));

std::uint64_t digest(std::span<const std::byte> bytes, std::uint64_t value = 14695981039346656037ull) {
    for (auto byte : bytes) {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= 1099511628211ull;
    }
    return value;
}

template <class T>
void hash_value(std::uint64_t& value, const T& item) {
    static_assert(std::is_trivially_copyable_v<T>);
    value = digest(std::as_bytes(std::span { &item, 1 }), value);
}

std::uint64_t gameplay_hash(WorldRuntime& c) {
    auto value = digest(save::detail::encode(c.simulation.current->saved));
    hash_value(value, c.random.crt_random_state);
    hash_value(value, c.random.mesh_passenger_random_state);
    for (const auto& cell : c.simulation.current->map_cells) {
        const auto bytes = save::detail::encode(cell.base.common);
        value = digest(bytes, value);
        hash_value(value, cell.who);
        hash_value(value, cell.is_obstacle);
    }
    if (auto* row = c.entities.at(c.simulation.current->saved.bank_account).payload) {
        value = digest(save::detail::encode(*reinterpret_cast<BankRecord*>(row)), value);
    }
    return value;
}

//Hash values recursively, never host padding or pointer addresses. This is a
//diagnostic snapshot, not a save operation (the native save mutates routing).
template <class T>
void hash_logical(std::uint64_t& value, const T& item) {
    if constexpr (std::is_pointer_v<T>) {
        return;
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        hash_value(value, item);
    } else if constexpr (std::ranges::range<T>) {
        for (const auto& element : item) {
            hash_logical(value, element);
        }
    } else {
        constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
        template for (constexpr auto member : members) hash_logical(value, item.[:member:]);
    }
}

std::uint64_t entity_hash(const WorldRuntime& c) {
    std::uint64_t value = 14695981039346656037ull;
    for (const auto& row : c.entities) {
        if (const auto* header = row.payload) {
            if (header->kind == ThingKind::person) {
                hash_logical(value, *reinterpret_cast<const PersonRuntimeView*>(header));
            } else if (is_staff_record_kind(header->kind)) {
                hash_logical(value, *reinterpret_cast<const StaffRuntimeRecord*>(header));
                match (header->kind) {
                    case ThingKind::mechanic    => hash_logical(value, *reinterpret_cast<const MechanicRuntimeRecord*>(header));
                    case ThingKind::handyman    => hash_logical(value, *reinterpret_cast<const HandymanRuntimeRecord*>(header));
                    case ThingKind::entertainer => {
                        hash_logical(value, *reinterpret_cast<const EntertainerRuntimeRecord*>(header));
                    }
                    case ThingKind::guard    => hash_logical(value, *reinterpret_cast<const GuardRuntimeRecord*>(header));
                    case ThingKind::gardener => hash_logical(value, *reinterpret_cast<const GardenerRuntimeRecord*>(header));
                    case _                   => {
                    }
                }
            }
        }
    }
    return value;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(static_cast<double>(values.size() - 1) * p)];
}
} //namespace

int run_park_profiler(std::span<const std::string_view> args, const std::filesystem::path& default_root) {
    if (args.size() < 2) {
        throw std::invalid_argument("profile-park <theme> <save> [frames] [root] [--profile low|med|high] [--render] [--sweep] [--paused] [--warmup N] [--width N] [--height N] [--output prefix] [--capture N]");
    }
    std::filesystem::path root = default_root.empty() ? std::filesystem::current_path() : default_root;
    std::filesystem::path output;
    int frames = 120, warmup = 30, capture = -1;
    std::uint32_t profile_index = 1, width = 1024, height = 768;
    bool render = false, sweep = false, paused = false, software = false;
    std::size_t positional {};
    for (std::size_t i = 2; i < args.size(); ++i) {
        const auto option = args[i];
        const auto next = [&]() {
            if (++i >= args.size()) {
                throw std::invalid_argument(std::format("Missing value for {}", option));
            }
            return std::string { args[i] };
        };
        if (option == "--profile") {
            const auto name = next();
            if (name == "high") {
                profile_index = 2;
            } else if (name == "med") {
                profile_index = 1;
            } else if (name == "low") {
                profile_index = 0;
            } else {
                throw std::invalid_argument("Unknown profile");
            }
        } else if (option == "--render") {
            render = true;
        } else if (option == "--sweep") {
            sweep = true;
        } else if (option == "--paused") {
            paused = true;
        } else if (option == "--software") {
            software = true;
        } else if (option == "--warmup") {
            warmup = std::stoi(next());
        } else if (option == "--width") {
            width = static_cast<std::uint32_t>(std::stoul(next()));
        } else if (option == "--height") {
            height = static_cast<std::uint32_t>(std::stoul(next()));
        } else if (option == "--output") {
            output = next();
        } else if (option == "--capture") {
            capture = std::stoi(next());
        } else if (option == "--root") {
            root = next();
        } else if (!option.starts_with("--") && positional++ == 0) {
            frames = std::stoi(std::string { option });
        } else if (!option.starts_with("--") && positional == 2) {
            root = option;
        } else {
            throw std::invalid_argument(std::format("Unknown profile option: {}", option));
        }
    }
    if (frames < 1 || frames > 10000 || warmup < 0 || warmup > 10000 || !width || !height || width > 16384 || height > 16384 || capture >= frames) {
        throw std::invalid_argument("Invalid profiling count, dimensions or capture frame");
    }
    if (!output.empty() && !output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    const auto profile = load_application_hardware_profile(root, profile_index);
    if (!profile) {
        throw std::runtime_error(profile.error().reason);
    }
    ParkRuntimeOpen open;
    open.theme = args[0];
    open.saved_park = std::filesystem::path { args[1] };
    open.hardware_profile = *profile;
    ParkRuntime park { root, std::move(open) };
    const auto initial_camera = park.render_camera();
    std::unique_ptr<MeshRenderer> renderer;
    if (render) {
        renderer = std::make_unique<MeshRenderer>(width, height, nullptr, software, software ? std::nullopt : std::optional { 0u }, profile->graphics);
        if (!software && renderer->software_adapter()) {
            throw std::runtime_error("Hardware benchmark fell back to WARP");
        }
    }
    std::vector<FrameProfile> results;
    std::vector<GpuFrameProfile> gpu;
    std::vector<double> totals;
    std::uint64_t image_hash {};
    MeshScene scene;
    UiDrawList ui;
    EffectScene screen;
    for (int frame = -warmup; frame < frames; ++frame) {
        FrameProfile statistics;
        statistics.frame = static_cast<std::uint64_t>(frame + warmup);
        FrameProfileScope scope { statistics };
        auto camera = initial_camera;
        if (sweep) {
            const auto t = static_cast<float>(frame + warmup) * 0.01f;
            camera.yaw += t;
            camera.target[0] += 12.f * std::sin(t * .7f);
            camera.target[2] += 8.f * std::sin(t * .4f);
            camera.distance *= 1.f + .15f * std::sin(t * .6f);
        }
        park.set_render_camera(camera);
        const auto begin = std::chrono::steady_clock::now();
        {
            FrameTimer timer { FrameStage::update };
            park.advance_frame(std::chrono::milliseconds { 16 }, paused);
        }
        camera = park.render_camera();
        {
            FrameTimer timer { FrameStage::scene };
            park.build_scene(scene, camera, static_cast<float>(width) / static_cast<float>(height));
        }
        {
            FrameTimer timer { FrameStage::ui_build };
            park.ui().build_draw(ui, width, height);
        }
        if (renderer) {
            renderer->draw(scene, camera);
            renderer->draw_ui(ui);
            park.build_screen_particles(screen, static_cast<float>(width) / static_cast<float>(height));
            renderer->draw_effects(screen);
            renderer->finish_profile_frame();
        }
        const auto end = std::chrono::steady_clock::now();
        if (frame >= 0) {
            results.push_back(statistics);
            totals.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
        if (renderer) {
            auto samples = renderer->take_profile_samples();
            gpu.insert(gpu.end(), samples.begin(), samples.end());
            if (frame == capture) {
                const auto image = renderer->capture();
                image_hash = digest(std::as_bytes(std::span { image.pixels }));
                if (!output.empty()) {
                    write_texture_png(image, output.native_encoded_string() + ".png");
                    std::ofstream raw(output.native_encoded_string() + ".rgba", std::ios::binary);
                    raw.write(reinterpret_cast<const char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size() * sizeof(RgbaPixel)));
                }
            }
        }
    }
    if (renderer) {
        auto rest = renderer->take_profile_samples(true);
        gpu.insert(gpu.end(), rest.begin(), rest.end());
    }
    std::erase_if(gpu, [&](const auto& row) {
        return row.frame < static_cast<std::uint64_t>(warmup) || row.disjoint;
    });
    std::vector<double> gpu_totals;
    for (const auto& row : gpu) {
        gpu_totals.push_back(row.total_ms);
    }
    print_text(stdout, "Profile={} procedural={} sky_shadow={} sprite_shadow={} lighting={} resolution={}x{} adapter={} sweep={:d} paused={:d}\n", profile_index, profile->graphics.procedural_texturing, profile->graphics.sky_shadow, profile->graphics.sprite_shadow, profile->graphics.lighting, width, height, renderer ? renderer->adapter_name() : "CPU only", sweep, paused);
    print_text(stdout, "CPU frame median={:.4f} p95={:.4f} GPU median={:.4f} p95={:.4f} samples={}\n", percentile(totals, .5), percentile(totals, .95), percentile(gpu_totals, .5), percentile(gpu_totals, .95), gpu.size());
    for (std::size_t stage = 0; stage < frame_stage_names.size(); ++stage) {
        double sum {};
        for (const auto& row : results) {
            sum += row.cpu_ms[stage];
        }
        print_text(stdout, "{}={:.4f} ", frame_stage_names[stage], sum / static_cast<double>(results.size()));
    }
    print_text(stdout, "\nGameplay digest={} entity digest={} image digest={}\n", gameplay_hash(park.context()), entity_hash(park.context()), image_hash);
    if (!output.empty()) {
        std::ofstream csv(output.native_encoded_string() + ".csv");
        std::string line { "frame,total_ms" };
        for (auto name : frame_stage_names) {
            std::format_to(std::back_inserter(line), ",{}", name);
        }
        template for (constexpr auto member : counter_members) std::format_to(std::back_inserter(line), ",{}", std::meta::identifier_of(member));
        line += ",gpu_ms,gpu_background,gpu_world,gpu_sprites,gpu_foreground,gpu_ui,gpu_screen\n";
        csv.write(line.data(), static_cast<std::streamsize>(line.size()));
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& row = results[i];
            line.clear();
            std::format_to(std::back_inserter(line), "{},{:.6g}", row.frame, totals[i]);
            for (auto time : row.cpu_ms) {
                std::format_to(std::back_inserter(line), ",{:.6g}", time);
            }
            template for (constexpr auto member : counter_members) std::format_to(std::back_inserter(line), ",{}", row.counters.[:member:]);
            const auto found = std::ranges::find(gpu, row.frame, &GpuFrameProfile::frame);
            if (found == gpu.end()) {
                line += ",,,,,,,";
            } else {
                std::format_to(std::back_inserter(line), ",{:.6g}", found->total_ms);
                for (auto time : found->pass_ms) {
                    std::format_to(std::back_inserter(line), ",{:.6g}", time);
                }
            }
            line += '\n';
            csv.write(line.data(), static_cast<std::streamsize>(line.size()));
        }
        if (!csv) {
            throw std::runtime_error("Cannot write profiling CSV");
        }
    }
    return 0;
}
} //namespace simcoaster
