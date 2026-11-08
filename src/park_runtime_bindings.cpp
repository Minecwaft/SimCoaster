#include "simcoaster/require_bound.hpp"
#include "simcoaster/rotation_tables.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/application_coaster_source.hpp"
#include "simcoaster/application_frontend.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/campaign_profile.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/layout_world_runtime.hpp"
#include "simcoaster/mesh_surface.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/park_authoring.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/park_construction_refresh.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/particle_runtime.hpp"
#include "simcoaster/resource_variant_io.hpp"
#include "simcoaster/rsse_people.hpp"
#include "simcoaster/rsse_runtime.hpp"
#include "simcoaster/rsse_save.hpp"
#include "simcoaster/rsse_tour_runtime.hpp"
#include "simcoaster/save_container.hpp"
#include "simcoaster/scape_runtime.hpp"
#include "simcoaster/sprite_catalog.hpp"
#include "simcoaster/terrain_sky_shadow.hpp"
#include "simcoaster/terrain_texture.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/weather_runtime.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_calendar_ui.hpp"
#include "simcoaster/world_camera_render.hpp"
#include "simcoaster/world_coaster_guest_restore.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_cursor_pick.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_graphics_profile.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_particle_scene.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_scene.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include "simcoaster/world_source_startup.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_staff_view.hpp"
#include "simcoaster/world_startup.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "world_research_balance.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>

#include "park_runtime_bindings.hpp"
#include "simcoaster/resource_key.hpp"

namespace simcoaster {
void
ParkRuntimeBindings::resource_visibility(WorldMeshInstance& instance, std::uint32_t hidden, std::uint32_t visible) {
    for (const auto& resource : instance.playback.model.resources) {
        auto& node = instance.playback.model.nodes.at(resource.node_index);
        if (resource.flags & hidden)
            node.flags |= 0x8000u;
        else if (resource.flags & visible)
            node.flags &= ~0x8000u;
    }
}

void ParkRuntimeBindings::camera_notification(WorldRuntime& c) {
    ++c.view.camera.render_revision;
    c.view.camera.render_pose.reset();
    c.view.camera.render_base_pose.reset();
    initialize_world_camera_render(c);
    ui_runtime().request_action(WorldCameraAction{ 0, 0 });
}

void ParkRuntimeBindings::set_camera_controls_visible(bool mode) {
    auto& controls = ui_runtime().controls();
    if (auto* group = controls.find(ui->secondary_root(), 39700))
        if (auto* control = controls.find(group, 39704))
            controls.set_visible(*control, mode);
}

ParticleEffect* ParkRuntimeBindings::effect(std::uint32_t handle) {
    return particles ? find_particle_effect(*particles, handle) : nullptr;
}

MeshModelLibrary* ParkRuntimeBindings::library(const std::filesystem::path& directory) {
    auto path = directory;
    if (!std::filesystem::is_regular_file(path) && !std::filesystem::is_directory(path))
        path += ".wad";
    if (!std::filesystem::exists(path))
        return nullptr;
    const auto key = canonical_asset_key(std::filesystem::weakly_canonical(path).native_encoded_string());
    if (const auto found = libraries_.find(key); found != libraries_.end())
        return &found->second;
    auto opened = open_mesh_model_library(path);
    if (!opened)
        throw std::runtime_error(describe_mesh_error(opened.error()));
    return &libraries_.emplace(key, std::move(*opened)).first->second;
}

void ParkRuntimeBindings::refresh_calendar_date_label(WorldRuntime& c, CalendarReportFields report) {
    calendar_ui::refresh_calendar_date_label(c, report);
}

void ParkRuntimeBindings::refresh_park_statistics_month(WorldRuntime& c) {
    calendar_ui::refresh_park_statistics_month(c);
}

void ParkRuntimeBindings::refresh_financial_statistics_month(WorldRuntime& c) {
    calendar_ui::refresh_financial_statistics_month(c);
}

void ParkRuntimeBindings::refresh_outstanding_loans_month(WorldRuntime& c) {
    calendar_ui::refresh_outstanding_loans_month(c);
}

WorldUiRuntime& ParkRuntimeBindings::ui_runtime() {
    return require_bound(ui, "World UI owner is not bound");
}

void ParkRuntimeBindings::set_selected_ui_checkbutton_id(WorldRuntime&, UiControl* control, int id) {
    if (control)
        ui_runtime().controls().set_selected(*control, id);
}

int ParkRuntimeBindings::get_selected_ui_checkbutton_id(WorldRuntime&, UiControl* control) {
    return control ? control->selected_id : 0;
}

UiControl* ParkRuntimeBindings::find_ui_control(WorldRuntime&, UiControl* parent, std::uint32_t id) {
    return parent ? ui_runtime().controls().find(parent, id) : nullptr;
}

void ParkRuntimeBindings::dispatch_ui_control_event(WorldRuntime&, UiControl* control, int event, int id, int value) {
    (void)ui_runtime().controls().dispatch(control, event, static_cast<std::uint32_t>(id), value);
}

std::intptr_t ParkRuntimeBindings::invoke_secondary_ui_event_callback(WorldRuntime&,
                                                                      UiControl* control,
                                                                      int event,
                                                                      std::uint32_t id,
                                                                      std::intptr_t value) {
    if (!control)
        return 0;
    return ui_runtime().controls().secondary(*control, { event, id, value });
}

void ParkRuntimeBindings::set_ui_visibility(WorldRuntime&, UiControl* control, std::int16_t visible) {
    if (control)
        ui_runtime().controls().set_visible(*control, visible != 0);
}

void ParkRuntimeBindings::send_ui_count_value(WorldRuntime&, UiControl* control, std::int32_t value) {
    if (!control)
        return;
    const auto text = std::to_string(value);
    control->value = value; //Numeric attachment value, not a0..1024 bar.
    const std::u16string wide{ text.begin(), text.end() };
    ui_runtime().controls().set_text(*control, wide);
}

void ParkRuntimeBindings::start_mail_count_animation(WorldRuntime&,
                                                     UiControl* control,
                                                     MailCountAnimation kind,
                                                     int interval) {
    ui_runtime().animate_mail(control, static_cast<std::int32_t>(kind), std::chrono::milliseconds{ interval });
}

void ParkRuntimeBindings::set_primary_ui_event_callback(
    WorldRuntime& c,
    UiControl* control,
    std::intptr_t (*callback)(WorldRuntime&, UiControl*, int, std::uint32_t, std::intptr_t)) {
    if (!control)
        throw std::invalid_argument("Missing callback control");
    UiCallback binding;
    if (callback)
        binding = [&c, callback](UiControl& target, const UiEvent& event) {
            //UiRuntime retains the native packed key/modifier word for edit/list
            //widgets; the already ported World playback callback consumes VK.
            const auto id = (event.type == 65546 || event.type == 65547) && event.id > 255 ? event.id >> 8 : event.id;
            return callback(c, &target, event.type, id, event.value);
        };
    ui_runtime().controls().set_primary_callback(*control, std::move(binding));
}

void ParkRuntimeBindings::set_focused_ui_control(WorldRuntime&, UiControl* control, int) {
    ui_runtime().controls().set_focus(control);
}

void ParkRuntimeBindings::center_game_pointer(WorldRuntime&) {
    ui_runtime().controls().pointer_move({ 1024, 768 });
    ui_runtime().request_action(WorldCenterPointerAction{});
}

void ParkRuntimeBindings::open_objectives_panel(WorldRuntime&) {
    ui_runtime().open_objectives();
}

void ParkRuntimeBindings::configure_ui_control_particles(WorldRuntime& c, UiControl* control) {
    if (!control || !particles)
        throw std::invalid_argument("Missing control particle owner");
    ui_runtime().configure_control_particles(control);
    const auto& bounds = control->rect;
    const auto width = bounds.width(), height = bounds.height();
    const auto center_x = static_cast<float>(static_cast<double>(width / 2 + bounds.left - 1024) * 0.0009765625);
    const auto center_y = static_cast<float>(static_cast<double>(height / 2 + bounds.top - 768) * 0.0013020834);
    const auto half_width = static_cast<float>(width * 0.00048828125);
    const auto half_height = static_cast<float>(height * 0.00065104169);
    const auto count = std::clamp(
        2
            - fixed::truncate_integer((static_cast<double>(half_width) * half_height - static_cast<double>(0.0016f))
                                      / static_cast<double>(0.0051f)
                                      * -3.0),
        2,
        5);
    //Native7BACC0 uses authored template35/Button; this group owns effects,
    //not a cosmetic overlay quad. Geometry follows7BAAC0 each UI frame.
    (void)resize_control_particle_group(*particles, count, 35);
    auto& motion = control_particle_motion_;
    motion.position_from = motion.position_to = { center_x, center_y };
    motion.extent_from = { half_width * 0.33333334f, half_height * 0.33333334f };
    motion.extent_to = { half_width, half_height };
    motion.start_ms = static_cast<float>(fixed::word(advisor_presentation_time_ms(c)));
    motion.duration_ms = 400.f;
    motion.phase_scale = 0.40000001f;
}

void ParkRuntimeBindings::update_control_particles(WorldRuntime& c) {
    if (!particles || particles->control_particle_handles.empty())
        return;
    const auto& sine = particle_phase_sines();
    const auto& motion = control_particle_motion_;
    const auto t = static_cast<float>(
        (static_cast<double>(fixed::word(advisor_presentation_time_ms(c))) - motion.start_ms) / motion.duration_ms);
    const auto extent_fraction = t <= 1.f ? t : 1.f;
    const auto center = [&](std::size_t axis) {
        return static_cast<float>((static_cast<double>(motion.position_to[axis]) - motion.position_from[axis]) * t
                                  + motion.position_from[axis]);
    };
    const auto center_x = center(0), center_y = center(1);
    auto& handles = particles->control_particle_handles;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        double integral;
        const auto phase = static_cast<std::uint32_t>(
            fixed::truncate_integer(std::modf(static_cast<double>(i) / static_cast<double>(handles.size())
                                                  + static_cast<double>(t) * motion.phase_scale,
                                              &integral)
                                    * 512.0));
        const auto sn = static_cast<float>(sine[phase & 511u]) / 256.f;
        const auto co = static_cast<double>(sine[(phase + 128u) & 511u]) / 256.0;
        const auto x_extent = (static_cast<double>(motion.extent_to[0]) - motion.extent_from[0]) * extent_fraction
                            + motion.extent_from[0];
        const auto y_extent = (static_cast<double>(motion.extent_to[1]) - motion.extent_from[1]) * extent_fraction
                            + motion.extent_from[1];
        const auto x_offset = static_cast<float>(x_extent * sn);
        const auto normalized_x = static_cast<float>(static_cast<double>(x_offset) + center_x);
        set_particle_effect_position(
            c,
            handles[i],
            { fixed::truncate_integer((static_cast<double>(normalized_x) + 1.0) * 50000.0),
              0,
              fixed::truncate_integer((static_cast<double>(center_y) - co * y_extent + 1.0) * 37500.0) });
    }
}

