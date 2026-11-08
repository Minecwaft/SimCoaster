#include "application_movie_internal.hpp"
#include "application_movie_tables.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster::application_movie_detail {
namespace {
class PictureBits {
    std::span<const std::byte> bytes_;
    std::size_t position_{};

public:
    explicit PictureBits(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::size_t position() const {
        return position_;
    }

    //Original867260 reads little-endian DWORDs, from each word's MSB down.
    //Lookahead is zero padded, but consuming a field must stay in the packet.
    std::uint32_t peek(unsigned count) const {
        std::uint32_t result{};
        for (unsigned i = 0; i < count; ++i) {
            const auto bit = position_ + i;
            const auto byte = (bit / 32) * 4 + 3 - (bit % 32) / 8;
            result <<= 1;
            if (byte < bytes_.size())
                result |= (std::to_integer<unsigned>(bytes_[byte]) >> (7 - bit % 8)) & 1u;
        }
        return result;
    }

    std::uint32_t take(unsigned count) {
        if (count > 32 || position_ > bytes_.size() * 8 || count > bytes_.size() * 8 - position_)
            fail("truncated TQI coefficient", 8 + position_ / 8);
        const auto result = peek(count);
        position_ += count;
        return result;
    }
};

using Block = std::array<std::int32_t, 64>;

std::int32_t checked(std::int64_t value, std::size_t offset) {
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
        fail("TQI coefficient overflow", offset);
    return static_cast<std::int32_t>(value);
}

//Native8691CC/8693F5 use the same eight-point butterfly. Replacing their
//x87 magic-double integer conversion with explicit ties-to-even rounding
//retains the algorithm without carrying the original floating-point ABI.
std::array<std::int32_t, 8> inverse_row(const std::array<std::int32_t, 8>& a) {
    const auto multiply = [](std::int64_t value) {
        return ((value * transform_integer(0)) >> 32) * 2;
    };
    const auto round = [](double value) {
        return static_cast<std::int64_t>(std::nearbyint(value));
    };
    const std::int64_t pair35 = std::int64_t(a[3]) + a[5];
    const std::int64_t total = pair35 + a[7] + a[1];
    const double difference17 = double(a[1]) - a[7];
    const double difference53 = double(a[5]) - a[3];
    const double mixed = (difference17 + difference53) * transform_float(12);
    const auto odd3 = round(difference17 * transform_float(8) - mixed);
    const auto odd5 = round(difference53 * transform_float(4) + mixed);
    const auto odd_rotation = multiply(std::int64_t(a[7]) + a[1] - pair35);
    const auto even_sum = std::int64_t(a[2]) + a[6];
    const auto even_rotation = multiply(std::int64_t(a[2]) - a[6]);
    const auto dc_plus = std::int64_t(a[0]) + a[4];
    const auto dc_minus = std::int64_t(a[0]) - a[4];
    const auto e0 = dc_plus + even_sum + even_rotation;
    const auto e1 = dc_minus + even_rotation;
    const auto e2 = dc_minus - even_rotation;
    const auto e3 = dc_plus - even_sum - even_rotation;
    const std::array<std::int64_t, 8> values{
        e0 + total + odd3, e1 + odd_rotation + odd3, e2 + odd_rotation + odd5, e3 + odd5,
        e3 - odd5,         e2 - odd_rotation - odd5, e1 - odd_rotation - odd3, e0 - total - odd3
    };
    std::array<std::int32_t, 8> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = checked(values[i], 0);
    return result;
}

void inverse_transform(Block& block) {
    Block transposed{};
    for (std::size_t row = 0; row < 8; ++row) {
        std::array<std::int32_t, 8> input{};
        std::copy_n(block.begin() + row * 8, 8, input.begin());
        const auto output = inverse_row(input);
        for (std::size_t column = 0; column < 8; ++column)
            transposed[column * 8 + row] = output[column];
    }
    for (std::size_t row = 0; row < 8; ++row) {
        std::array<std::int32_t, 8> input{};
        std::copy_n(transposed.begin() + row * 8, 8, input.begin());
        const auto output = inverse_row(input);
        for (std::size_t column = 0; column < 8; ++column)
            block[row * 8 + column] = output[column] >> 17;
    }
}

Block quantization(unsigned quality) {
    Block result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto scaling = table_word(quantization_tables, i * 4);
        if (quality == 100)
            result[i] = static_cast<std::int32_t>(scaling * 8);
        else {
            const float factor = static_cast<float>((107.0 - quality) * .0625);
            const auto quantizer =
                static_cast<std::int32_t>(double(table_word(quantization_tables, 256 + i * 4)) * factor * 65536.0);
            result[i] = checked((std::int64_t(quantizer) * scaling + 32768) >> 16, 0);
        }
    }
    result[0] = 65536;
    return result;
}

std::uint32_t ac_descriptor(std::uint32_t prefix) {
    std::size_t base{}, index{};
    if (prefix >= 32768) {
        base = 260;
        index = prefix >> 13;
    } else if (prefix >= 2048) {
        base = 308;
        index = prefix >> 9;
    } else if (prefix >= 1024) {
        base = 532;
        index = prefix >> 7;
    } else if (prefix >= 512) {
        base = 532;
        index = prefix >> 5;
    } else if (prefix >= 256) {
        base = 596;
        index = prefix >> 4;
    } else if (prefix >= 128) {
        base = 660;
        index = prefix >> 3;
    } else if (prefix >= 64) {
        base = 724;
        index = prefix >> 2;
    } else if (prefix >= 32) {
        base = 788;
        index = prefix >> 1;
    } else
        fail("invalid TQI AC prefix", 0);
    return table_word(transform_tables, base + index * 4);
}

Block decode_block(PictureBits& bits, const Block& weights, std::int32_t& dc_predictor, bool chroma) {
    const auto dc = transform_tables[(chroma ? 1044 : 916) + bits.peek(chroma ? 8 : 7)];
    const unsigned length = dc & 15, magnitude = dc >> 4;
    if (!length || magnitude > 11)
        fail("invalid TQI DC prefix", bits.position() / 8);
    bits.take(length);
    std::int32_t difference{};
    if (magnitude) {
        const auto value = bits.take(magnitude);
        difference = static_cast<std::int32_t>(value);
        if (!(value & (1u << (magnitude - 1))))
            difference -= static_cast<std::int32_t>((1u << magnitude) - 1);
    }
    dc_predictor = checked(std::int64_t(dc_predictor) + difference, bits.position() / 8);
    Block block{};
    block[0] = checked(std::int64_t(dc_predictor) * weights[0], bits.position() / 8);
    int position = 63;
    while (true) {
        const auto descriptor = ac_descriptor(bits.peek(17));
        const unsigned code_length = descriptor >> 16;
        const unsigned run = (descriptor >> 8) & 255;
        if (!code_length || code_length > 16)
            fail("invalid TQI AC descriptor", bits.position() / 8);
        bits.take(code_length);
        if (run == 65)
            break;
        std::int32_t coefficient{};
        if (run > 65) {
            const auto escaped = bits.take(14);
            position -= static_cast<int>(escaped >> 8) + 1;
            const auto level = escaped & 255;
            if (level == 0)
                coefficient = static_cast<std::int32_t>(bits.take(8));
            else if (level == 128)
                coefficient = static_cast<std::int32_t>(bits.take(8)) - 256;
            else
                coefficient = level > 128 ? static_cast<std::int32_t>(level) - 256 : static_cast<std::int32_t>(level);
        } else {
            position -= static_cast<int>(run);
            coefficient = static_cast<std::int32_t>(descriptor & 255);
            if (bits.take(1))
                coefficient = -coefficient;
        }
        if (position < 0 || position >= 63)
            fail("TQI AC run exceeds block", bits.position() / 8);
        const auto index = table_word(transform_tables, 24 + std::size_t(position) * 4);
        if (!index || index >= block.size())
            fail("invalid original reverse zigzag", 0);
        block[index] = checked(std::int64_t(coefficient) * weights[index], bits.position() / 8);
    }
    inverse_transform(block);
    return block;
}

//862DC0 builds packed Y/Cb/Cr contributions. Preserve the original clamping,
//half-step rounding and channel lookup rather than guessing a JPEG matrix.
struct ColorTables {
    std::array<std::uint32_t, 512> y{}, cb{}, cr{};

