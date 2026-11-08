#include "simcoaster/world_staff_interaction.hpp"
#include "simcoaster/world_camera_picker.hpp"
#include "simcoaster/resource_variant.hpp"
#include "simcoaster/world_grid.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace {
MapCell& map_cell(StaffWorld& c, CellId id) {
    if (!id || id > map_cell_count)
        throw std::out_of_range("staff interaction cell");
    return primary(c).map_cells[id - 1];
}

WorldInteractionHandlerKind kind(const WorldInteractionHandler& handler) {
    // clang-format off
  return match(handler.value)->WorldInteractionHandlerKind {
  case {DefaultWorldInteraction const &value} => value.alternate ? WorldInteractionHandlerKind::alternate:
    WorldInteractionHandlerKind::default_;
    case {HiredStaffInteraction} => WorldInteractionHandlerKind::hired_staff;
    case {HeldStaffInteraction} => WorldInteractionHandlerKind::held_staff;
    case {StaffAreaSelectionInteraction} => WorldInteractionHandlerKind::staff_area;
    case {RideTrackPickInteraction} => WorldInteractionHandlerKind::ride_track_pick;
  };
    // clang-format on
}

void finish_hiring(StaffWorld& c, bool confirmed) {
    if (!c.staff.placement)
        return;
    const auto resource = c.staff.placement->preview_resource;
    if (resource)
        world_operations::release_resource_pool_handle(c, resource);
    c.staff.placement->preview_resource = 0;
    release_world_staff_entity_links(c, primary(c), !confirmed);
    world_operations::cancel_advisor_action_by_id(c, &record<AdvisorRecord>(c, primary(c).saved.advisor), 254);
    c.staff.placement.reset();
}

void deactivate(StaffWorld& c) {
    auto& handler = c.staff.interaction.handler;
    if (handler.completed)
        return;
    handler.completed = true;
    // clang-format off
  match(handler.value){
    case {HiredStaffInteraction const& hired} => finish_hiring(c,hired.placement_confirmed);
    case {HeldStaffInteraction& held} => {
      if(held.resource)world_operations::release_resource_pool_handle(c,held.resource);
      held.resource=0;
      auto& control=record<StaffHqRecord>(c,primary(c).saved.staff_hq);
      if(control.staff_member_picked_up){const auto id=control.staff_member_picked_up;
      drop_held_staff_member_at_cell(c,control,cell(record<StaffRuntimeRecord>(c,id).base));}
    }
    case {StaffAreaSelectionInteraction& editor} => {
      if(editor.panel_open){editor.panel_open=false;
      if(editor.target)enter_world_staff_state(c,record<StaffRuntimeRecord>(c,editor.target),StaffState::frozen);
      c.construction.mode=0;}
      if(editor.preview.begin)c.ports.memory.release(Allocation::staff_area_cells,editor.preview.begin);
      editor.preview.begin=editor.preview.end=editor.preview.capacity=nullptr;
    }
    case _ => ;
  }
    // clang-format on
}

void install(StaffWorld& c, WorldInteractionHandler::Value value) {
    deactivate(c);
    c.staff.interaction.handler = { std::move(value), false };
    c.construction.handler = &c.staff.interaction.handler;
    c.staff.interaction.left_button_held = false;
    c.staff.interaction.overlays.clear();
}

// clang-format off
std::uint32_t role_sprite(StaffType type){return match(type)->std::uint32_t{case StaffType::handyman=>9;
case StaffType::mechanic=>10;
case StaffType::entertainer=>8;
case StaffType::guard=>11;
case StaffType::researcher=>12;
case StaffType::gardener=>18;
case _=>throw std::out_of_range("staff preview role");};}
// clang-format on
} //namespace

WorldGridObjectCategory
get_first_world_grid_object_ui_category(StaffWorld& c, const CellBase& base, ParkObjectRuntimeView*& output) {
    if (base.common.type != 4)
        return WorldGridObjectCategory::unsupported;
    const auto id = find_first_park_object_id_in_map_cell(c, &map_cell(c, base.common.parent_id));
    if (!id)
        return WorldGridObjectCategory::unsupported;
    output = &object(c, id);
    const auto* definition = get_park_object_definition(c, output);
    if (!definition)
        throw std::logic_error("staff placement object definition");
    // clang-format off
  return match(static_cast<std::int32_t>(definition->which_ui_type))->WorldGridObjectCategory{
    case 0=>WorldGridObjectCategory::rides;
    case 1=>WorldGridObjectCategory::shops;
    case 2=>WorldGridObjectCategory::sideshows;
    case 3=>WorldGridObjectCategory::features;
    case 4 or 5=>WorldGridObjectCategory::facilities;
    case _=>WorldGridObjectCategory::unsupported;
  };
    // clang-format on
}

