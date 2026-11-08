#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "simcoaster/sound_output_win32.hpp"
#include <algorithm>
#include <array>
#include <audioclient.h>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mmdeviceapi.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace simcoaster {
namespace {
using Microsoft::WRL::ComPtr;

struct Event {
    HANDLE value {};

    explicit Event(bool manual) : value(CreateEventW(nullptr, manual ? TRUE : FALSE, FALSE, nullptr)) {}

    ~Event() {
        if (value) {
            CloseHandle(value);
        }
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ~Apartment() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};
} //namespace

struct SoundOutputWin32::State {
    std::uint32_t rate;
    mutable std::mutex mutex;
    std::condition_variable initialized;
    SoundOutputStatus statistics;
    std::vector<float> queue;
    std::size_t head {}, count {};
    std::uint64_t flush_epoch {};
    std::uint64_t clock_epoch_base {}, stream_position {}, render_cursor {};
    float requested_gain = 1.f, applied_gain = -1.f;
    //Test launchers mute only this process's render streams. Keep PCM queues,
    //playback clocks and the requested game gain intact for audio assertions.
    const bool test_output_muted = [] {
        wchar_t value[2] {};
        return GetEnvironmentVariableW(L"SIMCOASTER_TEST_MUTE_AUDIO", value, 2) == 1 && value[0] == L'1';
    }();
    bool ready {}, flush_requested {};
    Event stop { true }, refill { false };
    std::jthread worker;

    State(std::uint32_t sample_rate, std::size_t queue_frames, float gain = 1.f) : rate(sample_rate), queue(queue_frames * 2), requested_gain(gain) {
        if (!stop.value || !refill.value) {
            throw std::runtime_error("Cannot create audio output event");
        }
        worker = std::jthread([this](std::stop_token token) {
            run(token);
        });
        std::unique_lock lock(mutex);
        initialized.wait(lock, [this] {
            return ready;
        });
    }

    ~State() {
        worker.request_stop();
        SetEvent(stop.value);
        if (worker.joinable()) {
            worker.join();
        }
    }

    void publish(HRESULT result, bool available) {
        {
            std::lock_guard lock(mutex);
            statistics.hresult = static_cast<std::int32_t>(result);
            statistics.available = available;
            ready = true;
        }
        initialized.notify_all();
    }

    HRESULT update_clock(IAudioClock& clock, UINT64 frequency) {
        UINT64 position {};
        const auto result = clock.GetPosition(&position, nullptr);
        if (FAILED(result)) {
            return result;
        }
        //GetPosition units are defined only in relation to GetFrequency. They
        //need not be endpoint frames, even for an unchanged shared-mode format.
        const auto seconds = position / frequency;
        const auto available = std::numeric_limits<std::uint64_t>::max() - clock_epoch_base;
        if (seconds > available / rate) {
            return E_UNEXPECTED;
        }
        const auto fraction = static_cast<std::uint64_t>(static_cast<double>(position % frequency) * rate / static_cast<double>(frequency));
        const auto whole = seconds * rate;
        if (fraction > available - whole) {
            return E_UNEXPECTED;
        }
        stream_position = std::max(stream_position, whole + fraction);
        std::lock_guard lock(mutex);
        statistics.played_frames = clock_epoch_base + stream_position;
        return S_OK;
    }

