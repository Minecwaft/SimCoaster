#include "sound_runtime_internal.hpp"
#include <format>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
using sound_runtime_detail::Flag;
using sound_runtime_detail::Target;

void SoundRuntime::State::collect_completions() {
    std::vector<Target*> completed;
    for (auto it = voices.begin(); it != voices.end();) {
        if (mixer.contains(it->first)) {
            ++it;
            continue;
        }
        auto* target = it->second.target;
        if (target && target->voice == it->first) {
            target->voice = 0;
            //Child readers have no timing callback. Their native playing bit stays
            //set until metadata expiry, even when the independent voice has ended.
            if (!target->parent) {
                target->set(Flag::playing, false);
                completed.push_back(target);
            }
        }
        it = voices.erase(it);
    }
    for (auto* target : completed) {
        if (target->has(Flag::recycle) || target->has(Flag::out_of_range) || target->has(Flag::channel_suspended)) {
            continue;
        }
        if (target->sequence()) {
            if (advance_selection(*target, true) && select_sample(*target, true)) {
                if (!start_voice(*target, true) && !target->has(Flag::suspended)) {
                    target->set(Flag::recycle);
                }
            } else if (!target->has(Flag::suspended)) {
                target->set(Flag::recycle);
            }
        } else if (!target->parent) {
            target->set(Flag::recycle);
        }
    }
}

void SoundRuntime::State::process_emitter(Target& parent) {
    if (parent.paused || parent.has(Flag::suspended)) {
        return;
    }
    if (parent.children.empty()) {
        if (parent.cursor.keys[0] && !parent.has(Flag::keys_initialized)) {
            parent.cursor.selection.reset();
            parent.set(Flag::keys_initialized);
        }
        if (!spawn_child(parent)) {
            return;
        }
    }
    //Native52F0 captures next before starting/creating a successor. A newly
    //appended child starts on the following manager pass, not recursively now.
    const auto existing = parent.children.size();
    std::size_t processed {};
    for (auto it = parent.children.begin(); it != parent.children.end() && processed < existing; ++it, ++processed) {
        auto& child = **it;
        if (!child.voice && !child.has(Flag::playing) && !child.has(Flag::recycle)) {
            start_voice(child);
        }
        if (now > child.scheduled_tick && std::next(it) == parent.children.end()) {
            spawn_child(parent);
        }
    }
    for (auto it = parent.children.begin(); it != parent.children.end();) {
        if (now < (*it)->scheduled_tick) {
            ++it;
            continue;
        }
        //A child source contains no timing callback to its metadata target. Its
        //PCM reader/provider lives with the output voice and survives this unlink.
        stop_voice(**it, true);
        it = parent.children.erase(it);
    }
}

void SoundRuntime::State::update_targets() {
    collect_completions();
    if (last_update && *last_update == now && !force_update) {
        return;
    }
    last_update = now;
    force_update = false;
    auto pending = std::move(queued);
    queued.clear();
    for (const auto& request : pending) {
        (void)create(request);
    }
    const auto current = active;
    std::vector<Target*> eligible;
    for (const auto index : current) {
        auto* target = slots[index].target.get();
        if (!target) {
            continue;
        }
        auto& t = *target;
        if (t.has(Flag::recycle) || expired(t)) {
            retire(index);
            continue;
        }
        if (t.has(Flag::retire_voice)) {
            t.set(Flag::recycle);
            stop_children(t);
            stop_voice(t);
            continue;
        }
        if (t.has(Flag::suspended)) {
            continue;
        }
        if (!channels_allow(t.channel_mask)) {
            t.set(Flag::channel_suspended, (t.descriptor->parameters.flags & 12u) != 0);
            stop_voice(t);
            if (!t.has(Flag::channel_suspended)) {
                t.set(Flag::recycle);
            }
            continue;
        }
        t.set(Flag::channel_suspended, false);
        if (t.paused) {
            continue;
        }
        if (t.has(Flag::fading) && settings.fading_enabled) {
            if (!t.voice || t.fade_parameter == 100) {
                if (t.voice) {
                    t.controls.level = 0;
                    mixer.update(t.voice, t.controls);
                }
                t.set(Flag::retire_voice);
                continue;
            }
            float product = t.controls.level;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (t.channel_mask & (1u << channel)) {
                    product *= static_cast<float>(channel_level[channel]) * .01f;
                }
            }
            const auto next = std::trunc(product * static_cast<float>(t.fade_parameter) * .01f);
            //8ADAC0 stores this as the new base AND sends this already-scaled level
            //directly to the driver. Do not apply channel scalars a second time.
            t.controls.level = std::max(0.f, next);
            t.controls.channel_mask = 0;
            mixer.update(t.voice, t.controls);
            if (next <= 0) {
                t.set(Flag::fading, false);
                t.set(Flag::retire_voice);
            }
            continue;
        }
        if (in_range(t)) {
            if (auto voice = voices.find(t.voice); voice != voices.end()) {
                voice->second.priority = t.priority;
            }
            eligible.push_back(&t);
        }
    }
    std::stable_sort(eligible.begin(), eligible.end(), [](const Target* a, const Target* b) {
        return a->priority > b->priority;
    });
    if (eligible.size() > settings.voice_capacity) {
        eligible.resize(settings.voice_capacity);
    }
    for (auto* t : eligible) {
        if (t->emitter()) {
            process_emitter(*t);
        } else if (!t->voice && !t->has(Flag::recycle)) {
            start_voice(*t);
        }
    }
    library.collect_released();
}

