#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

namespace simcoaster {
enum class FrameStage {
    update,
    terrain_atlas,
    mesh_bases,
    terrain,
    world_meshes,
    sky_shadow,
    sprites,
    particles,
    weather,
    scene,
    ui_build,
    world_submit,
    ui_submit,
    screen_particles,
    present,
    count
};
inline constexpr auto frame_stage_names = std::define_static_array([] consteval {
    std::vector<const char*> names;
    for (auto member : std::meta::enumerators_of(^^FrameStage)) {
        if (std::meta::identifier_of(member) != "count") {
            names.push_back(std::define_static_string(std::meta::identifier_of(member)));
        }
    }
    return names;
}());

struct FrameCounters {
    std::uint64_t draw_calls {}, constant_uploads {}, state_bindings {}, geometry_maps {}, geometry_bytes {}, texture_uploads {}, texture_bytes {}, mip_generations {}, terrain_lighting_rebuilds {}, terrain_cells {}, terrain_cells_culled {}, sky_geometry_rebuilds {}, sky_uv_updates {}, mesh_base_evaluations {}, mesh_base_compositions {}, draws_coalesced {};
};

struct FrameProfile {
    std::uint64_t frame {};
    std::array<double, static_cast<std::size_t>(FrameStage::count)> cpu_ms {};
    FrameCounters counters;
};

inline thread_local FrameProfile* active_frame_profile {};

class FrameProfileScope {
    FrameProfile* previous_ = active_frame_profile;

public:
    explicit FrameProfileScope(FrameProfile& frame) {
        active_frame_profile = &frame;
    }

    ~FrameProfileScope() {
        active_frame_profile = previous_;
    }
};

class FrameTimer {
    FrameProfile* profile_ = active_frame_profile;
    FrameStage stage_;
    std::chrono::steady_clock::time_point start_ {};

public:
    explicit FrameTimer(FrameStage stage) : stage_(stage) {
        if (profile_) {
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~FrameTimer() {
        if (profile_) {
            profile_->cpu_ms[static_cast<std::size_t>(stage_)] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
        }
    }
};
enum class GpuPass {
    background,
    world,
    sprites,
    foreground,
    ui,
    screen,
    count
};

struct GpuFrameProfile {
    std::uint64_t frame {};
    double total_ms {};
    std::array<double, static_cast<std::size_t>(GpuPass::count)> pass_ms {};
    bool disjoint {};
};
} //namespace simcoaster
