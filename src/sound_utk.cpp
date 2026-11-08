#include "simcoaster/sound_utk.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
//Tables read from Game.exe9FC388/9FC488/9FC688. The lookup byte selects
//one12-byte command; command bit length is not a fixed Huffman depth.
constexpr std::array<float, 64> reflection_table{
    0.0f,
    -0.9967759847640991f,
    -0.990327000617981f,
    -0.983879029750824f,
    -0.9774309992790222f,
    -0.970982015132904f,
    -0.9645339846611023f,
    -0.9580850005149841f,
    -0.9516370296478271f,
    -0.9307540059089661f,
    -0.9049599766731262f,
    -0.8791670203208923f,
    -0.8533729910850525f,
    -0.8275790214538574f,
    -0.8017860054969788f,
    -0.7759919762611389f,
    -0.7501980066299438f,
    -0.7244049906730652f,
    -0.6986110210418701f,
    -0.6706349849700928f,
    -0.619047999382019f,
    -0.5674600005149841f,
    -0.5158730149269104f,
    -0.4642859995365143f,
    -0.41269800066947937f,
    -0.36111098527908325f,
    -0.3095239996910095f,
    -0.2579370141029358f,
    -0.20634900033473969f,
    -0.15476199984550476f,
    -0.10317499935626984f,
    -0.051587000489234924f,
    0.0f,
    0.051587000489234924f,
    0.10317499935626984f,
    0.15476199984550476f,
    0.20634900033473969f,
    0.2579370141029358f,
    0.3095239996910095f,
    0.36111098527908325f,
    0.41269800066947937f,
    0.4642859995365143f,
    0.5158730149269104f,
    0.5674600005149841f,
    0.619047999382019f,
    0.6706349849700928f,
    0.6986110210418701f,
    0.7244049906730652f,
    0.7501980066299438f,
    0.7759919762611389f,
    0.8017860054969788f,
    0.8275790214538574f,
    0.8533729910850525f,
    0.8791670203208923f,
    0.9049599766731262f,
    0.9307540059089661f,
    0.9516370296478271f,
    0.9580850005149841f,
    0.9645339846611023f,
    0.970982015132904f,
    0.9774309992790222f,
    0.983879029750824f,
    0.990327000617981f,
    0.9967759847640991f,
};
constexpr std::array<std::array<std::uint8_t, 256>, 2> pulse_lookup{ {
    {
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 21,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 25,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 22,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 0,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 21,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 26,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 22,
        4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 2,
    },
    {
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 27,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 1,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 28,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 3,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 27,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 1,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 28,
        4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 3,
    },
} };

struct PulseCommand {
    unsigned next_model, bits;
    float value;
};

constexpr std::array<PulseCommand, 29> pulse_commands{ {
    { 1, 8, 0.0f }, { 1, 7, 0.0f },  { 0, 8, 0.0f }, { 0, 7, 0.0f },  { 0, 2, 0.0f }, { 0, 2, -1.0f },
    { 0, 2, 1.0f }, { 0, 3, -1.0f }, { 0, 3, 1.0f }, { 1, 4, -2.0f }, { 1, 4, 2.0f }, { 1, 3, -2.0f },
    { 1, 3, 2.0f }, { 1, 5, -3.0f }, { 1, 5, 3.0f }, { 1, 4, -3.0f }, { 1, 4, 3.0f }, { 1, 6, -4.0f },
    { 1, 6, 4.0f }, { 1, 5, -4.0f }, { 1, 5, 4.0f }, { 1, 7, -5.0f }, { 1, 7, 5.0f }, { 1, 6, -5.0f },
    { 1, 6, 5.0f }, { 1, 8, -6.0f }, { 1, 8, 6.0f }, { 1, 7, -6.0f }, { 1, 7, 6.0f },
} };

struct EndOfInput {
    std::size_t offset;
};

class Bits {
    std::span<const std::byte> source_;
    std::uint32_t cache_{};
    unsigned cached_{};
    std::size_t cursor_{};

public:
    explicit Bits(std::span<const std::byte> source) : source_(source) {
        if (source.empty())
            throw EndOfInput{ 0 };
        cache_ = std::to_integer<std::uint8_t>(source[0]);
        cached_ = 8;
        cursor_ = 1;
    }

    std::uint32_t peek() const {
        return cache_;
    }

    std::uint32_t read(unsigned count) {
        //Native8BDD20/8BDDE0 maintain8..15 lookahead bits after each read.
        //Refill exactly one byte, even when the consumed bits were buffered.
        const auto result = cache_ & ((1u << count) - 1u);
        cache_ >>= count;
        cached_ -= count;
        if (cached_ < 8) {
            if (cursor_ == source_.size())
                throw EndOfInput{ cursor_ };
            cache_ |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source_[cursor_++])) << cached_;
            cached_ += 8;
        }
        return result;
    }

    bool at_end() const {
        return cursor_ == source_.size();
    }

    std::size_t cursor() const {
        return cursor_;
    }
};

