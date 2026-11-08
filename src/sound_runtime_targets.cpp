#include "sound_frequency.hpp"
#include "sound_runtime_internal.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
using sound_runtime_detail::Flag;
using sound_runtime_detail::Target;

bool SoundRuntime::set_xz_bounds(std::uint32_t id, const SoundTargetXZBounds& bounds) {
    if (bounds.min_x > bounds.max_x || bounds.min_z > bounds.max_z) {
        throw std::invalid_argument("Invalid sound target X/Z bounds");
    }
    auto& state = *state_;
    auto* target = state.find(id);
    if (!target) {
        return false;
    }
    target->cursor.payload_present = true;
    target->payload.min_x = bounds.min_x;
    target->payload.max_z = bounds.max_z;
    target->payload.max_x = bounds.max_x;
    target->payload.min_z = bounds.min_z;
    target->set(Flag::has_bounds);
    state.force_update = true;
    return true;
}

SoundRuntime::State::State(SoundAssetReader reader, std::string root, SoundRuntimeSettings config) : library(std::move(reader), std::move(root)), settings(config) {
    if (!std::isfinite(config.request_coordinate_scale) || config.request_coordinate_scale <= 0 || !config.voice_capacity) {
        throw std::invalid_argument("Invalid sound runtime settings");
    }
}

SoundRuntime::State::Target* SoundRuntime::State::find(std::uint32_t id) {
    const auto slot = id & 65535u;
    return id && slot < slots.size() && slots[slot].id == id ? slots[slot].target.get() : nullptr;
}

const SoundRuntime::State::Target* SoundRuntime::State::find(std::uint32_t id) const {
    const auto slot = id & 65535u;
    return id && slot < slots.size() && slots[slot].id == id ? slots[slot].target.get() : nullptr;
}

bool SoundRuntime::State::grow_pool() {
    if (slots.size() + 100 > 65536) {
        return false;
    }
    const auto begin = slots.size();
    slots.resize(begin + 100);
    for (auto i = begin; i < slots.size(); ++i) {
        slots[i].id = static_cast<std::uint32_t>(i);
        free_slots.push_back(static_cast<std::uint16_t>(i));
    }
    return true;
}

void SoundRuntime::State::configure(Target& t) {
    const auto flags = t.descriptor->parameters.flags;
    t.flags = 0;
    if (flags & 8u) {
        t.set(Flag::persistent);
        t.set(Flag::loop);
    }
    if (flags & 4u) {
        t.set(Flag::persistent);
    }
    if (flags & 512u) {
        t.set(Flag::unpositioned);
    }
    if (flags & 32u) {
        t.set(Flag::descriptor_packet);
    }
    if (flags & 16384u) {
        t.set(Flag::output_mode4);
    }
    if (std::isfinite(t.parameter) && t.parameter != 0) {
        t.set(Flag::parameter_valid);
    }
}

SoundSelectionCursor SoundRuntime::State::value_cursor(const Target& t) const {
    auto result = t.cursor;
    if (t.parent) {
        result.values = t.parent->cursor.values;
        result.payload_present = t.parent->cursor.payload_present;
    }
    return result;
}

bool SoundRuntime::State::advance_selection(Target& t, bool avoid) {
    if (!t.request_list.empty()) {
        t.set(Flag::persistent);
        if (t.next_request >= t.request_list.size()) {
            return false;
        }
        const auto& entry = t.request_list[t.next_request++];
        if (!entry.catalog || !entry.descriptor) {
            return false;
        }
        t.catalog = entry.catalog;
        t.catalog_slot = entry.slot;
        t.descriptor = entry.descriptor;
        const auto selected = select_sound_record(*t.descriptor, t.cursor, random, false);
        if (!selected) {
            error(selected.error());
            return false;
        }
        return *selected;
    }
    std::expected<bool, SoundError> selected;
    match (t.kind) {
        case SoundTargetKind::sequential => {
            selected = advance_sound_sequential_record(*t.descriptor, t.cursor, t.has(Flag::loop));
        }
        case SoundTargetKind::playlist => {
            t.cursor.selection = select_sound_playlist_record(*t.descriptor, t.playlist, t.has(Flag::loop));
            selected = t.cursor.selection.has_value();
        }
        case SoundTargetKind::conditional_sequence => {
            selected = select_sound_conditional_record(*t.descriptor, t.cursor, random, t.cursor.values[0]);
        }
        case SoundTargetKind::conditional_child => {
            t.cursor.selection = t.parent->cursor.selection;
            selected = select_sound_conditional_record(*t.descriptor, t.cursor, random, t.parent->cursor.values[0]);
            t.parent->cursor.selection = t.cursor.selection;
            if (!selected || !*selected) {
                if (t.cursor.values[0] == 127) {
                    t.parent->cursor.values[0] = 127;
                    t.parent->set(Flag::suspended);
                }
            }
        }
        case _ => selected = select_sound_record(*t.descriptor, t.cursor, random, avoid);
    } if (!selected) {
        error(selected.error());
        return false;
    }
    if (!*selected) {
        if (t.cursor.values[0] == 127) {
            t.set(Flag::suspended);
        }
        return false;
    }
    refresh_sound_selection_keys(*t.descriptor, t.cursor);
    return true;
}