    HRESULT pump(IAudioClient& client, IAudioRenderClient& render, IAudioClock& clock, UINT64 frequency, IAudioStreamVolume& volume, UINT32 buffer_frames) {
        bool flush {};
        std::uint64_t epoch {};
        {
            std::lock_guard lock(mutex);
            flush = flush_requested;
            flush_requested = false;
            epoch = flush_epoch;
        }
        if (flush) {
            auto result = client.Stop();
            if (FAILED(result)) {
                return result;
            }
            result = update_clock(clock, frequency);
            if (FAILED(result)) {
                return result;
            }
            result = client.Reset();
            if (FAILED(result)) {
                return result;
            }
            clock_epoch_base += stream_position;
            stream_position = render_cursor = 0;
            result = client.Start();
            if (FAILED(result)) {
                return result;
            }
        }
        float gain {};
        {
            std::lock_guard lock(mutex);
            gain = requested_gain;
        }
        if (gain != applied_gain) {
            const auto output_gain = test_output_muted ? 0.f : gain;
            const std::array<float, 2> levels { output_gain, output_gain };
            const auto result = volume.SetAllVolumes(static_cast<UINT32>(levels.size()), levels.data());
            if (FAILED(result)) {
                return result;
            }
            applied_gain = gain;
        }
        auto result = update_clock(clock, frequency);
        if (FAILED(result)) {
            return result;
        }
        UINT32 padding {};
        result = client.GetCurrentPadding(&padding);
        if (FAILED(result)) {
            return result;
        }
        if (padding > buffer_frames) {
            return E_UNEXPECTED;
        }
        //Keep the admission cursor aligned after a genuine endpoint underrun.
        //The first PCM anchor excludes any startup silence before its buffer.
        render_cursor = std::max(render_cursor, stream_position + padding);
        const auto frames = buffer_frames - padding;
        if (!frames) {
            return S_OK;
        }
        BYTE* data {};
        result = render.GetBuffer(frames, &data);
        if (FAILED(result)) {
            return result;
        }
        const auto samples = static_cast<std::size_t>(frames) * 2;
        auto* destination = reinterpret_cast<float*>(data);
        std::size_t taken {};
        {
            std::lock_guard lock(mutex);
            //clear() may have run while the device calls above were in flight.
            //In that case newly queued post-clear PCM belongs to the next epoch:
            //do not consume it into a device buffer that the pending reset discards.
            if (epoch == flush_epoch) {
                taken = std::min(count, samples);
                for (std::size_t i = 0; i < taken; ++i) {
                    const auto value = queue[(head + i) % queue.size()];
                    destination[i] = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
                }
                head = (head + taken) % queue.size();
                count -= taken;
                if (taken && !statistics.first_content_frame) {
                    statistics.first_content_frame = clock_epoch_base + render_cursor;
                }
                statistics.underrun_frames += (samples - taken) / 2;
            }
            statistics.device_frames += frames;
        }
        std::fill(destination + taken, destination + samples, 0.0f);
        result = render.ReleaseBuffer(frames, taken ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
        if (SUCCEEDED(result)) {
            render_cursor += frames;
        }
        return result;
    }

    void run(std::stop_token token) {
        Apartment apartment;
        if (FAILED(apartment.result)) {
            publish(apartment.result, false);
            return;
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> render;
        ComPtr<IAudioClock> clock;
        ComPtr<IAudioStreamVolume> volume;
        auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(result)) {
            result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        }
        if (SUCCEEDED(result)) {
            result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
        }
        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = rate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 8;
        format.nAvgBytesPerSec = rate * format.nBlockAlign;
        //Microsoft shared event-mode contract: duration and periodicity are0.
        //AUTOCONVERTPCM keeps our owned blocks at a fixed stereo-float rate while
        //Windows performs the endpoint's native format/channel conversion.
        if (SUCCEEDED(result)) {
            result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 0, 0, &format, nullptr);
        }
        if (SUCCEEDED(result)) {
            result = client->SetEventHandle(refill.value);
        }
        UINT32 buffer_frames {};
        if (SUCCEEDED(result)) {
            result = client->GetBufferSize(&buffer_frames);
        }
        if (SUCCEEDED(result) && !buffer_frames) {
            result = E_UNEXPECTED;
        }
        if (SUCCEEDED(result)) {
            result = client->GetService(IID_PPV_ARGS(&render));
        }
        if (SUCCEEDED(result)) {
            result = client->GetService(IID_PPV_ARGS(&clock));
        }
        UINT64 frequency {};
        if (SUCCEEDED(result)) {
            result = clock->GetFrequency(&frequency);
        }
        if (SUCCEEDED(result) && !frequency) {
            result = E_UNEXPECTED;
        }
        if (SUCCEEDED(result)) {
            result = client->GetService(IID_PPV_ARGS(&volume));
        }
        if (SUCCEEDED(result) && test_output_muted) {
            const std::array<float, 2> silence {};
            result = volume->SetAllVolumes(static_cast<UINT32>(silence.size()), silence.data());
        }
        if (SUCCEEDED(result)) {
            result = client->Start();
        }
        if (FAILED(result)) {
            publish(result, false);
            return;
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        publish(S_OK, true);
        SetEvent(refill.value);
        const std::array<HANDLE, 2> events { stop.value, refill.value };
        while (!token.stop_requested()) {
            const auto event = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
            if (event == WAIT_OBJECT_0) {
                break;
            }
            if (event != WAIT_OBJECT_0 + 1) {
                result = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            result = pump(*client.Get(), *render.Get(), *clock.Get(), frequency, *volume.Get(), buffer_frames);
            if (FAILED(result)) {
                break;
            }
        }
        (void)client->Stop();
        publish(result, false);
    }
};

SoundOutputWin32::SoundOutputWin32(std::uint32_t sample_rate, std::size_t queue_frames) : sample_rate_(sample_rate), queue_frames_(queue_frames) {
    if (!sample_rate || sample_rate > std::numeric_limits<DWORD>::max() / 8u || !queue_frames || queue_frames > std::vector<float> {}.max_size() / 2) {
        throw std::invalid_argument("Invalid audio output configuration");
    }
    state_ = std::make_unique<State>(sample_rate, queue_frames);
}

SoundOutputWin32::~SoundOutputWin32() = default;

bool SoundOutputWin32::available() const {
    return status().available;
}

bool SoundOutputWin32::submit(std::span<const float> stereo) {
    if (stereo.size() % 2) {
        throw std::invalid_argument("Invalid stereo audio block");
    }
    auto& state = *state_;
    std::lock_guard lock(state.mutex);
    if (!state.statistics.available || stereo.size() > state.queue.size() - state.count) {
        state.statistics.rejected_frames += stereo.size() / 2;
        return false;
    }
    const auto tail = (state.head + state.count) % state.queue.size();
    const auto first = std::min(stereo.size(), state.queue.size() - tail);
    std::copy_n(stereo.begin(), first, state.queue.begin() + static_cast<std::ptrdiff_t>(tail));
    std::copy(stereo.begin() + static_cast<std::ptrdiff_t>(first), stereo.end(), state.queue.begin());
    state.count += stereo.size();
    state.statistics.submitted_frames += stereo.size() / 2;
    return true;
}

void SoundOutputWin32::clear() {
    auto& state = *state_;
    {
        std::lock_guard lock(state.mutex);
        state.head = state.count = 0;
        state.statistics.first_content_frame.reset();
        ++state.flush_epoch;
        state.flush_requested = true;
    }
    SetEvent(state.refill.value);
}

void SoundOutputWin32::set_gain(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 1.f) {
        throw std::invalid_argument("Invalid audio output gain");
    }
    auto& state = *state_;
    {
        std::lock_guard lock(state.mutex);
        state.requested_gain = value;
    }
    SetEvent(state.refill.value);
}

float SoundOutputWin32::gain() const {
    std::lock_guard lock(state_->mutex);
    return state_->requested_gain;
}

SoundOutputStatus SoundOutputWin32::status() const {
    std::lock_guard lock(state_->mutex);
    auto result = state_->statistics;
    result.queued_frames = state_->count / 2;
    return result;
}

bool SoundOutputWin32::reopen() {
    auto replacement = std::make_unique<State>(sample_rate_, queue_frames_, gain());
    state_ = std::move(replacement);
    return available();
}
} //namespace simcoaster
