#include "simcoaster/sound_environment.hpp"
#include "sound_io.hpp"
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
bool finite(const SoundReverbParameters& p) {
    return std::isfinite(p.size)
        && std::isfinite(p.diffusion)
        && std::isfinite(p.decay_seconds)
        && std::isfinite(p.decay_hf_ratio)
        && std::isfinite(p.reflections_delay_seconds)
        && std::isfinite(p.reverb_delay_seconds)
        && std::isfinite(p.room_rolloff)
        && std::isfinite(p.air_absorption_hf);
}

bool supported(const SoundReverbParameters& p) {
    //Standard EAX2 listener ranges bound the portable network allocation and
    //stability. Keep unsupported rows in the catalog; selecting one can fail,
    //as a real driver's ALLPARAMETERS request can fail after a successful load.
    return p.environment != std::numeric_limits<std::uint32_t>::max()
        && finite(p)
        && p.size >= 1.f
        && p.size <= 100.f
        && p.diffusion >= 0.f
        && p.diffusion <= 1.f
        && p.room_millibels >= -10000
        && p.room_millibels <= 0
        && p.room_hf_millibels >= -10000
        && p.room_hf_millibels <= 0
        && p.decay_seconds >= .1f
        && p.decay_seconds <= 20.f
        && p.decay_hf_ratio >= .1f
        && p.decay_hf_ratio <= 2.f
        && p.reflections_millibels >= -10000
        && p.reflections_millibels <= 1000
        && p.reverb_millibels >= -10000
        && p.reverb_millibels <= 2000
        && p.reflections_delay_seconds >= 0.f
        && p.reflections_delay_seconds <= .3f
        && p.reverb_delay_seconds >= 0.f
        && p.reverb_delay_seconds <= .1f
        && p.room_rolloff >= 0.f
        && p.room_rolloff <= 10.f
        && p.air_absorption_hf >= -100.f
        && p.air_absorption_hf <= 0.f;
}

float amplitude(double millibels) {
    return static_cast<float>(std::pow(10., millibels / 2000.));
}

bool finite_position(const std::array<float, 3>& position) {
    return std::isfinite(position[0]) && std::isfinite(position[2]);
}

struct Delay {
    std::vector<float> samples;
    std::size_t cursor{};

    void reset(std::size_t length) {
        samples.assign(length, 0.f);
        cursor = 0;
    }

    float push(float value) {
        if (samples.empty())
            return value;
        const auto previous = samples[cursor];
        samples[cursor] = value;
        if (++cursor == samples.size())
            cursor = 0;
        return previous;
    }

    float read() const {
        return samples[cursor];
    }

    void write(float value) {
        (void)push(value);
    }
};

struct Reverb {
    std::uint32_t rate{};
    std::array<Delay, 8> lines;
    std::array<float, 8> low_history{}, low_feedback{}, high_feedback{}, input_gain{};
    std::array<Delay, 2> early;
    Delay late_predelay;
    std::array<float, 2> input_low{};
    float filter_coefficient{}, room_hf{}, early_gain{}, late_gain{}, diffusion{};

