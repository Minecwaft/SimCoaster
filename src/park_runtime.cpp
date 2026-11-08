#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

ParkRuntime::ParkRuntime(const std::filesystem::path& root, const std::filesystem::path& file, std::string theme)
    : ParkRuntime(root, ParkRuntimeOpen{ std::move(theme), file, nullptr }) {}

ParkRuntime::ParkRuntime(const std::filesystem::path& root, ParkRuntimeOpen request)
    : state_(std::make_unique<State>(std::filesystem::absolute(root),
                                     request.theme,
                                     request.coaster_designer,
                                     request.resource_pump,
                                     std::move(request.hardware_profile))) {
    state_->open(*this, root, request);
}

ParkRuntime::~ParkRuntime() = default;

const ApplicationHardwareProfile& ParkRuntime::hardware_profile() const {
    return state_->configuration.hardware_profile;
}

void ParkRuntime::apply_hardware_profile(const ApplicationHardwareProfile& profile) {
    state_->apply_hardware_profile(profile);
}

WorldRuntime& ParkRuntime::context() {
    return *state_->simulation.world;
}

WorldMeshRuntime& ParkRuntime::meshes() {
    return state_->rendering.meshes;
}

ParticleRuntime& ParkRuntime::particles() {
    return *state_->simulation.particles;
}

WorldMeshScene& ParkRuntime::world_scene() {
    return state_->rendering.scene;
}

ParkConstructionRuntime& ParkRuntime::construction() {
    return state_->tools.construction;
}

ParkAuthoringController& ParkRuntime::authoring() {
    return *state_->presentation.authoring;
}

std::uint16_t ParkRuntime::pick_ui_world_cursor() {
    auto& s = *state_;
    const auto dimensions = s.presentation.ui->viewport_size();
    const auto pointer = s.presentation.ui->controls().pointer_position();
    const auto camera = s.render_camera();
    const std::array pixel{ static_cast<std::int32_t>(static_cast<std::int64_t>(pointer.x) * dimensions[0] / 2048),
                            static_cast<std::int32_t>(static_cast<std::int64_t>(pointer.y) * dimensions[1] / 1536) };
    const auto cursor = pick_world_interaction_cursor(*s.simulation.world,
                                                      camera,
                                                      s.rendering.terrain,
                                                      pixel,
                                                      dimensions[0],
                                                      dimensions[1])
                            .value_or(StaffInteractionCursor{});
    update_world_staff_interaction_cursor(*s.simulation.world, cursor);
    return cursor.cell;
}

bool ParkRuntime::coaster_geometry_pointer(CoasterGeometryPointerPhase phase,
                                           std::array<std::int32_t, 2> pixel,
                                           std::uint32_t width,
                                           std::uint32_t height) {
    auto& s = *state_;
    if (width && height)
        s.simulation.world->view.camera.viewport = { width, height };
    if (!s.tools.coaster_editor.geometry_interaction_active())
        return false;
    std::uint32_t slot{};
    if (phase != CoasterGeometryPointerPhase::cancel) {
        const auto camera = s.render_camera();
        const auto pick = pick_world_cursor(*s.simulation.world, camera, s.rendering.terrain, pixel, width, height);
        if (pick && pick->mesh)
            slot = pick->mesh->mesh_slot;
    }
    const auto handled = s.tools.coaster_editor.geometry_pointer(*s.simulation.world, slot, pixel, phase);
    s.presentation.authoring->synchronize_from_world();
    return handled;
}

NativeCoasterEditor& ParkRuntime::coaster_editor() {
    return state_->tools.coaster_editor;
}

CoasterCatalogRuntime& ParkRuntime::coaster_catalog() {
    return state_->tools.coaster_catalog;
}

LayoutReplayRuntime& ParkRuntime::action_recording() {
    return state_->tools.layout_replay;
}

SoundRuntime& ParkRuntime::audio() {
    return state_->presentation.audio->runtime();
}

WorldUiRuntime& ParkRuntime::ui() {
    return *state_->presentation.ui;
}

