#include "native_coaster_editor_internal.hpp"

namespace simcoaster {
using namespace native_coaster_detail;

NativeCoasterEditor::NativeCoasterEditor(ParkConstructionRuntime& construction) : state_(std::make_unique<State>(construction)) {}

NativeCoasterEditor::~NativeCoasterEditor() = default;

std::optional<std::uint32_t> NativeCoasterEditor::active_map() const {
    return state_->map;
}

std::optional<std::size_t> NativeCoasterEditor::active_node() const {
    return state_->current;
}

const NativeCoasterPreview* NativeCoasterEditor::preview() const {
    return state_->geometry_preview ? &*state_->geometry_preview : nullptr;
}

void NativeCoasterEditor::refresh_preview(AttractionWorld& c) {
    state_->rebuild_preview(c, false);
}

void NativeCoasterEditor::reset_after_restore() {
    auto& editor = state_->construction;
    state_ = std::make_unique<State>(editor);
}

void NativeCoasterEditor::bind_catalog_context(AttractionWorld& c, std::uint32_t map) {
    auto& s = *state_;
    auto* target = world(c).find_map(map);
    require(target != nullptr, "catalog runtime map is missing");
    require(!s.detached && (!s.map || *s.map == map), "clear the previous catalog/editor context first");
    s.geometry_preview.reset();
    s.preview_signature.clear();
    s.map = map;
    s.current = target->geometry.empty() ? std::nullopt : std::optional { target->geometry.size() - 1 };
}

void NativeCoasterEditor::clear_catalog_context(AttractionWorld&) {
    require(!state_->detached, "catalog clear cannot discard a detached source cell");
    auto& construction = state_->construction;
    state_ = std::make_unique<State>(construction);
}

void NativeCoasterEditor::complete_supports(AttractionWorld& c) {
    auto& s = *state_;
    require(s.map.has_value(), "support completion has no active context");
    const auto map = *s.map;
    s.cleanup(c, s.construction);
    checked(set_coaster_track_closed(c, meshes(c), map, true));
    //Native475220 assigns the active context directly to null. It does not call
    //48CEA0, emit ACT68 or reopen admission as the normal selection-exit path
    //does.
    clear_catalog_context(c);
}

void NativeCoasterEditor::retire_context(AttractionWorld& c, std::uint32_t map) {
    auto& s = *state_;
    if (!s.map || *s.map != map) {
        return;
    }
    s.parameter_drag.reset();
    c.construction.coaster_editor_active = false;
    s.cleanup(c, s.construction);
    s.pulsing(c, false);
    clear_catalog_context(c);
}

void NativeCoasterEditor::activate(AttractionWorld& c, std::uint32_t map_id, bool recording) {
    auto& s = *state_;
    auto& editor = s.construction;
    s.geometry_preview.reset();
    s.preview_signature.clear();
    auto* target = map_id ? world(c).find_map(map_id) : nullptr;
    //48DD40 passes a null lookup through the normal context-exit path.
    if (recording) {
        record(c, { LayoutActionCode::coaster_edit, LayoutWord { static_cast<std::uint16_t>(target ? target->map_id : 0) } });
    }
    if (target) {
        const bool different_owner = !s.map || *s.map != target->map_id;
        if (!s.map) {
            require(target->park_object != nullptr, "active runtime has no park object");
            disable_admission(c, *target->park_object);
            const auto flags = target->train_state.placement_flags;
            transition_coaster_detail_state(c, meshes(c), *target, (!(flags & 1u) || (flags & 2u)) ? RideDetailState::inactive : RideDetailState::vehicles_only);
        } else if (*s.map != target->map_id) {
            //Real recorded context switches close the old context first. Reject a
            //cross-owner detached preview rather than transferring its mesh handle.
            require(!s.detached && !s.preview, "cannot transfer a detached edit to another coaster");
        }
        s.map = target->map_id;
        if (different_owner && (target->train_state.placement_flags & 2u)) {
            //Native478465/478472 restores these per-ride pointers before either
            //live UI entry or a recorded SETEDIT. Station node2 is implicit.
            const auto& geometry = target->geometry;
            require(geometry.size() >= 3, "open coaster edit has no station anchor");
            s.current = s.validation = 2;
            for (std::size_t i = 0; i < geometry.size(); ++i) {
                if (geometry[i].geometry_flags & 0x10000u) {
                    s.current = i;
                }
                if (geometry[i].geometry_flags & 0x20000u) {
                    s.validation = i;
                }
            }
        } else if (!(target->train_state.placement_flags & 2u)) {
            s.current = target->geometry.empty() ? std::nullopt : std::optional { target->geometry.size() - 1 };
        }
        return;
    }
    if (!s.map) {
        return;
    }
    const auto departing = *s.map;
    s.cleanup(c, editor);
    auto& r = s.ride(c);
    mark(r);
    const auto can_reopen = (r.train_state.detail_state_flags & 2u) != 0;
    auto* object = r.park_object;
    s.map.reset();
    s.current.reset();
    s.validation.reset();
    s.selected_tools = 0;
    s.entry.reset();
    s.active_tool.reset();
    s.input_route = CoasterEditorInputRoute::world_default;
    s.geometry_anchor.reset();
    if (can_reopen && object && is_park_object_open_eligible(c, *object)) {
        open_park_object(c, *object);
    }
    (void)departing;
}

void NativeCoasterEditor::activate_object(AttractionWorld& c, ThingId id, bool recording) {
    require(id != 0, "cannot activate a null park object");
    auto* header = c.entities.at(id).payload;
    require(header && header->kind == ThingKind::park_object, "selected record is not a park object");
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object.mesh_instance_id);
    require(opening != nullptr, "selected object has no coaster opening state");
    const auto tool = opening->geometry_peer_reference_count == 0 ? CoasterEditorEntryTool::adjust_support_height : CoasterEditorEntryTool::build_supports;
    enter_tool(c, id, tool, recording);
}