UiControl* ParkRuntimeBindings::create_world_animation_playback_window(WorldRuntime&) {
    return ui_runtime().create_playback_window();
}

void ParkRuntimeBindings::disable_command_families_and_set_disabled_flag(WorldRuntime& c) {
    command_families_disabled = true;
    c.advisor.presentation.commands_enabled = false;
}

void ParkRuntimeBindings::restore_command_families_state(WorldRuntime& c, bool disabled) {
    command_families_disabled = disabled;
    c.advisor.presentation.commands_enabled = !disabled;
}

void ParkRuntimeBindings::close_ui_roots_and_hide_persistent_roots(WorldRuntime& c) {
    if (c.construction.editor) {
        c.construction.editor->cancel(c, construction_runtime());
        c.construction.editor->activate(c, 0, false);
    }
    ui_runtime().close_panel();
    ui_runtime().hide_persistent();
}

void ParkRuntimeBindings::restore_persistent_roots_after_transition(WorldRuntime& c) {
    ui_runtime().restore_persistent();
    command_families_disabled = false;
    c.advisor.presentation.commands_enabled = true;
}

void ParkRuntimeBindings::finish_intro_sequence(WorldRuntime&, std::uint8_t force) {
    ui_runtime().request_action(WorldFinishIntroAction{ force });
}

void ParkRuntimeBindings::request_resource_source_change_overlay(WorldRuntime&, int source, std::uint8_t force) {
    ui_runtime().request_action(WorldSourceChangeAction{ source, force });
}

void ParkRuntimeBindings::set_renderer_fog_distance_range(WorldRuntime& c, float start, float end) {
    c.view.fog_distance_start = start;
    c.view.fog_distance_end = end;
}