const UiPreferences& ParkRuntime::preferences() const {
    return state_->configuration.preferences.options;
}

void ParkRuntime::set_game_speed_percent(std::int16_t value) {
    state_->configuration.preferences.options.game_speed_percent = value;
    state_->simulation.world->timing.speed_percent = value;
    state_->simulation.world->timing.scale = static_cast<double>(value) * .0125;
}

void ParkRuntime::apply_preferences(const UiPreferences& options, bool apply_clock) {
    auto& s = *state_;
    s.configuration.preferences.options = options;
    s.presentation.ui->preferences(options);
    s.simulation.world->view.camera.isometric_mode = options.isometric_on != 0;
    s.simulation.world->timing.speed_percent = options.game_speed_percent;
    //45A040 consumes the signed saved value directly; zero remains paused.
    //Options previews update the value without committing its clock scale.
    if (apply_clock)
        s.simulation.world->timing.scale = static_cast<double>(options.game_speed_percent) * .0125;
    //MovieVolume is a separate ten-bit stream. MusicEnabled is not another
    //channel mute; the three native channel pairs remain independent.
    for (const auto& [channel, volume] : std::array<std::pair<std::uint32_t, UiOptionVolume>, 3>{
             { { 1, options.music_volume }, { 2, options.speech_volume }, { 3, options.sfx_volume } } }) {
        s.presentation.audio->set_channel_level(channel, volume.value);
        s.presentation.audio->set_channel_enabled(channel, volume.enabled != 0);
    }
    s.presentation.audio->set_quality(options.sound_quality);
}

std::vector<CampaignAmbientRecord>
ParkRuntime::remap_campaign_ambient(std::span<const CampaignAmbientRecord> imported) const {
    return state_->presentation.scape ? state_->presentation.scape->remap_imported_ambient(imported)
                                      : std::vector<CampaignAmbientRecord>{ imported.begin(), imported.end() };
}

CampaignProfile ParkRuntime::capture_campaign(CampaignAuxiliaryState auxiliary) const {
    auto& s = *state_;
    auxiliary.ambient = remap_campaign_ambient(auxiliary.ambient);
    return capture_campaign_profile(*s.simulation.world,
                                    s.configuration.preferences.options,
                                    s.bindings.selected_resource_source(*s.simulation.world),
                                    std::move(auxiliary));
}

void ParkRuntime::apply_campaign_for_lobby(const CampaignProfile& profile) {
    state_->presentation.ui->close_panel();
    apply_campaign_profile_to_lobby(*state_->simulation.world, profile);
    apply_preferences(profile.options, false);
    state_->simulation.world->ui.panels.objectives_refresh_pending = true;
}

void ParkRuntime::set_lobby_audio(bool entering, bool intro) {
    state_->presentation.audio->set_lobby_mode(entering, intro);
    if (state_->presentation.scape && !intro)
        state_->presentation.scape->set_retained_ambient_level(
            entering ? 0u : static_cast<std::uint32_t>(std::clamp(preferences().sfx_volume.value, 0, 100)));
}

void ParkRuntime::begin_failure_audio_transition() {
    state_->presentation.audio->begin_failure_transition();
}

void ParkRuntime::set_debug_music_level(std::int32_t value) {
    state_->presentation.audio->set_music_level(value);
}

void ParkRuntime::set_debug_lighting(bool raised) {
    auto& s = *state_;
    const auto color = [](std::uint32_t rgb) -> MeshPoint {
        const auto v = mesh_argb_color(rgb);
        return { v[0] / 255.f, v[1] / 255.f, v[2] / 255.f };
    };
    s.rendering.lighting.ambient = color(raised ? static_cast<std::uint32_t>(-11184792) : 16777215u);
    std::erase_if(s.rendering.lighting.lights, [](const auto& light) {
        return light.kind == MeshLightKind::directional;
    });
    MeshLight light;
    light.color = color(raised ? static_cast<std::uint32_t>(-40) : 16777215u);
    light.direction = raised ? MeshPoint{ .40000001f, -.80000001f, .40000001f } : MeshPoint{ 0, -1, 0 };
    const auto length = std::sqrt(light.direction[0] * light.direction[0]
                                  + light.direction[1] * light.direction[1]
                                  + light.direction[2] * light.direction[2]);
    for (auto& component : light.direction)
        component /= length;
    s.rendering.lighting.lights.push_back(light);
    s.rendering.meshes.invalidate_mesh_renderer_cache(*s.simulation.world);
}