bool NativeCoasterEditor::select_at_tile(AttractionWorld& c, WorldCellCoordinates tile, bool recording) {
    if (!state_->map || !construction_detail::bounds(tile)) {
        return false;
    }
    const auto slot = construction_detail::track(c, construction_detail::cell_id(tile)).base.common.mesh_instance;
    const auto& r = state_->ride(c);
    auto found = std::ranges::find(r.geometry_state, slot, &CoasterNodeState::mesh_slot);
    if (!slot || found == r.geometry_state.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - r.geometry_state.begin());
    if (fixed_owner(world(c), r, index)) {
        return false;
    }
    begin_move(c, index, recording);
    return true;
}

void NativeCoasterEditor::begin_move(AttractionWorld& c, std::size_t ordinal, bool recording) {
    auto& s = *state_;
    auto& editor = s.construction;
    if (recording && (c.construction.mode != 22 || geometry_interaction_active())) {
        select_tool(c, CoasterEditorTool::move_supports, true, true);
    }
    auto& r = s.ride(c);
    require(ordinal < r.geometry.size(), "move geometry ordinal is out of range");
    if (fixed_owner(world(c), r, ordinal)) {
        return;
    }
    s.cleanup(c, editor);
    auto& refreshed = s.ride(c);
    const auto r_index = s.ride_index(c);
    auto& node = refreshed.geometry.at(ordinal);
    auto& local = refreshed.geometry_state.at(ordinal);
    const auto& owner = world(c).placements.at(local.placement);
    s.edit = State::Edit::move;
    s.current = ordinal;
    s.preview = ordinal;
    s.original_tile = coordinate(owner);
    s.original_height = static_cast<float>(coaster_editor_detail::cumulative(world(c), owner, coaster_editor_detail::level(owner, { r_index, ordinal })));
    s.interaction |= 0x100u;
    refreshed.train_state.runtime_flags &= ~0x2000u;
    s.range_anchor = ordinal ? ordinal - 1 : ordinal;
    s.range_cursor = ordinal + 1 < refreshed.geometry.size() ? ordinal + 1 : ordinal;
    const auto low = std::min(*s.range_anchor, *s.range_cursor), high = std::max(*s.range_anchor, *s.range_cursor);
    for (auto i = low; i < high; ++i) {
        refreshed.geometry[i].geometry_flags |= 0x1000000u;
    }
    refreshed.selection_texture_bias = 3;
    mark(refreshed);
    auto position = s.original_tile;
    require(c.weather.map_heightfield_width && c.weather.map_heightfield_height, "terrain dimensions are missing");
    position.x = std::clamp(position.x, 0, static_cast<std::int32_t>(c.weather.map_heightfield_width) - 1);
    position.y = std::clamp(position.y, 0, static_cast<std::int32_t>(c.weather.map_heightfield_height) - 1);
    auto& base = construction_detail::map(c, construction_detail::cell_id(position)).base;
    auto* track = resolve_track_cell_for_grid_entry(c, &base);
    require(track != nullptr, "move source has no track cell");
    s.detached = State::Detached { position, track->base.common.mesh_instance };
    track->base.common.mesh_instance = 0;
    if (owner.links.size() <= 1) {
        remove_map_cell_contents(c, &base, 0, 0);
    }
    if (recording) {
        record(c, { LayoutActionCode::coaster_move, LayoutWordTriple { 0, 0, static_cast<std::uint16_t>(ordinal) } });
    }
    //The MOVE packet itself does not change the interaction mode. Live UI
    //activation and recorded SET_MODE/SET_MODE_NR maintain that separate state.
    (void)node;
}

