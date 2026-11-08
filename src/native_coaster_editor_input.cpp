#include "native_coaster_editor_internal.hpp"

namespace simcoaster {
using namespace native_coaster_detail;

namespace {
void feedback(ParkConstructionState& state, WorldCellCoordinates tile) {
    state.preview_coordinates = tile;
    state.validation_failed = state.status_latch = state.stop = false;
    state.insufficient_funds = state.existing_endpoint = state.queue_joined = false;
    state.faces.clear();
    state.cost = 0;
    state.candidate_overwrite_priority = 3;
    state.occupant_overwrite_priority = 0;
}

WorldCellCoordinates projected(WorldCellCoordinates cursor, WorldCellCoordinates anchor) {
    const auto dx = static_cast<double>(anchor.x) - cursor.x, dy = static_cast<double>(anchor.y) - cursor.y;
    const auto distance = std::hypot(dx, dy);
    require(distance > 0, "projected pylon requires a distinct anchor");
    //48DE20 uses truncating __ftol conversions, not nearest-integer rounding.
    return { static_cast<std::int32_t>(cursor.x + dx / distance * 4), static_cast<std::int32_t>(cursor.y + dy / distance * 4) };
}

void pointer_record(AttractionWorld& c, bool down, WorldCellCoordinates tile, WorldInputModifierMask modifiers) {
    const auto bits = static_cast<std::uint16_t>(std::to_underlying(modifiers));
    if (bits) {
        record(c, { down ? LayoutActionCode::modified_mouse_down : LayoutActionCode::modified_mouse_up, LayoutModifiedCoordinates { static_cast<std::uint8_t>(tile.x), static_cast<std::uint8_t>(tile.y), bits } });
    } else {
        record(c, { down ? LayoutActionCode::mouse_down : LayoutActionCode::mouse_up, LayoutCoordinates { static_cast<std::uint8_t>(tile.x), static_cast<std::uint8_t>(tile.y) } });
    }
}

void append_point(ParkConstructionState& state, WorldCellCoordinates tile) {
    if (state.path_points.size() <= 1024) {
        state.path_points.push_back(tile);
    }
}
} //namespace

bool NativeCoasterEditor::State::valid_destination(AttractionWorld& c, WorldCellCoordinates tile, std::optional<std::size_t> moving) {
    if (!construction_detail::bounds(tile)) {
        return false;
    }
    auto& r = ride(c);
    auto& w = world(c);
    const auto* terrain = c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
    require(terrain && terrain->width_cells && terrain->height_cells, "terrain is missing");
    if (tile.x < 0 || tile.y < 0 || static_cast<std::uint32_t>(tile.x) >= terrain->width_cells || static_cast<std::uint32_t>(tile.y) >= terrain->height_cells) {
        return false;
    }
    const auto terrain_index = static_cast<std::size_t>(tile.y) * terrain->width_cells + static_cast<std::uint32_t>(tile.x);
    require(terrain_index < c.construction.original_terrain_cells.size(), "original terrain snapshot is missing");
    if (std::to_underlying(c.construction.original_terrain_cells[terrain_index].flags) & 1u) {
        return false;
    }
    const auto index = moving.value_or((r.train_state.placement_flags & 2u) && current ? *current + 1 : r.geometry.size());
    const auto candidate_pylon = moving ? w.placements.at(r.geometry_state.at(*moving).placement).pylon : ((interaction & 0x4040u) ? 1u : 0u);
    require(candidate_pylon < r.definition->pylon_templates.size() && r.definition->pylon_templates[candidate_pylon], "selected pylon resources are unavailable");
    for (const auto& owner : w.placements) {
        if (!(owner.flags & 5u) && equal(tile, coordinate(owner))) {
            if (owner.definition != r.definition || owner.pylon != candidate_pylon) {
                return false;
            }
            const auto same = moving && r.geometry_state.at(*moving).placement == static_cast<std::size_t>(&owner - w.placements.data());
            const auto limits = coaster_placement_rules(r.definition->balance.pylons[candidate_pylon], candidate_pylon != 0);
            if (!same && owner.links.size() >= limits.maximum_stack) {
                return false;
            }
            for (const auto& ref : owner.links) {
                if (!ref || ref->ride != ride_index(c)) {
                    return false;
                }
            }
            if (!owner.links.empty()) {
                const auto first = owner.links.front();
                const auto forbidden = moving || (r.train_state.placement_flags & 2u) ? 0x600u : 0x200u;
                if (r.geometry.at(first->node).geometry_flags & forbidden) {
                    return false;
                }
                if (!moving && owner.links.back()->node + 1 == r.geometry.size()) {
                    return false;
                }
            }
        }
    }
    const auto at = [&](std::size_t n) {
        return coordinate(w.placements.at(r.geometry_state.at(n).placement));
    };
    const auto flags = [&](std::size_t n) {
        return w.placements.at(r.geometry_state.at(n).placement).flags;
    };
    const auto validate = [&](std::size_t origin_index, WorldCellCoordinates origin, WorldCellCoordinates destination, std::optional<WorldCellCoordinates> before, std::uint32_t owner_flags, std::size_t pylon) {
        CoasterConnectionCandidate request { { origin.x, origin.y }, { destination.x, destination.y }, {}, static_cast<std::uint32_t>(origin_index), owner_flags };
        if (before) {
            request.preceding = std::array { before->x, before->y };
        }
        return validate_coaster_connection(coaster_placement_rules(r.definition->balance.pylons.at(pylon), pylon != 0), request) == CoasterPlacementValidity::valid;
    };
    if (index && !(moving && (r.train_state.placement_flags & 2u) && (r.geometry[index - 1].geometry_flags & 0x10000u))) {
        const auto pylon = (r.geometry.at(index - 1).geometry_flags & 0x40u) ? 1u : candidate_pylon;
        if (!validate(index - 1, at(index - 1), tile, index > 1 ? std::optional { at(index - 2) } : std::nullopt, flags(index - 1), pylon)) {
            return false;
        }
    }
    //473D88 starts with NO revalidation anchor. An ordinary new support ends
    //at474420; it must not connect across the open gap to the next list node.
    //Only moving a node or closing onto the existing validation/head owner
    //establishes the recursive anchor in473DD1/473DD9/473DEC/4743AF/4743E9.
    auto revalidation = moving;
    if (!moving && validation && equal(tile, at(*validation))) {
        revalidation = validation;
    }
    if (!moving && !(r.train_state.placement_flags & 1u) && !r.geometry.empty() && equal(tile, at(0))) {
        revalidation = 0;
    }
    if (revalidation && *revalidation + 1 < r.geometry.size() && !(r.geometry[*revalidation].geometry_flags & 0x10000u)) {
        const auto origin = *revalidation, next = origin + 1;
        const auto pylon = (r.geometry[origin].geometry_flags & 0x40u) ? 1u : 0u;
        if (!validate(origin, tile, at(next), origin ? std::optional { at(origin - 1) } : std::optional { at(r.geometry.size() - 1) }, flags(origin), pylon)) {
            return false;
        }
        if (next + 1 < r.geometry.size() && !(r.geometry[next].geometry_flags & 0x10000u)) {
            const auto following_pylon = w.placements.at(r.geometry_state[next].placement).pylon;
            if (!validate(next, at(next), at(next + 1), tile, flags(next), following_pylon)) {
                return false;
            }
        } else if (moving && (r.train_state.placement_flags & 1u) && r.geometry.size() > 2 && !(r.geometry[next].geometry_flags & 0x10000u)) {
            const auto closing_pylon = w.placements.at(r.geometry_state[next].placement).pylon;
            if (!validate(next, at(next), at(1), tile, flags(next), closing_pylon)) {
                return false;
            }
        }
    }
    return true;
}

bool NativeCoasterEditor::hover(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates tile, WorldInputModifierMask) {
    auto& s = *state_;
    require(&editor == &s.construction, "wrong construction owner");
    feedback(editor.state, tile);
    s.hover_tile = tile;
    if (!s.map || !construction_detail::bounds(tile)) {
        s.geometry_preview.reset();
        s.preview_signature.clear();
        editor.state.validation_failed = editor.state.status_latch = true;
        return false;
    }
    if (geometry_interaction_active()) {
        s.rebuild_preview(c, true);
        return true;
    }
    auto& r = s.ride(c);
    const auto anchor = s.anchor(c);
    const auto anchor_tile = anchor ? std::optional { coordinate(world(c).placements.at(r.geometry_state.at(*anchor).placement)) } : std::nullopt;
    bool valid = true;
    if (c.construction.mode == 21 && !s.edit_event_pending) {
        valid = (anchor_tile && equal(tile, *anchor_tile)) || s.valid_destination(c, tile);
        if (s.interaction & 0x4000u) {
            if (!anchor_tile || equal(tile, *anchor_tile)) {
                valid = false;
            } else {
                const auto other = projected(tile, *anchor_tile);
                valid = valid && construction_detail::map(c, construction_detail::cell_id(tile)).base.common.type != 21 && s.valid_destination(c, other);
                if (construction_detail::bounds(other)) {
                    editor.command_line(c, 256u | 21u, other, other);
                }
            }
        }
        editor.state.validation_failed = !valid;
        editor.command_line(c, 256u | 21u, tile, tile);
    } else if (c.construction.mode == 22) {
        if (s.detached && s.preview) {
            valid = s.valid_destination(c, tile, s.preview);
        }
        editor.state.validation_failed = !valid;
        editor.command_line(c, 256u | 22u, tile, tile);
    }
    editor.state.validation_failed = editor.state.validation_failed || !valid;
    editor.state.status_latch = editor.state.status_latch || editor.state.validation_failed;
    if ((c.construction.mode == 21 && !s.edit_event_pending) || (c.construction.mode == 22 && s.detached)) {
        const auto failed = editor.state.validation_failed, latched = editor.state.status_latch;
        s.draw_valid_region(c, editor);
        editor.state.validation_failed = failed;
        editor.state.status_latch = latched;
    }
    s.preview_candidate_valid = valid && !editor.state.validation_failed;
    s.rebuild_preview(c, true);
    return !editor.state.status_latch;
}

void NativeCoasterEditor::State::draw_valid_region(AttractionWorld& c, ParkConstructionRuntime& editor) {
    auto& r = ride(c);
    std::optional<std::size_t> selected;
    if (interaction & 0x100u) {
        require(preview && *preview, "move region has no preceding control");
        selected = *preview - 1;
        if ((r.train_state.placement_flags & 2u) && (r.geometry[*selected].geometry_flags & 0x10000u)) {
            require(*preview + 1 < r.geometry.size(), "excluded move region has no following control");
            selected = *preview + 1;
        }
    } else if (r.train_state.placement_flags & 2u) {
        selected = current;
    } else if (!r.geometry.empty()) {
        selected = r.geometry.size() - 1;
        while (*selected && (r.geometry[*selected].geometry_flags & 0x2000u)) {
            --*selected;
        }
        if (r.geometry[*selected].geometry_flags & 0x2000u) {
            selected.reset();
        }
    }
    require(selected.has_value(), "placement region has no unmarked anchor");
    const auto center = coordinate(world(c).placements.at(r.geometry_state.at(*selected).placement));
    const auto pylon = (interaction & 0x4040u) ? 1u : 0u;
    const auto limits = coaster_placement_rules(r.definition->balance.pylons.at(pylon), pylon != 0);
    const auto radius = static_cast<std::int32_t>(std::sqrt(static_cast<double>(limits.maximum_distance_squared))) + 1;
    const auto xstart = c.visitors.environment.MapInfo.HeightfieldXStart, ystart = c.visitors.environment.MapInfo.HeightfieldYStart;
    const auto x0 = std::max(center.x - radius, xstart), y0 = std::max(center.y - radius, ystart);
    const auto x1 = std::min(center.x + radius, static_cast<std::int32_t>(c.weather.map_heightfield_width) + xstart);
    const auto y1 = std::min(center.y + radius, static_cast<std::int32_t>(c.weather.map_heightfield_height) + ystart);
    //Approved repairs of4749A4/65F826: correct axis and half-open map bounds.
    for (auto y = y0; y < y1; ++y) {
        for (auto x = x0; x < x1; ++x) {
            const WorldCellCoordinates tile { x, y };
            if (!construction_detail::bounds(tile)) {
                continue;
            }
            const auto& cell = construction_detail::map(c, construction_detail::cell_id(tile));
            const auto type = cell.base.common.type;
            const auto& paired = construction_detail::root_track(c, cell.base);
            if ((cell.base.common.flags & 0x40u) || type == 2 || type == 5 || type == 7 || type == 8 || type == 30 || type == 3 || construction_detail::footprint(type) || paired.base.common.type == 25 || world_track_reference_matches_type_set(c, cell.base.index) || cell.is_obstacle || (type == 1 && (cell.base.common.flags & 0x20u))) {
                continue;
            }
            if (valid_destination(c, tile, detached ? preview : std::nullopt)) {
                //65F937/65FB70 pass11F: preview plus unconditional orange31.
                //Selector7 is a cell request, not the native valid-region material.
                editor.command_line(c, 256u | 31u, tile, tile);
            }
        }
    }
}

bool NativeCoasterEditor::State::place(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates tile, bool charge_cost, coaster_editor_detail::PlacementValidation placement_validation) {
    geometry_preview.reset();
    preview_signature.clear();
    auto& r = ride(c);
    if (validation) {
        const auto& owner = world(c).placements.at(r.geometry_state.at(*validation).placement);
        if (equal(tile, coordinate(owner))) {
            require(current.has_value(), "validation boundary has no predecessor");
            r.geometry.at(*current).geometry_flags &= ~0x50000u;
            r.geometry.at(*validation).geometry_flags &= (r.geometry.at(*current).geometry_flags & 0x400u) ? ~0x20000u : ~0xA0000u;
            r.train_state.placement_flags &= ~2u;
            current = r.geometry.size() - 1;
            validation.reset();
            transition_coaster_detail_state(c, meshes(c), r, RideDetailState::vehicles_only);
            mark(r);
            return true;
        }
    }
    append_point(editor.state, tile);
    bool continuing = true;
    for (const auto op : std::array { 135u, 21u, 131u, 132u, 129u }) {
        continuing = editor.command_line(c, op, tile, tile) && continuing;
    }
    if (!continuing) {
        edit_event_pending = false;
        support_refresh_pending = true;
        return false;
    }
    CoasterNodePlacement placement;
    placement.charge_cost = charge_cost;
    placement.tile_x = tile.x;
    placement.tile_y = tile.y;
    placement.pylon = (interaction & 0x4040u) ? 1u : 0u;
    placement.second_piece = remaining == 1;
    if ((r.train_state.placement_flags & 2u) && current) {
        placement.insertion_after = current;
    }
    const auto index = checked(coaster_editor_detail::place_node_transaction(c, meshes(c), *map, placement, placement_validation));
    auto& result = ride(c);
    //Native473420 keeps a pointer to the validation object while inserting
    //into its linked chain. Our vector ordinal must continue to identify that
    //same successor, including across multiple placements into one open gap.
    if (validation && *validation >= index) {
        ++*validation;
    }
    current = index;
    if (interaction & 0x4040u) {
        require(remaining > 0, "special placement countdown underflow");
        --remaining;
        if (!remaining) {
            interaction &= ~0x4040u;
        }
    }
    construction_detail::track(c, construction_detail::cell_id(tile)).base.common.mesh_instance = result.geometry_state.at(index).mesh_slot;
    return true;
}

void NativeCoasterEditor::State::commit_move(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates tile) {
    geometry_preview.reset();
    preview_signature.clear();
    require(preview && detached, "move preview is not detached");
    const auto ordinal = *preview;
    const auto source = *detached;
    //Another runtime/pylon type at the destination restores the original
    //placement, as48B80E..48B860 does. Never merge incompatible support owners.
    const auto& r = ride(c);
    const auto& owner = world(c).placements.at(r.geometry_state.at(ordinal).placement);
    bool compatible = true;
    for (const auto& candidate : world(c).placements) {
        if (!(candidate.flags & 5u) && equal(tile, coordinate(candidate))) {
            compatible = compatible && candidate.definition == owner.definition && candidate.pylon == owner.pylon;
        }
    }
    if (!compatible) {
        restore_detached(c, editor);
        cleanup(c, editor);
        return;
    }
    for (const auto op : std::array { 135u, 21u, 131u, 129u }) {
        editor.command_line(c, op, tile, tile);
    }
    checked(move_coaster_node(c, meshes(c), *map, ordinal, tile.x, tile.y));
    const auto slot = ride(c).geometry_state.at(ordinal).mesh_slot;
    construction_detail::track(c, construction_detail::cell_id(tile)).base.common.mesh_instance = slot;
    //A remaining link keeps the source support alive; otherwise begin_move
    //already removed its cell. Reinstall the actual surviving stack mesh.
    if (!equal(source.tile, tile)) {
        for (const auto& candidate : world(c).placements) {
            if (equal(coordinate(candidate), source.tile) && !candidate.links.empty()) {
                const auto ref = candidate.links.front();
                if (ref) {
                    construction_detail::track(c, construction_detail::cell_id(source.tile)).base.common.mesh_instance = world(c).bindings.at(ref->ride).geometry_state.at(ref->node).mesh_slot;
                }
            }
        }
    }
    detached.reset();
    interaction &= ~0x100u;
    preview.reset();
    clear_range(c);
    edit = Edit::none;
    remaining = 0;
    stage = Stage::idle;
    mark(ride(c));
}

void NativeCoasterEditor::mouse_down(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates tile, WorldInputModifierMask modifiers, bool use_preview) {
    auto& s = *state_;
    require(&editor == &s.construction, "wrong construction owner");
    editor.state.mouse_down_pending = true;
    if (s.edit_event_pending) {
        s.edit_event_pending = false;
        if (c.construction.mode == 21) {
            s.cleanup(c, editor);
            s.edit = State::Edit::cleanup;
            editor.state.validation_failed = editor.state.status_latch = editor.state.stop = true;
        }
        return;
    }
    s.support_refresh_pending = false;
    if (c.construction.action_recording_disabled) {
        editor.state.validation_failed = false;
        if (!c.construction.selected_track_type) {
            hover(c, editor, tile, modifiers);
        }
    }
    if (use_preview) {
        tile = editor.state.preview_coordinates;
    }
    if (!construction_detail::bounds(tile)) {
        return;
    }
    if (!editor.state.status_latch) {
        pointer_record(c, true, tile, modifiers);
    }
    if (editor.state.status_latch || !s.map) {
        return;
    }
    if (c.construction.mode == 22) {
        editor.refresh_origin(c, false);
        if (s.detached) {
            s.commit_move(c, editor, tile);
        } else if (!c.construction.action_recording_disabled) {
            (void)select_at_tile(c, tile, true);
        }
        return;
    }
    if (c.construction.mode != 21) {
        return;
    }
    const auto index = s.anchor(c);
    require(index.has_value(), "placement has no active anchor");
    const auto anchor_tile = coordinate(world(c).placements.at(s.ride(c).geometry_state.at(*index).placement));
    if (equal(tile, anchor_tile)) {
        activate(c, 0, true);
        editor.set_mode(c, 0, true);
        return;
    }
    const bool paired = (s.interaction & 0x4000u) != 0;
    const auto other = paired ? projected(tile, anchor_tile) : tile;
    require(!paired || construction_detail::bounds(other), "projected support is outside the World");
    const bool on_path = construction_detail::map(c, construction_detail::cell_id(tile)).base.common.type == 1 || (paired && construction_detail::map(c, construction_detail::cell_id(other)).base.common.type == 1);
    if (!c.construction.action_recording_disabled) {
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 101, tile.x * 10, 0, tile.y * 10);
    }
    if (!s.place(c, editor, tile)) {
        return;
    }
    if (paired && !s.place(c, editor, other)) {
        return;
    }
    if (!s.advisor_followup && on_path) {
        s.advisor_followup = true;
        world_operations::queue_scored_advisor_action(c, 641);
    }
    const auto flags = s.ride(c).train_state.placement_flags;
    if ((flags & 1u) && !(flags & 2u)) {
        s.cleanup(c, editor);
        checked(update_coaster_animation(c, meshes(c)));
        activate(c, 0, true);
        editor.set_mode(c, 0, true);
    } else {
        s.clear_range(c);
        s.interaction &= ~0x4040u;
        s.remaining = 0;
        s.stage = State::Stage::idle;
        s.edit = State::Edit::none;
        checked(update_coaster_animation(c, meshes(c)));
        s.edit_event_pending = true;
        s.support_refresh_pending = false;
        c.staff.interaction.cursor_presentation = 12;
    }
}

