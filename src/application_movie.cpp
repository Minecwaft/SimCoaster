#include "application_movie_internal.hpp"
#include "simcoaster/sound_output_win32.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace simcoaster {
namespace {
using namespace application_movie_detail;

std::vector<std::byte> read_movie(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(std::format("Cannot open original movie {}", path.native_encoded_string()));
    }
    const auto length = input.tellg();
    if (length < 8 || length > 536870912) {
        fail("invalid file size", 0);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        fail("cannot read file", 0);
    }
    return bytes;
}

bool tag(std::span<const std::byte> bytes, std::size_t offset, std::string_view value) {
    return offset <= bytes.size() && value.size() <= bytes.size() - offset && std::equal(value.begin(), value.end(), bytes.begin() + offset, [](char a, std::byte b) {
               return static_cast<unsigned char>(a) == std::to_integer<unsigned char>(b);
           });
}
} //namespace

struct ApplicationMovieDecoder::State {
    std::vector<std::byte> bytes;
    ApplicationMovieInfo info;

    struct Picture {
        std::size_t offset {}, size {};
    };

    std::vector<Picture> pictures;
    std::vector<float> pcm;

    explicit State(std::vector<std::byte> source) : bytes(std::move(source)) {
        const std::span<const std::byte> input(bytes);
        bool header {}, ended {};
        std::optional<std::uint32_t> declared_pictures, declared_samples;
        info.sample_rate = 22050;
        unsigned bits = 16, codec = 0;
        info.channels = 1;
        for (std::size_t offset = 0; offset < input.size();) {
            const auto size = little(input, offset + 4);
            if (size < 8 || size > input.size() - offset) {
                fail("invalid chunk bounds", offset);
            }
            const auto payload = input.subspan(offset + 8, size - 8);
            if (tag(input, offset, "SCHl")) {
                if (header || offset != 0 || !tag(payload, 0, "PT")) {
                    fail("invalid audio header", offset);
                }
                header = true;
                bool patch {};
                std::size_t cursor = 4;
                while (cursor < payload.size()) {
                    const auto key = little(payload, cursor++, 1);
                    if (key == 253) {
                        patch = true;
                        continue;
                    }
                    if (key == 138 || key == 255) {
                        break;
                    }
                    const auto width = little(payload, cursor++, 1);
                    if (width > 4 || width > payload.size() - cursor) {
                        fail("invalid audio header value", offset + 8 + cursor);
                    }
                    std::uint32_t value {};
                    for (unsigned i = 0; i < width; ++i) {
                        value = (value << 8) | little(payload, cursor++, 1);
                    }
                    if (!patch && key == 27) {
                        info.frames_per_second = value;
                    }
                    if (patch) {
                        match (key) {
                            case 129 => bits = value;
                            case 130 => info.channels = value;
                            case 131 => codec = value;
                            case 132 => info.sample_rate = value;
                            case 133 => declared_samples = value;
                            case _   => {
                            }
                        }
                    }
                }
                if (bits != 16 || codec != 7 || info.channels != 2 || !info.frames_per_second || info.frames_per_second > 120 || info.sample_rate < 8000 || info.sample_rate > 192000) {
                    fail("unsupported movie stream parameters", offset);
                }
            } else if (tag(input, offset, "pIQT")) {
                if (!header || ended || pictures.size() >= 180000) {
                    fail("picture outside movie stream", offset);
                }
                const auto width = little(payload, 0, 2), height = little(payload, 2, 2);
                if (pictures.empty()) {
                    info.width = width;
                    info.height = height;
                }
                if (width != info.width || height != info.height || !width || !height || width > 4096 || height > 4096) {
                    fail("changing/invalid movie dimensions", offset);
                }
                pictures.push_back({ offset + 8, size - 8 });
            } else if (tag(input, offset, "SCDl")) {
                if (!header || ended) {
                    fail("audio outside movie stream", offset);
                }
                auto decoded = decode_ea_adpcm(payload, info.channels);
                if (decoded.size() > 230400000 || pcm.size() > 230400000 - decoded.size()) {
                    fail("movie audio exceeds bounded duration", offset);
                }
                pcm.insert(pcm.end(), decoded.begin(), decoded.end());
            } else if (tag(input, offset, "SCCl")) {
                if (declared_pictures || payload.size() != 4) {
                    fail("invalid stream frame count", offset);
                }
                declared_pictures = little(payload, 0);
            } else if (tag(input, offset, "SCEl")) {
                if (size != 8 || ended) {
                    fail("invalid stream terminator", offset);
                }
                ended = true;
            } else {
                fail("unsupported original movie chunk", offset);
            }
            offset += size;
        }
        if (!header || !ended || pictures.empty() || pcm.empty()) {
            fail("incomplete original movie", input.size());
        }
        info.frames = static_cast<std::uint32_t>(pictures.size());
        info.audio_frames = pcm.size() / 2;
        if ((declared_pictures && *declared_pictures != info.frames) || (declared_samples && *declared_samples != info.audio_frames)) {
            fail("stream count mismatch", input.size());
        }
    }
};

