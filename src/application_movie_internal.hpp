#pragma once
#include "simcoaster/application_movie.hpp"
#include <format>
#include <stdexcept>

namespace simcoaster::application_movie_detail {
[[noreturn]] inline void fail(std::string_view message, std::size_t offset) {
    throw std::runtime_error(std::format("Original movie: {} at byte {}", message, offset));
}

inline std::uint32_t little(std::span<const std::byte> bytes, std::size_t offset, std::size_t width = 4) {
    if (width > 4 || offset > bytes.size() || width > bytes.size() - offset) {
        fail("truncated field", offset);
    }
    std::uint32_t value {};
    for (std::size_t i = 0; i < width; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

TextureImagePtr decode_tqi_picture(std::span<const std::byte>);
std::vector<float> decode_ea_adpcm(std::span<const std::byte>, std::uint32_t);
void append_full_viewport(UiDrawList&, const TextureImagePtr&, std::array<float, 2> offset = {});
} //namespace simcoaster::application_movie_detail