    explicit Reverb(const SoundReverbParameters& p, std::uint32_t sample_rate) : rate(sample_rate) {
        //Portable presentation policy: an eight-line Householder feedback delay
        //network. Base transit uses the recovered room size and343m/s; distinct
        //path ratios decorrelate stereo. Driver delay topology is not in Game.exe.
        constexpr std::array<double, 8> paths{ .911, 1.113, 1.397, 1.613, 1.837, 2.093, 2.371, 2.633 };
        const auto frames = [&](double seconds) {
            return static_cast<std::size_t>(std::llround(seconds * rate));
        };
        const double transit = p.size / 343.;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto delay = std::max<std::size_t>(1, frames(transit * paths[i]));
            lines[i].reset(delay);
            const auto seconds = static_cast<double>(delay) / rate;
            low_feedback[i] = static_cast<float>(std::pow(10., -3. * seconds / p.decay_seconds));
            //Normalize injection energy as the decay changes; the reverb-level
            //control remains independent of the selected decay duration.
            input_gain[i] = std::sqrt(1.f - low_feedback[i] * low_feedback[i]);
            high_feedback[i] = static_cast<float>(std::pow(10., -3. * seconds / (p.decay_seconds * p.decay_hf_ratio)))
                             * amplitude(p.air_absorption_hf * seconds * 343.);
        }
        early[0].reset(frames(p.reflections_delay_seconds));
        early[1].reset(frames(p.reflections_delay_seconds + transit * .173 * p.diffusion));
        //EAX2 late delay is relative to the first early reflection. This field
        //convention is also documented in Creative's Effects Extension Guide:
        //https://openal-soft.org/misc-downloads/EffectsExtensionGuide.pdf
        late_predelay.reset(frames(p.reflections_delay_seconds + p.reverb_delay_seconds));
        filter_coefficient = static_cast<float>(std::exp(-2. * std::numbers::pi * 5000. / rate));
        room_hf = amplitude(p.room_hf_millibels);
        early_gain = amplitude(static_cast<double>(p.room_millibels) + p.reflections_millibels);
        late_gain = amplitude(static_cast<double>(p.room_millibels) + p.reverb_millibels);
        diffusion = p.diffusion;
    }

    void process(std::span<float> stereo, std::span<const float> send) {
        constexpr float inverse_root_eight = .3535533905932738f;
        constexpr std::array<float, 8> signs{ 1.f, -1.f, 1.f, 1.f, -1.f, 1.f, -1.f, -1.f };
        for (std::size_t frame = 0; frame < stereo.size(); frame += 2) {
            const auto left = std::isfinite(stereo[frame]) ? std::clamp(stereo[frame], -1.f, 1.f) : 0.f;
            const auto right = std::isfinite(stereo[frame + 1]) ? std::clamp(stereo[frame + 1], -1.f, 1.f) : 0.f;
            const auto send_left = std::isfinite(send[frame]) ? std::clamp(send[frame], -1.f, 1.f) : 0.f;
            const auto send_right = std::isfinite(send[frame + 1]) ? std::clamp(send[frame + 1], -1.f, 1.f) : 0.f;
            input_low[0] = (1.f - filter_coefficient) * send_left + filter_coefficient * input_low[0];
            input_low[1] = (1.f - filter_coefficient) * send_right + filter_coefficient * input_low[1];
            const auto filtered_left = input_low[0] + room_hf * (send_left - input_low[0]);
            const auto filtered_right = input_low[1] + room_hf * (send_right - input_low[1]);
            const auto early_left = early[0].push(filtered_left), early_right = early[1].push(filtered_right);
            const auto input = late_predelay.push((filtered_left + filtered_right) * .5f);
            std::array<float, 8> delayed{};
            float sum{}, late_left{}, late_right{};
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto raw = lines[i].read();
                low_history[i] = (1.f - filter_coefficient) * raw + filter_coefficient * low_history[i];
                delayed[i] = low_feedback[i] * low_history[i] + high_feedback[i] * (raw - low_history[i]);
                sum += delayed[i];
                late_left += raw;
                late_right += raw * signs[i];
            }
            const auto mean_twice = sum * .25f;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto mixed = delayed[i] + diffusion * (mean_twice - 2.f * delayed[i]);
                lines[i].write(input * inverse_root_eight * input_gain[i] + mixed);
            }
            stereo[frame] =
                std::clamp(left + early_gain * early_left + late_gain * late_left * inverse_root_eight, -1.f, 1.f);
            stereo[frame + 1] =
                std::clamp(right + early_gain * early_right + late_gain * late_right * inverse_root_eight, -1.f, 1.f);
        }
    }
};
} //namespace

struct SoundEnvironment::State {
    std::vector<std::optional<SoundReverbParameters>> presets;
    std::optional<std::uint32_t> active;
    std::optional<Reverb> reverb;
    std::map<std::uint32_t, SoundEnvironmentRegion> regions;
    std::uint32_t next_region{};
    float radius_cutoff{};
    bool occlusion_enabled{};
};

SoundEnvironment::SoundEnvironment() : state_(std::make_unique<State>()) {}

SoundEnvironment::~SoundEnvironment() = default;

std::expected<void, SoundError> SoundEnvironment::load_presets(std::span<const std::byte> bytes) {
    try {
        sound_detail::Reader input{ bytes };
        if (input.read<std::uint32_t>() != 2000u)
            input.fail(SoundFailure::signature, 0);
        const auto count = input.read<std::uint32_t>();
        if (count > bytes.size() - input.cursor)
            input.fail(SoundFailure::count, input.cursor);
        std::vector<std::optional<SoundReverbParameters>> presets;
        presets.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (input.read<std::uint8_t>()) {
                const auto at = input.cursor;
                auto row = input.read<SoundReverbParameters>();
                if (!finite(row))
                    input.fail(SoundFailure::configuration, at);
                presets.push_back(row);
            } else
                presets.emplace_back();
        }
        if (input.cursor != bytes.size())
            input.fail(SoundFailure::count, input.cursor);
        state_->presets = std::move(presets);
        state_->reverb.reset();
        if (state_->active && (!preset(*state_->active) || !supported(*preset(*state_->active))))
            state_->active.reset();
        return {};
    } catch (const SoundError& error) {
        return std::unexpected(error);
    }
}

const SoundReverbParameters* SoundEnvironment::preset(std::uint32_t index) const {
    if (index >= state_->presets.size() || !state_->presets[index])
        return nullptr;
    return &*state_->presets[index];
}