void ParkRuntimeBindings::focus_world_view_camera_on_park_object_center(WorldRuntime& c,
                                                                        ParkObjectRuntimeView* object) {
    if (!object)
        return;
    const auto* definition = get_park_object_definition(c, object);
    if (!definition || !definition->balance || !definition->balance->Info.Shape || !object->top_left)
        throw std::logic_error("Camera focus requires a placed object shape");
    //Native466050 intentionally uses Shape.width on BOTH axes.
    const auto half_width = static_cast<double>(definition->balance->Info.Shape->width) * 5.0;
    const auto cell = static_cast<std::uint32_t>(object->top_left) - 1u;
    auto& focus = c.view.camera.focus_position;
    focus[0] = static_cast<float>(static_cast<double>(cell & 127u) * 10.0 + half_width);
    focus[2] = static_cast<float>(static_cast<double>(cell >> 7) * 10.0 + half_width);
    camera_notification(c);
}

void ParkRuntimeBindings::select_camera_ui_roots(WorldRuntime& c, bool secondary) {
    //4F8080/550DB0: scheduler17/18 and transition/congratulations UI retain
    //their routing owner. Changing projection does not bypass those gates.
    if (c.mode.scheduler == 17
        || c.mode.scheduler == 18
        || !c.simulation.current
        || c.simulation.current->saved.state == WorldState::transition
        || c.messages.congratulations_popup)
        return;
    auto& controls = ui_runtime().controls();
    if (!secondary) {
        if (c.ui.interaction_toolbar)
            if (auto* group = controls.find(c.ui.interaction_toolbar, 64))
                controls.set_selected(*group, 67);
        c.staff.interaction.pointer_mode_code = 1;
    }
    controls.dispatch(ui->primary_root(), 6, secondary ? 0u : 1u);
    controls.dispatch(ui->secondary_root(), 6, secondary ? 1u : 0u);
    controls.set_focus(secondary                                             ? ui->secondary_root()
                       : c.ui.active_root && controls.owns(c.ui.active_root) ? c.ui.active_root
                                                                             : ui->primary_root());
}

void ParkRuntimeBindings::special_camera_terrain(WorldRuntime& c, bool special) {
    if (!scene)
        throw std::logic_error("Special camera terrain owner is not bound");
    if (special) {
        if (scene->procedural_terrain_enabled() && !scene->special_view_terrain()) {
            //4B0100 owes restoration only when procedural blending was active.
            c.view.flags |= 0x200u;
            scene->set_special_view_terrain(true);
        }
    } else if (c.view.flags & 0x200u) {
        scene->set_special_view_terrain(false);
        c.view.flags &= ~0x200u;
    }
}

void ParkRuntimeBindings::finish_camera_mode_transition(WorldRuntime& c) {
    if (c.view.flags & 0x16u)
        for (std::size_t i = 1; i < c.resources.meshes->slot_limit(); ++i)
            if (auto* instance = c.resources.meshes->instance(static_cast<std::uint32_t>(i)))
                resource_visibility(*instance, 0x4000000u, 0x8000000u);
    c.view.flags &= ~0x1000u;
    //Native500A40 is precisely VisitorLocator::close (event4), rather than
    //a request to close every open management panel.
    auto& controls = ui_runtime().controls();
    if (auto* locator = controls.find_recursive(ui->primary_root(), 39000))
        controls.dispatch(locator, 4);
}

std::optional<WorldViewInspectionAttachment> ParkRuntimeBindings::world_view_inspection_attachment(WorldRuntime& c) {
    if (!(c.view.flags & 4u) || !inspection_resource_ || !inspection_slot_)
        return {};
    auto* instance = c.resources.meshes->instance(inspection_slot_);
    if (!instance
        || instance->generation != inspection_generation_
        || *inspection_resource_ >= instance->playback.model.resources.size()
        || *inspection_resource_ >= instance->embedded.size()) {
        if (ui)
            restore_special_camera_modes(c, true);
        else
            abandon_inspection_for_shutdown(c);
        return {};
    }
    const auto index = *inspection_resource_;
    const auto& resource = instance->playback.model.resources[index];
    if (!(resource.flags & 0x1000u) || resource.id != 1 || !(instance->embedded[index].flags & 4u)) {
        if (ui)
            restore_special_camera_modes(c, true);
        else
            abandon_inspection_for_shutdown(c);
        return {};
    }
    //Native499AB0 refreshes the selected auxiliary clone at entry; the
    //existing visible activation repairs any deferred animated pose before
    //evaluating the same live resource transform on subsequent frames.
    const auto slot = inspection_slot_;
    const auto generation = inspection_generation_;
    const auto revision = inspection_binding_revision_;
    const auto activated = c.resources.meshes->mark_visible(c, slot);
    if (!activated)
        throw activated.error();
    instance = c.resources.meshes->instance(slot);
    if (inspection_slot_ != slot
        || inspection_binding_revision_ != revision
        || !instance
        || instance->generation != generation)
        return {};
    const auto poses = evaluate_mesh_pose(instance->playback.model);
    if (!poses)
        throw poses.error();
    const auto transform = mesh_attachment_transform(instance->playback.model, *poses, index);
    if (!transform)
        throw transform.error();
    return WorldViewInspectionAttachment{ inspection_slot_, index, inspection_binding_revision_, *transform };
}

void
ParkRuntimeBindings::retire_world_view_inspection_mesh(WorldRuntime& c, std::uint32_t slot, std::uint64_t generation) {
    if (slot == inspection_slot_ && generation == inspection_generation_) {
        if (ui)
            restore_special_camera_modes(c, true);
        else
            abandon_inspection_for_shutdown(c);
    }
}

void ParkRuntimeBindings::abandon_inspection_for_shutdown(WorldRuntime& c) noexcept {
    //Teardown after UI destruction still owns physical clone state. It must
    //neither call a dead UiRuntime nor carry a camera binding into slot reuse.
    if (auto* instance = c.resources.meshes->instance(inspection_slot_);
        instance && instance->generation == inspection_generation_) {
        if (inspection_resource_ && *inspection_resource_ < instance->embedded.size())
            instance->embedded[*inspection_resource_].flags &= ~4u;
        instance->playback.runtime_flags &= ~0x2000u;
    }
    if (c.resources.meshes->selected_slot == inspection_slot_)
        c.resources.meshes->selected_slot = 0;
    inspection_slot_ = 0;
    inspection_generation_ = 0;
    inspection_resource_.reset();
    ++inspection_binding_revision_;
    c.view.flags &= ~(2u | 4u | 0x400u | 0x800u | 0x1000u);
    c.view.camera.render_pose.reset();
    c.view.camera.render_base_pose.reset();
    ++c.view.camera.render_revision;
}