void SoundRuntime::State::render(std::span<float> stereo, std::uint32_t rate) {
    if (!rate || stereo.size() % 2) {
        throw std::invalid_argument("Invalid sound render block");
    }
    std::size_t cursor {};
    update_targets();
    while (cursor < stereo.size()) {
        //Work at bounded presentation quanta while splitting exactly at source
        //EOF. This preserves callback-driven sequences without inserting zeros
        //until the next World frame or tying sample timing to a 32-bit driver.
        auto frames = std::min<std::size_t>((stereo.size() - cursor) / 2, std::max<std::uint32_t>(rate / 100u, 1u));
        if (const auto end = mixer.frames_until_completion(rate)) {
            if (!*end) {
                mixer.render({}, rate);
                collect_completions();
                continue;
            }
            frames = std::min<std::size_t>(frames, *end);
        }
        auto block = stereo.subspan(cursor, frames * 2);
        wet_buffer.resize(block.size());
        mixer.set_reverb_room_rolloff(environment.room_rolloff());
        mixer.render(block, wet_buffer, rate);
        environment.process(block, wet_buffer, rate);
        const auto milliseconds = static_cast<double>(frames) * 1000.0 / rate + fractional_ms;
        const auto whole = static_cast<std::uint64_t>(milliseconds);
        fractional_ms = milliseconds - static_cast<double>(whole);
        now += static_cast<std::uint32_t>(whole);
        cursor += block.size();
        update_targets();
    }
    if (stereo.empty()) {
        mixer.render({}, rate);
        collect_completions();
    }
}

SoundRuntime::SoundRuntime(SoundAssetReader reader, std::string root, SoundRuntimeSettings settings) : state_(std::make_unique<State>(std::move(reader), std::move(root), settings)) {}

SoundRuntime::~SoundRuntime() = default;

SoundCatalogLibrary& SoundRuntime::catalogs() {
    return state_->library;
}

const SoundCatalogLibrary& SoundRuntime::catalogs() const {
    return state_->library;
}

std::uint32_t SoundRuntime::submit(std::uint32_t previous, std::uint32_t catalog, std::uint32_t key, const std::array<float, 3>& position, float parameter) {
    auto& s = *state_;
    if (!catalog || !key) {
        return previous;
    }
    auto asset = s.library.catalog(catalog);
    if (!asset) {
        return previous;
    }
    const auto* descriptor = asset->find(key);
    if (!descriptor) {
        return previous;
    }
    if (!std::ranges::all_of(position, [](float value) {
            return std::isfinite(value);
        })) {
        return previous;
    }
    if (!(descriptor->parameters.flags & 12u) && !s.channels_allow(asset->effects.groups.front().parameters.channel_mask)) {
        return previous;
    }
    if (auto* current = s.find(previous); current && !current->has(Flag::retire_voice)) {
        if ((current->priority >> 16) >= descriptor->parameters.priority) {
            return previous;
        }
        current->set(Flag::retire_voice);
    }
    sound_runtime_detail::QueuedRequest request { std::move(asset), descriptor, catalog, position, parameter };
    if (descriptor->parameters.flags & 1u) {
        s.queued.push_back(std::move(request));
        s.force_update = true;
        return previous;
    }
    return s.create(request);
}

