#include "simcoaster/simulation_sine.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace simcoaster {
std::span<const float, simulation_sine_sample_count> simulation_sine_samples() {
    static const auto samples = [] {
        std::array<float, simulation_sine_sample_count> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] =
                static_cast<float>(std::sin(static_cast<double>(i) / result.size() * static_cast<double>(6.2831855f)));
        return result;
    }();
    return samples;
}

float simulation_sine_at_index(std::int32_t index) {
    return simulation_sine_samples()[static_cast<std::uint32_t>(index) & (simulation_sine_sample_count - 1)];
}

float simulation_sine_at_rounded_index(float index) {
    const auto lower = std::floor(static_cast<double>(index)), fraction = static_cast<double>(index) - lower;
    const auto nearest = lower + ((fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) != 0)) ? 1.0 : 0.0);
    if (!std::isfinite(nearest)
        || nearest < std::numeric_limits<std::int32_t>::min()
        || nearest > std::numeric_limits<std::int32_t>::max())
        return simulation_sine_at_index(0);
    return simulation_sine_at_index(static_cast<std::int32_t>(nearest));
}
} //namespace simcoaster
