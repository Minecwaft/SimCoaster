#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace simcoaster::numeric {
//Game counters and script words wrap at their declared width. Keep that
//arithmetic explicit while runtime objects and addresses remain native x64.
constexpr std::uint32_t bits(std::int32_t value) {
    return std::bit_cast<std::uint32_t>(value);
}

constexpr std::int32_t word(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

constexpr std::int16_t low_short(std::int32_t value) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

constexpr std::int32_t narrow(std::int64_t value) {
    return word(static_cast<std::uint32_t>(value));
}

constexpr std::int32_t add(std::int32_t a, std::int32_t b) {
    return word(bits(a) + bits(b));
}

constexpr std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return word(bits(a) - bits(b));
}

constexpr std::int32_t product(std::int32_t a, std::int32_t b) {
    return word(bits(a) * bits(b));
}

constexpr std::int32_t negate(std::int32_t value) {
    return word(0u - bits(value));
}

constexpr std::int32_t absolute(std::int32_t value) {
    return value < 0 ? negate(value) : value;
}

constexpr std::int32_t quotient(std::int32_t numerator, std::int32_t denominator, const char* error) {
    if (!denominator || (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1)) {
        throw std::domain_error(error);
    }
    return numerator / denominator;
}
} //namespace simcoaster::numeric
