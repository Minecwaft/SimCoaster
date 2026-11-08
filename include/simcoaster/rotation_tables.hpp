#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace simcoaster {
//The sprite and particle tables use different native angle approximations.
inline const std::array<float, 256>& sprite_rotation_sines() {
    static const auto table = [] {
        std::array<float, 256> values {};
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<float>(std::sin(static_cast<double>(i) * static_cast<double>(3.1415927f) * .0078125));
        }
        return values;
    }();
    return table;
}

inline const std::array<std::int32_t, 512>& particle_phase_sines() {
    static const auto table = [] {
        std::array<std::int32_t, 512> values {};
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<std::int32_t>(std::sin(static_cast<double>(i * 2) * .00613591796875) * 256.0);
        }
        return values;
    }();
    return table;
}
} //namespace simcoaster