CellId select_world_map_cell_for_grid_object_interaction(StaffWorld& c,
                                                         WorldGridObjectCategory category,
                                                         CellId origin,
                                                         ParkObjectRuntimeView* target) {
    CellId selected = 0;
    // clang-format off
  match(category){
    case WorldGridObjectCategory::rides=>{if(!target)return 0;
    selected=target->exit_cell;}
    case WorldGridObjectCategory::shops or WorldGridObjectCategory::sideshows or WorldGridObjectCategory::facilities=>{if(!target)return 0;
    selected=target->entry_cell;}
    case WorldGridObjectCategory::features=>;
    case _=>selected=origin;
  }
    // clang-format on
    if (!selected && target)
        selected = find_nearest_path_cell_around_object(c, *target, origin);
    return selected;
}

bool is_staff_placement_cell_admissible(StaffWorld& c, CellId id) {
    if (!id || id > map_cell_count)
        return false;
    auto& base = map_cell(c, id).base;
    ParkObjectRuntimeView* target = nullptr;
    if (get_first_world_grid_object_ui_category(c, base, target) != WorldGridObjectCategory::unsupported)
        return true;
    if (base.common.flags & 64u)
        return false;
    return !world_track_reference_matches_type_set(c, id)
        && (base.common.type == 0 || base.common.type == 1 || base.common.type == 3 || base.common.type == 9);
}

WorldInteractionHandlerKind world_staff_interaction_kind(const StaffQuery& c) {
    return kind(c.staff.interaction.handler);
}

void initialize_world_staff_interaction(StaffWorld& c) {
    auto& state = c.staff.interaction;
    if (state.initialized)
        return;
    initialize_game_mode_selector(c);
    state.initialized = true;
    state.handler = { DefaultWorldInteraction{ c.mode.game == GameMode::online }, false };
    c.construction.handler = &state.handler;
    state.last_cursor = { -1, -1 };
    state.pointer_mode_code = 0;
    state.left_button_held = false;
    if (c.mode.game == GameMode::online || !state.suppress_default_cursor_refresh)
        state.cursor_presentation = 0;
}

void world_operations::builtin::ensure_world_interaction_handler_initialized(WorldRuntime& c) {
    initialize_world_staff_interaction(c);
}

std::int32_t world_operations::builtin::world_interaction_handler_kind(WorldRuntime& c, void* handler) {
    if (handler != &c.staff.interaction.handler)
        throw std::invalid_argument("foreign world interaction handler");
    return std::to_underlying(world_staff_interaction_kind(c));
}

void cancel_world_staff_interaction(StaffWorld& c) {
    initialize_world_staff_interaction(c);
    install(c, DefaultWorldInteraction{ c.mode.game == GameMode::online });
    if (c.mode.game == GameMode::online || !c.staff.interaction.suppress_default_cursor_refresh)
        c.staff.interaction.cursor_presentation = 0;
}

void release_world_staff_interaction(StaffWorld& c) {
    if (c.staff.interaction.initialized)
        deactivate(c);
    if (c.construction.handler == &c.staff.interaction.handler)
        c.construction.handler = nullptr;
    c.staff.interaction.handler = { DefaultWorldInteraction{}, false };
    c.staff.interaction.initialized = false;
    c.staff.interaction.overlays.clear();
}

void activate_hired_staff_placement_interaction(StaffWorld& c) {
    if (!c.staff.placement)
        throw std::logic_error("staff placement has no candidate");
    initialize_world_staff_interaction(c);
    install(c, HiredStaffInteraction{});
    c.staff.interaction.cursor_presentation = 9;
    const auto& candidate = c.staff.placement->candidate;
    const auto resource = world_operations::allocate_resource_variant(
        c,
        { ResourceProgramFamily::staff_cursor_preview, 0 },
        { role_sprite(candidate.type), static_cast<std::uint8_t>(candidate.resource_variant), 0, 0, 0, 0 });
    const auto old = std::exchange(c.staff.placement->preview_resource, resource);
    if (old)
        world_operations::release_resource_pool_handle(c, old);
}

void world_operations::builtin::begin_world_staff_pickup(WorldRuntime& c, StaffRuntimeRecord& s) {
    initialize_world_staff_interaction(c);
    install(c, HeldStaffInteraction{ 0, s.mobile.sprite_category, static_cast<std::uint8_t>(s.mobile.sprite_id) });
    c.staff.interaction.cursor_presentation = 9;
    auto& held = std::get<HeldStaffInteraction>(c.staff.interaction.handler.value);
    held.resource = world_operations::allocate_resource_variant(c,
                                                                { ResourceProgramFamily::staff_cursor_preview, 0 },
                                                                { held.category, held.variant, 0, 0, 0, 0 });
}