bool SoundRuntime::State::select_sample(Target& t, bool avoid) {
    const auto chosen = select_sound_sample(*t.descriptor, t.cursor, random, avoid);
    if (!chosen) {
        error(chosen.error());
        return false;
    }
    return *chosen;
}

bool SoundRuntime::State::bind(Target& t) {
    configure(t);
    const auto initial = t.descriptor->parameters.initial_key;
    if (initial) {
        t.cursor.payload_present = true;
        t.cursor.keys[0] = static_cast<std::uint8_t>(initial);
    }
    if (t.emitter() || t.kind == SoundTargetKind::playlist) {
        t.cursor.payload_present = true;
    }
    if (!advance_selection(t, false) && !t.cursor.selection) {
        return false;
    }
    refresh_sound_selection_keys(*t.descriptor, t.cursor);
    if (!t.sample() && !select_sample(t, false)) {
        return false;
    }
    t.scheduled_tick = now + t.sample()->clock_offset_ms + 250u;
    return true;
}

std::uint32_t SoundRuntime::State::create(const sound_runtime_detail::QueuedRequest& request, std::vector<Target::RequestEntry> requests) {
    if (free_slots.empty() && !grow_pool()) {
        return 0;
    }
    const auto index = free_slots.back();
    free_slots.pop_back();
    auto& slot = slots[index];
    slot.id += (++generation) << 16;
    auto target = std::make_unique<Target>();
    target->id = slot.id;
    target->catalog = request.catalog;
    target->catalog_slot = request.catalog_slot;
    target->descriptor = request.descriptor;
    target->position = request.position;
    target->parameter = request.parameter;
    target->request_list = std::move(requests);
    target->playlist.random = now;
    target->channel_mask = target->catalog->effects.groups.front().parameters.channel_mask;
    const auto flags = target->descriptor->parameters.flags;
    if (flags & 4u) {
        if (flags & 16u) {
            target->kind = SoundTargetKind::sequential;
        } else if (flags & 2u) {
            target->kind = flags & 1024u ? SoundTargetKind::conditional_sequence : flags & 256u ? SoundTargetKind::playlist : SoundTargetKind::weighted_sequence;
        } else {
            target->kind = flags & 1024u ? SoundTargetKind::conditional_emitter : SoundTargetKind::emitter;
        }
    }
    if (!target->request_list.empty()) {
        target->kind = SoundTargetKind::weighted_sequence;
    }
    if (!bind(*target) && !target->emitter()) {
        target->set(Flag::recycle);
    }
    slot.target = std::move(target);
    active.insert(active.begin(), index);
    force_update = true;
    return slot.id;
}

void SoundRuntime::State::stop_voice(Target& t, bool detach) {
    if (t.voice) {
        if (detach) {
            if (auto it = voices.find(t.voice); it != voices.end()) {
                it->second.target = nullptr;
            }
        } else {
            mixer.stop(t.voice);
            voices.erase(t.voice);
        }
    }
    t.voice = 0;
    t.set(Flag::playing, false);
}

void SoundRuntime::State::stop_children(Target& t, bool detach) {
    for (auto& child : t.children) {
        stop_voice(*child, detach);
        stop_children(*child, detach);
    }
    t.children.clear();
}

void SoundRuntime::State::retire(std::uint16_t index) {
    auto& slot = slots[index];
    auto& target = *slot.target;
    stop_children(target);
    stop_voice(target);
    slot.target.reset();
    //Native8A3000 clears the tag before89D8A0 recycles this slot.
    slot.id &= 65535u;
    free_slots.push_back(index);
    std::erase(active, index);
}

