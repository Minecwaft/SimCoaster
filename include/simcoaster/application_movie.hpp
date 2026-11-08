#pragma once
#include "ui_draw.hpp"
#include <chrono>

namespace simcoaster {
struct ApplicationMovieInfo {
    std::uint32_t width{}, height{}, frames{}, frames_per_second{}, sample_rate{}, channels{};
    std::uint64_t audio_frames{};
};

//The installed .tgq files contain pIQT (TQI) intra pictures and stereo EA
//ADPCM. Unsupported original movie codecs are reported, never substituted.
class ApplicationMovieDecoder {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit ApplicationMovieDecoder(const std::filesystem::path&);
    explicit ApplicationMovieDecoder(std::vector<std::byte>);
    ~ApplicationMovieDecoder();
    ApplicationMovieDecoder(const ApplicationMovieDecoder&) = delete;
    ApplicationMovieDecoder& operator=(const ApplicationMovieDecoder&) = delete;
    const ApplicationMovieInfo& info() const;
    TextureImagePtr decode_video(std::uint32_t frame) const;
    std::span<const float> audio() const; //Interleaved float stereo, unattenuated.
};
enum class ApplicationMovieStop {
    none,
    completed,
    skipped,
    deactivated,
    replaced,
    failed
};

struct ApplicationMovieStatus {
    bool playing{}, audio_available{};
    ApplicationMovieStop reason{};
    std::uint32_t displayed_frame{};
    std::uint64_t played_audio_frames{};
};

class ApplicationMoviePlayer {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationMoviePlayer();
    ~ApplicationMoviePlayer();
    ApplicationMoviePlayer(const ApplicationMoviePlayer&) = delete;
    ApplicationMoviePlayer& operator=(const ApplicationMoviePlayer&) = delete;
    void open(const std::filesystem::path&, std::uint16_t volume = 1023);
    //Native ten-bit movie volume, logarithmic dB.
    void set_volume(std::uint16_t);
    //Fraction of the viewport; native day-selected movies start at y64/480.
    void set_viewport_offset(std::array<float, 2>);
    void advance(std::chrono::milliseconds);
    //Native full-viewport stretch, no letterbox.
    void append(UiDrawList&) const;
    void stop(ApplicationMovieStop = ApplicationMovieStop::skipped);
    const ApplicationMovieInfo* info() const;
    ApplicationMovieStatus status() const;
};
} //namespace simcoaster
