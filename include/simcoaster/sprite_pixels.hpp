#pragma once
#include "sprite_catalog.hpp"

namespace simcoaster {
enum class SpritePixelFailure {
    short_input,
    unsupported_format,
    frame_count,
    dimensions,
    run_overflow,
    output_bounds
};

struct SpritePixelError {
    SpritePixelFailure reason;
    std::size_t offset;
};

struct EncodedSpriteFrame {
    std::uint16_t width{}, height{}, reference_width{}, reference_height{};
    std::int16_t origin_x{}, origin_y{};
    float aspect_ratio{};
    std::span<const std::byte> encoded_pixels;
};

struct SpritePixelPayload {
    std::uint16_t format{}, header_word{};
    std::array<std::uint32_t, 256> palette{};
    std::vector<EncodedSpriteFrame> frames;
    std::size_t bytes_consumed{};
};

//Frame byte spans borrow the supplied immutable asset; keep it alive.
std::expected<SpritePixelPayload, SpritePixelError> parse_sprite_pixel_payload(std::span<const std::byte>);

struct SpritePixelBuffer {
    std::span<std::uint32_t> pixels;
    std::uint32_t width{}, height{}, pitch_pixels{};
};

struct SpritePixelDecodeResult {
    std::size_t input_consumed{}, rows_written{};
};

//Original palette/run decoder in a native 32-bit-color buffer. Dynamic row
//storage replaces its fixed scratch array; destination writes are bounded.
std::expected<SpritePixelDecodeResult, SpritePixelError>
decode_sprite_pixel_frame(const EncodedSpriteFrame&, std::span<const std::uint32_t, 256> palette, SpritePixelBuffer);

struct SpriteImage {
    std::uint32_t width{}, height{};
    std::vector<std::uint32_t> pixels;
};

std::expected<SpriteImage, SpritePixelError> decode_sprite_image(const SpritePixelPayload&, std::size_t frame);

struct SpritePixelCatalog {
    const LoadedSpriteCatalog* catalog{};
    std::vector<std::optional<SpritePixelPayload>> payloads;
    const SpritePixelPayload& payload(std::size_t resource);
    void bind(const LoadedSpriteCatalog&);
};
} //namespace simcoaster