float SoundRuntime::State::radius(const Target& t) const {
    if (t.has(Flag::parameter_valid)) {
        return settings.request_coordinate_scale * t.parameter;
    }
    return t.selection() ? t.selection()->parameters.request_scalar : 0.f;
}

bool SoundRuntime::State::channels_allow(std::uint8_t mask) const {
    for (std::size_t i = 0; i < 4; ++i) {
        if ((mask & (1u << i)) && !channel_enabled[i]) {
            return false;
        }
    }
    return true;
}

bool SoundRuntime::State::expired(const Target& t) const {
    return !t.paused && !t.has(Flag::persistent) && now > t.scheduled_tick + t.interval;
}

bool SoundRuntime::State::in_range(Target& t) {
    const auto listener = mixer.listener().position;
    if (t.has(Flag::has_bounds)) {
        const auto scale = settings.request_coordinate_scale;
        t.position = { std::clamp(listener[0] / scale, static_cast<float>(t.payload.min_x), static_cast<float>(t.payload.max_x)), listener[1] / scale, std::clamp(listener[2] / scale, static_cast<float>(t.payload.min_z), static_cast<float>(t.payload.max_z)) };
    }
    const auto dx = t.position[0] * settings.request_coordinate_scale - listener[0], dz = t.position[2] * settings.request_coordinate_scale - listener[2];
    const auto distance = dx * dx + dz * dz;
    const auto limit = radius(t);
    const auto ranked = std::nearbyint(static_cast<double>(static_cast<float>(distance * .01f)));
    const auto low = distance <= 0 ? 65535u : ranked >= 0 && ranked <= 65535 ? 65535u - static_cast<std::uint32_t>(ranked) : t.priority & 65535u;
    t.priority = (t.descriptor->parameters.priority << 16) | low;
    if (t.has(Flag::unpositioned) || limit * limit >= distance) {
        t.set(Flag::out_of_range, false);
        return true;
    }
    //The emitter itself has no driver voice; native range culling leaves its
    //child chain and independent voice tails alive until ordinary expiry.
    t.set(Flag::out_of_range);
    stop_voice(t);
    return false;
}

bool SoundRuntime::State::spawn_child(Target& parent) {
    auto child = std::make_unique<Target>();
    child->parent = &parent;
    child->catalog = parent.catalog;
    child->catalog_slot = parent.catalog_slot;
    child->descriptor = parent.descriptor;
    child->channel_mask = parent.channel_mask;
    child->kind = parent.kind == SoundTargetKind::conditional_emitter ? SoundTargetKind::conditional_child : SoundTargetKind::child;
    child->cursor.payload_present = true;
    configure(*child);
    if (!advance_selection(*child, false) || !select_sample(*child, false)) {
        return false;
    }
    auto values = value_cursor(*child);
    child->scheduled_tick = now + choose_sound_interval(*child->descriptor, values, random);
    parent.children.push_back(std::move(child));
    refresh_sound_selection_keys(*parent.descriptor, parent.cursor);
    return true;
}

void SoundRuntime::State::update_controls(Target& t, bool level) {
    if (!t.voice || !mixer.contains(t.voice)) {
        return;
    }
    auto values = value_cursor(t);
    if (level && !t.has(Flag::fading)) {
        t.controls.level = static_cast<float>(choose_sound_level(*t.descriptor, values, random));
    }
    const auto frequency_parameter = choose_sound_frequency_parameter(*t.descriptor, values, random);
    if (const auto ratio = sound_detail::frequency_ratio(frequency_parameter)) {
        t.controls.frequency_ratio = *ratio;
    } else {
        //A live voice retains its last valid pitch. Native out-of-table reads
        //have no defined coefficient; do not replace it with an invented ratio.
        error({ SoundFailure::selection, t.sample()->token, t.catalog->path + ": frequency parameter " + std::to_string(frequency_parameter) });
    }
    const auto& position = t.parent ? t.parent->position : t.position;
    for (std::size_t i = 0; i < 3; ++i) {
        t.controls.position[i] = position[i] * settings.request_coordinate_scale;
    }
    t.controls.high_frequency_gain = t.controls.positioned ? environment.occlusion(mixer.listener().position, t.controls.position).eq : 1.f;
    mixer.update(t.voice, t.controls);
}

