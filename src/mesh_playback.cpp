#include "simcoaster/mesh_playback.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
constexpr double frame_ms = static_cast<double>(33.333332f), frames_per_ms = static_cast<double>(0.03f);

constexpr auto event(MeshPlaybackEvent value) {
    return std::to_underlying(value);
}

[[noreturn]] void fail(MeshFailure reason, std::size_t index = 0) {
    throw MeshError { reason, index, {}, {} };
}

void require(bool value, std::size_t index = 0) {
    if (!value) {
        fail(MeshFailure::animation_binding, index);
    }
}

MeshMilliseconds milliseconds(double value) {
    if (!std::isfinite(value) || value <= static_cast<double>(std::numeric_limits<std::int64_t>::min()) || value >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        fail(MeshFailure::animation_frame);
    }
    return MeshMilliseconds { static_cast<std::int64_t>(value) };
}

MeshMilliseconds clock(const MeshPlaybackControl& control, const MeshPlaybackClocks& clocks) {
    return (control.flags & 64u) ? clocks.unscaled : clocks.simulation;
}

const MeshAnimation* clip(const MeshPlaybackInstance& instance, MeshClipSelection selection) {
    return instance.assets ? instance.assets->animations.find(selection.category, selection.variant) : nullptr;
}

void valid_selection(const MeshPlaybackInstance& instance, MeshClipSelection selection, bool requests = false) {
    require(selection.category <= (requests ? mesh_request_hold_last : mesh_request_clear), selection.category);
    if (selection.active() && !instance.assets->animations.groups[selection.category].empty()) {
        require(clip(instance, selection) != nullptr, selection.variant);
    }
}

void active_count(MeshPlaybackInstance& instance) {
    if (instance.runtime_flags & 0x1000000u) {
        instance.active_control_count = 1;
    } else {
        instance.active_control_count = 0;
        for (const auto& control : instance.controls) {
            if (control.clip.active()) {
                ++instance.active_control_count;
            }
        }
    }
    if (instance.active_control_count) {
        instance.model.flags |= 0x80000u;
    } else {
        instance.model.flags &= ~0x80000u;
    }
}

void mark_binding(MeshPlaybackInstance& instance, const MeshAnimation& animation) {
    for (const auto& entry : animation.entries) {
        require(entry.target_node < instance.model.nodes.size(), entry.target_node);
        auto& node = instance.model.nodes[entry.target_node];
        node.flags |= 0x40000u;
        if (entry.flags & 0x1000u) {
            node.flags |= 0x200000u;
        }
        if (entry.flags & 0x10000u) {
            node.flags |= 0x400000u;
        }
        if (entry.flags & 0x289u) {
            node.flags |= 0x100000u;
        }
    }
    for (auto hidden : animation.hidden_nodes) {
        require(hidden < instance.model.nodes.size(), hidden);
        auto& flags = instance.model.nodes[hidden].flags;
        if (!(flags & 0x80000000u)) {
            flags |= 0x10u;
        }
    }
}