void ParkRuntimeBindings::toggle_world_view_point_of_interest(WorldRuntime& c) {
    if (!c.simulation.current
        || !c.resources.meshes
        || !c.weather.map_heightfield_width
        || !c.weather.map_heightfield_height
        || c.weather.map_heightfield_width > 128
        || c.weather.map_heightfield_height > 128)
        throw std::logic_error("Point-of-interest camera requires a live terrain grid");
    auto& camera = c.view.camera;
    auto& flags = c.view.flags;
    const auto& pick = c.staff.interaction.cursor.ground_point;
    bool accepted = (flags & 0x16u) != 0;
    if (!accepted) {
        const auto x = std::clamp(fixed::truncate_integer(static_cast<double>(pick[0]) * .1),
                                  0,
                                  static_cast<int>(c.weather.map_heightfield_width) - 1);
        const auto y = std::clamp(fixed::truncate_integer(static_cast<double>(pick[2]) * .1),
                                  0,
                                  static_cast<int>(c.weather.map_heightfield_height) - 1);
        const auto& cell = c.simulation.current->map_cells.at(static_cast<std::size_t>(y * 128 + x));
        const auto type = cell.base.common.type;
        accepted = (type == 30 && !cell.is_obstacle) || type == 1 || type == 3 || type == 9 || type == 0;
    }
    if (!accepted) {
        update_objectives_panel_and_queue_mission_advisor_action(c);
        return;
    }
    //467030(op2,0,toggle=true,slot0,force=false) clears view-mode flag2048
    //at A1465C even when a ride/blocked camera prevents the toggle. It is
    //independent of the input flags at A14654 (including zoom-limit512).
    const auto before = flags & 0x16u;
    flags &= ~0x800u;
    if (!(flags & 0x84u)) {
        flags = (flags & ~255u) | ((flags & 255u) & 0x43u);
        flags ^= 2u;
        set_camera_controls_visible(true);
        world_operations::queue_scored_advisor_action(c, 451);
        if ((flags & 2u) && !before) {
            //Mesh attribute-buffer aliasing is absent from the owned modern
            //model: there is no borrowed renderer attribute pointer to restore.
            select_camera_ui_roots(c, true);
            special_camera_terrain(c, true);
            camera.near_clip_distance = .1f;
        } else if (!(flags & 2u) && before) {
            select_camera_ui_roots(c, false);
            special_camera_terrain(c, false);
            camera.near_clip_distance = 10.f;
        }
    }
    finish_camera_mode_transition(c);
    if (flags & 0x16u) {
        camera.saved_focus_position = camera.focus_position;
        camera.saved_heading_radians = camera.heading_radians;
        camera.focus_position = { pick[0], 0.f, pick[2] };
        if (reload_sprite_family)
            reload_sprite_family(true);
    } else {
        camera.focus_position = camera.saved_focus_position;
        camera.heading_radians = camera.saved_heading_radians;
        if (reload_sprite_family)
            reload_sprite_family(false);
    }
    update_objectives_panel_and_queue_mission_advisor_action(c);
    camera_notification(c);
}

void ParkRuntimeBindings::restore_special_camera_modes(WorldRuntime& c, bool clear_previous_poi) {
    auto& camera = c.view.camera;
    const auto original = c.view.flags;
    if (!(original & 6u))
        return;
    if (original & 2u) {
        toggle_world_view_point_of_interest(c);
        return;
    }
    auto flags = original & ~0x800u;
    if (auto* instance = c.resources.meshes->instance(inspection_slot_);
        instance && instance->generation == inspection_generation_) {
        if (inspection_resource_ && *inspection_resource_ < instance->embedded.size())
            instance->embedded[*inspection_resource_].flags &= ~4u;
        instance->playback.runtime_flags &= ~0x2000u;
        resource_visibility(*instance, 0x20000000u, 0x10000000u);
    }
    if (c.resources.meshes->selected_slot == inspection_slot_)
        c.resources.meshes->selected_slot = 0;
    inspection_slot_ = 0;
    inspection_generation_ = 0;
    inspection_resource_.reset();
    ++inspection_binding_revision_;
    flags = (flags & ~255u) | ((flags & 255u) & 0x45u);
    flags &= ~4u;
    camera.heading_radians = camera.saved_ride_y_rotation;
    camera.pitch_radians = camera.saved_ride_x_rotation;
    camera.focus_position = inspection_saved_focus_;
    if (original & 0x400u) {
        //Native inspection exit first re-enters the previous POI mode;
        //the outer50B4E0 wrapper immediately toggles it off as well.
        flags &= ~0x400u;
        if (clear_previous_poi) {
            camera.focus_position = camera.saved_focus_position;
            camera.heading_radians = camera.saved_heading_radians;
        }
    }
    set_camera_controls_visible(true);
    c.view.flags = flags;
    if (original & 0x400u) {
        //50B4E0 restores inspection to its previous POI mode, then executes
        //the real POI exit (including451 and its terrain/sprite obligations).
        c.view.flags |= 2u;
        if (clear_previous_poi)
            toggle_world_view_point_of_interest(c);
        else {
            //Direct4657F0 returns to the retained POI without invoking4675A0:
            //no extra451, sprite-family cycle or root/terrain exit and re-entry.
            finish_camera_mode_transition(c);
            camera.near_clip_distance = .1f;
        }
        return;
    }
    special_camera_terrain(c, false);
    finish_camera_mode_transition(c);
    camera.near_clip_distance = 10.f;
    select_camera_ui_roots(c, false);
    if (reload_sprite_family)
        reload_sprite_family(false);
    update_objectives_panel_and_queue_mission_advisor_action(c);
    camera_notification(c);
}

void ParkRuntimeBindings::restore_world_view_from_special_modes(WorldRuntime& c) {
    restore_special_camera_modes(c, true);
}