void ParkRuntime::exit_special_view_from_escape() {
    auto& s = *state_;
    auto& c = *s.simulation.world;
    //5070A0 handles flags2 and4 independently. A bare flag16 is consumed
    //without inventing an exit operation; its mode bits remain unchanged.
    if (c.view.flags & 2u)
        s.bindings.toggle_world_view_point_of_interest(c);
    else if (c.view.flags & 4u)
        s.bindings.restore_special_camera_modes(c, false);
    if (!(c.view.flags & 0x16u)) {
        auto& controls = s.presentation.ui->controls();
        auto* toolbar = controls.find_recursive(s.presentation.ui->primary_root(), 62);
        auto* selection = controls.find(toolbar, 64);
        if (!selection)
            throw std::logic_error("Original World toolbar has no tool selection group");
        //Presentation only: native85AD05 does not invoke a second tool action.
        controls.set_selected(*selection, 67, false);
    }
}

void ParkRuntime::set_lobby_view(const ApplicationLobbyView* view) {
    auto& s = *state_;
    if (!view) {
        s.presentation.lobby_view.reset();
        s.presentation.lobby_lighting.reset();
        return;
    }
    if (!std::ranges::all_of(view->camera_to_world, [](float v) {
            return std::isfinite(v);
        }))
        throw std::invalid_argument("Invalid lobby camera pose");
    s.presentation.lobby_view = *view;
    s.presentation.lobby_lighting = s.rendering.lighting;
    const auto color = [](std::uint32_t rgb) -> MeshPoint {
        const auto channels = mesh_argb_color(rgb);
        return { channels[0] / 255.f, channels[1] / 255.f, channels[2] / 255.f };
    };
    s.presentation.lobby_lighting->ambient = color(view->ambient_rgb);
    for (auto& light : s.presentation.lobby_lighting->lights)
        if (light.kind == MeshLightKind::directional)
            light.color = color(view->directional_rgb);
}

void ParkRuntime::stage_coaster_source(const ApplicationCoasterSourceSelection& selection) {
    validate_coaster_source_selection(*state_->simulation.world, selection);
    //Copy only after validation. No old editor, World cells, interaction mode
    //or recording stream changes until the actual map handler activates.
    state_->presentation.pending_coaster_source = selection;
}

bool ParkRuntime::activate_pending_coaster_source() {
    auto& s = *state_;
    if (!s.presentation.pending_coaster_source)
        return false;
    auto selection = std::move(*s.presentation.pending_coaster_source);
    s.presentation.pending_coaster_source.reset();
    install_coaster_source_selection(*s.simulation.world, selection);
    begin_coaster_source_world_interaction(*s.simulation.world, s.tools.construction, selection);
    return true;
}

std::optional<WorldMobilePick>
ParkRuntime::pick_mobile(std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height) {
    auto& s = *state_;
    return s.rendering.mobile_picker
        .pick(*s.simulation.world, render_camera(), s.rendering.terrain, pixel, width, height);
}

bool ParkRuntime::focus_staff_view(std::uint16_t id) {
    if (!id || id >= state_->simulation.world->entities.size())
        return false;
    const auto* record = state_->simulation.world->entities[id].payload;
    if (!record || !is_staff_record_kind(record->kind))
        return false;
    return focus_mobile_view(id, true);
}

bool ParkRuntime::focus_person_view(std::uint16_t id, bool follow) {
    if (!id || id >= state_->simulation.world->entities.size())
        return false;
    const auto* record = state_->simulation.world->entities[id].payload;
    if (!record || record->kind != ThingKind::person)
        return false;
    return focus_mobile_view(id, follow);
}