ApplicationMovieDecoder::ApplicationMovieDecoder(const std::filesystem::path& path) : ApplicationMovieDecoder(read_movie(path)) {}

ApplicationMovieDecoder::ApplicationMovieDecoder(std::vector<std::byte> bytes) : state_(std::make_unique<State>(std::move(bytes))) {}

ApplicationMovieDecoder::~ApplicationMovieDecoder() = default;

const ApplicationMovieInfo& ApplicationMovieDecoder::info() const {
    return state_->info;
}

TextureImagePtr ApplicationMovieDecoder::decode_video(std::uint32_t frame) const {
    if (frame >= state_->pictures.size()) {
        throw std::out_of_range("Original movie frame");
    }
    const auto& picture = state_->pictures[frame];
    try {
        return application_movie_detail::decode_tqi_picture(std::span<const std::byte>(state_->bytes).subspan(picture.offset, picture.size));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::format("Original movie frame {} (file byte {}): {}", frame, picture.offset, error.what()));
    }
}

std::span<const float> ApplicationMovieDecoder::audio() const {
    return state_->pcm;
}

void application_movie_detail::append_full_viewport(UiDrawList& draw, const TextureImagePtr& image, std::array<float, 2> offset) {
    if (!image || draw.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4 || draw.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6) {
        throw std::runtime_error("Invalid application image draw");
    }
    const auto vertex = static_cast<std::uint32_t>(draw.vertices.size());
    const auto index = static_cast<std::uint32_t>(draw.indices.size());
    const auto width = draw.canvas_width, height = draw.canvas_height;
    const auto left = width * offset[0], top = height * offset[1];
    draw.vertices.insert(draw.vertices.end(), { { { left, top, 0 }, { 0, 0 } }, { { left + width, top, 0 }, { 1, 0 } }, { { left + width, top + height, 0 }, { 1, 1 } }, { { left, top + height, 0 }, { 0, 1 } } });
    draw.indices.insert(draw.indices.end(), { vertex, vertex + 1, vertex + 2, vertex, vertex + 2, vertex + 3 });
    MeshMaterial material;
    material.flags = 0x8000 | 0x800 | 0x40000 | 0x10;
    draw.commands.push_back({ image, index, 6, { 0, 0, width, height }, material });
}

struct ApplicationMoviePlayer::State {
    std::unique_ptr<ApplicationMovieDecoder> decoder;
    std::unique_ptr<SoundOutputWin32> output;
    TextureImagePtr frame;
    ApplicationMovieStatus status;
    std::uint16_t volume = 1023;
    std::uint64_t submitted {};
    std::chrono::milliseconds fallback_elapsed {};
    std::uint64_t fallback_origin {};
    std::array<float, 2> viewport_offset {};

    void fill() {
        if (!decoder || !output || !output->available()) {
            return;
        }
        const auto& info = decoder->info();
        const auto pcm = decoder->audio();
        while (submitted < info.audio_frames) {
            const auto count = std::min<std::uint64_t>(info.audio_frames - submitted, info.sample_rate / 20);
            if (!output->submit(pcm.subspan(static_cast<std::size_t>(submitted) * 2, static_cast<std::size_t>(count) * 2))) {
                break;
            }
            submitted += count;
        }
    }
};

ApplicationMoviePlayer::ApplicationMoviePlayer() : state_(std::make_unique<State>()) {}

ApplicationMoviePlayer::~ApplicationMoviePlayer() = default;