void ParkRuntimeBindings::prepare_park_object_animation(WorldRuntime& c,
                                                        ParkObjectRuntimeView* object,
                                                        std::int32_t force_transition,
                                                        std::int32_t fallback_context) {
    if (!object)
        throw std::invalid_argument("Missing animation park object");
    const auto* definition = get_park_object_definition(c, object);
    if (!definition || !definition->balance)
        throw std::logic_error("Animation object has no definition balance");
    const auto& balance = *definition->balance;
    object->gone_first_person = 1;
    if (balance.Bumper.WhichTrackType == 3) {
        const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object->mesh_instance_id);
        if (opening && opening->geometry_peer_reference_count) {
            object->gone_first_person = 0;
            return;
        }
    } else if (balance.Info.IsObstacle != 1 && balance.Info.IsCameraPath != 1) {
        world_operations::set_script_slot(c, world_operations::find_script(c, object->script_id), 12, 1);
    }
    std::uint32_t slot = object->mesh_instance_id;
    bool has_track_mesh{}, has_auxiliary_mesh{};
    if (object->track_ride_handle && track_rides)
        if (const auto* ride = track_rides->find(object->track_ride_handle);
            ride && ride->first_simulation && *ride->first_simulation < ride->simulations.size()) {
            const auto& simulation = ride->simulations[*ride->first_simulation];
            if (simulation.flags & static_cast<std::uint32_t>(TrackSimulationFlag::primary)) {
                slot = simulation.mesh_instance_id;
                has_track_mesh = slot != 0;
            }
        }
    if (!has_track_mesh)
        if (const auto* script = world_operations::find_script(c, object->script_id)) {
            const auto auxiliary = script->auxiliary_runtime_object_handle;
            if (auxiliary && auxiliary < c.scripts.tours.slots.size() && c.scripts.tours.slots[auxiliary]) {
                slot = c.scripts.tours.slots[auxiliary]->entries[0].mesh_instance_handle;
                has_auxiliary_mesh = true;
            }
        }
    //4668C0 resolves a coaster station through 476AB0/476B10 before
    //looking up camera resource 1. The camera belongs to the selected car.
    if (const auto* instance = c.resources.meshes->instance(slot);
        instance && (instance->playback.runtime_flags & 0x40000u)) {
        const auto* coasters = c.resources.meshes->coasters();
        const auto* ride = coasters ? coasters->find(slot) : nullptr;
        slot = 0;
        if (ride && (ride->train_state.runtime_flags & 0x800u)) {
            const auto& trains = ride->train_state.trains;
            //A null native current-train pointer selects the first train;
            //the owned index has that same initial value of zero.
            const auto& train = trains.at(ride->train_state.current_train_index);
            slot = train.cars.at(train.selected_car).mesh_slot;
        }
    }
    auto& camera = c.view.camera;
    auto& flags = c.view.flags;
    const auto before = flags & 0x16u;
    if (flags & 4u) {
        const bool return_to_poi = (flags & 0x400u) != 0;
        const auto poi_focus = inspection_saved_focus_;
        const auto poi_heading = camera.saved_ride_y_rotation;
        restore_special_camera_modes(c, false);
        if (return_to_poi) {
            flags |= 2u;
            camera.focus_position = poi_focus;
            camera.heading_radians = poi_heading;
            camera.near_clip_distance = .1f;
            select_camera_ui_roots(c, true);
            special_camera_terrain(c, true);
        }
        if (reload_sprite_family)
            reload_sprite_family(balance.Info.FirstPersonSprites == 1 || (flags & 2u));
        camera_notification(c);
        return;
    }
    if (auto* old = c.resources.meshes->instance(inspection_slot_); old && old->generation == inspection_generation_) {
        if (inspection_resource_ && *inspection_resource_ < old->embedded.size())
            old->embedded[*inspection_resource_].flags &= ~4u;
        if (flags & 4u)
            old->playback.runtime_flags &= ~0x2000u;
    }
    inspection_resource_.reset();
    inspection_slot_ = 0;
    inspection_generation_ = 0;
    ++inspection_binding_revision_;
    if (auto* instance = c.resources.meshes->instance(slot)) {
        const auto& resources = instance->playback.model.resources;
        const auto selected = std::ranges::find_if(resources, [](const auto& row) {
            return (row.flags & 0x1000u) && row.id == 1;
        });
        if (selected != resources.end() && (!(flags & 0x92u) || force_transition)) {
            inspection_saved_focus_ = camera.focus_position;
            camera.saved_ride_x_rotation = camera.pitch_radians;
            camera.saved_ride_y_rotation = camera.heading_radians;
            inspection_resource_ = static_cast<std::size_t>(selected - resources.begin());
            inspection_slot_ = slot;
            inspection_generation_ = instance->generation;
            if (*inspection_resource_ < instance->embedded.size())
                instance->embedded[*inspection_resource_].flags |= 4u;
            c.resources.meshes->selected_slot = slot;
            flags = (flags & ~255u) | ((flags & 255u) & 0x45u);
            flags |= 4u;
            if (before)
                flags |= 0x400u;
            const bool initialize_input =
                c.mode.scheduler == 17
                || (!has_track_mesh && !has_auxiliary_mesh && balance.UsageInfo.ConstrainCamera != 0);
            if (!before && initialize_input) {
                flags |= 0x800u;
                camera.heading_input = camera.pitch_input = 0.f;
            }
            camera.near_clip_distance = .1f;
            instance->playback.runtime_flags |= 0x2000u;
            for (std::size_t i = 1; i < c.resources.meshes->slot_limit(); ++i)
                if (auto* other = c.resources.meshes->instance(static_cast<std::uint32_t>(i)))
                    resource_visibility(*other, 0x4000000u, 0x8000000u);
            resource_visibility(*instance, 0x10000000u, 0x20000000u);
            if (!before) {
                select_camera_ui_roots(c, true);
                special_camera_terrain(c, true);
            }
            //Original5E5B20->4668C0->467030 hides the primary toolbar but
            //leaves its independent Button particle group alive. POI entry
            //clears it via54D660; apply that visibility obligation here too.
            //The user approved repairing this original ride-entry omission.
            clear_control_particle_group(c);
            set_camera_controls_visible(false);
        }
    }
    (void)fallback_context; //Native passes it to an unused transition context.
    if (reload_sprite_family)
        reload_sprite_family(balance.Info.FirstPersonSprites == 1 || (flags & 2u));
    flags &= ~0x1000u;
    c.resources.renderer_cache.flags |= 0x40000000u;
    camera_notification(c);
}

