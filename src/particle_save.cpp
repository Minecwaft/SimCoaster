#include "simcoaster/schema_projection.hpp"
#include "simcoaster/save_stream.hpp"
#include "simcoaster/particle_save.hpp"
#include "simcoaster/world_save.hpp"
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using save::read;
using save::write;

struct SavedParticleState {
    [[=save::Field{ "live_particle_count", 0 }]] std::int32_t live_particle_count{};
    [[=save::Field{ "update_call_count", 1 }]] std::int32_t update_call_count{};
    [[=save::Field{ "creation_filter_enabled", 2 }]] std::int32_t creation_filter_enabled{};
    [[=save::Field{ "particle_capacity", 3 }]] std::int32_t particle_capacity{};
    [[=save::Field{ "random_state", 4 }]] std::int32_t random_state{};
    [[=save::Field{ "unknown_reset_one_020", 5 }]] std::int32_t unknown_reset_one_020{};
    [[=save::Field{ "next_effect_generation", 6 }]] std::uint16_t next_effect_generation{};
    [[=save::Field{ "next_attractor_generation", 7 }]] std::uint16_t next_attractor_generation{};
    [[=save::Field{ "sine_marker", 8 }]] std::uint32_t sine_marker{};
    [[=save::Field{ "parameter_scale", 9 }]] std::int32_t parameter_scale{};
    [[=save::Field{ "surface_callback_marker", 10 }]] std::uint32_t surface_callback_marker{};
    [[=save::Field{ "particle_pool_marker", 11 }]] std::uint32_t particle_pool_marker{};
    [[=save::Field{ "effects", 12 }]] std::array<ParticleEffect, 120> effects{};
    [[=save::Field{ "attractors", 13 }]] std::array<ParticleAttractor, 20> attractors{};
    [[=save::Field{ "active_effect_head", 14 }]] std::int16_t active_effect_head{};
    [[=save::Field{ "active_attractor_head", 15 }]] std::int16_t active_attractor_head{};
    [[=save::Field{ "free_effect_head", 16 }]] std::int16_t free_effect_head{};
    [[=save::Field{ "free_attractor_head", 17 }]] std::int16_t free_attractor_head{};
    [[=save::Field{ "free_particle_head", 18 }]] std::int16_t free_particle_head{};
    [[=save::Field{ "unknown_tail", 19 }]] std::array<std::uint8_t, 18> unknown_tail{};
};

struct SavedParticleTemplates {
    [[=save::Field{ "templates", 0 }]] std::array<ParticleEffect, 105> templates{};
    [[=save::Field{ "attractor_templates", 1 }]] std::array<ParticleAttractor, 20> attractor_templates{};
};

static_assert(save::record_width<SavedParticleState> == 40552);
static_assert(save::record_width<SavedParticleTemplates> == 35680);

using schema::copy_matching;

constexpr std::uint32_t section_tag = 1397773123u;
constexpr std::array<char, 4> snapshot_tag{ 'L', 'C', 'T', 'P' };

void clear_live_particles(ParticleRuntime& runtime) {
    auto& state = runtime.state;
    state.live_particle_count = 0;
    state.free_particle_head = 0;
    for (std::size_t i = 0; i < runtime.particles.size(); ++i) {
        runtime.particles[i] = {};
        runtime.particles[i].links.previous = static_cast<std::int16_t>(static_cast<std::int32_t>(i) - 1);
        runtime.particles[i].links.next = i + 1 == runtime.particles.size() ? -1 : static_cast<std::int16_t>(i + 1);
    }
    auto index = state.active_effect_head;
    std::size_t visited = 0;
    while (index >= 0) {
        if (static_cast<std::size_t>(index) >= state.effects.size() || visited++ >= state.effects.size())
            throw std::out_of_range("invalid active particle-effect chain");
        auto& effect = state.effects[static_cast<std::size_t>(index)];
        effect.particle_head = -1;
        effect.particle_count = 0;
        index = effect.links.next;
    }
}
} //namespace

