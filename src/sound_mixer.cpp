#include "simcoaster/sound_mixer.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
using Vec3 = std::array<float, 3>;
constexpr auto unlimited = std::numeric_limits<std::uint32_t>::max();

bool finite(const Vec3& value) {
    return std::ranges::all_of(value, [](float x) {
        return std::isfinite(x);
    });
}

template <class T, class U>
double dot(const std::array<T, 3>& a, const std::array<U, 3>& b) {
    return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1] + static_cast<double>(a[2]) * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

Vec3 unit(Vec3 vector) {
    const auto length = std::sqrt(dot(vector, vector));
    if (!(length > 0) || !std::isfinite(length)) {
        throw std::invalid_argument("Invalid audio listener orientation");
    }
    for (auto& component : vector) {
        component = static_cast<float>(component / length);
    }
    return vector;
}

void validate(const SoundPcmSample& sample, const SoundVoiceParameters& parameters) {
    if (!sample.sample_rate || (sample.channels != 1 && sample.channels != 2) || sample.interleaved.empty() || sample.interleaved.size() % sample.channels) {
        throw std::invalid_argument("Invalid decoded sound sample");
    }
    if (!finite(parameters.position) || !std::isfinite(parameters.level) || parameters.level < 0 || !std::isfinite(parameters.frequency_ratio) || parameters.frequency_ratio <= 0 || !std::isfinite(parameters.min_distance) || parameters.min_distance < 0 || !std::isfinite(parameters.max_distance) || parameters.max_distance <= parameters.min_distance) {
        throw std::invalid_argument("Invalid sound voice parameters");
    }
    if (!std::isfinite(parameters.high_frequency_gain) || parameters.high_frequency_gain < 0 || parameters.high_frequency_gain > 1 || !std::isfinite(parameters.high_frequency_crossover_hz) || parameters.high_frequency_crossover_hz <= 0) {
        throw std::invalid_argument("Invalid sound spectral parameters");
    }
    if (parameters.reverb_send && (!std::isfinite(*parameters.reverb_send) || *parameters.reverb_send < 0 || *parameters.reverb_send > 1)) {
        throw std::invalid_argument("Invalid sound reverb send");
    }
    const auto end = parameters.loop_end_frame ? parameters.loop_end_frame : sample.frames();
    if (parameters.loop_begin_frame >= end || end > sample.frames()) {
        throw std::invalid_argument("Invalid sound loop region");
    }
}

float clean(float sample) {
    return std::isfinite(sample) ? sample : 0.0f;
}
} //namespace

struct SoundVoiceMixer::State {
    struct Voice {
        std::shared_ptr<const SoundPcmSample> sample;
        SoundVoiceParameters parameters;
        double source_frame {};
        std::uint64_t rendered_frames {}, silence_frames_left {};
        std::uint32_t remaining_loops {}, silence_rate {};
        bool paused {}, trailing_silence {}, loop_region_open {};
        std::array<double, 2> low_band {};
    };

    struct Slot {
        std::optional<Voice> voice;
        std::uint16_t generation {};
    };

    std::vector<Slot> slots;
    std::vector<std::uint16_t> free_slots;
    std::size_t active {};
    SoundListener listener;
    Vec3 listener_right { 1, 0, 0 };
    float master = 1;
    float room_rolloff {};
    std::array<float, 4> levels { 1, 1, 1, 1 };
    std::array<bool, 4> enabled { true, true, true, true };

    Voice* find(std::uint32_t key) {
        const auto index = static_cast<std::uint16_t>(key);
        if (!index || index > slots.size()) {
            return nullptr;
        }
        auto& slot = slots[index - 1];
        return slot.voice && slot.generation == static_cast<std::uint16_t>(key >> 16) ? &*slot.voice : nullptr;
    }

    const Voice* find(std::uint32_t key) const {
        return const_cast<State*>(this)->find(key);
    }

    void retire(std::size_t index) {
        slots[index].voice.reset();
        free_slots.push_back(static_cast<std::uint16_t>(index));
        --active;
    }