ParkConstructionRuntime& ParkRuntimeBindings::construction_runtime() {
    return require_bound(construction, "construction owner is not bound");
}

ParkRuntimeBindings::ParkRuntimeBindings(std::filesystem::path root) : root_(std::move(root)) {}

void ParkRuntimeBindings::replay_tick(WorldRuntime& c) {
    if (!layout_replay || !layout_backend)
        throw std::logic_error("Layout recording/replay owner is not bound");
    const auto result = layout_replay->tick(c, *layout_backend);
    if (!result)
        throw std::runtime_error(describe_layout_error(result.error()));
}

void* ParkRuntimeBindings::allocate(Allocation kind, std::size_t count) {
    auto* result = ::operator new(count ? count : 1, std::nothrow);
    if (result && kind != Allocation::payload)
        try {
            allocations_.emplace(result, kind);
        } catch (...) {
            ::operator delete(result);
            throw;
        }
    return result;
}

void ParkRuntimeBindings::release(Allocation kind, void* pointer) {
    if (!pointer)
        return;
    if (kind == Allocation::payload) {
        ::operator delete(pointer);
        return;
    }
    const auto found = allocations_.find(pointer);
    if (found == allocations_.end() || found->second != kind)
        throw std::logic_error("Mismatched native runtime allocation lifetime");
    allocations_.erase(found);
    ::operator delete(pointer);
}

std::optional<std::vector<std::byte>> ParkRuntimeBindings::read_asset(std::string_view name) {
    if (resource_pump)
        resource_pump();
    const auto path = root_ / std::filesystem::path{ std::string{ name } };
    if (std::filesystem::is_regular_file(path)) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Cannot open " + path.native_encoded_string());
        const auto end = file.tellg();
        if (end < 0)
            throw std::runtime_error("Cannot size asset");
        std::vector<std::byte> data(static_cast<std::size_t>(end));
        file.seekg(0);
        if (!data.empty()
            && !file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
            throw std::runtime_error("Cannot read " + path.native_encoded_string());
        return data;
    }
    for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        if (auto* source = library(parent)) {
            const auto relative = path.lexically_relative(parent).generic_native_encoded_string();
            auto data = source->read_resource(relative);
            if (!data)
                throw std::runtime_error(describe_mesh_error(data.error()));
            if (*data)
                return std::move(**data);
        }
        if (parent == parent.parent_path() || parent == root_)
            break;
    }
    return std::nullopt;
}

std::optional<std::string> ParkRuntimeBindings::read_balance_asset(WorldRuntime&, std::string_view path) {
    auto bytes = read_asset(path);
    if (!bytes)
        return std::nullopt;
    return std::string{ reinterpret_cast<const char*>(bytes->data()), bytes->size() };
}

std::unique_ptr<Stream> ParkRuntimeBindings::open_asset_stream(WorldRuntime&, std::string_view path) {
    auto bytes = read_asset(path);
    if (!bytes)
        return {};
    auto file = std::make_unique<MemoryByteStream>();
    file->bytes = std::move(*bytes);
    return file;
}

std::vector<std::string> ParkRuntimeBindings::find_asset_files(WorldRuntime&, std::string_view pattern) {
    const auto path = root_ / std::filesystem::path{ pattern };
    std::vector<std::string> result;
    if (!std::filesystem::is_directory(path.parent_path()))
        return result;
    const auto mask = canonical_asset_key(path.filename().native_encoded_string());
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        if (entry.is_regular_file()) {
            auto name = entry.path().filename().native_encoded_string();
            if (resource_name_matches(mask, canonical_asset_key(name)))
                result.push_back(std::move(name));
        }
    return result;
}

int ParkRuntimeBindings::load_quickload_index_files(WorldRuntime&,
                                                    std::string_view,
                                                    std::string_view directory,
                                                    std::optional<std::string_view> additional) {
    auto* primary = library(root_ / std::filesystem::path{ directory });
    if (additional)
        (void)library(root_ / std::filesystem::path{ *additional });
    return primary ? 1 : 0;
}

void ParkRuntimeBindings::log_debug_message(WorldRuntime&, std::string_view message) {
    std::clog << message << '\n';
}

void ParkRuntimeBindings::refresh_world_interaction_origin_state(WorldRuntime& c, bool record_action) {
    construction_runtime().refresh_origin(c, record_action);
}

void ParkRuntimeBindings::set_world_interaction_mode(WorldRuntime& c, WorldInteractionMode mode, bool record_action) {
    construction_runtime().set_mode(c, std::to_underlying(mode), record_action);
}

std::uint8_t ParkRuntimeBindings::dispatch_world_interaction(WorldRuntime& c,
                                                             WorldCellCoordinates position,
                                                             WorldInputModifierMask modifiers,
                                                             bool preview) {
    return construction_runtime().dispatch(c, position, modifiers, preview);
}

void ParkRuntimeBindings::remove_park_object_at_world_tile(WorldRuntime& c,
                                                           std::int32_t x,
                                                           std::int32_t y,
                                                           std::int32_t from_track) {
    (void)construction_runtime().demolish(c, { x, y }, from_track != 0);
}

void ParkRuntimeBindings::rebuild_terrain_cell_shading(WorldRuntime& c) {
    auto* model = scene ? scene->terrain() : nullptr;
    if (!model || !model->terrain)
        throw std::logic_error("construction terrain owner is not bound");
    rebuild_construction_terrain_shading(c, *model->terrain);
    //4A629B shades the terrain MODEL's drawable tree. The grid has its own
    //independent lighting kernel (6D0050), not this bilinear normal pass.
    apply_construction_mesh_darkening(*model, model->terrain->view(), TerrainShadowSampling::terrain_centered);
}

void ParkRuntimeBindings::draw_stranded_mobile_cell(WorldRuntime&, const WorldVector3& p) {
    if (!runtime_overlays)
        throw std::logic_error("dynamic World face owner is not bound");
    const auto x = static_cast<std::int32_t>(p[0] / 10), z = static_cast<std::int32_t>(p[2] / 10);
    if (is_world_map_coordinate_in_bounds(x, z))
        runtime_overlays->push_back({ static_cast<CellId>(x + z * 128 + 1), 1 });
}