void reconcile(MeshPlaybackInstance& instance, const MeshAnimation& previous, const MeshAnimation* next) {
    auto& model = instance.model;
    const auto& source = instance.assets->model;
    std::vector<std::uint32_t> masks(model.nodes.size());
    if (instance.runtime_flags & 8u) {
        for (auto& node : model.nodes) {
            node.flags &= ~0x10u;
        }
    }
    for (auto index : previous.hidden_nodes) {
        require(index < model.nodes.size(), index);
        if (!(model.nodes[index].flags & 0x80000000u)) {
            model.nodes[index].flags &= ~0x10u;
        }
    }
    const auto clear_mask = (model.flags & 4u) ? 0x800000u : 0xF40000u;
    for (const auto& entry : previous.entries) {
        require(entry.target_node < model.nodes.size(), entry.target_node);
        masks[entry.target_node] = entry.flags;
        auto& flags = model.nodes[entry.target_node].flags;
        flags &= ~clear_mask;
        if (!(flags & 0x80000000u)) {
            flags &= ~0x10u;
        }
    }
    if (next) {
        for (const auto& entry : next->entries) {
            require(entry.target_node < masks.size(), entry.target_node);
            masks[entry.target_node] &= ~entry.flags;
        }
    }
    if (!(instance.runtime_flags & 0x100000u) || (model.flags & 4u)) {
        for (std::size_t i = 0; i < masks.size(); ++i) {
            auto& node = model.nodes[i];
            const auto& original = source.nodes[i];
            const auto mask = masks[i];
            if (mask & 0x289u) {
                for (std::size_t row = 0; row < 4; ++row) {
                    for (std::size_t col = 0; col < 3; ++col) {
                        node.local_transform[row * 4 + col] = original.local_transform[row * 4 + col];
                    }
                }
            }
            if (mask & 0x1000u) {
                require(node.drawable.has_value() && original.drawable.has_value(), i);
                node.drawable->positions = original.drawable->positions;
                node.drawable->bounds = original.drawable->bounds;
            }
            if (mask & 0x10000u) {
                require(node.drawable.has_value() && original.drawable.has_value(), i);
                node.drawable->texture_coordinates = original.drawable->texture_coordinates;
            }
        }
    }
    if ((previous.flags & 2u) && !instance.options.suppress_texture_variants) {
        for (auto& slot : model.textures) {
            if (slot.flags & 0x40u) {
                slot.source_variant = 0;
                slot.flags |= 0x400u;
            }
        }
    }
}

void sample_time(MeshPlaybackControl& control, const MeshPlaybackClocks& clocks) {
    const auto now = clock(control, clocks);
    control.unpaused = now;
    if (control.flags & 2u) {
        control.current = control.start;
        control.frame = 0;
    } else if (control.flags & 4u) {
        control.current = control.start + (control.rate == 0 ? MeshMilliseconds {} : milliseconds(static_cast<double>(control.total_frames) * frame_ms / control.rate));
        control.frame = control.total_frames;
    } else {
        control.current = now;
        const auto elapsed = control.current - control.start;
        if (elapsed.count() < 0) {
            fail(MeshFailure::animation_frame);
        }
        control.frame = static_cast<float>(static_cast<double>(elapsed.count()) * control.rate * frames_per_ms);
    }
}

MeshMilliseconds immediate(MeshPlaybackInstance& instance, std::size_t index, const MeshPlaybackRequest& request, const MeshPlaybackClocks& clocks) {
    auto& control = instance.controls[index];
    valid_selection(instance, request.clip, true);
    const bool hold = request.clip.category == mesh_request_hold_first || request.clip.category == mesh_request_hold_last;
    control.flags = (control.flags & ~(1u | 32u)) | ((request.flags & 1u) ? 1u : 0u) | ((request.flags & 4u) ? 32u : 0u);
    const auto* previous = clip(instance, control.clip);
    const auto* selected = hold ? previous : clip(instance, request.clip);
    if (hold) {
        if (previous) {
            if (request.clip.category == mesh_request_hold_first) {
                control.flags |= 2u;
            } else {
                control.flags |= 20u;
            }
        }
        control.start = clock(control, clocks);
        control.unpaused = control.start;
        //Even an inactive control follows this branch; HOLD_FIRST wins when both
        //native hold bits have accumulated on the control.
        if (control.flags & 2u) {
            control.current = control.start;
            control.frame = 0;
        } else {
            control.current = control.start + (control.rate == 0 ? MeshMilliseconds {} : milliseconds(static_cast<double>(control.total_frames) * frame_ms / control.rate));
            control.frame = control.total_frames;
        }
    } else if (!selected || request.clip.category == mesh_request_clear) {
        if (previous && !(request.flags & 12u)) {
            reconcile(instance, *previous, nullptr);
        }
        control.clip.category = mesh_request_clear;
        control.start = {};
        control.current = {};
        control.unpaused = {};
        control.rate = 1;
        control.frame = 0;
        instance.sampling[index] = {};
    } else {
        require(std::isfinite(request.rate) && request.rate >= 0, index);
        const bool reset = !previous || control.frame <= control.total_frames;
        const auto overshoot = reset ? 0.0f : control.frame - control.total_frames;
        if (previous && !(request.flags & 12u)) {
            reconcile(instance, *previous, selected);
        }
        if (previous) {
            control.flags &= ~6u;
        }
        control.clip = request.clip;
        control.rate = request.rate;
        control.total_frames = selected->frame_span();
        if (reset) {
            control.start = clock(control, clocks);
            control.current = control.start;
            control.unpaused = control.start;
        } else {
            const auto offset = std::min(overshoot, control.total_frames);
            require(control.rate > 0 || offset == 0, index);
            control.start = control.current - (offset == 0 ? MeshMilliseconds {} : milliseconds(static_cast<double>(offset) * frame_ms / control.rate));
            control.unpaused = control.start;
        }
        if (!(instance.runtime_flags & 0x8000u)) {
            control.frame = static_cast<float>(static_cast<double>((control.current - control.start).count()) * control.rate * frames_per_ms);
        }
        //Native transitions retain the last sampled block for visible activation
        //after offscreen requests that suppressed resource binding.
        instance.sampling[index] = {};
        if (!(request.flags & 8u)) {
            mark_binding(instance, *selected);
        }
    }
    active_count(instance);
    const auto* active = clip(instance, control.clip);
    return active ? milliseconds(static_cast<double>(active->frame_span()) * frame_ms) : MeshMilliseconds {};
}

