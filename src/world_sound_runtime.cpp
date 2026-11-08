#include "simcoaster/standard_balance.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
std::string path_key(std::string path) {
    for (auto& character : path) {
        if (character == '\\')
            character = '/';
        else if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
    }
    //Native catalog persistence retains the suffix beginning at \\data\\.
    //Translate that original installation prefix to the current data owner;
    //no bank/category substitution or missing-resource fallback is performed.
    if (const auto data = path.rfind("/data/"); data != path.npos)
        return path.substr(data + 6);
    if (path.starts_with("data/"))
        return path.substr(5);
    return path;
}

std::string virtual_directory(const ObjectDefinitionAsset& asset) {
    auto path = std::filesystem::path{ asset.source_path };
    if (path_key(path.extension().native_encoded_string()) == ".wad")
        path.replace_extension();
    return path.native_encoded_string();
}

std::uint32_t percent(std::int32_t value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0, 100));
}

std::string_view text_bytes(const std::vector<std::byte>& bytes) {
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}
} //namespace

WorldSoundRuntime::WorldSoundRuntime(WorldRuntime& context,
                                     SoundAssetReader reader,
                                     const std::filesystem::path& data_root,
                                     std::string source)
    : context_(&context),
      reader_(std::move(reader)),
      data_root_(data_root),
      source_(std::move(source)) {
    const auto settings_path = (data_root_ / "sound.sam").native_encoded_string();
    auto bytes = reader_(settings_path);
    if (!bytes)
        throw std::runtime_error(describe_sound_error(bytes.error()));
    if (!*bytes)
        throw std::runtime_error("Missing sound.sam: " + settings_path);
    const auto parsed = sam::parse_record<WorldSoundBalance, true>(balance_, text_bytes(**bytes));
    if (!parsed)
        throw std::runtime_error(sam::describe_error(settings_path, parsed.error()));
    globals_.sfx_volume = balance_.volumes.sfx;
    globals_.music_volume = balance_.volumes.music;
    globals_.speech_volume = balance_.volumes.speech;
    globals_.sfx_enabled = globals_.music_enabled = globals_.speech_enabled = balance_.info.sound_on != 0;
    //651C00 sends float1.0 through89C0F0->8A05C0; both requests and listener
    //use this same world-coordinate scale. Particle1024 scaling does not apply.
    runtime_ = std::make_unique<SoundRuntime>(reader_,
                                              (data_root_ / "Global").native_encoded_string(),
                                              SoundRuntimeSettings{});
    const auto reverb_path = (data_root_.parent_path() / "SndReverb.map").native_encoded_string();
    auto reverb = reader_(reverb_path);
    if (!reverb)
        throw std::runtime_error(describe_sound_error(reverb.error()));
    if (*reverb) {
        const auto loaded = runtime_->load_reverb_presets(**reverb);
        if (!loaded)
            throw std::runtime_error(describe_sound_error(loaded.error()));
    } else
        diagnostics_.push_back({ SoundFailure::missing, 0, reverb_path });
    context.resources.resource_manager_feature_flag_0400 = 0x400u;
    context.resources.resource_manager_dispatch_initialized = 1;
    context.resources.ambient_catalog_slot = load_catalog(data_root_ / "Global/Sound", "cat_ambient");
    context.resources.global_rides_catalog_slot = load_catalog(data_root_ / "Global/Sound", "cat_rides");
    context.resources.ui_catalog_slot = load_catalog(data_root_ / "Global/Sound", "cat_ui");
    context.resources.kids_catalog_slot = load_catalog(data_root_ / "Global/Sound", "cat_kids");
    context.resources.staff_catalog_slot = load_catalog(data_root_ / "Global/Sound", "cat_staff");
    context.resources.global_speech_catalog_slot = load_catalog(data_root_ / "Global/Speech", "cat_speech", true);
    context.resources.local_ambient_catalog_slot =
        load_catalog(data_root_ / "levels" / source_ / "Sound", "cat_ambient", true);
    context.resources.local_rides_catalog_slot =
        load_catalog(data_root_ / "levels" / source_ / "Sound", "cat_rides", true);
    context.advisor.presentation.local_speech_catalog_slot =
        load_catalog(data_root_ / "levels" / source_ / "Speech", "cat_speech", true);
    music_catalog_ = load_catalog(data_root_ / "levels" / source_ / "Music", "cat_music", true);
    set_quality(balance_.info.default_quality);
    apply_channels();
}

WorldSoundRuntime::~WorldSoundRuntime() {
    for (const auto& [region, owner] : regions_) {
        (void)region;
        runtime_->remove_occlusion(owner->handle);
    }
}