bool SoundRuntime::State::start_voice(Target& t, bool continuation) {
    if (!t.sample() || !t.selection()) {
        t.set(Flag::recycle);
        return false;
    }
    const auto* record = t.sample();
    auto decoded = library.sample(*t.catalog, *record);
    if (!decoded) {
        error(decoded.error());
        t.set(Flag::recycle);
        return false;
    }
    if (!(*decoded)->frames()) {
        error({ SoundFailure::sample_format, record->token, t.catalog->path });
        t.set(Flag::recycle);
        return false;
    }
    auto header = library.sample_data(*t.catalog, *record);
    if (!header) {
        error(header.error());
        t.set(Flag::recycle);
        return false;
    }
    auto values = value_cursor(t);
    SoundVoiceParameters controls = continuation ? t.controls : SoundVoiceParameters {};
    if (t.sequence() && (t.selection()->parameters.minimum_interval || t.selection()->parameters.maximum_interval)) {
        controls.silence_ms = choose_sound_interval(*t.descriptor, values, random);
    } else {
        controls.silence_ms = 0;
    }
    if (!continuation) {
        controls.level = static_cast<float>(t.has(Flag::level_override) ? t.level_override : choose_sound_level(*t.descriptor, values, random));
        const auto frequency_parameter = choose_sound_frequency_parameter(*t.descriptor, values, random);
        const auto ratio = sound_detail::frequency_ratio(frequency_parameter);
        if (!ratio) {
            error({ SoundFailure::selection, record->token, t.catalog->path + ": frequency parameter " + std::to_string(frequency_parameter) });
            t.set(Flag::recycle);
            return false;
        }
        controls.frequency_ratio = *ratio;
        controls.channel_mask = t.channel_mask;
        controls.positioned = !t.has(Flag::unpositioned);
        controls.loop_count = t.has(Flag::loop) && !t.sequence() ? 9999u : 1u;
        if (controls.loop_count > 1 && (*header)->header.extent && (*header)->header.loop_end) {
            const auto bytes_per_frame = ((*header)->header.bits_per_sample == 16 ? 2u : 1u) * (*decoded)->channels;
            controls.loop_begin_frame = (*header)->header.extent / bytes_per_frame;
            controls.loop_end_frame = (*header)->header.loop_end / bytes_per_frame;
        }
        controls.min_distance = 2.f;
        controls.max_distance = std::max(radius(t), 2.1f);
        const auto& position = t.parent ? t.parent->position : t.position;
        for (std::size_t i = 0; i < 3; ++i) {
            controls.position[i] = position[i] * settings.request_coordinate_scale;
        }
        if (!t.parent) {
            t.interval = choose_sound_interval(*t.descriptor, values, random);
            if (expired(t)) {
                return false;
            }
            const auto sample_period = record->clock_offset_ms;
            const auto elapsed = now - (t.scheduled_tick - sample_period - 250u);
            if (!sample_period) {
                error({ SoundFailure::selection, record->token, t.catalog->path });
                t.set(Flag::recycle);
                return false;
            }
            if (!t.has(Flag::persistent) && sample_period > 1000u && elapsed % sample_period > 1000u && sample_period - elapsed % sample_period < 1000u) {
                return false;
            }
        }
    }
    controls.high_frequency_gain = controls.positioned ? environment.occlusion(mixer.listener().position, controls.position).eq : 1.f;
    const auto priority = t.parent ? t.parent->priority : t.priority;
    if (mixer.active_voices() >= settings.voice_capacity) {
        auto victim = voices.end();
        for (auto it = voices.begin(); it != voices.end(); ++it) {
            if (mixer.contains(it->first) && (victim == voices.end() || it->second.priority < victim->second.priority)) {
                victim = it;
            }
        }
        if (victim == voices.end() || victim->second.priority >= priority) {
            return false;
        }
        if (victim->second.target) {
            auto* owner = victim->second.target;
            owner->voice = 0;
            if (!owner->parent) {
                owner->set(Flag::playing, false);
            }
        }
        mixer.stop(victim->first);
        voices.erase(victim);
    }
    t.controls = controls;
    t.voice = mixer.start(*decoded, controls);
    if (!t.voice) {
        return false;
    }
    t.set(Flag::playing);
    voices.emplace(t.voice, sound_runtime_detail::VoiceOwner { &t, priority });
    if (t.paused) {
        mixer.pause(t.voice);
    }
    return true;
}
} //namespace simcoaster