bool first_complete(MeshPlaybackInstance& instance, const MeshPlaybackClocks& clocks) {
    bool has_blocks = false;
    for (const auto& group : instance.assets->animations.groups) {
        has_blocks |= !group.empty();
    }
    if (!has_blocks || (instance.runtime_flags & 0x8000u)) {
        return false;
    }
    if (instance.controls.empty() || !instance.controls[0].clip.active()) {
        return true;
    }
    auto& control = instance.controls[0];
    control.current = clock(control, clocks);
    control.unpaused = control.current;
    const auto elapsed = control.current - control.start;
    if (elapsed.count() < 0) {
        fail(MeshFailure::animation_frame);
    }
    control.frame = static_cast<float>(static_cast<double>(elapsed.count()) * control.rate * frames_per_ms);
    return control.frame > control.total_frames;
}

MeshPlaybackResult pose(MeshPlaybackInstance& instance, bool update, bool force) {
    std::vector<MeshAnimationLayer> layers(instance.controls.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        auto& control = instance.controls[i];
        const auto* animation = clip(instance, control.clip);
        const bool sample = force || (instance.runtime_flags & (0x8000u | 0x400000u)) || (update && (!(control.flags & 4u) || (control.flags & 16u)));
        layers[i] = { animation, control.frame, instance.options, sample };
    }
    const auto applied = evaluate_mesh_animation_layers(instance.assets->model, instance.model, layers, instance.sampling);
    if (!applied) {
        throw applied.error();
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].sample && instance.controls[i].clip.active()) {
            //4DFA30 publishes a null previous block and consumes NEEDS_POSE even
            //when a restored active category has no installed resource group.
            instance.controls[i].previous_clip = layers[i].animation;
            instance.controls[i].flags &= ~16u;
        }
    }
    MeshPlaybackResult result;
    if (instance.runtime_flags & 0x40000000u) {
        for (auto& node : instance.model.nodes) {
            if (node.flags & 0x100u) {
                node.flags &= ~0x800u;
                result.events |= event(MeshPlaybackEvent::renderer_invalidated);
            }
        }
        instance.runtime_flags &= ~0x40000000u;
    }
    instance.runtime_flags &= ~0x400000u;
    return result;
}

MeshMilliseconds extend(std::int32_t ticks, MeshMilliseconds anchor) {
    const auto difference = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(ticks) - static_cast<std::uint32_t>(anchor.count()));
    return anchor + MeshMilliseconds { difference };
}

std::int32_t wire_time(MeshMilliseconds value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value.count()));
}
} //namespace

std::expected<MeshPlaybackAssets, MeshError> read_mesh_playback_assets(const MeshModelLibrary& library, std::string_view filename, bool relative) {
    auto path = std::filesystem::path { filename };
    if (path.extension().empty()) {
        path += ".md2";
    }
    const auto name = path.native_encoded_string();
    const auto bytes = library.read_resource(name);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!*bytes) {
        return std::unexpected(MeshError { MeshFailure::missing, 0, name, {} });
    }
    auto model = parse_mesh_model(**bytes);
    if (!model) {
        return std::unexpected(model.error());
    }
    if (!relative && (model->flags & 4u)) {
        model->flags &= ~4u;
        model->flags |= 0x1000000u;
    }
    auto animations = load_mesh_animation_library(library, name, &*model);
    if (!animations) {
        return std::unexpected(animations.error());
    }
    return MeshPlaybackAssets { std::move(*model), std::move(*animations) };
}

