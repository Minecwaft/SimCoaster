#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/park_authoring.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/park_object_context_geometry.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_camera_picker.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_coaster_editor.hpp"
#include "simcoaster/world_ui_runtime.hpp"

namespace simcoaster {
namespace {
ParkObjectRuntimeView* live_object(WorldRuntime& c, ThingId id) {
    auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    return header && header->kind == ThingKind::park_object && !header->retirement_pending
             ? reinterpret_cast<ParkObjectRuntimeView*>(header)
             : nullptr;
}
} //namespace

void ParkAuthoringController::begin_cursor_map_action(std::int32_t mode, ThingId id) {
    if (replaying())
        return;
    cancel();
    auto& c = park_.context();
    if (id) {
        auto* owner = live_object(c, id);
        if (!owner)
            return;
        remember_selection(*owner);
        bind_selected(true);
    }
    cancel_world_staff_interaction(c);
    if (mode == 14 || mode == 19)
        set_construction_track_type(c, mode == 14 ? 1u : 2u, true);
    if (mode == 26) {
        bind_land_boundary_anchors(c);
        park_.construction().state.hoarding_origin = { c.construction.hoarding_chain_start_x,
                                                       c.construction.hoarding_chain_start_y };
    }
    park_.construction().set_mode(c, mode, true);
    if (mode == 20 || mode == 14 || mode == 19)
        park_.construction().initialize_context_mode(c);
    synchronize_from_world();
}

void ParkAuthoringController::synchronize_from_world() {
    auto& c = park_.context();
    if (!std::holds_alternative<DefaultWorldInteraction>(c.staff.interaction.handler.value)) {
        tool_ = ParkAuthoringTool::inspect;
        mouse_down_ = false;
        return;
    }
    //Native handler installation performs the zero-coordinate initialization
    //immediately, before accepting its first user event. Existing context
    //callers already leave mode3, so this also covers Special activation once.
    if (c.construction.mode == 20)
        park_.construction().initialize_context_mode(c);
    if (auto* object = c.construction.park_object; object && !object->base.header.retirement_pending)
        remember_selection(*object);
    const auto old_tool = tool_;
    synchronize_tool();
    feedback_.rotation = park_.construction().state.rotation_degrees;
    if (tool_ == ParkAuthoringTool::object) {
        placement_definition_ = c.construction.selected_object_definition_id;
    }
    if (old_tool != tool_)
        mouse_down_ = false;
}

bool ParkAuthoringController::edit_object_queue(ThingId id) {
    auto& c = park_.context();
    auto* object = live_object(c, id);
    if (replaying() || !object)
        return false;
    cancel();
    remember_selection(*object);
    bind_selected(true);
    cancel_world_staff_interaction(c);
    park_.construction().begin_context_queue_edit(c, *object);
    const auto cursor = park_.pick_ui_world_cursor();
    const auto back = get_back_of_queue(c, object);
    if (cursor && back) {
        const auto cursor_zero = static_cast<std::uint32_t>(cursor) - 1u;
        const auto back_zero = static_cast<std::uint32_t>(back) - 1u;
        auto& focus = c.view.camera.focus_position;
        //465A10 truncates the combined cell/focus expression before restoring
        //the cell center; it does not simply center on the queue's back tile.
        focus[0] = static_cast<float>(static_cast<std::int32_t>(static_cast<double>(back_zero & 127u)
                                                                + static_cast<double>(focus[0]) * .1
                                                                - static_cast<double>(cursor_zero & 127u)))
                     * 10.f
                 + 5.f;
        focus[2] = static_cast<float>(static_cast<std::int32_t>(static_cast<double>(back_zero >> 7)
                                                                + static_cast<double>(focus[2]) * .1
                                                                - static_cast<double>(cursor_zero >> 7)))
                     * 10.f
                 + 5.f;
        park_.ui().request_action(WorldCameraAction{ 0, 0 });
    }
    synchronize_from_world();
    return true;
}

bool ParkAuthoringController::edit_object_track(ThingId id) {
    auto& c = park_.context();
    auto* object = live_object(c, id);
    if (replaying() || !object)
        return false;
    if (get_park_object_definition(c, object)->which_track_type == 3) {
        const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object->mesh_instance_id);
        if (!opening)
            throw std::logic_error("Coaster context has no live geometry state");
        const auto tool = opening->geometry_peer_reference_count == 0 ? CoasterEditorEntryTool::adjust_support_height
                                                                      : CoasterEditorEntryTool::build_supports;
        return edit_coaster_tool(id, tool);
    }
    cancel();
    remember_selection(*object);
    bind_selected(true);
    cancel_world_staff_interaction(c);
    const auto cursor = park_.pick_ui_world_cursor();
    park_.construction().begin_context_track_edit(c, *object, cursor);
    synchronize_from_world();
    park_.ui().request_action(WorldCameraAction{ 0, 0 });
    return true;
}