std::uint32_t WorldSoundRuntime::load_catalog(const std::filesystem::path& root, std::string_view name, bool optional) {
    const auto category = std::string{ name };
    const auto map = (root / (category + "SFX.map")).native_encoded_string();
    const auto present = reader_(map);
    if (!present)
        throw std::runtime_error(describe_sound_error(present.error()));
    if (!*present) {
        if (!optional)
            throw std::runtime_error("Missing sound catalog: " + map);
        return 0;
    }
    const auto slot = runtime_->catalogs().load(root.native_encoded_string(), category);
    if (!slot)
        throw std::runtime_error(describe_sound_error(slot.error()));
    return *slot;
}

void WorldSoundRuntime::load_object_catalog(ObjectDefinitionAsset& asset) {
    //43E300 passes the object's provider root and literal singular cat_ride.
    //A catalogless object continues using the shared/global RSSE categories.
    asset.runtime.resource_tail.ride_sound_catalog_slot = load_catalog(virtual_directory(asset), "cat_ride", true);
}

void WorldSoundRuntime::configure_world_balance(std::string_view text, bool preserve) {
    WorldReverbBalance value;
    value.sound.preset = world_reverb_;
    const auto parsed = sam::parse_record<WorldReverbBalance, true>(value, text, preserve);
    if (!parsed)
        throw std::runtime_error("Invalid Sound.Reverb balance at " + std::to_string(parsed.error().offset));
    world_reverb_ = value.sound.preset;
}

namespace {
[[noreturn]] void
curve_error(std::string_view file, std::string_view first, int low, std::string_view second, int high, int quality) {
    throw std::runtime_error(std::string{ file }
                             + ": unusable sound curve at quality "
                             + std::to_string(quality)
                             + ": "
                             + std::string{ first }
                             + "="
                             + std::to_string(low)
                             + ", "
                             + std::string{ second }
                             + "="
                             + std::to_string(high));
}
} //namespace

std::int32_t
sound_quality_voice_capacity(const WorldSoundBalance& balance, std::int32_t quality, std::string_view file) {
    const auto& channels = balance.channels;
    std::int32_t capacity{};
    if (quality <= channels[2].threshold) {
        if (quality <= channels[1].threshold) {
            const auto denominator = channels[1].threshold - channels[0].threshold;
            if (denominator <= 0)
                curve_error(file,
                            "ChannelInfo[0].SWITCH",
                            channels[0].threshold,
                            "ChannelInfo[1].SWITCH",
                            channels[1].threshold,
                            quality);
            capacity = channels[0].channels
                     + (quality - channels[0].threshold) * (channels[1].channels - channels[0].channels) / denominator;
        } else
            capacity = channels[1].channels;
    } else {
        //The native capacity query reads an uninitialized query_flags DWORD
        //(8A0390/8A1090), so its result is not a reliable host policy. Use an
        //explicit100-voice software ceiling for the modern output, keeping the
        //original interpolation and avoiding dependence on old hardware voices.
        const auto denominator = 100 - channels[1].threshold;
        if (denominator <= 0)
            curve_error(file, "ChannelInfo[1].SWITCH", channels[1].threshold, "quality maximum", 100, quality);
        capacity =
            channels[1].channels + (quality - channels[1].threshold) * (100 - channels[1].channels) / denominator;
    }
    return std::clamp(capacity, 1, 100);
}

void WorldSoundRuntime::set_quality(std::int32_t quality) {
    quality = std::clamp(quality, 0, 100);
    const auto file = (data_root_ / "sound.sam").native_encoded_string();
    const auto capacity = sound_quality_voice_capacity(balance_, quality, file);
    const auto [geometry, cutoff] =
        sound_quality_geometry(balance_, quality, (context_->view.flags & 0x16u) != 0, file);
    quality_ = quality;
    runtime_->set_voice_capacity(static_cast<std::uint32_t>(capacity));
    runtime_->set_fading_enabled(quality > balance_.info.fade_toggle);
    runtime_->catalogs().set_sample_dropping(quality <= balance_.info.droppable);
    runtime_->set_occlusion_enabled(geometry, cutoff);
}

void WorldSoundRuntime::set_channel_level(std::uint32_t channel, std::int32_t level) {
    level = std::clamp(level, 0, 100);
    match (channel) {
        case 0 => globals_.channel0_volume = level;
        case 1 => globals_.music_volume = level;
        case 2 => globals_.speech_volume = level;
        case 3 => globals_.sfx_volume = level;
        case _ => throw std::out_of_range("Sound channel");
    }
    globals_.dirty = 1;
}