std::expected<std::shared_ptr<const MeshPlaybackAssets>, MeshError> load_mesh_playback_assets(const MeshModelLibrary& library, std::string_view filename, bool relative) {
    auto loaded = read_mesh_playback_assets(library, filename, relative);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return std::shared_ptr<const MeshPlaybackAssets> { std::make_shared<MeshPlaybackAssets>(std::move(*loaded)) };
}

std::expected<MeshPlaybackInstance, MeshError> create_mesh_playback(std::shared_ptr<const MeshPlaybackAssets> assets, std::size_t count, std::uint32_t flags, bool registered, bool alternate) {
    try {
        require(assets != nullptr);
        count = std::max(count, std::size_t { 1 });
        require(count <= std::numeric_limits<std::uint32_t>::max());
        MeshPlaybackInstance instance;
        instance.assets = std::move(assets);
        instance.model = instance.assets->model;
        instance.runtime_flags = flags;
        instance.registered = registered;
        instance.controls.resize(count);
        instance.sampling.resize(count);
        for (auto& control : instance.controls) {
            if (alternate) {
                control.flags |= 64u;
            }
        }
        for (auto& node : instance.model.nodes) {
            node.flags |= 0x100u;
        }
        bool primary_only = true;
        for (std::size_t category = 0; category < instance.assets->animations.groups.size(); ++category) {
            const auto& group = instance.assets->animations.groups[category];
            if (category && !group.empty()) {
                primary_only = false;
            }
            for (const auto& loaded : group) {
                for (const auto& entry : loaded.animation.entries) {
                    require(entry.target_node < instance.model.nodes.size(), entry.target_node);
                    auto& node = instance.model.nodes[entry.target_node];
                    node.flags |= 0x800u;
                    if (category) {
                        node.flags &= ~0x100u;
                    }
                }
            }
        }
        if (primary_only) {
            instance.runtime_flags |= 16u;
        }
        active_count(instance);
        return instance;
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

MeshMilliseconds mesh_playback_nominal_remaining(const MeshPlaybackControl& control) {
    return milliseconds(static_cast<double>(control.total_frames - control.frame) * frame_ms);
}

std::expected<MeshPlaybackResult, MeshError> request_mesh_playback(MeshPlaybackInstance& instance, std::size_t index, const MeshPlaybackRequest& request, const MeshPlaybackClocks& clocks) {
    try {
        require(instance.assets && index < instance.controls.size(), index);
        valid_selection(instance, request.clip, true);
        require(std::isfinite(request.rate) && request.rate >= 0, index);
        auto& control = instance.controls[index];
        MeshPlaybackResult result;
        if ((instance.runtime_flags & 8u) && !instance.registered && request.clip.category != mesh_request_clear) {
            instance.registered = true;
            result.events |= event(MeshPlaybackEvent::registered);
        }
        const auto* current = clip(instance, control.clip);
        const auto* requested = clip(instance, request.clip);
        if (current && control.frame <= control.total_frames && !(control.flags & 6u) && request.clip.category != mesh_request_hold_first && request.clip.category != mesh_request_hold_last && !(request.flags & 2u)) {
            control.deferred = request;
            result.nominal_delay = mesh_playback_nominal_remaining(control) + (requested ? milliseconds(static_cast<double>(requested->frame_span()) * frame_ms) : MeshMilliseconds { 1000 });
            return result;
        }
        if (request.flags & 2u) {
            control.deferred.clip = { 12, 0 };
            control.deferred.rate = 0;
        }
        result.nominal_delay = immediate(instance, index, request, clocks);
        if (result.nominal_delay.count() == 0) {
            result.nominal_delay = MeshMilliseconds { 1000 };
        }
        return result;
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

bool clear_deferred_mesh_playback(MeshPlaybackInstance& instance, std::size_t index) {
    if (index >= instance.controls.size() || instance.controls[index].deferred.clip.category == mesh_request_clear) {
        return false;
    }
    instance.controls[index].deferred.clip.category = mesh_request_clear;
    return true;
}

std::expected<MeshPlaybackResult, MeshError> evaluate_mesh_playback_pose(MeshPlaybackInstance& instance, bool force) {
    try {
        require(instance.assets && instance.controls.size() == instance.sampling.size());
        return pose(instance, true, force);
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

std::expected<void, MeshError> reactivate_mesh_playback(MeshPlaybackInstance& instance) {
    try {
        require(instance.assets != nullptr);
        if (instance.runtime_flags & 0x800000u) {
            for (auto& control : instance.controls) {
                if (const auto* selected = clip(instance, control.clip); selected && control.previous_clip) {
                    if (!(control.flags & 32u)) {
                        reconcile(instance, *control.previous_clip, selected);
                    }
                    mark_binding(instance, *selected);
                }
            }
            instance.runtime_flags &= ~0x800000u;
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<MeshPlaybackResult, MeshError> advance_mesh_playback(MeshPlaybackInstance& instance, const MeshPlaybackClocks& clocks, bool update, std::uint32_t automatic_flags) {
    try {
        require(instance.assets && instance.controls.size() == instance.sampling.size());
        MeshPlaybackResult result;
        if (!(instance.runtime_flags & 0x8004u)) {
            for (unsigned tries = 0; tries < 3 && first_complete(instance, clocks); ++tries) {
                const auto requested = request_mesh_playback(instance, 0, { { 5, 0 }, automatic_flags, 1 }, clocks);
                if (!requested) {
                    throw requested.error();
                }
                result.events |= requested->events;
            }
        }
        std::vector<bool> clear_after(instance.controls.size());
        for (std::size_t i = 0; i < instance.controls.size(); ++i) {
            auto& control = instance.controls[i];
            if (!control.clip.active() || (instance.transient_state & 255u) == 1) {
                continue;
            }
            //A native save can retain an active category with no sidecar at all
            //(the original1jkj Science Lobby/ATM record uses missing I/0). Native
            //4DFA30 still advances its clock/completion and applies no pose.
            //An invalid variant in an existing group remains a binding error.
            valid_selection(instance, control.clip);
            if (!(instance.runtime_flags & 0x8000u)) {
                sample_time(control, clocks);
                if (control.frame > control.total_frames) {
                    if (control.clip.category == 0) {
                        control.flags |= 8u;
                    }
                    if (control.deferred.clip.category != mesh_request_clear) {
                        const auto deferred = control.deferred;
                        immediate(instance, i, { deferred.clip, deferred.flags | (update ? 0u : 8u), deferred.rate }, clocks);
                        control.deferred.clip = { 12, 0 };
                        control.deferred.rate = 0;
                    } else if (instance.runtime_flags & 24u) {
                        if (instance.runtime_flags & 8u) {
                            immediate(instance, i, { { 12, 0 }, 0, 1 }, clocks);
                            if (instance.registered) {
                                instance.registered = false;
                                result.events |= event(MeshPlaybackEvent::unregistered);
                            }
                        } else {
                            control.frame = control.total_frames;
                            clear_after[i] = true;
                            instance.runtime_flags |= 0x40400000u;
                            control.flags &= ~8u;
                        }
                    } else if (control.flags & 1u) {
                        const auto rate = control.deferred.rate != 0 ? control.deferred.rate : control.rate;
                        immediate(instance, i, { control.clip, 1u | (update ? 0u : 8u), rate }, clocks);
                    } else {
                        immediate(instance, i, { { 14, 0 }, update ? 0u : 8u, 1 }, clocks);
                    }
                }
            }
            if (control.clip.category != 0 && (control.flags & 8u)) {
                instance.runtime_flags |= 0x40000000u;
                control.flags &= ~8u;
            }
            if (control.clip.active() && instance.assets->animations.groups[control.clip.category].empty()) {
                control.clip.variant = 0;
            }
        }
        if ((instance.transient_state & 255u) != 1) {
            result.events |= pose(instance, update, false).events;
        }
        for (std::size_t i = 0; i < clear_after.size(); ++i) {
            if (clear_after[i]) {
                immediate(instance, i, { { 12, 0 }, 4, 1 }, clocks);
            }
        }
        return result;
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

std::expected<MeshPlaybackResult, MeshError> restore_mesh_playback_controls(MeshPlaybackInstance& instance, std::span<const MeshAnimationControl> saved, const MeshPlaybackClocks& clocks) {
    try {
        require(instance.assets && saved.size() == instance.controls.size());
        MeshPlaybackResult result;
        bool primary = false;
        for (std::size_t i = 0; i < saved.size(); ++i) {
            const auto& wire = saved[i];
            auto& control = instance.controls[i];
            control = {};
            control.flags = wire.flags;
            if (control.flags & 4u) {
                control.flags |= 16u;
            }
            control.clip = { wire.animation_id, wire.sub_animation_id };
            control.deferred = { { wire.deferred_animation_id, wire.deferred_sub_animation_id }, wire.deferred_flags, wire.deferred_duration_divisor };
            valid_selection(instance, control.clip);
            valid_selection(instance, control.deferred.clip);
            require(std::isfinite(wire.duration_divisor) && wire.duration_divisor >= 0, i);
            control.rate = wire.duration_divisor;
            const auto anchor = clock(control, clocks);
            control.current = extend(wire.animation_time, anchor);
            control.unpaused = extend(wire.unpaused_time, anchor);
            //Both FILD scratch high words are explicitly zeroed at 0x4DEE1C.
            //Recover the unsigned wire delta here, then use only 64-bit runtime time.
            const auto elapsed = static_cast<std::uint32_t>(wire.animation_time) - static_cast<std::uint32_t>(wire.start_time);
            control.start = control.current - MeshMilliseconds { elapsed };
            if (const auto* animation = clip(instance, control.clip)) {
                control.total_frames = animation->frame_span();
                //A zero rate is a defined frozen clock in the new runtime.
                control.frame = control.rate == 0 ? 0 : static_cast<float>(static_cast<double>(elapsed) * frames_per_ms / control.rate);
                if (!(control.frame >= 0) || !std::isfinite(control.frame)) {
                    fail(MeshFailure::animation_frame, i);
                }
            } else {
                //4C5770 skips only derived frame reconstruction when4DEE70 cannot
                //resolve the group. Preserve the saved category and all clock fields.
                control.total_frames = 0;
                control.frame = 0;
            }
            instance.sampling[i] = {};
            if (control.clip.category == 0) {
                primary = true;
            }
        }
        if (!primary) {
            for (auto& node : instance.model.nodes) {
                if (node.flags & 0x100u) {
                    node.flags &= ~0x800u;
                    result.events |= event(MeshPlaybackEvent::renderer_invalidated);
                }
            }
            instance.runtime_flags &= ~0x40000000u;
        }
        active_count(instance);
        if (instance.active_control_count) {
            instance.runtime_flags |= 0x400000u;
        }
        return result;
    } catch (const MeshError& failure) {
        return std::unexpected(failure);
    }
}

std::vector<MeshAnimationControl> capture_mesh_playback_controls(const MeshPlaybackInstance& instance) {
    std::vector<MeshAnimationControl> result;
    result.reserve(instance.controls.size());
    for (const auto& control : instance.controls) {
        MeshAnimationControl wire;
        wire.flags = control.flags;
        wire.animation_id = control.clip.category;
        wire.sub_animation_id = control.clip.variant;
        wire.start_time = wire_time(control.start);
        wire.animation_time = wire_time(control.current);
        wire.unpaused_time = wire_time(control.unpaused);
        wire.duration_divisor = control.rate;
        wire.deferred_animation_id = control.deferred.clip.category;
        wire.deferred_sub_animation_id = control.deferred.clip.variant;
        wire.deferred_flags = control.deferred.flags;
        wire.deferred_duration_divisor = control.deferred.rate;
        wire.total_animation_frames = control.total_frames;
        wire.animation_frame = control.frame;
        wire.previous_resource_block = control.previous_clip;
        result.push_back(wire);
    }
    return result;
}
} //namespace simcoaster