struct Channel {
    unsigned multipulse_threshold{};
    std::array<float, 64> gains{};
    std::array<float, 12> reflection{}, synthesis{};
    std::array<float, 756> excitation{};

    //Native adaptive history permits indices down to-39. Those addresses
    //overlap preceding channel fields; retain the observed values explicitly,
    //without forming an out-of-bounds pointer or copying a32-bit object ABI.
    float adaptive(std::ptrdiff_t index) const {
        if (index >= 0)
            return excitation[static_cast<std::size_t>(index)];
        if (index >= -12)
            return synthesis[static_cast<std::size_t>(12 + index)];
        if (index >= -24)
            return reflection[static_cast<std::size_t>(24 + index)];
        return gains[static_cast<std::size_t>(88 + index)];
    }
};

void initialize(Channel& channel, unsigned threshold, float gain, float multiplier) {
    channel.multipulse_threshold = threshold;
    channel.gains[0] = gain;
    for (std::size_t i = 1; i < channel.gains.size(); ++i)
        channel.gains[i] = channel.gains[i - 1] * multiplier;
}

void excitation(Bits& bits, bool multipulse, float* output, unsigned stride) {
    if (!multipulse) {
        for (unsigned i = 0; i < 108; i += stride) {
            const auto code = bits.peek() & 3u;
            if (!(code & 1u)) {
                output[i] = 0.0f;
                (void)bits.read(1);
            } else {
                output[i] = code == 1 ? -2.0f : 2.0f;
                (void)bits.read(2);
            }
        }
        return;
    }
    unsigned model{}, index{};
    while (index < 108) {
        const auto opcode = pulse_lookup[model][bits.peek() & 255u];
        const auto& command = pulse_commands[opcode];
        (void)bits.read(command.bits);
        model = command.next_model;
        if (opcode > 3) {
            output[index] = command.value;
            index += stride;
        } else if (opcode < 2) {
            unsigned magnitude = 7;
            while (bits.read(1))
                ++magnitude;
            const auto sign = bits.read(1);
            const auto value = static_cast<float>(magnitude);
            output[index] = sign ? value : -value;
            index += stride;
        } else {
            auto count = bits.read(6) + 7u;
            count = std::min(count, (108u - index) / stride);
            while (count--) {
                output[index] = 0.0f;
                index += stride;
            }
        }
    }
}

std::array<float, 12> lpc_coefficients(const std::array<float, 12>& reflection) {
    //8BF000 has two stores missing in its decompilation; native instructions
    //at8BF039/8BF082 establish lattice[0]=1 and lattice[0]=accumulator.
    std::array<float, 12> lattice{}, response{}, lpc{};
    lattice[0] = 1.0f;
    for (std::size_t i = 1; i < lattice.size(); ++i)
        lattice[i] = reflection[i - 1];
    for (std::size_t i = 0; i < lpc.size(); ++i) {
        double accumulator = -static_cast<double>(lattice[11]) * reflection[11];
        for (std::size_t j = 11; j-- > 0;) {
            const auto old = lattice[j];
            accumulator -= static_cast<double>(old) * reflection[j];
            lattice[j + 1] = static_cast<float>(accumulator * reflection[j] + old);
        }
        lattice[0] = static_cast<float>(accumulator);
        response[i] = static_cast<float>(accumulator);
        for (std::size_t j = 0; j < i; ++j)
            accumulator -= static_cast<double>(response[i - 1 - j]) * lpc[j];
        lpc[i] = static_cast<float>(accumulator);
    }
    return lpc;
}

void synthesize(Channel& channel, std::size_t offset, std::size_t groups) {
    const auto lpc = lpc_coefficients(channel.reflection);
    //This order folds the native unrolled feedback into a12-tap recurrence.
    //Each emitted FLOAT updates history before the next output sample.
    constexpr std::array<std::size_t, 12> order{ 7, 8, 5, 11, 9, 10, 1, 2, 0, 6, 3, 4 };
    for (std::size_t group = 0; group < groups; ++group)
        for (std::size_t j = 0; j < 12; ++j) {
            double value = static_cast<double>(lpc[(order[0] + j) % 12]) * channel.synthesis[order[0]];
            for (std::size_t k = 1; k < order.size(); ++k)
                value += static_cast<double>(lpc[(order[k] + j) % 12]) * channel.synthesis[order[k]];
            const auto index = 324 + offset + group * 12 + j;
            value += channel.excitation[index];
            const auto sample = static_cast<float>(value);
            channel.synthesis[11 - j] = sample;
            channel.excitation[index] = sample;
        }
}