void NativeCoasterEditor::stage(AttractionWorld& c, bool special, bool select_pylon, bool recording) {
    auto& s = *state_;
    s.finish_parameter_drag(c, true);
    const auto tool = !special ? CoasterEditorTool::build_supports : select_pylon ? CoasterEditorTool::build_loop_the_loop : CoasterEditorTool::build_complex_loop;
    s.active_tool = tool;
    s.input_route = CoasterEditorInputRoute::map_action;
    s.selected_tools |= std::to_underlying(tool);
    s.geometry_action_enabled = true;
    auto& r = s.ride(c);
    s.geometry_preview.reset();
    s.preview_signature.clear();
    if (recording) {
        record(c, { special ? LayoutActionCode::coaster_special : LayoutActionCode::coaster_normal, std::monostate {} });
    }
    r.train_state.runtime_flags &= ~0x2000u;
    if (!special) {
        if (s.stage == State::Stage::special && s.remaining == 2 && special_available(r)) {
            s.interaction &= ~0x4040u;
        }
        s.remaining = 0;
        s.stage = State::Stage::normal;
    } else {
        const auto selected = (r.train_state.placement_flags & 2u) ? s.current : (r.geometry.empty() ? std::nullopt : std::optional { r.geometry.size() - 1 });
        if (special_available(r) && selected && (!(r.train_state.placement_flags & 1u) || (r.train_state.placement_flags & 2u)) && (r.geometry.at(*selected).geometry_flags & 0x80u)) {
            s.remaining = 1;
            s.interaction |= 0x40u;
        } else if (special_available(r) && (select_pylon || !(s.interaction & 0x40u))) {
            s.remaining = 2;
            s.interaction |= select_pylon ? 0x4040u : 0x40u;
        }
        s.stage = State::Stage::special;
    }
}

void NativeCoasterEditor::cancel(AttractionWorld& c, ParkConstructionRuntime& editor) {
    require(&editor == &state_->construction, "wrong construction owner");
    if (state_->map) {
        state_->cleanup(c, editor);
    }
}

void NativeCoasterEditor::delete_range(AttractionWorld& c, std::uint16_t first, std::uint16_t second, bool recording) {
    state_->delete_range(c, state_->construction, first, second, recording);
}

