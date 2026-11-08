#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace simcoaster::application_movie_detail {
//Exact original Game.exe tables, extracted into assets for standalone builds: 863B30 uses 944300/944400 (PE file
//offsets5522176/5522432), 867260/867549 use C72598..C72AAC
//(file6342040 onward). These are data, never executable x86 instructions.
//Table bases deliberately overlap: the AC lookup indexes only its applicable
//prefix range; C725B0 contains reverse zigzag positions for the63 AC entries.
inline constexpr std::uint8_t transform_tables[] = {
#embed "../assets/movie_transform_tables.bin"
};
inline constexpr std::uint8_t quantization_tables[] = {
#embed "../assets/movie_quantization_tables.bin"
};
inline constexpr std::uint8_t audio_coefficients[] = {
#embed "../assets/movie_audio_coefficients.bin"
};

template <std::size_t N>
constexpr std::uint32_t table_word(const std::uint8_t (&bytes)[N], std::size_t offset) {
    if (offset > N || 4 > N - offset) {
        throw "Original movie table offset";
    }
    return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

constexpr std::int32_t transform_integer(std::size_t offset) {
    return std::bit_cast<std::int32_t>(table_word(transform_tables, offset));
}

constexpr float transform_float(std::size_t offset) {
    return std::bit_cast<float>(table_word(transform_tables, offset));
}

static_assert(table_word(quantization_tables, 0) == 8192);
static_assert(transform_integer(24) == 63);
static_assert(table_word(audio_coefficients, 4) == 240);
} //namespace simcoaster::application_movie_detail
