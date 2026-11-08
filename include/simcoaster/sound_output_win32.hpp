#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace simcoaster {
struct SoundOutputStatus {
    bool available {};
    std::int32_t hresult {};
    std::size_t queued_frames {};
    std::uint64_t submitted_frames {}, device_frames {}, underrun_frames {}, rejected_frames {};
    //IAudioClock position, converted to this stream's sample rate. Includes
    //silence, remains monotonic across clear(), and resets only on reopen().
    std::uint64_t played_frames {};
    //Actual endpoint timeline of the first admitted PCM in the current flush
    //epoch. Subtract this from played_frames for a content-relative clock;
    //submitted/device frame counters describe admission, never playback.
    std::optional<std::uint64_t> first_content_frame;
};

//A modern shared-mode WASAPI endpoint. Only completed float-stereo blocks
//cross to its worker; no World objects or voice pointers leave their owner.
//The configured sample rate is the submitted block rate. WASAPI performs any
//endpoint rate/channel conversion, so software/headless playback uses the
//same mixer and timing regardless of the current device's native format.
class SoundOutputWin32 {
    struct State;
    std::unique_ptr<State> state_;
    std::uint32_t sample_rate_;
    std::size_t queue_frames_;

public:
    explicit SoundOutputWin32(std::uint32_t sample_rate = 48000, std::size_t queue_frames = 48000);
    ~SoundOutputWin32();
    SoundOutputWin32(const SoundOutputWin32&) = delete;
    SoundOutputWin32& operator=(const SoundOutputWin32&) = delete;
    bool available() const;
    //Nonblocking whole-block admission. A full or unavailable endpoint returns
    //false and counts rejected frames; it never pretends to consume that block.
    bool submit(std::span<const float> stereo);
    //Discard queued blocks and request an endpoint flush on the worker. The
    //worker acknowledges this through its normal event loop before refill.
    void clear();
    //Shared-stream volume, applied on the worker immediately after notification.
    //It affects PCM already queued in WASAPI without altering owned samples.
    //Linear gain is finite and in [0,1]; the World mixer keeps the default1.
    void set_gain(float);
    float gain() const;
    SoundOutputStatus status() const;
    //Recreate after a device change/failure; software voices remain independent.
    bool reopen();

    std::uint32_t sample_rate() const {
        return sample_rate_;
    }
};
} //namespace simcoaster