void ApplicationMoviePlayer::open(const std::filesystem::path& path, std::uint16_t volume) {
    if (volume > 1023) {
        throw std::out_of_range("Movie volume exceeds ten bits");
    }
    //Decode/validate before replacing the active movie. Failure never presents
    //an invented successful movie or leaves half-created sound ownership.
    auto decoder = std::make_unique<ApplicationMovieDecoder>(path);
    auto first = decoder->decode_video(0);
    auto output = std::make_unique<SoundOutputWin32>(decoder->info().sample_rate, decoder->info().sample_rate);
    stop(ApplicationMovieStop::replaced);
    state_->decoder = std::move(decoder);
    state_->frame = std::move(first);
    state_->output = std::move(output);
    state_->status = { true, state_->output->available(), ApplicationMovieStop::none, 0, 0 };
    state_->fallback_elapsed = {};
    state_->fallback_origin = 0;
    state_->submitted = 0;
    state_->viewport_offset = {};
    set_volume(volume);
    state_->fill();
}

void ApplicationMoviePlayer::set_volume(std::uint16_t value) {
    if (value > 1023) {
        throw std::out_of_range("Movie volume exceeds ten bits");
    }
    state_->volume = value;
    //Native651220 changes the playing buffer's attenuation immediately. Keep
    //PCM unmodified so IAudioStreamVolume also updates already queued samples.
    const auto attenuation = int(value) * 10000 / 1023 - 10000;
    const float gain = value ? static_cast<float>(std::pow(10.0, attenuation / 2000.0)) : 0.f;
    if (state_->output) {
        state_->output->set_gain(gain);
    }
}

void ApplicationMoviePlayer::set_viewport_offset(std::array<float, 2> value) {
    if (!std::isfinite(value[0]) || !std::isfinite(value[1])) {
        throw std::invalid_argument("Invalid movie viewport offset");
    }
    state_->viewport_offset = value;
}

void ApplicationMoviePlayer::advance(std::chrono::milliseconds delta) {
    if (delta.count() < 0) {
        throw std::invalid_argument("Negative movie frame duration");
    }
    if (!state_->status.playing) {
        return;
    }
    try {
        const auto& info = state_->decoder->info();
        if (state_->output->available()) {
            const auto output = state_->output->status();
            state_->status.played_audio_frames = output.first_content_frame && output.played_frames >= *output.first_content_frame ? output.played_frames - *output.first_content_frame : 0;
            state_->fill();
        } else {
            //Original8651C0 also falls back to a rate-derived timer when no sound
            //device exists. A device failure does not stall the application forever.
            if (state_->status.audio_available) {
                state_->fallback_origin = state_->status.played_audio_frames;
                state_->fallback_elapsed = {};
            }
            state_->status.audio_available = false;
            state_->fallback_elapsed += delta;
            state_->status.played_audio_frames = state_->fallback_origin + static_cast<std::uint64_t>(state_->fallback_elapsed.count()) * info.sample_rate / 1000;
        }
        const auto frame = std::min<std::uint64_t>(info.frames - 1, state_->status.played_audio_frames * info.frames_per_second / info.sample_rate);
        if (frame != state_->status.displayed_frame) {
            state_->frame = state_->decoder->decode_video(static_cast<std::uint32_t>(frame));
            state_->status.displayed_frame = static_cast<std::uint32_t>(frame);
        }
        const auto video_end = std::uint64_t(info.frames) * info.sample_rate / info.frames_per_second;
        const auto end = std::max(info.audio_frames, video_end);
        if (state_->status.played_audio_frames >= end) {
            stop(ApplicationMovieStop::completed);
        }
    } catch (...) {
        stop(ApplicationMovieStop::failed);
        throw;
    }
}

void ApplicationMoviePlayer::append(UiDrawList& draw) const {
    if (state_->frame) {
        application_movie_detail::append_full_viewport(draw, state_->frame, state_->viewport_offset);
    }
}

void ApplicationMoviePlayer::stop(ApplicationMovieStop reason) {
    if (state_->output) {
        state_->output->clear();
    }
    state_->output.reset();
    state_->decoder.reset();
    state_->frame.reset();
    state_->status.playing = false;
    state_->status.reason = reason;
}

const ApplicationMovieInfo* ApplicationMoviePlayer::info() const {
    return state_->decoder ? &state_->decoder->info() : nullptr;
}

ApplicationMovieStatus ApplicationMoviePlayer::status() const {
    return state_->status;
}
} //namespace simcoaster