    static void seek(Voice& voice, std::uint32_t milliseconds) {
        voice.source_frame = std::min(static_cast<double>(voice.sample->frames()), static_cast<double>(milliseconds) * voice.sample->sample_rate / 1000.0);
        voice.trailing_silence = false;
        voice.low_band = {};
        voice.silence_frames_left = 0;
        voice.silence_rate = 0;
        voice.remaining_loops = std::max(voice.parameters.loop_count, 1u);
        const auto end = voice.parameters.loop_end_frame ? voice.parameters.loop_end_frame : voice.sample->frames();
        voice.loop_region_open = voice.source_frame < static_cast<double>(end) && voice.remaining_loops > 1;
    }

    struct Gains {
        std::array<double, 2> dry, send;
    };

    Gains gains(const Voice& voice) const {
        const auto& parameters = voice.parameters;
        //Native8A2E90 uses the product of EVERY selected channel scalar.
        double level = static_cast<double>(parameters.level) * 0.01 * master;
        for (std::size_t channel = 0; channel < levels.size(); ++channel) {
            if (parameters.channel_mask & (1u << channel)) {
                level *= enabled[channel] ? levels[channel] : 0.0f;
            }
        }
        const auto send_level = parameters.reverb_send.value_or(parameters.positioned ? 1.0f : 0.0f);
        if (!parameters.positioned) {
            return { { level, level }, { level * send_level, level * send_level } };
        }
        std::array<double, 3> delta {};
        for (std::size_t i = 0; i < delta.size(); ++i) {
            delta[i] = static_cast<double>(parameters.position[i]) - listener.position[i];
        }
        const auto distance = std::sqrt(dot(delta, delta));
        const auto attenuation = distance <= parameters.min_distance ? 1.0 : std::clamp((parameters.max_distance - distance) / (parameters.max_distance - parameters.min_distance), 0.0, 1.0);
        const auto pan = distance > 0 ? std::clamp(dot(delta, listener_right) / distance, -1.0, 1.0) : 0.0;
        //Software stereo replaces the optional QSound/EAX hardware presentation.
        //Use a constant-power source pan and linear min/max-distance attenuation.
        const auto angle = (pan + 1.0) * std::numbers::pi / 4.0;
        //Native flRoomRolloffFactor is recovered (preset9=10, the other shipped
        //presets0), but the vendor's actual DSP is not available. Portable policy:
        //wet=send/(1+rolloff*max(0,d-min)/max(min,1)), independently of the linear
        //dry-distance curve. Rolloff0 therefore leaves room-send distance neutral.
        const auto reference = std::max(static_cast<double>(parameters.min_distance), 1.0);
        const auto wet = send_level / (1.0 + room_rolloff * std::max(0.0, distance - parameters.min_distance) / reference);
        const auto left = level * std::cos(angle), right = level * std::sin(angle);
        return { { left * attenuation, right * attenuation }, { left * wet, right * wet } };
    }

    static std::uint64_t loop_end(const Voice& voice) {
        return voice.parameters.loop_end_frame ? voice.parameters.loop_end_frame : voice.sample->frames();
    }

    static void advance_source(Voice& voice, double step) {
        voice.source_frame += step;
        if (!voice.loop_region_open) {
            return;
        }
        const auto end = static_cast<double>(loop_end(voice));
        if (voice.source_frame < end) {
            return;
        }
        const auto period = end - static_cast<double>(voice.parameters.loop_begin_frame);
        const auto wanted = std::floor((voice.source_frame - end) / period) + 1.0;
        if (voice.remaining_loops == unlimited) {
            voice.source_frame = static_cast<double>(voice.parameters.loop_begin_frame) + std::fmod(voice.source_frame - end, period);
            return;
        }
        const auto available = std::min(wanted, static_cast<double>(voice.remaining_loops - 1));
        voice.source_frame -= available * period;
        voice.remaining_loops -= static_cast<std::uint32_t>(available);
        if (voice.remaining_loops == 1) {
            voice.loop_region_open = false;
        }
    }

