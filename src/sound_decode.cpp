#include "simcoaster/sound_catalog.hpp"
#include "simcoaster/sound_utk.hpp"
#include <cmath>
#include <limits>
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
//CC0 minimp3, upstream ea99364f61c14656440e8d77e9c233ccf3124633.
//Layers I/II remain enabled: the installed SDTs contain Layer II audio.
#include <minimp3.h>

namespace simcoaster {
std::expected<SoundPcmSample, SoundError> decode_sound_sample(const SoundSampleBank& bank, std::uint32_t token) {
    const auto selected = bank.sample(token);
    if (!selected)
        return std::unexpected(selected.error());
    const auto* sample = *selected;
    const auto& header = sample->header;
    const auto input = bank.payload(*sample);
    const auto failure = [&](SoundFailure reason, std::size_t offset = 0) {
        return std::unexpected(SoundError{ reason, sample->payload_offset + offset, {}, token });
    };
    SoundPcmSample result{ header.sample_rate, (header.format_flags & 1u) ? 2u : 1u, {} };
    if (!header.sample_rate)
        return failure(SoundFailure::sample_header);
    //MPEG has precedence over UTK, and UTK over the SoundFont discriminator.
    if (header.format_flags & 32u) {
        if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return failure(SoundFailure::mpeg);
        mp3dec_t decoder{};
        mp3dec_init(&decoder);
        std::array<float, MINIMP3_MAX_SAMPLES_PER_FRAME> frame{};
        std::size_t cursor{};
        bool initialized{};
        while (cursor < input.size()) {
            mp3dec_frame_info_t info{};
            const auto produced = mp3dec_decode_frame(&decoder,
                                                      reinterpret_cast<const std::uint8_t*>(input.data() + cursor),
                                                      static_cast<int>(input.size() - cursor),
                                                      frame.data(),
                                                      &info);
            if (info.frame_bytes <= 0)
                break;
            if (static_cast<std::size_t>(info.frame_bytes) > input.size() - cursor)
                return failure(SoundFailure::mpeg, cursor);
            cursor += static_cast<std::size_t>(info.frame_bytes);
            if (!produced)
                continue;
            if (produced < 0
                || (info.channels != 1 && info.channels != 2)
                || info.hz <= 0
                || static_cast<std::size_t>(produced) * static_cast<std::size_t>(info.channels) > frame.size())
                return failure(SoundFailure::mpeg, cursor);
            if (!initialized) {
                result.channels = static_cast<std::uint32_t>(info.channels);
                result.sample_rate = static_cast<std::uint32_t>(info.hz);
                initialized = true;
            } else if (result.channels != static_cast<std::uint32_t>(info.channels)
                       || result.sample_rate != static_cast<std::uint32_t>(info.hz))
                return failure(SoundFailure::mpeg, cursor);
            result.interleaved.insert(result.interleaved.end(),
                                      frame.begin(),
                                      frame.begin() + produced * info.channels);
        }
        if (!initialized)
            return failure(SoundFailure::mpeg);
        //8B0A00 constrains MPEG output by the decoded-byte extent, adjusted when
        //the decoder corrects the advertised sample rate. Keep the byte contract
        //while allowing the host decoder to do its own floating-point arithmetic.
        if (header.format_flags & 4u) {
            const auto ratio = std::nearbyint(static_cast<double>(header.sample_rate) / result.sample_rate);
            if (ratio < 1 || ratio > std::numeric_limits<std::uint32_t>::max())
                return failure(SoundFailure::sample_header);
            const auto sample_limit = (header.extent / static_cast<std::uint32_t>(ratio)) / 2u;
            const auto aligned_limit = sample_limit - (sample_limit % result.channels);
            //The original reader reports the requested capped length even if its
            //codec returns fewer bytes. Deterministically fill that tail with zero.
            result.interleaved.resize(aligned_limit, 0.0f);
        }
        return result;
    }
    if (header.format_flags & 16u) {
        auto decoded = decode_utk_sample(input, header);
        if (!decoded) {
            auto error = decoded.error();
            error.offset += sample->payload_offset;
            error.sample_token = token;
            return std::unexpected(std::move(error));
        }
        return decoded;
    }
    if (header.format_flags & 64u)
        return failure(SoundFailure::sample_format);
    if (header.bits_per_sample != 8 && header.bits_per_sample != 16)
        return failure(SoundFailure::pcm);
    const auto width = header.bits_per_sample / 8u;
    if (input.size() % (width * result.channels))
        return failure(SoundFailure::pcm);
    result.interleaved.reserve(input.size() / width);
    for (std::size_t cursor = 0; cursor < input.size(); cursor += width) {
        if (width == 1)
            result.interleaved.push_back((static_cast<float>(std::to_integer<unsigned>(input[cursor])) - 128.0f)
                                         / 128.0f);
        else {
            std::int16_t value{};
            save::detail::decode_scalar(std::span<const std::byte, 2>{ input.data() + cursor, 2 }, value);
            result.interleaved.push_back(static_cast<float>(value) / 32768.0f);
        }
    }
    return result;
}
} //namespace simcoaster