void NativeCoasterEditor::finalize(AttractionWorld& c, ParkConstructionRuntime& editor) {
    auto& s = *state_;
    require(&editor == &s.construction, "wrong construction owner");
    if (!s.map) {
        return;
    }
    const bool advisor = s.advisor_followup;
    complete_supports(c);
    editor.set_mode(c, 0, true);
    if (advisor) {
        world_operations::queue_scored_advisor_action(c, 642);
    }
}

void NativeCoasterEditor::State::backtrack(AttractionWorld& c, ParkConstructionRuntime& editor, bool recording) {
    geometry_preview.reset();
    preview_signature.clear();
    auto& r = ride(c);
    r.train_state.runtime_flags &= ~0x2000u;
    if (recording) {
        record(c, { LayoutActionCode::coaster_backtrack, std::monostate {} });
    }
    const auto flags = r.train_state.placement_flags;
    const auto index = (flags & 2u) ? current : (!(flags & 1u) && !r.geometry.empty() ? std::optional { r.geometry.size() - 1 } : std::nullopt);
    if (!index || fixed_owner(world(c), r, *index)) {
        return;
    }
    const auto previous = *index ? std::optional { *index - 1 } : std::nullopt;
    checked(remove_coaster_node(c, meshes(c), *map, *index));
    current = previous;
    if ((flags & 2u) && current) {
        ride(c).geometry.at(*current).geometry_flags |= 0x50000u;
    }
    (void)editor;
}