std::uint32_t SoundRuntime::submit_sequence(std::uint32_t previous, std::span<const SoundRequestSelection> selections, const std::array<float, 3>& position, float parameter) {
    auto& s = *state_;
    if (selections.empty() || !selections.front().catalog || !selections.front().key) {
        return previous;
    }
    auto asset = s.library.catalog(selections.front().catalog);
    if (!asset) {
        return previous;
    }
    const auto* descriptor = asset->find(selections.front().key);
    if (!descriptor) {
        return previous;
    }
    if (descriptor->parameters.flags & 1u) {
        return submit(previous, selections.front().catalog, selections.front().key, position, parameter);
    }
    if (!std::ranges::all_of(position, [](float value) {
            return std::isfinite(value);
        })) {
        return previous;
    }
    if (!(descriptor->parameters.flags & 12u) && !s.channels_allow(asset->effects.groups.front().parameters.channel_mask)) {
        return previous;
    }
    if (auto* current = s.find(previous); current && !current->has(Flag::retire_voice)) {
        if ((current->priority >> 16) >= descriptor->parameters.priority) {
            return previous;
        }
        current->set(Flag::retire_voice);
    }
    std::vector<Target::RequestEntry> requests;
    requests.reserve(selections.size());
    for (const auto selection : selections) {
        auto entry = s.library.catalog(selection.catalog);
        const auto* selected = entry && selection.key ? entry->find(selection.key) : nullptr;
        requests.push_back({ std::move(entry), selected, selection.catalog });
    }
    return s.create({ std::move(asset), descriptor, selections.front().catalog, position, parameter }, std::move(requests));
}

std::uint32_t SoundRuntime::operation(std::uint32_t id, std::int32_t operation, std::int32_t value) {
    auto& s = *state_;
    auto* t = s.find(id);
    if (!t) {
        return 0;
    }
    const auto key = static_cast<std::uint8_t>(operation), byte = static_cast<std::uint8_t>(value);
    t->cursor.payload_present = true;
    bool notify {};
    for (std::size_t i = 0; i < 4; ++i) {
        if (t->cursor.keys[i] == key) {
            t->cursor.values[i] = byte;
            if (i) {
                notify = true;
            }
        }
    }
    if (notify && !t->has(Flag::channel_suspended)) {
        s.update_controls(*t);
        for (auto& child : t->children) {
            s.update_controls(*child);
        }
    }
    if (t->has(Flag::suspended) && t->cursor.values[0] != 127) {
        if (t->emitter()) {
            t->set(Flag::suspended, false);
        } else if (t->kind == SoundTargetKind::conditional_sequence && s.advance_selection(*t, true) && s.select_sample(*t, true)) {
            t->set(Flag::suspended, false);
        }
    }
    s.force_update = true;
    return id;
}

std::uint32_t SoundRuntime::set_position(std::uint32_t id, const std::array<float, 3>& position) {
    auto& s = *state_;
    auto* t = s.find(id);
    if (!t || !std::ranges::all_of(position, [](float value) {
            return std::isfinite(value);
        })) {
        return 0;
    }
    t->position = position;
    const auto update = [&](Target& target) {
        if (!target.voice) {
            return;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            target.controls.position[i] = position[i] * s.settings.request_coordinate_scale;
        }
        target.controls.high_frequency_gain = target.controls.positioned ? s.environment.occlusion(s.mixer.listener().position, target.controls.position).eq : 1.f;
        s.mixer.update(target.voice, target.controls);
    };
    update(*t);
    for (auto& child : t->children) {
        update(*child);
    }
    s.force_update = true;
    return id;
}

void SoundRuntime::recycle(std::uint32_t id) {
    if (auto* t = state_->find(id)) {
        t->set(Flag::recycle);
        state_->stop_children(*t);
        state_->stop_voice(*t);
        state_->force_update = true;
    }
}

