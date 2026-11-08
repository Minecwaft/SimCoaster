#pragma once
#include "application_configuration.hpp"

namespace simcoaster {
struct WorldRuntime;
struct ParticleRuntime;
struct LoadedSpriteCatalog;

struct ParticleLibrarySettings {
    std::uint32_t clamped_value {}, trailer_value {};
};

//Source6A3374/6580E0 loads the actual PLB before either fresh population or
//saved particle restoration. Parsing is bounded and commits only complete
//records; corrupt files never become successful empty effect libraries.
std::expected<ParticleLibrarySettings, ApplicationConfigurationError> initialize_world_profile_particles(ParticleRuntime&, std::span<const std::byte>, const ApplicationHardwareProfile&);
//Resource script handles and catalog-relative variant identities survive
//changes. Only future appearance selection uses the new native limits.
void apply_world_graphics_profile(WorldRuntime&, ParticleRuntime&, const LoadedSpriteCatalog&, const ApplicationHardwareProfile&);
} //namespace simcoaster