bool ParkRuntime::focus_mobile_view(std::uint16_t id, bool interpolate) {
    auto& s = *state_;
    if (!id || id >= s.simulation.world->entities.size())
        return false;
    const auto* record = s.simulation.world->entities[id].payload;
    if (!record || record->retirement_pending)
        return false;
    const MapObjectBase* base{};
    const MobileEntityState* mobile{};
    if (record->kind == ThingKind::person) {
        const auto& person = *reinterpret_cast<const PersonRuntimeView*>(record);
        base = &person.base;
        mobile = &person.mobile;
    } else if (is_staff_record_kind(record->kind)) {
        const auto& employee = *reinterpret_cast<const StaffRuntimeRecord*>(record);
        base = &employee.base;
        mobile = &employee.mobile;
    } else
        return false;
    if (!interpolate) {
        set_world_view_camera_focus(
            *s.simulation.world,
            { static_cast<float>(10 * (base->x >> 8)), static_cast<float>(10 * (base->y >> 8)) });
        return true;
    }
    const auto elapsed = s.rendering.meshes.clocks.simulation.count()
                       - s.frame.scheduler.world_step_cursor.value_or(s.rendering.meshes.clocks.simulation.count());
    const auto factor = static_cast<float>(static_cast<double>(elapsed) * .0040322579);
    const auto pose = interpolate_mobile_entity_render_position(*mobile, factor, true);
    auto& camera = s.simulation.world->view.camera;
    camera.focus_position[0] = pose.position[0];
    camera.focus_position[2] = pose.position[2];
    camera.damped_pan_x = camera.damped_pan_z = 0;
    return true;
}

bool ParkRuntime::ui_commands_enabled() const {
    return !state_->bindings.command_families_disabled;
}

std::vector<SoundError> ParkRuntime::audio_diagnostics() const {
    return state_->presentation.audio->diagnostics();
}

void ParkRuntime::set_audio_inspected_mesh(std::uint32_t slot) {
    //Native466720 only exposes a nonnull selected-mesh pointer. Reject a stale
    //external slot rather than allowing an orphan RSSE state to select reverb.
    state_->presentation.audio->set_inspected_mesh(slot && state_->rendering.meshes.instance(slot) ? slot : 0u);
}

void ParkRuntime::set_audio_channel_level(std::uint32_t channel, std::int32_t level) {
    state_->presentation.audio->set_channel_level(channel, level);
}

void ParkRuntime::set_audio_channel_enabled(std::uint32_t channel, bool enabled) {
    state_->presentation.audio->set_channel_enabled(channel, enabled);
}

void ParkRuntime::set_audio_quality(std::int32_t quality) {
    state_->presentation.audio->set_quality(quality);
}

void ParkRuntime::replay_layout(LayoutReplayTiming timing) {
    auto& s = *state_;
    const auto result = s.tools.layout_replay.begin(*s.simulation.world, s.tools.layout_backend, timing);
    if (!result)
        throw std::runtime_error(describe_layout_error(result.error()));
}

void ParkRuntime::cancel_layout_replay() {
    const auto result = state_->tools.layout_replay.cancel(*state_->simulation.world);
    if (!result)
        throw std::runtime_error(describe_layout_error(result.error()));
}

const ParkRuntimeStatistics& ParkRuntime::statistics() const {
    return state_->frame.statistics;
}

SceneCamera ParkRuntime::render_camera() {
    return state_->render_camera();
}

void ParkRuntime::set_camera_input(const WorldCameraInput& input) {
    state_->presentation.camera_input = input;
}

WorldCameraInputFeedback ParkRuntime::camera_input_feedback() const {
    return state_->presentation.camera_input_result;
}

void ParkRuntime::commit_ui_camera() {
    state_->frame.native_camera_zoom = state_->simulation.world->view.mobile.camera_zoom;
}
} //namespace simcoaster
