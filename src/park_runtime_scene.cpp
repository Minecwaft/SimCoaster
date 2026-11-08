#include "park_runtime_codec.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster {
using namespace park_detail;

SceneCamera ParkRuntime::State::render_camera() {
    if (presentation.lobby_view) {
        auto camera = presentation.audio_camera.value_or(SceneCamera{});
        camera.explicit_pose = presentation.lobby_view->camera_to_world;
        const auto position = camera.eye(), forward = camera.forward();
        for (std::size_t axis = 0; axis < 3; ++axis)
            camera.target[axis] = position[axis] + forward[axis] * 35.f;
        camera.yaw = std::atan2(-forward[0], -forward[2]);
        camera.pitch = std::asin(std::clamp(-forward[1], -1.f, 1.f));
        camera.near_plane = simulation.world->view.camera.near_clip_distance;
        camera.far_plane = std::max(simulation.world->view.camera.far_clip_distance, camera.near_plane + 1.f);
        apply_world_camera_projection(*simulation.world, camera);
        return camera;
    }
    auto& view = simulation.world->view.camera;
    //Guarded initialization also refreshes a direct look/focus edit from
    //this frame's filtered base. It never advances/reblends RAW history.
    initialize_world_camera_render(*simulation.world);
    auto camera = presentation.audio_camera.value_or(SceneCamera{});
    camera.target = view.focus_position;
    camera.yaw = view.heading_radians;
    camera.pitch = view.pitch_radians;
    camera.distance = simulation.world->view.mobile.camera_zoom;
    camera.near_plane = view.near_clip_distance;
    camera.far_plane = std::max(view.far_clip_distance, camera.near_plane + 1.f);
    apply_world_camera_render(*simulation.world, camera);
    return camera;
}

void ParkRuntime::State::publish_render_camera(bool paused) {
    auto camera = render_camera();
    simulation.world->view.mobile.camera_position = camera.eye();
    frame.preview_camera_heading = camera.yaw;
    presentation.audio_camera = camera;
    presentation.audio->set_camera(camera, paused);
}

void ParkRuntime::State::apply_hardware_profile(const ApplicationHardwareProfile& profile) {
    if (profile.profile_index > 2)
        throw std::out_of_range("Unknown native hardware profile");
    if (profile.graphics.animating_textures == 0 || profile.graphics.animating_textures == 1)
        rendering.scene.configure_animated_textures(profile.graphics.animating_textures != 0);
    const auto procedural = profile.graphics.procedural_texturing;
    if (procedural >= 0 && procedural <= 3) {
        const auto dimension = procedural == 1 ? 16u : procedural == 3 ? 64u : 32u;
        const auto changed = rendering.scene.procedural_terrain_enabled() != (procedural != 0)
                          || (procedural && rendering.scene.procedural_textures()->dimension() != dimension);
        rendering.scene.configure_procedural_terrain(procedural != 0, dimension);
        rendering.meshes.terrain_texture_blending = procedural != 0;
        if (changed)
            for (std::size_t i = 1; i < rendering.meshes.slot_limit(); ++i)
                if (auto* instance = rendering.meshes.instance(static_cast<std::uint32_t>(i))) {
                    const auto* source = rendering.meshes.definition(instance->template_id);
                    if (!source)
                        throw std::logic_error("Profile refresh has no mesh template");
                    rendering.scene.prepare_mesh_base(*instance, *source, true);
                }
    }
    const auto layers = profile.graphics.sky_quality;
    if (layers == 0 || layers == 1 || layers == 2 || layers == 4)
        simulation.weather->sky_layers = static_cast<std::uint32_t>(layers);
    if (profile.graphics.sky_detail == 0 || profile.graphics.sky_detail == 1)
        simulation.weather->sky_detail = profile.graphics.sky_detail != 0;
    if (profile.graphics.sky_shadow == 0 || profile.graphics.sky_shadow == 1) {
        if (simulation.weather->sky_shadow != (profile.graphics.sky_shadow != 0))
            rendering.terrain_sky_shadow.clear();
        simulation.weather->sky_shadow = profile.graphics.sky_shadow != 0;
    }
    apply_world_graphics_profile(*simulation.world, *simulation.particles, content.sprites, profile);
    if (const auto shadow = sprite_shadow_policy(profile.graphics.sprite_shadow, false))
        rendering.sprite_shadow_quality = shadow->quality;
    configuration.hardware_profile = profile;
}