void start_staff_pickup_interaction(StaffWorld& c, StaffRuntimeRecord& s) {
    initialize_world_staff_interaction(c);
    if (world_staff_interaction_kind(c) == WorldInteractionHandlerKind::held_staff)
        cancel_world_staff_interaction(c);
    record<StaffHqRecord>(c, world(c).saved.staff_hq).staff_member_picked_up = s.base.header.id;
    enter_world_staff_state(c, s, StaffState::picked_up);
}

void drop_held_staff_member_at_cell(StaffWorld& c, StaffHqRecord& control, CellId destination) {
    handle_dropped_staff_member(c, record<StaffRuntimeRecord>(c, control.staff_member_picked_up), destination);
    control.staff_member_picked_up = 0;
}

ThingId place_staff_at_cursor(StaffWorld& c) {
    const auto category_kind = world_staff_interaction_kind(c);
    if (category_kind != WorldInteractionHandlerKind::held_staff
        && category_kind != WorldInteractionHandlerKind::hired_staff)
        return 0;
    const auto origin = c.staff.interaction.cursor.cell;
    if (!is_staff_placement_cell_admissible(c, origin))
        return 0;
    ParkObjectRuntimeView* target = nullptr;
    const auto category = get_first_world_grid_object_ui_category(c, map_cell(c, origin).base, target);
    const auto destination = select_world_map_cell_for_grid_object_interaction(c, category, origin, target);
    if (!destination)
        return 0;
    ThingId id;
    if (category_kind == WorldInteractionHandlerKind::held_staff) {
        auto& control = record<StaffHqRecord>(c, primary(c).saved.staff_hq);
        id = control.staff_member_picked_up;
        drop_held_staff_member_at_cell(c, control, destination);
        cancel_world_staff_interaction(c);
    } else {
        id = create_hired_world_staff_record(c, destination, c.staff.placement->candidate);
        std::get<HiredStaffInteraction>(c.staff.interaction.handler.value).placement_confirmed = true;
        cancel_world_staff_interaction(c);
    }
    return id;
}

void update_world_staff_interaction_cursor(StaffWorld& c, StaffInteractionCursor cursor) {
    c.staff.interaction.cursor = cursor;
    c.staff.interaction.overlays.clear();
    if (std::holds_alternative<RideTrackPickInteraction>(c.staff.interaction.handler.value)) {
        update_world_camera_picker(c, cursor);
        return;
    }
    if (!cursor.cell || cursor.cell > map_cell_count)
        return;
    auto& value = c.staff.interaction.handler.value;
    if (auto* editor = std::get_if<StaffAreaSelectionInteraction>(&value)) {
        update_staff_area_selection_from_cursor(c, *editor, cursor.cell, c.staff.interaction.left_button_held);
        return;
    }
    // clang-format off
  const auto resource=match(value)->std::uint32_t{case {HeldStaffInteraction const& held}=>held.resource;
  case {HiredStaffInteraction}=>c.staff.placement?c.staff.placement->preview_resource:
      0u;
      case _=>0u;};
    // clang-format on
    if (!resource)
        return;
    world_operations::set_resource_variant_transform(c, resource, cursor.ray_point, 4);
    set_resource_variant_extended_frame_metadata(c, resource, false);
    c.staff.interaction.overlays.push_back(
        { cursor.cell, static_cast<std::uint8_t>(!is_staff_placement_cell_admissible(c, cursor.cell)) });
}

void world_staff_interaction_button(StaffWorld& c, WorldPointerButton button, bool down) {
    if (std::holds_alternative<RideTrackPickInteraction>(c.staff.interaction.handler.value)) {
        world_camera_picker_button(c, button, down);
        return;
    }
    auto& state = c.staff.interaction;
    if (button == WorldPointerButton::left)
        state.left_button_held = down;
    else
        state.right_button_held = down;
    if (button == WorldPointerButton::left) {
        if (auto* editor = std::get_if<StaffAreaSelectionInteraction>(&state.handler.value)) {
            if (down) {
                editor->drag_origin = state.cursor.cell;
                if (editor->selection_active) {
                    editor->drag_anchor = editor->selection;
                    const auto n = static_cast<std::uint32_t>(state.cursor.cell) - 1u;
                    editor->resize_handle = classify_staff_area_selection_resize_handle(*editor,
                                                                                        static_cast<int>(n & 127u),
                                                                                        static_cast<int>(n >> 7));
                }
            } else {
                if (!editor->selection_active) {
                    editor->selection_active = true;
                    auto& b = editor->selection;
                    if (b.start_x > b.end_x)
                        std::swap(b.start_x, b.end_x);
                    if (b.start_y > b.end_y)
                        std::swap(b.start_y, b.end_y);
                }
                editor->caption_row = 109;
            }
        } else if (down)
            (void)place_staff_at_cursor(c);
    } else if (!down && std::holds_alternative<StaffAreaSelectionInteraction>(state.handler.value))
        close_staff_area_selection_panel(c, false);
}
} //namespace simcoaster
