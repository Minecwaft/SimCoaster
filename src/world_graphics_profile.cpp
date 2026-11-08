#include "simcoaster/world_graphics_profile.hpp"
#include "simcoaster/particle_runtime.hpp"
#include "simcoaster/sprite_catalog.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
struct InvalidLibrary {
    std::size_t offset;
    const char* reason;
};

struct LibraryReader {
    std::span<const std::byte> bytes;
    std::size_t position{};

    template <class T>
    T read() {
        constexpr auto size = save::value_width<T>;
        if (size > bytes.size() - position)
            throw InvalidLibrary{ position, "Truncated native particle library" };
        T result{};
        save::detail::decode(std::span<const std::byte, size>{ bytes.data() + position, size }, result);
        position += size;
        return result;
    }
};
} //namespace

std::expected<ParticleLibrarySettings, ApplicationConfigurationError>
initialize_world_profile_particles(ParticleRuntime& runtime,
                                   std::span<const std::byte> bytes,
                                   const ApplicationHardwareProfile& profile) {
    if (runtime.initialized)
        return std::unexpected(
            ApplicationConfigurationError{ 0, "Particle library initialization requires an uninitialized owner" });
    try {
        LibraryReader input{ bytes };
        std::array<ParticleEffect, 105> effects{};
        std::array<ParticleAttractor, 20> attractors{};
        const auto count = input.read<std::uint32_t>();
        const auto width = input.read<std::uint32_t>();
        if (count > effects.size() || width != save::record_width<ParticleEffect>)
            throw InvalidLibrary{ 0, "Unsupported particle effect count/record width" };
        for (std::size_t i = 0; i < count; ++i)
            effects[i] = input.read<ParticleEffect>();
        const auto auxiliary_header = input.position;
        const auto auxiliary_count = input.read<std::uint32_t>();
        const auto auxiliary_width = input.read<std::uint32_t>();
        if (auxiliary_count > attractors.size() || auxiliary_width != save::record_width<ParticleAttractor>)
            throw InvalidLibrary{ auxiliary_header, "Unsupported particle attractor count/record width" };
        for (std::size_t i = 0; i < auxiliary_count; ++i)
            attractors[i] = input.read<ParticleAttractor>();
        ParticleLibrarySettings settings{ std::clamp(input.read<std::uint32_t>(), 10u, 500u),
                                          input.read<std::uint32_t>() };
        if (input.position != bytes.size())
            throw InvalidLibrary{ input.position, "Trailing particle library data" };
        for (auto& effect : effects) {
            effect.runtime_state = 0;
            if (!effect.render_position_scale_permille)
                effect.render_position_scale_permille = 1000;
        }
        for (auto& attractor : attractors)
            attractor.runtime_state = 0;
        //6A3374 uses fixed2048, not the parsed but unconsumed TOTALPARTICLES
        //field.
        initialize_particle_runtime(runtime, 2048, profile.game.particle_density);
        runtime.templates = std::move(effects);
        runtime.attractor_templates = std::move(attractors);
        runtime.state.creation_filter_enabled = profile.profile_index == 0;
        return settings;
    } catch (const InvalidLibrary& error) {
        return std::unexpected(ApplicationConfigurationError{ error.offset, error.reason });
    }
}

void apply_world_graphics_profile(WorldRuntime& c,
                                  ParticleRuntime& particles,
                                  const LoadedSpriteCatalog& catalog,
                                  const ApplicationHardwareProfile& profile) {
    if (profile.profile_index > 2)
        throw std::out_of_range("Unknown native hardware profile");
    SpriteCatalogSettings limits;
    limits.num_kids = profile.game.num_kids;
    c.resources.resource_category_variants = catalog.variants;
    auto& appearance_counts = c.resources.variants.appearance_variant_counts;
    appearance_counts.resize(catalog.variants.size());
    for (std::size_t i = 0; i < sprite_category_names.size(); ++i) {
        auto& count = appearance_counts[i];
        count = catalog.variants[i].loaded_resource_count;
        const auto name = sprite_category_names[i];
        const bool kids = name == "kids" || name == "kidsheads";
        const bool staff = name == "handymen" || name == "mechanics" || name == "guards" || name == "researchers";
        if (kids || staff)
            count = std::min(count, kids ? kid_sprite_variant_limit(limits) : staff_sprite_variant_limit(limits));
    }
    //Existing live particle/effect handles remain owned. This is the original
    //creation gate and parameter scale for subsequently created emitters.
    particles.state.creation_filter_enabled = profile.profile_index == 0;
    particles.state.parameter_scale = profile.game.particle_density ? profile.game.particle_density : 1;
    //49FA20 directly sets these renderer globals; special-view entry derives
    //actual far clip and fog from them. Invalid selectors preserve prior state.
    match (profile.graphics.first_person_view_distance) {
        case 0 => {
            c.view.first_person_view_distance = 200;
            c.view.fog_distance_start = 50;
            c.view.fog_distance_end = 60;
        }
        case 1 => {
            c.view.first_person_view_distance = 325;
            c.view.fog_distance_start = 100;
            c.view.fog_distance_end = 110;
        }
        case 2 => {
            c.view.first_person_view_distance = 450;
            c.view.fog_distance_start = 135;
            c.view.fog_distance_end = 150;
        }
        case 3 => {
            c.view.first_person_view_distance = 700;
            c.view.fog_distance_start = 230;
            c.view.fog_distance_end = 250;
        }
        case _ => {}
    }
}
} //namespace simcoaster