void WorldSoundRuntime::set_channel_enabled(std::uint32_t channel, bool enabled) {
    match (channel) {
        case 1 => globals_.music_enabled = enabled;
        case 2 => globals_.speech_enabled = enabled;
        case 3 => globals_.sfx_enabled = enabled;
        case _ => throw std::out_of_range("Native SoundInfo exposes enable controls for channels1..3");
    }
    globals_.dirty = 1;
}

std::pair<bool, float> sound_quality_geometry(const WorldSoundBalance& balance,
                                              std::int32_t quality,
                                              bool special_view,
                                              std::string_view file) {
    bool enabled = false;
    float cutoff{};
    if (special_view && quality > balance.info.geometry) {
        enabled = true;
        const auto& rows = balance.radii;
        if (quality <= rows[0].threshold)
            cutoff = rows[0].minimum_radius;
        else if (quality <= rows[1].threshold) {
            const auto span = rows[1].threshold - rows[0].threshold;
            if (span <= 0)
                curve_error(file,
                            "RadiusInfo[0].SWITCH",
                            rows[0].threshold,
                            "RadiusInfo[1].SWITCH",
                            rows[1].threshold,
                            quality);
            cutoff = static_cast<float>(rows[0].minimum_radius
                                        + static_cast<double>(rows[1].minimum_radius - rows[0].minimum_radius)
                                              * (quality - rows[0].threshold)
                                              / span);
        } else if (quality <= rows[2].threshold) {
            const auto span = rows[2].threshold - rows[1].threshold;
            if (span <= 0)
                curve_error(file,
                            "RadiusInfo[1].SWITCH",
                            rows[1].threshold,
                            "RadiusInfo[2].SWITCH",
                            rows[2].threshold,
                            quality);
            cutoff = static_cast<float>(rows[1].minimum_radius
                                        + static_cast<double>(rows[2].minimum_radius - rows[1].minimum_radius)
                                              * (quality - rows[1].threshold)
                                              / span);
        } else
            cutoff = rows[2].minimum_radius;
    }
    return { enabled, cutoff };
}

void WorldSoundRuntime::apply_geometry() {
    const auto [enabled, cutoff] = sound_quality_geometry(balance_,
                                                          quality_,
                                                          (context_->view.flags & 0x16u) != 0,
                                                          (data_root_ / "sound.sam").native_encoded_string());
    runtime_->set_occlusion_enabled(enabled, cutoff);
}

void WorldSoundRuntime::apply_channels() {
    if (!globals_.dirty)
        return;
    const bool duck = globals_.speech_ducking && globals_.speech_enabled && globals_.speech_volume > 0;
    const auto ducked = [&](std::int32_t volume) {
        return duck ? static_cast<std::int32_t>(static_cast<std::int64_t>(balance_.info.ducking) * volume / 100)
                    : volume;
    };
    runtime_->set_music_muted(globals_.music_mute != 0);
    runtime_->set_channel_level(0, percent(globals_.channel0_volume));
    runtime_->set_channel_level(1, globals_.music_mute ? 0u : percent(ducked(globals_.music_volume)));
    runtime_->set_channel_level(2, percent(globals_.speech_volume));
    runtime_->set_channel_level(3, percent(ducked(globals_.sfx_volume)));
    runtime_->set_channel_enabled(1, globals_.music_enabled != 0);
    runtime_->set_channel_enabled(2, globals_.speech_enabled != 0);
    runtime_->set_channel_enabled(3, globals_.sfx_enabled != 0);
    globals_.dirty = 0;
}

void WorldSoundRuntime::stage_music_mute(std::uint16_t value) {
    globals_.music_mute = value;
    globals_.dirty = 1;
}

void WorldSoundRuntime::set_lobby_mode(bool entering, bool intro) {
    //5BD890/5BE810 route through657130/657050: selecting a music row is a
    //keyed operation4, not a new template request. The ordinary chooser owns
    //its separate map sound; ParkRuntime owns its retained Scape levels.
    lobby_mode_ = entering;
    globals_.kids_enabled = entering ? 0u : 1u;
    if (entering) {
        if (globals_.kids_handle)
            runtime_->recycle(globals_.kids_handle);
        globals_.kids_handle = 0;
        globals_.music_handle = runtime_->operation(globals_.music_handle, 4, intro ? 21 : 0);
        globals_.reverb = world_reverb_;
        runtime_->set_reverb_preset(static_cast<std::uint32_t>(world_reverb_));
        if (!intro)
            stage_music_mute(1);
    } else {
        if (!intro)
            stage_music_mute(0);
        globals_.kids_parameter = 0;
        if (!globals_.kids_handle)
            globals_.kids_handle = runtime_->submit(0, context_->resources.kids_catalog_slot, 91, { 0, 0, 0 });
        globals_.kids_handle = runtime_->operation(globals_.kids_handle, 7, 0);
    }
    apply_channels();
}