void SoundRuntime::fade(std::uint32_t id, std::uint8_t parameter) {
    auto& s = *state_;
    if (!s.settings.fading_enabled) {
        recycle(id);
        return;
    }
    if (auto* t = s.find(id)) {
        t->fade_parameter = parameter;
        t->set(Flag::fading);
        s.force_update = true;
    }
}

void SoundRuntime::pause(std::uint32_t id) {
    auto& s = *state_;
    if (auto* t = s.find(id); t && !t->paused) {
        t->paused = true;
        t->pause_tick = s.now;
        if (t->voice) {
            s.mixer.pause(t->voice);
        }
        for (auto& child : t->children) {
            child->paused = true;
            if (child->voice) {
                s.mixer.pause(child->voice);
            }
        }
    }
}

void SoundRuntime::restart(std::uint32_t id) {
    auto& s = *state_;
    if (auto* t = s.find(id); t && t->paused) {
        const auto delta = s.now - t->pause_tick;
        t->scheduled_tick += delta;
        t->paused = false;
        if (t->voice) {
            s.mixer.restart(t->voice);
        }
        for (auto& child : t->children) {
            child->paused = false;
            child->scheduled_tick += delta;
            if (child->voice) {
                s.mixer.restart(child->voice);
            }
        }
    }
}

void SoundRuntime::set_level(std::uint32_t id, std::uint32_t level) {
    auto& s = *state_;
    if (auto* t = s.find(id)) {
        if (t->voice) {
            t->controls.level = static_cast<float>(level);
            s.mixer.update(t->voice, t->controls);
        } else {
            t->level_override = static_cast<std::uint16_t>(level);
            t->set(Flag::level_override);
        }
    }
}

std::uint32_t SoundRuntime::sample_time(std::uint32_t id) const {
    const auto* t = state_->find(id);
    return t && t->sample() ? t->sample()->clock_offset_ms : 1;
}

void SoundRuntime::set_listener(const std::array<float, 3>& position, const std::array<float, 3>& forward) {
    const bool changed = state_->mixer.listener().position != position;
    const std::array<float, 3> up = std::abs(forward[0]) + std::abs(forward[2]) < 1.e-6f ? std::array<float, 3> { 0, 0, 1 } : std::array<float, 3> { 0, 1, 0 };
    state_->mixer.set_listener({ position, forward, up });
    state_->force_update = true;
    if (changed) {
        state_->refresh_environment();
    }
}

void SoundRuntime::State::update_channel(std::uint32_t channel) {
    mixer.set_channel(channel, channel == 1 && music_muted ? 0.f : static_cast<float>(channel_level[channel]), channel_enabled[channel]);
    //A channel refresh recalculates output from each voice's saved base level.
    for (const auto& [key, owner] : voices) {
        if (owner.target && owner.target->controls.channel_mask != owner.target->channel_mask) {
            owner.target->controls.channel_mask = owner.target->channel_mask;
            mixer.update(key, owner.target->controls);
        }
    }
    force_update = true;
}

void SoundRuntime::set_channel_enabled(std::uint32_t channel, bool value) {
    if (channel >= 4) {
        throw std::out_of_range("Sound channel");
    }
    state_->channel_enabled[channel] = value;
    state_->update_channel(channel);
}

void SoundRuntime::set_channel_level(std::uint32_t channel, std::uint32_t value) {
    if (channel >= 4) {
        throw std::out_of_range("Sound channel");
    }
    state_->channel_level[channel] = value;
    state_->update_channel(channel);
}

void SoundRuntime::set_voice_capacity(std::uint32_t count) {
    if (!count) {
        throw std::invalid_argument("Sound capacity");
    }
    state_->settings.voice_capacity = count;
    state_->force_update = true;
}

void SoundRuntime::set_request_coordinate_scale(float scale) {
    if (!std::isfinite(scale) || scale <= 0) {
        throw std::invalid_argument("Sound coordinate scale");
    }
    state_->settings.request_coordinate_scale = scale;
    state_->force_update = true;
}

void SoundRuntime::set_fading_enabled(bool value) {
    state_->settings.fading_enabled = value;
}

void SoundRuntime::set_music_muted(bool value) {
    state_->music_muted = value;
    state_->update_channel(1);
}

void SoundRuntime::set_reverb_preset(std::uint32_t preset) {
    state_->environment.set_reverb_preset(preset);
}