    ColorTables() {
        for (int value = -256; value < 256; ++value) {
            const auto clamped = std::clamp(value, -64, 63);
            const auto index = static_cast<unsigned>(value - 192) & 511;
            const auto c = double(clamped) + .5;
            const auto field = [](double x) {
                return static_cast<std::uint32_t>(static_cast<std::int64_t>(x)) & 511;
            };
            y[index] = static_cast<std::uint32_t>(8390657ll * clamped + 1216600233);
            cb[index] = field(.5 + c * 1.772) + (field(.5 - c * .3441) << 11);
            cr[index] = ((field(.5 + c * 1.402) << 12) + field(.5 - c * .7141)) << 11;
        }
    }

    RgbaPixel pixel(int luma, int blue, int red) const {
        const auto index = [](int value) {
            if (value < -256 || value > 255)
                fail("TQI transformed sample outside lookup table", 0);
            return static_cast<std::size_t>(value + 256);
        };
        const auto packed = y[index(luma)] + cb[index(blue)] + cr[index(red)];
        const auto clamp = [](int value) {
            return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        };
        return { clamp(-177 + 2 * static_cast<int>(packed >> 23)),
                 clamp(-133 + 2 * static_cast<int>((packed & 0xFF800) >> 11)),
                 clamp(-225 + 2 * static_cast<int>(packed & 511)),
                 255 };
    }
};
} //namespace