void WorldSoundRuntime::set_music_level(std::int32_t value) {
    globals_.music_handle = runtime_->operation(globals_.music_handle, 4, value);
}

void WorldSoundRuntime::begin_failure_transition() {
    globals_.music_handle = runtime_->operation(globals_.music_handle, 4, 0);
    globals_.kids_enabled = 0;
    if (globals_.kids_handle)
        runtime_->recycle(globals_.kids_handle);
    globals_.kids_handle = 0;
}

RsseSoundOcclusionRegion* WorldSoundRuntime::add_occlusion(float x, float, float z, float radius, float height) {
    auto region = std::make_unique<RsseSoundOcclusionRegion>();
    region->handle = runtime_->add_occlusion({ { x, 0.f, z }, radius, height });
    if (!region->handle)
        return nullptr;
    auto* result = region.get();
    regions_.emplace(result, std::move(region));
    return result;
}

void WorldSoundRuntime::remove_occlusion(RsseSoundOcclusionRegion* region) {
    const auto found = regions_.find(region);
    if (found == regions_.end())
        return;
    runtime_->remove_occlusion(found->second->handle);
    regions_.erase(found);
}

void WorldSoundRuntime::set_camera(const SceneCamera& camera, bool paused) {
    apply_geometry();
    const auto eye = camera.eye();
    auto position = eye;
    const auto& context = *context_;
    if (!(context.view.flags & 0x16u))
        for (std::size_t axis = 0; axis < 3; ++axis)
            position[axis] = eye[axis] - (eye[axis] - camera.target[axis]) * 0.5f;
    if (paused || globals_.pause_override)
        position[1] = 10000.f;
    runtime_->set_listener(position, camera.forward());
}

void WorldSoundRuntime::update_music_and_kids() {
    auto& context = *context_;
    if (lobby_mode_
        || !context.simulation.current
        || context.mode.source_recreation
        || context.simulation.current->saved.state == WorldState::transition)
        return;
    if (!context.resources.resource_manager_feature_flag_0400) {
        globals_.music_handle = globals_.kids_handle = 0;
        return;
    }
    const auto analyser_id = context.simulation.current->saved.park_analyser;
    std::int32_t level{};
    if (analyser_id && context.entities[analyser_id].payload) {
        auto& analyser = *context.entities.payload<ParkAnalyserState>(analyser_id);
        const auto count = std::bit_cast<std::int32_t>(get_cached_eligible_person_record_count(context, analyser, 0));
        if (count > 0)
            level = std::min(count / 2, 89);
    }
    globals_.music_handle = runtime_->operation(globals_.music_handle, 4, level);
    const auto cursor = context.staff.interaction.cursor.cell;
    const auto nearby = cursor && cursor <= map_cell_count
                          ? std::min(query_world_used_thing_type_in_square_radius(context, 1, cursor, 4, false), 100u)
                          : 0u;
    globals_.kids_parameter = static_cast<std::int32_t>(nearby);
    if (globals_.kids_enabled) {
        if (!globals_.kids_handle)
            globals_.kids_handle = runtime_->submit(0, context.resources.kids_catalog_slot, 91, { 0, 0, 0 });
        globals_.kids_handle = runtime_->operation(globals_.kids_handle, 7, globals_.kids_parameter);
    } else if (globals_.kids_handle) {
        runtime_->recycle(globals_.kids_handle);
        globals_.kids_handle = 0;
    }
}

void WorldSoundRuntime::update_reverb() {
    auto& context = *context_;
    std::int32_t preset = world_reverb_;
    const auto inspected_mesh = context.resources.meshes ? context.resources.meshes->selected_slot : 0;
    //466720 reads the same selected mesh used by the inspection animation pass.
    //A separate audio selection would miss existing camera/mesh ownership.
    if ((context.view.flags & 4u) && inspected_mesh && context.resources.meshes->instance(inspected_mesh))
        for (auto* script = context.scripts.states.head; script; script = script->next)
            if (script->mesh_instance_handle == inspected_mesh
                && !script->parent_script_state_id
                && script->reverb_preset_override) {
                if (script->reverb_preset_override < 0)
                    return;
                preset = script->reverb_preset_override;
                break;
            }
    if (globals_.reverb != preset) {
        globals_.reverb = preset;
        runtime_->set_reverb_preset(static_cast<std::uint32_t>(preset));
    }
}