void NativeCoasterEditor::dispatch(AttractionWorld& c, ParkConstructionRuntime& editor, const LayoutAction& action) {
    require(&editor == &state_->construction, "wrong construction owner");
    using enum LayoutActionCode;
    auto& s = *state_;
    match (action.code) {
        case marker => {
            const auto extension = decode_coaster_insert_action(action);
            if (!extension) {
                throw extension.error();
            }
            if (*extension) {
                insert_support(c, **extension, false);
            }
            return;
        }
        case coaster_edit => {
            activate(c, std::get<LayoutWord>(action.payload).value, false);
            return;
        }
        case coaster_edit_legacy => {
            const auto slot = std::get<LayoutWord>(action.payload).value;
            const auto* r = world(c).find(slot);
            activate(c, r ? r->map_id : 0, false);
            return;
        }
        case set_mode or set_mode_nr => {
            const auto value = std::get<LayoutWord>(action.payload).value;
            require(value == 21 || value == 22, "wrong coaster interaction mode");
            if (action.code == set_mode) {
                editor.set_mode(c, value, false);
            } else {
                editor.set_mode_preserving_path(c, value, false);
            }
            s.input_route = CoasterEditorInputRoute::map_action;
            s.active_tool = value == 21 ? CoasterEditorTool::build_supports : CoasterEditorTool::move_supports;
            //673B20's park-object context wrapper is suppressed during replay. The
            //explicit SETEDIT record is the authoritative runtime selection.
            if (!c.construction.action_recording_disabled && c.construction.park_object) {
                auto* runtime = world(c).find(c.construction.park_object->mesh_instance_id);
                require(runtime != nullptr, "selected object has no coaster runtime");
                activate(c, runtime->map_id, false);
            }
            return;
        }
        case coaster_normal => {
            stage(c, false, false, false);
            return;
        }
        case coaster_special => {
            stage(c, true, false, false);
            return;
        }
        case coaster_move => {
            begin_move(c, std::get<LayoutWordTriple>(action.payload).third, false);
            return;
        }
        case coaster_loft or coaster_heading or coaster_wobble => {
            auto& r = s.ride(c);
            const auto ordinal = action.code == coaster_wobble ? std::get<LayoutTwoFloatsWord>(action.payload).ordinal : std::get<LayoutFloatWord>(action.payload).ordinal;
            require(ordinal < r.geometry.size(), "parameter geometry ordinal is out of range");
            const auto& local = r.geometry_state[ordinal];
            const auto flags = r.geometry[ordinal].geometry_flags;
            CoasterNodeParameters value { local.controls[0], local.angles[3], local.angles[1], local.angles[2], local.controls[4], (flags & 1u) != 0, (flags & 4u) != 0, (flags & 2u) != 0 };
            if (action.code == coaster_loft) {
                value.loft = std::get<LayoutFloatWord>(action.payload).value;
            } else if (action.code == coaster_heading) {
                value.heading = std::get<LayoutFloatWord>(action.payload).value;
                value.hold_heading = true;
            } else {
                const auto payload = std::get<LayoutTwoFloatsWord>(action.payload);
                if (flags & 0x40u) {
                    value.loop_rotation = payload.first;
                } else {
                    value.bank = payload.first;
                    value.incline = payload.second;
                    value.hold_bank = value.hold_incline = true;
                }
            }
            checked(set_coaster_node_parameters(c, meshes(c), *s.map, ordinal, value));
            return;
        }
        case coaster_stack_up or coaster_stack_down => {
            const auto ordinal = std::get<LayoutWord>(action.payload).value;
            auto& r = s.ride(c);
            require(ordinal < r.geometry.size(), "stack geometry ordinal is out of range");
            const auto& owner = world(c).placements.at(r.geometry_state[ordinal].placement);
            const auto level = coaster_editor_detail::level(owner, { s.ride_index(c), ordinal });
            require(action.code == coaster_stack_up ? level + 1 < owner.links.size() : level > 0, "stack operation has no adjacent link");
            const auto other = owner.links.at(action.code == coaster_stack_up ? level + 1 : level - 1);
            require(other && other->ride == s.ride_index(c), "stack adjacent link belongs to another owner");
            checked(swap_coaster_stack_levels(c, meshes(c), *s.map, ordinal, other->node));
            return;
        }
        case coaster_delete_range => {
            const auto value = std::get<LayoutWordPair>(action.payload);
            s.delete_range(c, editor, value.first, value.second, false);
            return;
        }
        case coaster_backtrack => {
            s.backtrack(c, editor, false);
            return;
        }
        case coaster_generate_legacy or coaster_generate => {
            auto* r = action.code == coaster_generate ? world(c).find_map(std::get<LayoutWord>(action.payload).value) : &s.ride(c);
            if (r && (r->train_state.runtime_flags & 1u)) {
                checked(regenerate_coaster_record(c, meshes(c), r->map_id));
            }
            return;
        }
        case coaster_speed => {
            const auto value = std::get<LayoutWordPair>(action.payload);
            (void)meshes(c).set_coaster_speed(c, value.first, value.second);
            if (auto* r = world(c).find_map(value.first)) {
                r->train_state.runtime_flags |= 0x8000u;
            }
            return;
        }
        case mouse_down or modified_mouse_down or mouse_up or modified_mouse_up => {
            WorldCellCoordinates tile;
            WorldInputModifierMask modifiers {};
            if (action.code == mouse_down || action.code == mouse_up) {
                const auto value = std::get<LayoutCoordinates>(action.payload);
                tile = { value.x, value.y };
            } else {
                const auto value = std::get<LayoutModifiedCoordinates>(action.payload);
                tile = { value.x, value.y };
                modifiers = static_cast<WorldInputModifierMask>(value.modifiers);
            }
            if (action.code == mouse_down || action.code == modified_mouse_down) {
                this->mouse_down(c, editor, tile, modifiers, false);
            } else {
                (void)this->mouse_up(c, editor, tile, modifiers, false);
            }
            return;
        }
        case _ => throw std::logic_error("action does not belong to native coaster editor");
    }
}

void world_operations::builtin::create_coaster_supports_and_close_runtime_context(WorldRuntime& c) {
    require(c.construction.editor != nullptr, "support completion editor is not bound");
    c.construction.editor->complete_supports(c);
}
} //namespace simcoaster