int ParkRuntimeBindings::selected_resource_source(WorldRuntime& c) {
    const auto name = canonical_asset_key(c.source.selected_resource_source_name);
    for (std::size_t i = 0; i < theme_source_count; ++i)
        if (canonical_asset_key(std::string{ theme_global_source_names[i] }) == name)
            return static_cast<int>(i);
    throw std::invalid_argument("Unknown selected park theme: " + c.source.selected_resource_source_name);
}

void ParkRuntimeBindings::set_mesh_runtime_speed(WorldRuntime& c, std::uint32_t id, std::int32_t speed) {
    (void)c.resources.meshes->set_coaster_speed(c, id, static_cast<std::uint32_t>(speed));
}

void
ParkRuntimeBindings::update_track_ride_interpolated_values(WorldRuntime&, std::uint32_t handle, std::int32_t speed) {
    if (auto* ride = track_rides ? track_rides->find(handle) : nullptr)
        update_track_ride_tuning(*ride, speed);
}

//These native operations validate the generation, but deliberately do not
//require an active slot. Position/spread use the particle subsystem's units.
void ParkRuntimeBindings::set_particle_effect_position(WorldRuntime&,
                                                       std::uint32_t handle,
                                                       const std::array<std::int32_t, 3>& position) {
    if (auto* item = effect(handle))
        for (std::size_t i = 0; i < 3; ++i)
            item->position[i] = position[i] >> 4;
}

void ParkRuntimeBindings::set_particle_effect_spread(WorldRuntime&,
                                                     std::uint32_t handle,
                                                     const std::array<std::int32_t, 3>& spread) {
    if (auto* item = effect(handle))
        for (std::size_t i = 0; i < 3; ++i)
            item->position_spread[i] = spread[i] >> 4;
}

void ParkRuntimeBindings::set_particle_effect_direction(WorldRuntime&,
                                                        std::uint32_t handle,
                                                        const std::array<std::int32_t, 3>& direction) {
    if (auto* item = effect(handle))
        for (std::size_t i = 0; i < 3; ++i)
            item->initial_particle_velocity[i] =
                std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(direction[i])
                                            * static_cast<std::uint32_t>(item->direction_magnitude))
                / 1024;
}

void ParkRuntimeBindings::set_particle_effect_lifetime(WorldRuntime&, std::uint32_t handle, std::int32_t lifetime) {
    if (auto* item = effect(handle))
        item->remaining_lifetime = lifetime;
}

void ParkRuntimeBindings::set_particle_render_scale(WorldRuntime&, std::uint32_t handle, std::int16_t scale) {
    if (auto* item = effect(handle))
        item->render_position_scale_permille = scale;
}

std::uint32_t ParkRuntimeBindings::create_particle_effect(WorldRuntime&,
                                                          std::uint32_t definition,
                                                          const std::array<std::int32_t, 3>& position,
                                                          std::int32_t lifetime) {
    if (!particles)
        throw std::logic_error("Particle pool is not bound");
    return simcoaster::create_particle_effect(*particles, definition, position, lifetime);
}

std::uint32_t ParkRuntimeBindings::create_particle_effect_with_direction(WorldRuntime&,
                                                                         std::uint32_t definition,
                                                                         const std::array<std::int32_t, 3>& position,
                                                                         const std::array<std::int32_t, 3>& direction) {
    if (!particles)
        throw std::logic_error("Particle pool is not bound");
    return simcoaster::create_particle_effect_with_direction(*particles, definition, position, direction);
}

void ParkRuntimeBindings::prepare_mesh_surface_query_geometry(WorldRuntime& c, std::uint32_t slot) {
    const auto* instance = c.resources.meshes->instance(slot);
    if (!instance)
        throw std::out_of_range("Mesh surface slot");
    auto prepared =
        prepare_mesh_surface_query(instance->playback.model,
                                   instance->terrain_base ? instance->terrain_base->best_node : std::nullopt,
                                   instance->metadata.orientation_degrees,
                                   (instance->embedded_flags & 8u) != 0);
    if (!prepared)
        throw std::runtime_error(describe_mesh_error(prepared.error()));
    surface_ = std::move(*prepared);
    surface_slot_ = slot;
}

float ParkRuntimeBindings::query_prepared_mesh_surface_height(WorldRuntime& c, float x, float z) {
    if (!surface_)
        throw std::logic_error("Mesh surface query was not prepared");
    auto* instance = c.resources.meshes->instance(surface_slot_);
    if (!instance)
        throw std::out_of_range("Prepared mesh surface was retired");
    //Native sanitizes on query, after choosing the current preparation mode.
    //The next preparation must observe the repaired model field.
    if (std::isnan(instance->playback.model.surface_height_offset))
        instance->playback.model.surface_height_offset = 0;
    return surface_->terrain
             ? static_cast<float>(world_operations::sample_terrain_surface_height(c, x, z) + surface_->offset)
             : surface_->sample(x, z);
}

void ParkRuntimeBindings::field_header(std::uint32_t, std::string_view label, bool) {
    last_field = label;
    ++field_transfers;
}

void ParkRuntimeBindings::prepare_world_save(WorldRuntime& c) {
    struct Restore {
        bool& flag;
        bool previous;

        ~Restore() {
            flag = previous;
        }
    };

    Restore restore{ c.staff.interaction.suppress_default_cursor_refresh,
                     std::exchange(c.staff.interaction.suppress_default_cursor_refresh, true) };
    cancel_world_staff_interaction(c);
    construction_runtime().finish_object_relocation(c, c.construction.modifiers);
    construction_runtime().set_mode(c, 0);
    construction_runtime().reset_after_restore();
    restore.previous = false;
}

void ParkRuntimeBindings::clear_control_particle_group(WorldRuntime&) {
    if (!particles)
        throw std::logic_error("control particle owner is not bound");
    (void)simcoaster::clear_control_particle_group(*particles);
}

TrackRideLayoutMetrics ParkRuntimeBindings::collect_track_ride_layout_metrics(WorldRuntime&, std::uint32_t handle) {
    if (!track_rides)
        throw std::logic_error("track ride metrics owner is not bound");
    return query_track_ride_layout_metrics(*track_rides, handle);
}

std::int32_t ParkRuntimeBindings::find_embedded_node_resource_record(WorldRuntime& c,
                                                                     std::uint32_t slot,
                                                                     std::uint32_t flags,
                                                                     std::int32_t id) {
    const auto* instance = c.resources.meshes->instance(slot);
    if (!instance)
        return -1;
    if (!(flags & 0x3DA1F83u))
        flags = 64626562u;
    const auto& rows = instance->playback.model.resources;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == std::bit_cast<std::uint32_t>(id) && (rows[i].flags & flags))
            return static_cast<std::int32_t>(i);
    return -1;
}

