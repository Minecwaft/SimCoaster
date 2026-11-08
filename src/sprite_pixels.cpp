#include "simcoaster/sprite_pixels.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
struct ParseFailure {
    SpritePixelError error;
};

struct Cursor {
    std::span<const std::byte> bytes;
    std::size_t offset{};

    std::span<const std::byte> take(std::size_t size) {
        if (size > bytes.size() - offset)
            throw ParseFailure{ { SpritePixelFailure::short_input, offset } };
        const auto result = bytes.subspan(offset, size);
        offset += size;
        return result;
    }

    template <class T>
    T read() {
        const auto data = take(save::value_width<T>);
        T value{};
        save::detail::decode(std::span<const std::byte, save::value_width<T>>{ data.data(), data.size() }, value);
        return value;
    }
};
} //namespace

std::expected<SpritePixelPayload, SpritePixelError> parse_sprite_pixel_payload(std::span<const std::byte> bytes) {
    try {
        Cursor input{ bytes };
        SpritePixelPayload result;
        result.format = input.read<std::uint16_t>();
        result.header_word = input.read<std::uint16_t>();
        const auto count = input.read<std::uint32_t>();
        if (result.format != 3)
            return std::unexpected(SpritePixelError{ SpritePixelFailure::unsupported_format, 0 });
        for (auto& color : result.palette)
            color = input.read<std::uint32_t>();
        if (count > (bytes.size() - input.offset) / 20u)
            return std::unexpected(SpritePixelError{ SpritePixelFailure::frame_count, input.offset });
        result.frames.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto encoded_size = input.read<std::uint32_t>();
            EncodedSpriteFrame frame;
            frame.width = input.read<std::uint16_t>();
            frame.height = input.read<std::uint16_t>();
            frame.reference_width = input.read<std::uint16_t>();
            frame.reference_height = input.read<std::uint16_t>();
            frame.origin_x = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(input.read<std::uint32_t>()));
            frame.origin_y = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(input.read<std::uint32_t>()));
            frame.aspect_ratio =
                frame.height ? static_cast<float>(static_cast<double>(frame.width) / frame.height) : 0.f;
            frame.encoded_pixels = input.take(encoded_size);
            result.frames.push_back(frame);
        }
        result.bytes_consumed = input.offset;
        return result;
    } catch (const ParseFailure& failure) {
        return std::unexpected(failure.error);
    }
}

std::expected<SpritePixelDecodeResult, SpritePixelError>
decode_sprite_pixel_frame(const EncodedSpriteFrame& frame,
                          std::span<const std::uint32_t, 256> palette,
                          SpritePixelBuffer output) {
    if (!output.width || !output.height)
        return std::unexpected(SpritePixelError{ SpritePixelFailure::dimensions, 0 });
    std::ptrdiff_t input = 0;
    std::size_t destination = 0, rows_written = 0;
    const auto fail = [&](SpritePixelFailure reason) {
        return std::unexpected(
            SpritePixelError{ reason, static_cast<std::size_t>(std::max<std::ptrdiff_t>(input, 0)) });
    };
    const auto byte = [&]() -> std::uint8_t {
        if (input < 0 || static_cast<std::size_t>(input) >= frame.encoded_pixels.size())
            throw ParseFailure{ { SpritePixelFailure::short_input,
                                  static_cast<std::size_t>(std::max<std::ptrdiff_t>(input, 0)) } };
        return std::to_integer<std::uint8_t>(frame.encoded_pixels[static_cast<std::size_t>(input++)]);
    };
    const auto row_fits = [&] {
        return destination <= output.pixels.size() && output.width <= output.pixels.size() - destination;
    };
    const auto step_x = (std::uint64_t{ frame.width } << 16) / output.width,
               step_y = (std::uint64_t{ frame.height } << 16) / output.height;
    std::uint64_t phase = 0;
    unsigned action = 1;
    std::vector<std::uint32_t> row(frame.width);
    try {
        for (std::uint32_t source_row = 0; source_row < frame.height; ++source_row) {
            if (action == 0) {
                const auto skip = std::bit_cast<std::int8_t>(byte());
                input += skip;
                if (input < 0 || static_cast<std::size_t>(input) > frame.encoded_pixels.size())
                    return fail(SpritePixelFailure::short_input);
            } else if (action == 2) {
                if (!row_fits() || destination < output.pitch_pixels)
                    return fail(SpritePixelFailure::output_bounds);
                for (std::uint32_t x = 0; x < output.width; ++x)
                    output.pixels[destination + x] = output.pixels[destination - output.pitch_pixels + x];
                destination += output.pitch_pixels;
                ++rows_written;
                phase += step_y;
            } else {
                (void)byte();
                std::size_t column = 0;
                while (column < frame.width) {
                    const auto token = std::bit_cast<std::int8_t>(byte());
                    const auto count = static_cast<std::size_t>(token > 0 ? token : -int(token));
                    if (count > frame.width - column)
                        return fail(SpritePixelFailure::run_overflow);
                    if (token <= 0) {
                        const auto color = palette[byte()];
                        std::fill_n(row.begin() + static_cast<std::ptrdiff_t>(column), count, color);
                    } else
                        for (std::size_t i = 0; i < count; ++i)
                            row[column + i] = palette[byte()];
                    column += count;
                }
                if (!row_fits())
                    return fail(SpritePixelFailure::output_bounds);
                std::uint64_t x = 0;
                for (std::uint32_t column_out = 0; column_out < output.width; ++column_out, x += step_x) {
                    const auto source_column = static_cast<std::size_t>(x >> 16);
                    if (source_column >= row.size())
                        return fail(SpritePixelFailure::dimensions);
                    output.pixels[destination + column_out] = row[source_column];
                }
                destination += output.pitch_pixels ? output.pitch_pixels : output.width;
                ++rows_written;
                phase += step_y;
            }
            if (phase < 131072u) {
                action = 2u - static_cast<unsigned>((phase & 65536u) != 0);
                phase = static_cast<std::uint16_t>(phase);
            } else {
                action = 0;
                phase -= 65536u;
            }
        }
        return SpritePixelDecodeResult{ static_cast<std::size_t>(input), rows_written };
    } catch (const ParseFailure& failure) {
        return std::unexpected(failure.error);
    }
}

std::expected<SpriteImage, SpritePixelError> decode_sprite_image(const SpritePixelPayload& payload, std::size_t index) {
    if (index >= payload.frames.size())
        return std::unexpected(SpritePixelError{ SpritePixelFailure::frame_count, index });
    const auto& frame = payload.frames[index];
    SpriteImage image{ frame.width, frame.height, {} };
    image.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);
    auto decoded =
        decode_sprite_pixel_frame(frame, payload.palette, { image.pixels, frame.width, frame.height, frame.width });
    if (!decoded)
        return std::unexpected(decoded.error());
    return image;
}

void SpritePixelCatalog::bind(const LoadedSpriteCatalog& assets) {
    catalog = &assets;
    payloads.clear();
    payloads.resize(assets.resources.size());
}

const SpritePixelPayload& SpritePixelCatalog::payload(std::size_t resource) {
    if (!catalog)
        throw std::logic_error("sprite pixel catalog is not bound");
    auto& cached = payloads.at(resource);
    if (!cached) {
        auto parsed = parse_sprite_pixel_payload(catalog->resources.at(resource).pixel_payload);
        if (!parsed)
            throw std::runtime_error(catalog->resources.at(resource).pixel_path
                                     + ": invalid sprite pixels at "
                                     + std::to_string(parsed.error().offset));
        cached = std::move(*parsed);
    }
    return *cached;
}
} //namespace simcoaster