void WorldSoundRuntime::set_inspected_mesh(std::uint32_t slot) {
    if (context_->resources.meshes)
        context_->resources.meshes->selected_slot = slot && context_->resources.meshes->instance(slot) ? slot : 0;
}

void WorldSoundRuntime::world_step() {
    update_reverb();
}

void WorldSoundRuntime::prepare_frame(bool paused) {
    auto& context = *context_;
    globals_.pause_override = paused ? 1u : 0u;
    if (context.advisor.presentation.sound_settings_dirty) {
        globals_.speech_ducking = context.advisor.presentation.speech_ducking_active;
        globals_.dirty = 1;
        context.advisor.presentation.sound_settings_dirty = false;
    }
    update_music_and_kids();
    apply_channels();
}

void WorldSoundRuntime::advance(std::chrono::milliseconds elapsed) {
    runtime_->advance(elapsed);
}

void WorldSoundRuntime::restore_globals(std::span<const std::byte> bytes) {
    auto storage = save::detail::encode(globals_);
    if (bytes.size() > storage.size())
        throw std::runtime_error("Sound global block exceeds84-byte native extent");
    std::ranges::copy(bytes, storage.begin());
    save::detail::decode<WorldSoundSavedGlobals>(std::span<const std::byte, 84>{ storage }, globals_);
    context_->weather.rain_intensity_sound_handle = std::bit_cast<std::int32_t>(globals_.rain_handle);
    context_->advisor.presentation.speech_ducking_active = globals_.speech_ducking != 0;
    context_->advisor.presentation.sound_settings_dirty = true;
    globals_.dirty = 1;
    apply_channels();
}

std::array<std::byte, 84> WorldSoundRuntime::capture_globals() {
    globals_.rain_handle = std::bit_cast<std::uint32_t>(context_->weather.rain_intensity_sound_handle);
    globals_.speech_ducking = context_->advisor.presentation.speech_ducking_active;
    return save::detail::encode(globals_);
}

void WorldSoundRuntime::restore_backend(std::span<const std::byte> bytes) {
    auto snapshot = parse_sound_backend_snapshot(bytes);
    if (!snapshot)
        throw std::runtime_error("Sound backend decode: " + describe_sound_error(snapshot.error()));
    const auto paths = runtime_->catalogs().reference_paths();
    for (auto& saved : snapshot->catalogs.paths)
        if (!saved.empty()) {
            const auto key = path_key(saved);
            const auto live = std::ranges::find_if(paths, [&](const auto& path) {
                return !path.empty() && path_key(path) == key;
            });
            if (live != paths.end())
                saved = *live;
        }
    const auto restored = runtime_->restore(*snapshot);
    if (!restored)
        throw std::runtime_error("Sound backend restore: " + describe_sound_error(restored.error()));
}

std::vector<std::byte> WorldSoundRuntime::capture_backend() const {
    auto snapshot = runtime_->snapshot();
    if (!snapshot)
        throw std::runtime_error("Sound backend snapshot: " + describe_sound_error(snapshot.error()));
    //8A6C60 stores the installation-independent suffix from "\\data\\".
    //Keep absolute paths in the live owner, never in the native save block.
    for (auto& path : snapshot->catalogs.paths)
        if (!path.empty()) {
            path = "\\data\\" + path_key(path);
            std::ranges::replace(path, '/', '\\');
        }
    auto bytes = encode_sound_backend_snapshot(*snapshot);
    if (!bytes)
        throw std::runtime_error("Sound backend encode: " + describe_sound_error(bytes.error()));
    return std::move(*bytes);
}

void WorldSoundRuntime::finish_restore(bool restored_backend) {
    globals_.pause_override = 0;
    if (!restored_backend) {
        globals_.music_handle = globals_.kids_handle = globals_.rain_handle = 0;
        context_->weather.rain_intensity_sound_handle = 0;
        if (music_catalog_ && context_->resources.resource_manager_feature_flag_0400) {
            globals_.music_handle = runtime_->submit(0, music_catalog_, 2, { 0, 0, 0 });
            globals_.music_handle = runtime_->operation(globals_.music_handle, 4, 0);
        }
    }
    globals_.dirty = 1;
    apply_channels();
    runtime_->set_reverb_preset(static_cast<std::uint32_t>(globals_.reverb));
}

std::vector<SoundError> WorldSoundRuntime::diagnostics() const {
    auto result = diagnostics_;
    result.insert(result.end(), runtime_->diagnostics().begin(), runtime_->diagnostics().end());
    return result;
}
} //namespace simcoaster
