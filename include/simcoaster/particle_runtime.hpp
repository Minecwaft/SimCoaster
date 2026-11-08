#pragma once
#include "particle_save.hpp"
#include <functional>

namespace simcoaster {
void initialize_particle_runtime(ParticleRuntime&, std::int32_t capacity, std::int32_t parameter_scale);
void reset_particle_runtime_pools(ParticleRuntime&);
//Native handle identity survives save restoration: low WORD slot, high WORD
//generation. Like the original setters, lookup does not test runtime_state.
ParticleEffect* find_particle_effect(ParticleRuntime&, std::uint32_t handle);
int resize_control_particle_group(ParticleRuntime&, int count, std::uint32_t definition);
int clear_control_particle_group(ParticleRuntime&);
std::uint32_t create_particle_effect(ParticleRuntime&,
                                     std::uint32_t definition,
                                     const std::array<std::int32_t, 3>& position,
                                     std::int32_t lifetime = 0);
std::uint32_t create_particle_effect_with_direction(ParticleRuntime&,
                                                    std::uint32_t definition,
                                                    const std::array<std::int32_t, 3>& position,
                                                    const std::array<std::int32_t, 3>& direction);
using ParticleHeightQuery = std::function_ref<std::int32_t(std::int32_t, std::int32_t)>;
std::int32_t default_particle_surface_height(std::int32_t, std::int32_t);
//One original simulation substep, independent of render elapsed time. Gameplay
//installs the zero-height callback; external coordinates have 1024 units/m.
void update_particle_system(ParticleRuntime&, ParticleHeightQuery = default_particle_surface_height);
} //namespace simcoaster