bool restore_particle_save_section(ParticleRuntime& runtime, Stream& stream) {
    std::uint32_t tag{};
    if (!read(stream, tag) || tag != section_tag || !runtime.initialized)
        return false;
    std::array<char, 4> magic{};
    std::uint32_t active{}, size{};
    if (!read(stream, magic) || magic != snapshot_tag || !read(stream, active))
        return false;
    if (!active)
        return true;
    SavedParticleState saved{};
    SavedParticleTemplates templates{};
    if (!read(stream, size)
        || size != 40552
        || !read(stream, saved)
        || saved.particle_capacity <= 0
        || saved.particle_capacity > std::numeric_limits<std::int16_t>::max()
        || !read(stream, size)
        || size != 35680
        || !read(stream, templates))
        return false;
    copy_matching(runtime.state, saved);
    copy_matching(runtime, templates);
    runtime.particles.resize(static_cast<std::size_t>(saved.particle_capacity));
    if (!read(stream, size))
        return false;
    const auto expected = runtime.particles.size() * save::record_width<ParticleInstance>;
    if (!size) {
        clear_live_particles(runtime);
        return true;
    }
    if (size != expected)
        return false;
    std::vector<std::byte> bytes(size);
    if (stream.read(bytes) != static_cast<std::int32_t>(size))
        return false;
    for (std::size_t i = 0; i < runtime.particles.size(); ++i)
        save::detail::decode(std::span<const std::byte, 52>{ bytes.data() + i * 52, 52 }, runtime.particles[i]);
    return true;
}

bool write_particle_save_section(const ParticleRuntime& runtime, Stream& stream, bool include_live_particles) {
    if (!write(stream, section_tag)
        || !write(stream, snapshot_tag)
        || !write(stream, static_cast<std::uint32_t>(runtime.initialized)))
        return false;
    if (!runtime.initialized)
        return true;
    if (runtime.state.particle_capacity <= 0
        || runtime.state.particle_capacity > std::numeric_limits<std::int16_t>::max()
        || runtime.particles.size() != static_cast<std::size_t>(runtime.state.particle_capacity))
        return false;
    SavedParticleState state{};
    copy_matching(state, runtime.state);
    //These legacy process pointers are discarded by the loader; canonical zero
    //avoids serializing host addresses into a portable save.
    SavedParticleTemplates templates{};
    copy_matching(templates, runtime);
    if (!write(stream, 40552u) || !write(stream, state) || !write(stream, 35680u) || !write(stream, templates))
        return false;
    const auto size = include_live_particles ? runtime.particles.size() * 52u : 0u;
    if (!write(stream, static_cast<std::uint32_t>(size)))
        return false;
    if (!size)
        return true;
    std::vector<std::byte> bytes;
    bytes.reserve(size);
    for (const auto& particle : runtime.particles) {
        const auto encoded = save::detail::encode(particle);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    return stream.write(bytes) == static_cast<std::int32_t>(size);
}

void expire_screen_particle_effects(ParticleRuntime& runtime) {
    if (!runtime.initialized)
        return;
    auto& state = runtime.state;
    auto effect_index = state.active_effect_head;
    std::size_t visited = 0;
    while (effect_index >= 0) {
        if (static_cast<std::size_t>(effect_index) >= state.effects.size() || visited++ >= state.effects.size())
            throw std::out_of_range("invalid active particle-effect chain");
        auto& effect = state.effects[static_cast<std::size_t>(effect_index)];
        if (effect.screen_space) {
            effect.remaining_lifetime = -2;
            effect.render_hidden = 1;
        }
        effect_index = effect.links.next;
    }
    auto attractor_index = state.active_attractor_head;
    visited = 0;
    while (attractor_index >= 0) {
        if (static_cast<std::size_t>(attractor_index) >= state.attractors.size()
            || visited++ >= state.attractors.size())
            throw std::out_of_range("invalid active particle-attractor chain");
        auto& attractor = state.attractors[static_cast<std::size_t>(attractor_index)];
        if (attractor.expire_with_screen_effects)
            attractor.remaining_lifetime = -2;
        attractor_index = attractor.links.next;
    }
}
} //namespace simcoaster