bool ParkAuthoringController::edit_coaster_tool(ThingId id, CoasterEditorEntryTool tool) {
    auto& c = park_.context();
    auto* object = live_object(c, id);
    if (replaying() || !object || get_park_object_definition(c, object)->which_track_type != 3)
        return false;
    remember_selection(*object);
    bind_selected(true);
    cancel_world_staff_interaction(c);
    const auto cursor = park_.pick_ui_world_cursor();
    (void)adjust_park_object_context_coaster_camera(c, *object, cursor);
    const auto opened = park_.ui().coaster_editor_controller().open(id, tool);
    if (opened) {
        synchronize_from_world();
        park_.ui().request_action(WorldCameraAction{ 0, 0 });
    }
    return opened;
}

void ParkAuthoringController::ride_object(ThingId id) {
    auto& c = park_.context();
    auto* object = live_object(c, id);
    if (replaying() || !object)
        return;
    cancel();
    if (!get_park_object_balance(c, *object).UsageInfo.ProvidesSecurity)
        world_operations::queue_scored_advisor_action(c, 452);
    c.ports.meshes.prepare_park_object_animation(c, object, 0, 0);
}

void ParkAuthoringController::retire(ThingId id) {
    retire_world_camera_picker(park_.context(), id);
    park_.construction().retire_relocation_reference(id);
    if (selected_ != id)
        return;
    selected_ = 0;
    selected_definition_ = 0;
    selected_creation_time_ = 0;
    mouse_down_ = track_changed_ = reopen_after_cancel_ = false;
}

bool ParkAuthoringController::select_toolbar_tool(std::uint32_t id, bool selected) {
    if (id < 65 || id > 68)
        return false;
    auto& c = park_.context();
    if (!selected) {
        if (id == 65) {
            //Native54B780's deselection leaves the current handler alone; the
            //selected replacement owns that lifetime. Exit any active view first.
            if (c.view.flags & 2u)
                c.ports.camera.toggle_world_view_point_of_interest(c);
            else if (c.view.flags & 4u)
                c.ports.camera.restore_world_view_from_special_modes(c);
        }
        return true;
    }
    if (replaying())
        return false;
    if (id == 65) {
        cancel(c.construction.modifiers);
        activate_world_camera_picker(c, [this] {
            (void)park_.ui().select_toolbar_tool(67);
        });
        c.staff.interaction.pointer_mode_code = 4;
        if (c.ui.persistent_root)
            c.ui.persistent_root->cursor_id = 19;
        return true;
    }
    const auto tool = id == 68 ? ParkAuthoringTool::path
                    : id == 66 ? ParkAuthoringTool::demolish_rectangle
                               : ParkAuthoringTool::inspect;
    if (!choose_tool(tool))
        return false;
    c.staff.interaction.pointer_mode_code = id == 68 ? 2 : id == 66 ? 3 : 1;
    park_.construction().state.shortcut_rearm_mode.reset();
    if (id == 67) {
        //Normal mode explicitly installs base1, including in online mode.
        c.staff.interaction.handler = { DefaultWorldInteraction{}, false };
        c.construction.handler = &c.staff.interaction.handler;
        c.staff.interaction.cursor_presentation = 0;
    } else if (id == 66)
        c.staff.interaction.cursor_presentation = 10;
    if (id == 68) {
        world_operations::queue_scored_advisor_action(c, 147);
        get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[5] = 0;
    }
    if (c.ui.persistent_root)
        c.ui.persistent_root->cursor_id = static_cast<std::int32_t>(c.staff.interaction.cursor_presentation);
    return true;
}
} //namespace simcoaster