std::uint8_t NativeCoasterEditor::mouse_up(AttractionWorld& c, ParkConstructionRuntime& editor, WorldCellCoordinates tile, WorldInputModifierMask modifiers, bool use_preview) {
    auto& s = *state_;
    require(&editor == &s.construction, "wrong construction owner");
    if (use_preview) {
        tile = editor.state.preview_coordinates;
    }
    if (!construction_detail::bounds(tile)) {
        return 0;
    }
    if (!editor.state.status_latch || c.construction.mode == 22) {
        pointer_record(c, false, tile, modifiers);
    }
    editor.state.mouse_down_pending = false;
    if (editor.state.status_latch || !s.map) {
        return 0;
    }
    if (c.construction.mode == 22) {
        if (s.detached) {
            s.commit_move(c, editor, tile);
        }
        return 0;
    }
    if (c.construction.mode != 21) {
        return 0;
    }
    if (s.support_refresh_pending) {
        s.support_refresh_pending = false;
        finalize(c, editor);
        return 0;
    }
    if (s.edit_event_pending) {
        s.edit_event_pending = false;
        s.cleanup(c, editor);
        s.edit = State::Edit::cleanup;
        editor.state.validation_failed = editor.state.status_latch = editor.state.stop = true;
        refresh_placement_tool(c);
    }
    return 0;
}
} //namespace simcoaster