void decode_channel(Bits& bits, Channel& channel, bool reduced_bandwidth) {
    std::array<float, 12> delta{};
    const auto first = bits.read(6);
    const bool multipulse = first < channel.multipulse_threshold;
    delta[0] = static_cast<float>((static_cast<double>(reflection_table[first]) - channel.reflection[0]) * 0.25);
    for (std::size_t i = 1; i < 12; ++i) {
        const auto index = i < 4 ? bits.read(6) : bits.read(5) + 16u;
        delta[i] = static_cast<float>((static_cast<double>(reflection_table[index]) - channel.reflection[i]) * 0.25);
    }
    for (std::size_t block = 0; block < 4; ++block) {
        const auto origin = 216 + static_cast<std::ptrdiff_t>(108 * block);
        const auto adaptive_index = origin - static_cast<std::ptrdiff_t>(bits.read(8));
        const auto adaptive_gain = static_cast<float>(static_cast<double>(bits.read(4)) * 0.06666667014360428);
        auto fixed_gain = channel.gains[bits.read(6)];
        std::array<float, 118> pulses{};
        auto* samples = pulses.data() + 5;
        if (reduced_bandwidth) {
            const auto phase = bits.read(1);
            const bool zero_other = bits.read(1) != 0;
            excitation(bits, multipulse, samples + phase, 2);
            if (!zero_other) {
                for (std::size_t missing = 1 - phase; missing < 108; missing += 2) {
                    const auto p = missing + 5;
                    const auto far = static_cast<double>(pulses[p - 5]) + pulses[p + 5];
                    const auto middle = static_cast<double>(pulses[p - 3]) + pulses[p + 3];
                    const auto near = static_cast<double>(pulses[p - 1]) + pulses[p + 1];
                    pulses[p] = static_cast<float>(
                        far * 0.018032679334282875 - middle * 0.1145915612578392 - near * (-0.5973859429359436));
                }
                fixed_gain *= 0.5f;
            }
        } else
            excitation(bits, multipulse, samples, 1);
        for (std::size_t i = 0; i < 108; ++i) {
            const auto fixed = static_cast<double>(fixed_gain) * samples[i];
            const auto adaptive =
                static_cast<double>(adaptive_gain) * channel.adaptive(adaptive_index + static_cast<std::ptrdiff_t>(i));
            channel.excitation[324 + 108 * block + i] = static_cast<float>(fixed + adaptive);
        }
    }
    //Preserve the raw adaptive mixture before in-place LPC changes output.
    std::copy_n(channel.excitation.begin() + 432, 324, channel.excitation.begin());
    for (unsigned stage = 0; stage < 4; ++stage) {
        for (std::size_t i = 0; i < 12; ++i)
            channel.reflection[i] += delta[i];
        synthesize(channel, stage * 12u, stage == 3 ? 33u : 1u);
    }
}

float pcm_sample(float sample) {
    //8B0200 rounds once, wraps to17 bits, then clips only the middle range.
    //Keep that integer contract with host arithmetic, without an x87 emulator.
    const auto rounded = std::nearbyint(static_cast<double>(sample));
    if (!std::isfinite(rounded)
        || rounded < std::numeric_limits<std::int32_t>::min()
        || rounded > std::numeric_limits<std::int32_t>::max())
        return 0.0f;
    auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded)) & 0x1FFFFu;
    if (value > 0x7FFFu && value < 0x18000u)
        value = 0x7FFFu + static_cast<std::uint32_t>(value >= 0x10000u);
    return static_cast<float>(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value))) / 32768.0f;
}
} //namespace

std::expected<SoundPcmSample, SoundError> decode_utk_sample(std::span<const std::byte> data,
                                                            const sound_wire::SampleHeader& header) {
    if (header.bits_per_sample != 16 || !header.sample_rate)
        return std::unexpected(SoundError{ SoundFailure::sample_header, 0, {} });
    SoundPcmSample result{ header.sample_rate, (header.format_flags & 1u) ? 2u : 1u, {} };
    bool header_ready{};
    try {
        Bits bits{ data };
        const bool stereo = (header.format_flags & 4u) ? false : bits.read(1) != 0;
        const bool reduced_bandwidth = bits.read(1) != 0;
        const auto threshold = 32u - bits.read(4);
        const auto initial_gain = static_cast<float>(bits.read(4) + 1u) * 8.0f;
        const auto multiplier =
            static_cast<float>(static_cast<double>(1.04f) + static_cast<double>(bits.read(6)) * 0.001f);
        if (result.channels == 2 && !stereo)
            return std::unexpected(SoundError{ SoundFailure::sample_header, bits.cursor(), {} });
        std::array<Channel, 2> channels{};
        initialize(channels[0], threshold, initial_gain, multiplier);
        if (stereo)
            initialize(channels[1], threshold, initial_gain, multiplier);
        header_ready = true;
        for (;;) {
            decode_channel(bits, channels[0], reduced_bandwidth);
            if (stereo)
                decode_channel(bits, channels[1], reduced_bandwidth);
            //Native checks its lookahead cursor AFTER decoding and emits none of
            //that frame when cursor>=source_end. A final partial frame is likewise
            //discarded here, rather than reading beyond the resident allocation.
            if (bits.at_end())
                break;
            for (std::size_t i = 0; i < 432; ++i)
                for (std::size_t c = 0; c < result.channels; ++c)
                    result.interleaved.push_back(pcm_sample(channels[c].excitation[324 + i]));
        }
    } catch (const EndOfInput& end) {
        if (!header_ready)
            return std::unexpected(SoundError{ SoundFailure::truncated, end.offset, {} });
    }
    return result;
}
} //namespace simcoaster
