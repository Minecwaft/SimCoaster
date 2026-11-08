#include "native_coaster_editor_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include <utility>

namespace simcoaster {
using namespace native_coaster_detail;

namespace {
CoasterEditorTool tool_for_entry(CoasterEditorEntryTool entry) {
    using E = CoasterEditorEntryTool;
    using T = CoasterEditorTool;
    match (entry) {
        case E::delete_track_section => return T::delete_track_section;
        case E::build_supports => return T::build_supports;
        case E::build_complex_loop => return T::build_complex_loop;
        case E::build_loop_the_loop => return T::build_loop_the_loop;
        case E::pivot_track => return T::pivot_track;
        case E::stack_track_up => return T::stack_track_up;
        case E::stack_track_down => return T::stack_track_down;
        case E::adjust_support_height => return T::adjust_support_height;
        case E::rotate_supports => return T::rotate_supports;
        case E::move_supports => return T::move_supports;
        case _ => {}
    }
    throw std::invalid_argument("Unknown native coaster entry tool");
}

bool map_tool(CoasterEditorTool tool) {
    return tool == CoasterEditorTool::build_supports
        || tool == CoasterEditorTool::build_complex_loop
        || tool == CoasterEditorTool::build_loop_the_loop
        || tool == CoasterEditorTool::move_supports;
}
} //namespace

std::uint32_t NativeCoasterEditor::tool_selection() const {
    return state_->selected_tools;
}

std::optional<CoasterEditorEntryTool> NativeCoasterEditor::entry_tool() const {
    return state_->entry;
}

std::optional<ThingId> NativeCoasterEditor::active_object(AttractionWorld& c) const {
    if (!state_->map || !c.construction.coaster_editor_active)
        return {};
    const auto* ride = world(c).find_map(*state_->map);
    if (!ride || !ride->park_object || ride->park_object->base.header.retirement_pending)
        return {};
    return ride->park_object->base.header.id;
}

bool NativeCoasterEditor::geometry_interaction_active() const {
    return state_->map && state_->input_route == CoasterEditorInputRoute::geometry;
}

CoasterEditorInputRoute NativeCoasterEditor::input_route() const {
    return state_->input_route;
}

std::optional<std::int32_t> NativeCoasterEditor::geometry_interaction_cost() const {
    if (!state_->parameter_drag || !state_->geometry_preview)
        return {};
    for (const auto& axis : state_->parameter_drag->axes)
        if (axis && axis->parameter == CoasterParameter::loft)
            return state_->geometry_preview->interaction_cost;
    return {};
}

bool NativeCoasterEditor::State::single_piece_endpoint(AttractionWorld& c) {
    const auto& r = ride(c);
    if (!special_available(r) || r.geometry.empty())
        return false;
    const auto flags = r.train_state.placement_flags;
    const auto node = !(flags & 1u) ? std::optional{ r.geometry.size() - 1 } : (flags & 2u) ? current : std::nullopt;
    //Native48AF40 tests the low BYTE's sign bit, not the DWORD sign bit.
    return node && (r.geometry.at(*node).geometry_flags & 128u);
}

std::uint32_t NativeCoasterEditor::tool_availability(AttractionWorld& c) const {
    auto& s = *state_;
    if (!s.map || !world(c).find_map(*s.map))
        return 0;
    const auto& r = s.ride(c);
    const auto& w = world(c);
    //48B520 has four selected-tool branches with the same baseline. These are
    //availability bits, so loop-the-loop uses bit2 here, not selection1024.
    std::uint32_t result = 4u | 8u | 16u | 32u | 64u | 128u | 256u | 2048u | 4096u;
    const auto single = s.single_piece_endpoint(c);
    if (!single)
        result |= 1u;
    //46D1D0 initializes coaster configuration bit128 after successful car
    //processing;46DB70 supplies bit2 for a real second pylon filename. Trough
    //SAM sets bit256. No manufactured asset/type availability is substituted.
    if (special_available(r)) {
        result |= 2u;
        if (r.definition->balance.coaster.bSpecialIsTrough)
            result |= 512u;
        const auto ordinary_selection = s.selected_tools & (1u | 4u | 8u | 16u | 32u | 64u | 128u | 256u);
        if (!ordinary_selection && !single)
            result |= 1024u;
    }
    const auto flags = r.train_state.placement_flags;
    result &= (!(flags & 1u) || (flags & 2u)) ? 0xFFFFE7BFu : 0xFFFFFBFCu;
    result &= 0xFFFFFE7Fu;
    if (std::ranges::any_of(w.placements, [&](const auto& owner) {
            return owner.links.size() > 1 && std::ranges::any_of(owner.links, [&](const auto& ref) {
                       return ref && ref->ride == s.ride_index(c);
                   });
        }))
        result |= 128u | 256u;
    return result;
}

void NativeCoasterEditor::enter_tool(AttractionWorld& c, ThingId id, CoasterEditorEntryTool entry, bool recording) {
    require(id && id < c.entities.size(), "cannot activate an invalid park object");
    auto* header = c.entities.at(id).payload;
    require(header && header->kind == ThingKind::park_object && !header->retirement_pending,
            "selected coaster is missing or retired");
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    auto* target = world(c).find(object.mesh_instance_id);
    require(target != nullptr, "selected object has no coaster runtime");
    const auto map = target->map_id;
    const auto tool = tool_for_entry(entry);
    auto& s = *state_;
    //52F870 marks this before origin capture/UI presentation. Safely finish a
    //prior owner without an invented SETEDIT0/deactivation transaction.
    object.prebuilt_track_edited = 1;
    if (s.map)
        s.cleanup(c, s.construction);
    c.view.secondary_cursor_record_snapshot = &object.base.header;
    s.construction.refresh_origin(c, recording);
    s.advisor_followup = false;
    c.construction.coaster_editor_active = true;
    activate(c, map, recording);
    s.entry = entry;
    select_tool(c, tool, true, recording);
}

void NativeCoasterEditor::select_tool(AttractionWorld& c, CoasterEditorTool tool, bool active, bool recording) {
    auto& s = *state_;
    require(s.map.has_value(), "tool selection has no active coaster");
    const auto bit = std::to_underlying(tool);
    require(bit && !(bit & (bit - 1u)) && (bit & 0x1DFFu), "unknown coaster selection bit");
    s.cleanup(c, s.construction);
    s.active_tool.reset();
    s.geometry_anchor.reset();
    if (!active) {
        s.selected_tools &= ~bit;
        return;
    }
    using T = CoasterEditorTool;
    match (tool) {
        case T::build_supports or T::build_complex_loop or T::build_loop_the_loop => {
            s.construction.set_mode(c, 21, recording);
            set_construction_track_type(c, 3, recording);
            stage(c, tool != T::build_supports, tool == T::build_loop_the_loop, recording);
            s.edit = State::Edit::cleanup;
            s.ride(c).train_state.runtime_flags |= 4u;
            s.pulsing(c, false);
        }
        case T::move_supports => {
            s.construction.set_mode(c, 22, recording);
            set_construction_track_type(c, 3, recording);
        }
        case T::pivot_track or T::adjust_support_height or T::rotate_supports or T::delete_track_section or T::stack_track_up or T::stack_track_down or T::delete_single_support or T::insert_support => {
            //52DA10 installs handler kind8. It does NOT request worldmode22;
            //geometry_pointer owns the typed mesh/pixel interaction independently.
        }
        case _ => {}
    }
    s.selected_tools |= bit;
    s.active_tool = tool;
    s.input_route = map_tool(tool) ? CoasterEditorInputRoute::map_action : CoasterEditorInputRoute::geometry;
    //Replacing the native map handler with kind8 stops terrain previews.
    //The retained x64 face queue must not keep its last invalid red tile.
    s.construction.state.faces.clear();
    s.construction.invalidate_interaction_cost_quote();
    s.geometry_action_enabled = true;
}

void NativeCoasterEditor::deactivate_ui_context(AttractionWorld& c) {
    auto& s = *state_;
    if (c.construction.coaster_editor_active && s.map) {
        s.cleanup(c, s.construction);
        s.selected_tools = 0;
        s.active_tool.reset();
        s.entry.reset();
        s.geometry_anchor.reset();
        s.input_route = CoasterEditorInputRoute::world_default;
        if (c.construction.mode != 3) {
            s.construction.set_mode(c, 0, true);
            s.construction.set_mode(c, 0, true); //52DFA0's duplicate is recorded.
        }
        activate(c, 0, true);
    }
    c.construction.coaster_editor_active = false;
    //52FB80 consumes the followup after UI teardown. Leave it intact for its
    //retained frontend's consume_followup call, including a no-context close.
}

void NativeCoasterEditor::refresh_placement_tool(AttractionWorld& c) {
    auto& s = *state_;
    //663C6D ->52EDF0 ->48B330: re-dispatch a placement ACTION after
    //release, without replacing the handler or recording another SET_MODE.
    if (!c.construction.coaster_editor_active || !s.map)
        return;
    const auto flags = s.ride(c).train_state.placement_flags;
    if ((flags & 1u) && !(flags & 2u)) {
        select_tool(c, CoasterEditorTool::adjust_support_height);
        return;
    }
    const bool special = s.single_piece_endpoint(c) || s.remaining == 1;
    s.cleanup(c, s.construction);
    s.selected_tools = 0;
    stage(c, special, false, true);
    s.edit = State::Edit::cleanup;
    s.ride(c).train_state.runtime_flags |= 4u;
    s.pulsing(c, false);
}

bool NativeCoasterEditor::consume_followup() noexcept {
    return std::exchange(state_->advisor_followup, false);
}

void NativeCoasterEditor::State::restore_build_tools_after_delete(AttractionWorld& c) {
    auto* editor = c.construction.editor;
    require(editor != nullptr, "deleted geometry has no editor owner");
    const bool special = single_piece_endpoint(c);
    selected_tools = 0;
    //487270 restores/records the native placement action BEFORE activating
    //its World Map Action handler, unlike a normal toolbar activation.
    editor->stage(c, special, false, true);
    edit = Edit::cleanup;
    ride(c).train_state.runtime_flags |= 4u;
    pulsing(c, false);
    construction.set_mode(c, 21, true);
    set_construction_track_type(c, 3, true);
    editor->deactivate_ui_context(c);
}

bool NativeCoasterEditor::coaster_key(AttractionWorld& c, std::uint32_t key, std::uint32_t event) {
    if (!state_->map || !c.construction.coaster_editor_active || event != 65547u || ((key >> 16u) & 255u))
        return false;
    const auto vk = key > 255u ? (key >> 8u) & 255u : key;
    if (vk == 27u) {
        deactivate_ui_context(c);
        return true;
    }
    if (vk != 8u)
        return false;
    //430C20 binds Backspace key-up through42F130. Kind8 geometry tools do not
    //pretend to be a World Map Action handler and cannot backtrack supports.
    if (state_->input_route == CoasterEditorInputRoute::map_action
        && !state_->edit_event_pending
        && c.construction.mode != 139)
        state_->backtrack(c, state_->construction, true);
    return true;
}
} //namespace simcoaster