std::expected<void, SoundError> SoundRuntime::load_reverb_presets(std::span<const std::byte> bytes) {
    return state_->environment.load_presets(bytes);
}

void SoundRuntime::set_occlusion_enabled(bool enabled, float cutoff) {
    state_->environment.set_occlusion_enabled(enabled, cutoff);
    if (enabled != state_->occlusion_enabled) {
        state_->occlusion_enabled = enabled;
        state_->refresh_environment();
    }
}

void SoundRuntime::State::refresh_environment() {
    for (const auto& [key, owner] : voices) {
        if (auto status = mixer.status(key)) {
            auto controls = status->parameters;
            controls.high_frequency_gain = controls.positioned ? environment.occlusion(mixer.listener().position, controls.position).eq : 1.f;
            if (owner.target) {
                owner.target->controls = controls;
            }
            mixer.update(key, controls);
        }
    }
}

std::uint32_t SoundRuntime::add_occlusion(const SoundOcclusionVolume& volume) {
    const auto id = state_->environment.add_occlusion(volume.position, volume.radius, volume.height);
    state_->refresh_environment();
    return id;
}

void SoundRuntime::remove_occlusion(std::uint32_t id) {
    state_->environment.remove_occlusion(id);
    state_->refresh_environment();
}

bool SoundRuntime::enable_output(bool enabled) {
    if (!enabled) {
        state_->output.reset();
        return true;
    }
    if (!state_->output) {
        state_->output = std::make_unique<SoundOutputWin32>();
    } else if (!state_->output->available()) {
        return state_->output->reopen();
    }
    return state_->output->available();
}

std::string SoundRuntime::output_diagnostic() const {
    if (!state_->output) {
        return "Audio output is disabled";
    }
    const auto status = state_->output->status();
    return status.available ? std::string {} : std::format("WASAPI unavailable (HRESULT {})", status.hresult);
}

void SoundRuntime::advance(std::chrono::milliseconds elapsed) {
    if (elapsed.count() < 0) {
        throw std::invalid_argument("Negative audio duration");
    }
    constexpr std::uint32_t rate = 48000;
    std::array<float, 960> block {};
    const auto device = state_->output ? state_->output->status() : SoundOutputStatus {};
    //The endpoint consumes continuously while the application renders. Mixing
    //only the previous frame's elapsed milliseconds keeps it perpetually on
    //the edge of underrun, and rounding each frame loses samples indefinitely.
    //Keep 125 ms of owned PCM ahead of the endpoint instead. Refilling from
    //actual consumption also recovers from a stall without queuing obsolete
    //catch-up audio or advancing voices twice by an additional frame duration.
    constexpr std::size_t lead_frames = rate / 8;
    if (!device.available && static_cast<std::uint64_t>(elapsed.count()) > std::numeric_limits<std::uint64_t>::max() / (rate / 1000u)) {
        throw std::overflow_error("Audio duration exceeds sample timeline");
    }
    auto remaining = device.available ? static_cast<std::uint64_t>(lead_frames - std::min(lead_frames, device.queued_frames)) : static_cast<std::uint64_t>(elapsed.count()) * (rate / 1000u);
    if (!remaining) {
        state_->render({}, rate);
    }
    while (remaining) {
        const auto frames = std::min<std::uint64_t>(remaining, block.size() / 2);
        auto output = std::span<float> { block }.first(static_cast<std::size_t>(frames) * 2);
        state_->render(output, rate);
        if (device.available && !state_->output->submit(output)) {
            break; //A device failure is handled by the software timeline next frame.
        }
        remaining -= frames;
    }
}

void SoundRuntime::render(std::span<float> stereo, std::uint32_t rate) {
    state_->render(stereo, rate);
}

SoundRuntimeStatistics SoundRuntime::statistics() const {
    SoundRuntimeStatistics result;
    result.targets = state_->active.size();
    result.voices = state_->mixer.active_voices();
    result.pool_slots = state_->slots.size();
    result.failed_requests = state_->errors.size();
    for (const auto index : state_->active) {
        result.children += state_->slots[index].target->children.size();
    }
    return result;
}

const std::vector<SoundError>& SoundRuntime::diagnostics() const {
    return state_->errors;
}

void SoundRuntime::clear_diagnostics() {
    state_->errors.clear();
}
} //namespace simcoaster