float ParkRuntimeBindings::get_mesh_root_translation_height(WorldRuntime& c, std::uint32_t slot) {
    const auto* instance = c.resources.meshes->instance(slot);
    if (!instance || !instance->playback.model.root)
        throw std::out_of_range("Missing mesh root");
    return instance->playback.model.nodes[*instance->playback.model.root].local_transform[13];
}

std::array<float, 3> ParkRuntimeBindings::get_mesh_root_translation(WorldRuntime& c, std::uint32_t slot) {
    const auto* instance = c.resources.meshes->instance(slot);
    if (!instance || !instance->playback.model.root)
        throw std::out_of_range("Missing mesh root");
    //Native6AC9C0 copies matrix translation12..14 for RSSE REPAIREFFECT.
    const auto& matrix = instance->playback.model.nodes[*instance->playback.model.root].local_transform;
    return { matrix[12], matrix[13], matrix[14] };
}

std::optional<RsseMeshBounds> ParkRuntimeBindings::get_ride_mesh_world_bounds(WorldRuntime& c, std::uint32_t slot) {
    const auto* instance = c.resources.meshes->instance(slot);
    if (!instance || !instance->cell_bounds)
        return std::nullopt;
    const auto* definition = c.resources.meshes->definition(instance->template_id);
    if (!definition || !definition->height_map)
        return std::nullopt;
    const auto& cells = *instance->cell_bounds;
    const auto& bounds = definition->height_map->bounds;
    const auto height = get_mesh_root_translation_height(c, slot);
    return RsseMeshBounds{ { static_cast<float>(cells.minimum[0]) * 10,
                             height + bounds.minimum[1],
                             static_cast<float>(cells.minimum[1]) * 10 },
                           { static_cast<float>(cells.maximum[0] + 1) * 10,
                             height + bounds.maximum[1],
                             static_cast<float>(cells.maximum[1] + 1) * 10 } };
}

std::uint32_t ParkRuntimeBindings::process_positioned_sound_request(WorldRuntime& c,
                                                                    std::uint32_t previous,
                                                                    const PositionedTemplateRequest& request) {
    if (!audio || !c.resources.resource_manager_feature_flag_0400 || !c.resources.resource_manager_dispatch_initialized)
        return 0;
    return audio->runtime().submit(previous,
                                   request.catalog_slot,
                                   request.request_key,
                                   request.position,
                                   request.parameter);
}

std::uint32_t ParkRuntimeBindings::process_resource_operation(WorldRuntime& c,
                                                              std::uint32_t target,
                                                              std::int32_t key,
                                                              std::int32_t value) {
    if (!audio || !c.resources.resource_manager_feature_flag_0400 || !c.resources.resource_manager_dispatch_initialized)
        return 0;
    return audio->runtime().operation(target, key, value);
}

std::uint32_t ParkRuntimeBindings::update_positioned_request_position(WorldRuntime& c,
                                                                      std::uint32_t target,
                                                                      const std::array<std::int32_t, 3>& position) {
    if (!audio || !c.resources.resource_manager_feature_flag_0400 || !c.resources.resource_manager_dispatch_initialized)
        return 0;
    return audio->runtime().set_position(
        target,
        { static_cast<float>(position[0]), static_cast<float>(position[1]), static_cast<float>(position[2]) });
}

std::uint32_t ParkRuntimeBindings::refresh_sound_target_position(WorldRuntime& c,
                                                                 std::uint32_t target,
                                                                 const std::array<std::int32_t, 3>& position) {
    return update_positioned_request_position(c, target, position);
}

void ParkRuntimeBindings::request_target_recycle(WorldRuntime& c, std::uint32_t target) {
    if (audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        audio->runtime().recycle(target);
}

void ParkRuntimeBindings::submit_resource_target_parameter_request(WorldRuntime& c, std::uint32_t target) {
    if (audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        audio->runtime().fade(target, 60);
}

void ParkRuntimeBindings::submit_sound_target_pause_request(WorldRuntime& c, std::uint32_t target) {
    if (audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        audio->runtime().pause(target);
}

void ParkRuntimeBindings::submit_sound_target_restart_request(WorldRuntime& c, std::uint32_t target) {
    if (audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        audio->runtime().restart(target);
}

void
ParkRuntimeBindings::submit_sound_target_level_request(WorldRuntime& c, std::uint32_t target, std::uint32_t level) {
    if (audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized)
        audio->runtime().set_level(target, level);
}

std::uint32_t ParkRuntimeBindings::get_sound_target_sample_time(WorldRuntime& c, std::uint32_t target) {
    return audio && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized
             ? audio->runtime().sample_time(target)
             : 1u;
}

void ParkRuntimeBindings::stage_music_mute_override(WorldRuntime&, std::uint16_t muted) {
    if (audio)
        audio->stage_music_mute(muted);
}

void ParkRuntimeBindings::submit_mail_notification_template(WorldRuntime& c, std::uint32_t catalog, std::uint32_t key) {
    (void)process_positioned_sound_request(c, 0, { catalog, key, { 0, 0, 0 }, 0.f });
}

RsseSoundOcclusionRegion* ParkRuntimeBindings::create_rsse_sound_occlusion_region(WorldRuntime& c,
                                                                                  float x,
                                                                                  float y,
                                                                                  float z,
                                                                                  float radius,
                                                                                  float height) {
    if (!c.resources.resource_manager_feature_flag_0400 || !c.resources.resource_manager_dispatch_initialized)
        return nullptr;
    if (!audio)
        missing_runtime_service();
    return audio->add_occlusion(x, y, z, radius, height);
}

void ParkRuntimeBindings::delete_rsse_sound_occlusion_region(WorldRuntime&, RsseSoundOcclusionRegion* region) {
    if (audio)
        audio->remove_occlusion(region);
}

void ParkRuntimeBindings::submit_world_cell_render_update(WorldRuntime&,
                                                          std::int32_t x,
                                                          std::int32_t y,
                                                          const CellRenderData& data) {
    if (scene)
        scene->update_cell_texture(x, y, data.texture_group, data.texture_member, data.orientation_degrees);
}
} //namespace simcoaster