void ParkRuntime::State::build_scene_geometry(MeshScene& output, const MeshMatrix* view_projection) {
    auto& s = *this;
    const MeshLighting unlit;
    const auto& frame_lighting = s.configuration.hardware_profile.graphics.lighting == 0 ? unlit
                               : s.presentation.lobby_lighting ? *s.presentation.lobby_lighting
                                                               : s.rendering.lighting;
    if (s.presentation.ui)
        s.presentation.ui->set_advisor_lighting(frame_lighting.ambient, frame_lighting.diffuse_mask);
    s.rendering.scene.build(*s.simulation.world, s.rendering.meshes, output, frame_lighting, view_projection);
    std::array<float, 2> preview_offset{};
    if (s.simulation.world->construction.mode == 4
        || s.simulation.world->construction.mode == 14
        || s.simulation.world->construction.mode == 19) {
        preview_offset = {
            s.frame.preview_reference[0] - static_cast<float>(s.tools.construction.state.preview_coordinates.x) * 10.f,
            s.frame.preview_reference[1] - static_cast<float>(s.tools.construction.state.preview_coordinates.y) * 10.f
        };
    }
    s.rendering.scene.append_construction_preview(*s.simulation.world,
                                                  s.tools.construction.state,
                                                  output,
                                                  frame_lighting,
                                                  s.frame.preview_phase,
                                                  s.frame.preview_camera_heading,
                                                  preview_offset);
    update_weather_effects(*s.simulation.world,
                           *s.simulation.weather,
                           static_cast<std::uint32_t>(s.rendering.meshes.clocks.simulation.count()));
    output.weather = s.simulation.weather->frame;
    const auto* model = s.rendering.scene.terrain();
    const auto grid = model && model->terrain ? std::optional{ model->terrain->view() } : std::nullopt;
    s.rendering.terrain_sky_shadow.append(output,
                                          grid ? &*grid : nullptr,
                                          *output.weather,
                                          view_projection ? *view_projection : s.render_camera().view_projection(1.f),
                                          view_projection != nullptr);
}

void ParkRuntime::State::prepare_world_poses(float interpolation) {
    frame.runtime_overlays.clear();

    //Every listed native mobile kind shares this base/mobile prefix. The
    //remaining role/person/chat data stays with its actual owning record.
    struct MobilePrefix {
        MapObjectBase base;
        MobileEntityState mobile;
    };

    for (auto* row = simulation.world->entities.active_head; row; row = row->active_next) {
        auto* record = row->payload;
        if (!record || record->retirement_pending)
            continue;
        if (record->kind == ThingKind::person
            || record->kind == ThingKind::chat_presence
            || is_staff_record_kind(record->kind)) {
            auto& entity = *reinterpret_cast<MobilePrefix*>(record);
            update_mobile_entity_render_pose(*simulation.world, entity.base, entity.mobile, interpolation);
        } else if (record->kind == ThingKind::park_object) {
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(record);
            if (!object.flash_unconnected_exit)
                continue;
            const auto* definition =
                find_object_definition_by_id(&simulation.world->resources.object_catalog, object.definition_id);
            if (!definition
                || definition->which_ui_type != ObjectUiCategory::rides
                || has_clear_entry_exit_path(*simulation.world, object))
                continue;
            if (!object.exit_cell || object.exit_cell > map_cell_count)
                throw std::out_of_range("unconnected ride exit cell");
            const auto& exit = simulation.world->simulation.current->map_cells[object.exit_cell - 1].base;
            if (const auto* neighbor = resolve_world_cell_from_selector_flag_byte(*simulation.world,
                                                                                  &exit,
                                                                                  get_world_cell_direction(&exit)))
                frame.runtime_overlays.push_back({ neighbor->base.index, 1 });
        }
    }
    frame.mobile_poses_ready = true;
}