std::optional<std::uint32_t> SoundEnvironment::active_preset() const {
    return state_->active;
}

bool SoundEnvironment::set_reverb_preset(std::uint32_t index) {
    const auto* value = preset(index);
    if (!value || !supported(*value))
        return false;
    if (state_->active != index) {
        state_->active = index;
        state_->reverb.reset();
    }
    return true;
}

void SoundEnvironment::reset_reverb() {
    state_->reverb.reset();
}

float SoundEnvironment::room_rolloff() const {
    const auto* selected = state_->active ? preset(*state_->active) : nullptr;
    return selected ? selected->room_rolloff : 0.f;
}

void SoundEnvironment::process(std::span<float> stereo, std::uint32_t rate) {
    process(stereo, std::span<const float>{ stereo }, rate);
}

void SoundEnvironment::process(std::span<float> stereo, std::span<const float> send, std::uint32_t rate) {
    if (stereo.size() % 2 || send.size() != stereo.size() || !rate || rate > 384000u)
        throw std::invalid_argument("Invalid stereo reverb format");
    if (!state_->active || stereo.empty())
        return;
    if (!state_->reverb || state_->reverb->rate != rate)
        state_->reverb.emplace(*preset(*state_->active), rate);
    state_->reverb->process(stereo, send);
}

std::uint32_t SoundEnvironment::add_occlusion(const SoundEnvironmentRegion& region) {
    if (!std::isfinite(region.x)
        || !std::isfinite(region.z)
        || !std::isfinite(region.radius)
        || region.radius < 0.f
        || state_->regions.size() >= std::numeric_limits<std::uint32_t>::max())
        return 0;
    do {
        ++state_->next_region;
    } while (!state_->next_region || state_->regions.contains(state_->next_region));
    state_->regions.emplace(state_->next_region, region);
    return state_->next_region;
}

std::uint32_t SoundEnvironment::add_occlusion(const std::array<float, 3>& position, float radius, float height) {
    //653170 drops Y and forwards raw height bits as the opaque region DWORD.
    return add_occlusion({ position[0], position[2], std::bit_cast<std::uint32_t>(height), radius });
}

void SoundEnvironment::remove_occlusion(std::uint32_t region) {
    state_->regions.erase(region);
}

void SoundEnvironment::clear_occlusion() {
    state_->regions.clear();
}

void SoundEnvironment::set_occlusion_enabled(bool enabled, float cutoff) {
    if (!std::isfinite(cutoff) || cutoff < 0.f)
        throw std::invalid_argument("Invalid audio region cutoff");
    state_->occlusion_enabled = enabled;
    state_->radius_cutoff = cutoff;
}

SoundOcclusionParameters SoundEnvironment::occlusion(const std::array<float, 3>& listener,
                                                     const std::array<float, 3>& source) const {
    SoundOcclusionParameters result;
    if (!state_->occlusion_enabled || !finite_position(listener) || !finite_position(source))
        return result;
    const auto dx = static_cast<double>(source[0]) - listener[0], dz = static_cast<double>(source[2]) - listener[2];
    const auto length_squared = dx * dx + dz * dz;
    if (!length_squared)
        return result;
    double best{};
    for (const auto& [id, region] : state_->regions) {
        (void)id;
        if (region.radius < state_->radius_cutoff || region.radius <= 0.f)
            continue;
        const auto radius_squared = static_cast<double>(region.radius) * region.radius;
        const auto source_dx = static_cast<double>(region.x) - source[0],
                   source_dz = static_cast<double>(region.z) - source[2];
        if (source_dx * source_dx + source_dz * source_dz < radius_squared)
            continue;
        const auto projection =
            ((static_cast<double>(region.x) - listener[0]) * dx + (static_cast<double>(region.z) - listener[2]) * dz)
            / length_squared;
        //Native clamps X and Z independently to their endpoint intervals. For
        //finite nondegenerate segments this equals clamping the projection ratio.
        const auto t = std::clamp(projection, 0., 1.);
        const auto nearest_x = listener[0] + t * dx, nearest_z = listener[2] + t * dz;
        const auto distance_x = region.x - nearest_x, distance_z = region.z - nearest_z;
        const auto distance_squared = distance_x * distance_x + distance_z * distance_z;
        if (distance_squared <= radius_squared)
            best = std::max(best, 1. - distance_squared / radius_squared);
    }
    result.scalar = static_cast<float>(best);
    result.eq = std::max(.5f, 1.f - result.scalar);
    result.obstruction_millibels = static_cast<std::int32_t>(std::nearbyint(-2000.f * result.scalar));
    result.occlusion_millibels = static_cast<std::int32_t>(std::nearbyint(-1000.f * result.scalar));
    return result;
}
} //namespace simcoaster
