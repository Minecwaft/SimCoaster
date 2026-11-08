#pragma once
#include <cstdint>
#include <span>

namespace simcoaster {
inline constexpr std::size_t simulation_sine_sample_count = 4096;
inline constexpr float simulation_sine_samples_per_radian = 4096.0f * 0.15915494f;
std::span<const float, simulation_sine_sample_count> simulation_sine_samples();
float simulation_sine_at_index(std::int32_t);
//x87 FISTP lookup sites use nearest-even with the game's default rounding mode.
float simulation_sine_at_rounded_index(float);
} //namespace simcoaster