void ParkRuntime::build_scene(MeshScene& scene) {
    state_->build_scene_geometry(scene);
    prepare_scene_atmosphere(scene, state_->render_camera());
}

void ParkRuntime::build_scene(MeshScene& scene, const SceneCamera& camera, float aspect) {
    const auto view_projection = camera.view_projection(aspect);
    state_->build_scene_geometry(scene, &view_projection);
    prepare_scene_atmosphere(scene, camera);
    append_staff_interaction_overlays(*state_->simulation.world, scene);
    const auto& world_lighting =
        state_->presentation.lobby_lighting ? *state_->presentation.lobby_lighting : state_->rendering.lighting;
    const WorldSpriteSceneSettings sprite_settings{ state_->rendering.sprite_shadow_quality,
                                                    world_lighting.lights,
                                                    std::nullopt };
    state_->rendering.sprite_scene
        .append(*state_->simulation.world, camera, aspect, scene, !state_->frame.mobile_poses_ready, sprite_settings);
    {
        FrameTimer timer{ FrameStage::particles };
        state_->rendering.particle_scene.append(*state_->simulation.world,
                                                *state_->simulation.particles,
                                                camera,
                                                aspect,
                                                scene);
    }
    append_world_runtime_overlays(*state_->simulation.world, scene, state_->frame.runtime_overlays);
}

void ParkRuntime::prepare_scene_atmosphere(MeshScene& scene, const SceneCamera& camera, ParkScenePass pass) const {
    const auto& s = *state_;
    auto colors = s.rendering.atmosphere_colors;
    //7C20E7/7C3547 bracket the actual ParkMap with fog colour zero. Keep
    //the override in this frame, so close/re-entry needs no global restoration.
    if (pass == ParkScenePass::park_map)
        colors.fog_argb = 0;
    const auto atmosphere = resolve_world_atmosphere(*s.simulation.world, colors, camera);
    scene.atmosphere = atmosphere;
    scene.sprites.atmosphere = atmosphere;
}

void ParkRuntime::build_screen_particles(EffectScene& output, float aspect) {
    output.vertices.clear();
    output.indices.clear();
    output.draws.clear();
    output.atmosphere.reset();
    state_->rendering.particle_scene.append_screen(*state_->simulation.world,
                                                   *state_->simulation.particles,
                                                   aspect,
                                                   output);
}

void ParkRuntime::set_render_camera(const SceneCamera& camera) {
    const auto eye = camera.eye();
    if (!std::isfinite(camera.distance) || camera.distance <= 0 || !std::ranges::all_of(eye, [](float coordinate) {
            return std::isfinite(coordinate);
        }))
        throw std::invalid_argument("invalid park render camera");
    if (state_->presentation.lobby_view) {
        //ATM owns a presentation pose, not the saved park focus/zoom. Keep the
        //ordinary camera intact so closing the chooser restores its real view.
        state_->presentation.audio_camera = camera;
        return;
    }
    //Native4691B7..469247 forms a camera offset of length gf_CameraZoom,
    //then4693BE..469408 rotates/translates it into the world-space eye. FOV
    //does not enter either input consumed by the mobile pose updater.
    if (!state_->frame.native_camera_zoom)
        state_->frame.native_camera_zoom = state_->simulation.world->view.mobile.camera_zoom;
    auto& view = state_->simulation.world->view.camera;
    if (!(state_->simulation.world->view.flags & 0x16u)) {
        //Normal diagnostic/orbit input may author the native focus. POI and
        //inspection own those fields themselves; publishing a previous host
        //camera must not replace a newly selected point or animated attachment.
        state_->simulation.world->view.mobile.camera_zoom = camera.distance;
        view.focus_position = camera.target;
        view.heading_radians = camera.yaw;
        view.near_clip_distance = camera.near_plane;
        view.far_clip_distance = camera.far_plane;
    }
    state_->presentation.audio_camera = camera;
    state_->publish_render_camera(false);
}
} //namespace simcoaster