TextureImagePtr decode_tqi_picture(std::span<const std::byte> packet) {
    const auto width = little(packet, 0, 2), height = little(packet, 2, 2);
    const auto quality = little(packet, 4, 1), flags = little(packet, 7, 1);
    if (!width || !height || width > 4096 || height > 4096 || quality > 100)
        fail("invalid TQI dimensions/quality", 0);
    if (packet.size() < 8 || (packet.size() - 8) % 4)
        fail("unaligned/truncated TQI word stream", 8);
    //Bit1 selects native867E1B full-color vs8681F7 reduced-color macroblocks.
    //Both installed original movies use full-color. Reject unimplemented data
    //explicitly instead of interpreting reduced-color packets as full pictures.
    if (!(flags & 2))
        fail("unsupported reduced-color TQI picture", 7);
    const auto weights = quantization(quality);
    PictureBits bits(packet.subspan(8));
    std::array<std::int32_t, 3> predictors{};
    auto result = std::make_shared<TextureImage>();
    result->width = width;
    result->height = height;
    result->alpha = TextureAlpha::opaque;
    result->pixels.resize(std::size_t(width) * height);
    static const ColorTables colors;
    for (std::uint32_t y = 0; y < height; y += 16)
        for (std::uint32_t x = 0; x < width; x += 16) {
            std::array<Block, 6> blocks{};
            for (std::size_t i = 0; i < blocks.size(); ++i)
                blocks[i] = decode_block(bits, weights, predictors[i < 4 ? 0 : i - 3], i >= 4);
            for (unsigned dy = 0; dy < 16 && y + dy < height; ++dy)
                for (unsigned dx = 0; dx < 16 && x + dx < width; ++dx) {
                    const auto quadrant = (dy / 8) * 2 + dx / 8;
                    const auto luma = blocks[quadrant][(dy % 8) * 8 + dx % 8];
                    const auto chroma = (dy / 2) * 8 + dx / 2;
                    result->pixels[std::size_t(y + dy) * width + x + dx] =
                        colors.pixel(luma, blocks[4][chroma], blocks[5][chroma]);
                }
        }
    return result;
}

std::vector<float> decode_ea_adpcm(std::span<const std::byte> packet, std::uint32_t channels) {
    const auto frames = little(packet, 0);
    if (channels != 2 || packet.size() < 12 || frames > 1048576)
        fail("unsupported EA ADPCM block", 0);
    const auto groups = (std::size_t(frames) + 27) / 28;
    if (groups > (packet.size() - 12) / 30)
        fail("truncated EA ADPCM groups", 12);
    std::array<std::int32_t, 2> current{ static_cast<std::int16_t>(little(packet, 4, 2)),
                                         static_cast<std::int16_t>(little(packet, 8, 2)) };
    std::array<std::int32_t, 2> previous{ static_cast<std::int16_t>(little(packet, 6, 2)),
                                          static_cast<std::int16_t>(little(packet, 10, 2)) };
    std::vector<float> result;
    result.reserve(std::size_t(frames) * 2);
    for (std::size_t group = 0; group < groups; ++group) {
        const auto offset = 12 + group * 30;
        const auto predictors = little(packet, offset, 1);
        const auto shifts = little(packet, offset + 1, 1);
        const std::array<unsigned, 2> predictor{ predictors >> 4, predictors & 15 };
        const std::array<unsigned, 2> shift{ (shifts >> 4) + 8, (shifts & 15) + 8 };
        if (predictor[0] >= 4 || predictor[1] >= 4)
            fail("invalid EA ADPCM predictor", offset);
        for (unsigned sample = 0; sample < 28; ++sample) {
            const auto packed = little(packet, offset + 2 + sample, 1);
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto nibble = channel ? packed & 15 : packed >> 4;
                const auto signed_nibble = static_cast<int>(nibble) - (nibble & 8 ? 16 : 0);
                const auto residual = (std::int64_t(signed_nibble) * 268435456) >> shift[channel];
                const auto first = std::bit_cast<std::int32_t>(table_word(audio_coefficients, predictor[channel] * 4));
                const auto second =
                    std::bit_cast<std::int32_t>(table_word(audio_coefficients, 16 + predictor[channel] * 4));
                const auto value = std::clamp<std::int64_t>(
                    (first * std::int64_t(current[channel]) + residual + second * std::int64_t(previous[channel]) + 128)
                        >> 8,
                    -32768,
                    32767);
                previous[channel] = current[channel];
                current[channel] = static_cast<std::int32_t>(value);
                //The final original block declares872 frames but stores896. Decode
                //padded groups safely, and expose only the packet's declared samples.
                if (group * 28 + sample < frames)
                    result.push_back(static_cast<float>(value) / 32768.0f);
            }
        }
    }
    return result;
}
} //namespace simcoaster::application_movie_detail