    static bool enter_silence(Voice& voice, std::uint32_t output_rate) {
        if (!voice.trailing_silence) {
            voice.trailing_silence = true;
            voice.silence_rate = output_rate;
            voice.silence_frames_left = (static_cast<std::uint64_t>(voice.parameters.silence_ms) * output_rate + 999u) / 1000u;
        } else if (voice.silence_rate != output_rate) {
            voice.silence_frames_left = rescale_frames(voice.silence_frames_left, voice.silence_rate, output_rate);
            voice.silence_rate = output_rate;
        }
        return voice.silence_frames_left != 0;
    }

    static std::uint64_t rescale_frames(std::uint64_t frames, std::uint32_t from, std::uint32_t to) {
        const auto whole = frames / from, remainder = frames % from;
        return whole * to + (remainder * to + from - 1u) / from;
    }

    static std::optional<std::uint64_t> source_frames_left(const Voice& voice, std::uint32_t output_rate) {
        if (voice.trailing_silence || voice.source_frame >= static_cast<double>(voice.sample->frames())) {
            return 0;
        }
        auto remaining = static_cast<double>(voice.sample->frames()) - voice.source_frame;
        if (voice.loop_region_open) {
            if (voice.remaining_loops == unlimited) {
                return std::nullopt;
            }
            remaining += static_cast<double>(voice.remaining_loops - 1) * static_cast<double>(loop_end(voice) - voice.parameters.loop_begin_frame);
        }
        const auto step = static_cast<double>(voice.sample->sample_rate) * voice.parameters.frequency_ratio / output_rate;
        const auto frames = std::ceil(remaining / step);
        if (frames >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(frames);
    }

    static std::optional<std::uint64_t> completion_frames_left(const Voice& voice, std::uint32_t output_rate) {
        const auto source = source_frames_left(voice, output_rate);
        if (!source) {
            return std::nullopt;
        }
        const auto silence = voice.trailing_silence ? rescale_frames(voice.silence_frames_left, voice.silence_rate, output_rate) : (static_cast<std::uint64_t>(voice.parameters.silence_ms) * output_rate + 999u) / 1000u;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return silence > maximum - *source ? maximum : *source + silence;
    }
};

SoundVoiceMixer::SoundVoiceMixer() : state_(std::make_unique<State>()) {}

SoundVoiceMixer::~SoundVoiceMixer() = default;

std::uint32_t SoundVoiceMixer::start(std::shared_ptr<const SoundPcmSample> sample, const SoundVoiceParameters& parameters) {
    if (!sample) {
        throw std::invalid_argument("Missing decoded sound sample");
    }
    validate(*sample, parameters);
    auto& state = *state_;
    std::size_t index {};
    if (state.free_slots.empty()) {
        if (state.slots.size() == std::numeric_limits<std::uint16_t>::max()) {
            throw std::length_error("Sound voice key capacity");
        }
        index = state.slots.size();
        state.slots.emplace_back();
    } else {
        index = state.free_slots.back();
        state.free_slots.pop_back();
    }
    auto& slot = state.slots[index];
    ++slot.generation;
    if (!slot.generation) {
        ++slot.generation;
    }
    slot.voice.emplace();
    auto& voice = *slot.voice;
    voice.sample = std::move(sample);
    voice.parameters = parameters;
    State::seek(voice, parameters.seek_ms);
    ++state.active;
    return (static_cast<std::uint32_t>(slot.generation) << 16) | static_cast<std::uint32_t>(index + 1);
}

bool SoundVoiceMixer::contains(std::uint32_t key) const {
    return state_->find(key) != nullptr;
}

std::optional<SoundVoiceStatus> SoundVoiceMixer::status(std::uint32_t key) const {
    const auto* voice = state_->find(key);
    if (!voice) {
        return std::nullopt;
    }
    return SoundVoiceStatus { voice->parameters, voice->source_frame, voice->rendered_frames, voice->remaining_loops, voice->paused, voice->trailing_silence };
}

bool SoundVoiceMixer::stop(std::uint32_t key) {
    if (!state_->find(key)) {
        return false;
    }
    state_->retire(static_cast<std::uint16_t>(key) - 1u);
    return true;
}

bool SoundVoiceMixer::pause(std::uint32_t key) {
    auto* voice = state_->find(key);
    if (!voice) {
        return false;
    }
    voice->paused = true;
    return true;
}

bool SoundVoiceMixer::restart(std::uint32_t key) {
    auto* voice = state_->find(key);
    if (!voice) {
        return false;
    }
    voice->paused = false;
    return true;
}

bool SoundVoiceMixer::seek(std::uint32_t key, std::uint32_t milliseconds) {
    auto* voice = state_->find(key);
    if (!voice) {
        return false;
    }
    State::seek(*voice, milliseconds);
    return true;
}

bool SoundVoiceMixer::update(std::uint32_t key, const SoundVoiceParameters& parameters) {
    auto* voice = state_->find(key);
    if (!voice) {
        return false;
    }
    validate(*voice->sample, parameters);
    const auto reset_loop = voice->parameters.loop_count != parameters.loop_count || voice->parameters.loop_begin_frame != parameters.loop_begin_frame || voice->parameters.loop_end_frame != parameters.loop_end_frame;
    voice->parameters = parameters;
    if (reset_loop) {
        voice->remaining_loops = std::max(parameters.loop_count, 1u);
        voice->loop_region_open = voice->source_frame < static_cast<double>(State::loop_end(*voice)) && voice->remaining_loops > 1;
    }
    return true;
}

void SoundVoiceMixer::stop_all() {
    for (std::size_t i = 0; i < state_->slots.size(); ++i) {
        if (state_->slots[i].voice) {
            state_->retire(i);
        }
    }
}

std::size_t SoundVoiceMixer::active_voices() const {
    return state_->active;
}

std::optional<std::uint64_t> SoundVoiceMixer::frames_until_completion(std::uint32_t sample_rate) const {
    if (!sample_rate) {
        throw std::invalid_argument("Invalid audio output sample rate");
    }
    std::optional<std::uint64_t> result;
    for (const auto& slot : state_->slots) {
        if (slot.voice && !slot.voice->paused) {
            const auto next = State::completion_frames_left(*slot.voice, sample_rate);
            if (next && (!result || *next < *result)) {
                result = next;
            }
        }
    }
    return result;
}

void SoundVoiceMixer::set_listener(const SoundListener& input) {
    if (!finite(input.position) || !finite(input.forward) || !finite(input.up)) {
        throw std::invalid_argument("Invalid sound listener");
    }
    auto listener = input;
    listener.forward = unit(input.forward);
    auto right = cross(input.up, listener.forward);
    if (dot(right, right) < 0.000001) {
        right = cross(std::abs(listener.forward[1]) > .99f ? Vec3 { 0, 0, 1 } : Vec3 { 0, 1, 0 }, listener.forward);
    }
    right = unit(right);
    listener.up = unit(cross(listener.forward, right));
    state_->listener = listener;
    state_->listener_right = right;
}

SoundListener SoundVoiceMixer::listener() const {
    return state_->listener;
}

void SoundVoiceMixer::set_master_level(float percent) {
    if (!std::isfinite(percent) || percent < 0) {
        throw std::invalid_argument("Invalid master sound level");
    }
    state_->master = percent * .01f;
}

void SoundVoiceMixer::set_channel(std::size_t index, float percent, bool enabled) {
    if (index >= state_->levels.size() || !std::isfinite(percent) || percent < 0) {
        throw std::invalid_argument("Invalid sound channel");
    }
    state_->levels[index] = percent * .01f;
    state_->enabled[index] = enabled;
}

void SoundVoiceMixer::set_reverb_room_rolloff(float value) {
    if (!std::isfinite(value) || value < 0) {
        throw std::invalid_argument("Invalid reverb room rolloff");
    }
    state_->room_rolloff = value;
}

void SoundVoiceMixer::render(std::span<float> stereo, std::uint32_t output_rate) {
    render(stereo, std::span<float> {}, output_rate);
}

void SoundVoiceMixer::render(std::span<float> stereo, std::span<float> send, std::uint32_t output_rate) {
    if (stereo.size() % 2 || !output_rate) {
        throw std::invalid_argument("Invalid stereo output block");
    }
    if (!send.empty()) {
        if (send.size() != stereo.size()) {
            throw std::invalid_argument("Invalid reverb-send block size");
        }
        const std::less<const float*> less;
        if (less(stereo.data(), send.data() + send.size()) && less(send.data(), stereo.data() + stereo.size())) {
            throw std::invalid_argument("Audio output buses overlap");
        }
    }
    std::ranges::fill(stereo, 0.0f);
    std::ranges::fill(send, 0.0f);
    auto& state = *state_;
    const auto frames = stereo.size() / 2;
    for (std::size_t slot = 0; slot < state.slots.size(); ++slot) {
        auto& entry = state.slots[slot];
        if (!entry.voice || entry.voice->paused) {
            continue;
        }
        auto& voice = *entry.voice;
        const auto gains = state.gains(voice);
        const auto& sample = *voice.sample;
        const auto step = static_cast<double>(sample.sample_rate) * voice.parameters.frequency_ratio / output_rate;
        const auto source_deadline = State::source_frames_left(voice, output_rate);
        const auto low_pass = 1.0 - std::exp(-2.0 * std::numbers::pi * voice.parameters.high_frequency_crossover_hz / output_rate);
        if (source_deadline && !*source_deadline && !State::enter_silence(voice, output_rate)) {
            state.retire(slot);
            continue;
        }
        bool finished {};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (source_deadline && frame < *source_deadline && voice.source_frame >= static_cast<double>(sample.frames())) {
                voice.source_frame = std::nextafter(static_cast<double>(sample.frames()), 0.0);
            }
            if (voice.source_frame >= static_cast<double>(sample.frames())) {
                if (!State::enter_silence(voice, output_rate)) {
                    finished = true;
                    break;
                }
                --voice.silence_frames_left;
                ++voice.rendered_frames;
                if (!voice.silence_frames_left) {
                    finished = true;
                    break;
                }
                continue;
            }
            const auto first = static_cast<std::uint64_t>(voice.source_frame);
            auto next = first + 1;
            if (voice.loop_region_open && next >= State::loop_end(voice)) {
                next = voice.parameters.loop_begin_frame;
            } else {
                next = std::min(next, sample.frames() - 1);
            }
            const auto fraction = voice.source_frame - static_cast<double>(first);
            std::array<double, 2> values {};
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto input_channel = sample.channels == 1 ? 0 : channel;
                const auto a = clean(sample.interleaved[static_cast<std::size_t>(first) * sample.channels + input_channel]);
                const auto b = clean(sample.interleaved[static_cast<std::size_t>(next) * sample.channels + input_channel]);
                values[channel] = a + (b - a) * fraction;
                voice.low_band[channel] += low_pass * (values[channel] - voice.low_band[channel]);
                //The old EAX/A3D implementations were proprietary spectral effects.
                //This portable shelf uses their requested HF scalar while keeping
                //the explicitly chosen crossover independent of World semantics.
                if (voice.parameters.high_frequency_gain != 1.0f) {
                    values[channel] = voice.low_band[channel] + voice.parameters.high_frequency_gain * (values[channel] - voice.low_band[channel]);
                }
            }
            for (std::size_t channel = 0; channel < 2; ++channel) {
                stereo[frame * 2 + channel] += static_cast<float>(values[channel] * gains.dry[channel]);
                if (!send.empty()) {
                    send[frame * 2 + channel] += static_cast<float>(values[channel] * gains.send[channel]);
                }
            }
            State::advance_source(voice, step);
            ++voice.rendered_frames;
            //Match the ceil boundary exposed to a sequence owner, without a
            //possible one-frame discrepancy from repeated floating additions.
            if (source_deadline && frame + 1 >= *source_deadline) {
                voice.source_frame = static_cast<double>(sample.frames());
                voice.loop_region_open = false;
                voice.remaining_loops = 1;
            }
            if (voice.source_frame >= static_cast<double>(sample.frames()) && !voice.parameters.silence_ms) {
                finished = true;
                break;
            }
        }
        if (finished) {
            state.retire(slot);
        }
    }
    for (auto& value : stereo) {
        value = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
    }
    for (auto& value : send) {
        if (!std::isfinite(value)) {
            value = 0.0f;
        }
    }
}
} //namespace simcoaster
