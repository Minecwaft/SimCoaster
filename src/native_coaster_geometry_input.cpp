#include "native_coaster_editor_internal.hpp"
#include "simcoaster/simulation_sine.hpp"
#include <utility>

namespace simcoaster {
using namespace native_coaster_detail;

namespace {
namespace core = coaster_editor_detail;

CoasterNodeParameters parameters(const WorldCoasterBinding& ride, std::size_t index) {
    const auto& local = ride.geometry_state.at(index);
    const auto flags = ride.geometry.at(index).geometry_flags;
    return { local.controls[0], local.angles[3],   local.angles[1],   local.angles[2],
             local.controls[4], (flags & 1u) != 0, (flags & 4u) != 0, (flags & 2u) != 0 };
}

std::optional<std::size_t> mesh_node(AttractionWorld& c, std::uint32_t map, std::uint32_t mesh_slot) {
    if (!mesh_slot)
        return {};
    const auto* ride = world(c).find_map(map);
    if (!ride)
        return {};
    const auto found = std::ranges::find(ride->geometry_state, mesh_slot, &CoasterNodeState::mesh_slot);
    if (found == ride->geometry_state.end())
        return {};
    const auto index = static_cast<std::size_t>(found - ride->geometry_state.begin());
    const auto& owner = world(c).placements.at(found->placement);
    //48D240's temporary pick list excludes geometry8192 when owner32 is set.
    if ((owner.flags & 32u) && (ride->geometry.at(index).geometry_flags & 8192u))
        return {};
    return index;
}

void apply_parameter(CoasterNodeParameters& value, CoasterParameter kind, float input) {
    match (kind) {
        case CoasterParameter::loft => value.loft = input;
        case CoasterParameter::heading => {
            value.heading = input;
            value.hold_heading = true;
        }
        case CoasterParameter::bank => {
            value.bank = input;
            value.hold_bank = true;
        }
        case CoasterParameter::incline => {
            value.incline = input;
            value.hold_incline = true;
        }
        case CoasterParameter::loop_rotation => value.loop_rotation = input;
        case _ => {}
    }
}
} //namespace

void NativeCoasterEditor::State::finish_parameter_drag(AttractionWorld& c, bool commit) {
    const auto drag = std::exchange(parameter_drag, std::nullopt);
    geometry_preview.reset();
    preview_signature.clear();
    if (!drag || !commit || !map || *map != drag->map)
        return;
    const auto* target = world(c).find_map(drag->map);
    if (!target
        || !target->park_object
        || target->park_object->base.header.retirement_pending
        || target->geometry.size() != drag->geometry_count
        || drag->ordinal >= target->geometry_state.size()
        || target->geometry_state[drag->ordinal].mesh_slot != drag->mesh_slot)
        return;
    const auto apply = [&](const ParameterAxis& axis) {
        checked(edit_coaster_node_parameter(c, meshes(c), drag->map, drag->ordinal, axis.parameter, axis.value, true));
    };
    const bool paired_angles = drag->axes[0]
                            && drag->axes[1]
                            && drag->axes[0]->parameter == CoasterParameter::bank
                            && drag->axes[1]->parameter == CoasterParameter::incline;
    if (paired_angles) {
        //486480 records the final bank/incline pair once. Each raw interactive
        //parameter kernel still validates its own SAM gate/limits; rollback
        //covers both axes if either update fails.
        core::EditTransaction transaction{ c, meshes(c) };
        {
            construction_detail::RestoreValue recording{ c.construction.action_recording_disabled };
            c.construction.action_recording_disabled = 1;
            apply(*drag->axes[0]);
            apply(*drag->axes[1]);
        }
        transaction.commit();
        const auto& local = ride(c).geometry_state.at(drag->ordinal);
        record(c,
               { LayoutActionCode::coaster_wobble,
                 LayoutTwoFloatsWord{ local.angles[1], local.angles[2], static_cast<std::uint16_t>(drag->ordinal) } });
    } else
        for (const auto& axis : drag->axes)
            if (axis)
                apply(*axis);
    ride(c).train_state.runtime_flags &= ~0x2000u;
}

void NativeCoasterEditor::State::rebuild_parameter_preview(AttractionWorld& c, bool update_feedback) {
    if (!parameter_drag || !map)
        return;
    const auto drag = *parameter_drag;
    const auto* initial = world(c).find_map(drag.map);
    if (!initial
        || initial->geometry.size() != drag.geometry_count
        || drag.ordinal >= initial->geometry_state.size()
        || initial->geometry_state[drag.ordinal].mesh_slot != drag.mesh_slot) {
        finish_parameter_drag(c, false);
        return;
    }
    //A synchronous graph/mesh rollback produces the original animated model
    //and track preview without committing expense/action records per frame.
    //Native485D00 settles loft exactly once when the gesture is released.
    NativeCoasterPreview output;
    output.valid = true;
    std::optional<std::array<float, 3>> camera_point, next_attachment;
    {
        auto& m = meshes(c);
        core::EditTransaction transaction{ c, m };
        const auto r = ride_index(c);
        auto& target = transaction.world.bindings.at(r);
        const auto original_attachment =
            core::profile_position(m, transaction.world, target.geometry_state.at(drag.ordinal));
        const auto old_cost = core::truncated_cost(core::piece_cost(transaction.world, r, drag.ordinal, false, false));
        auto value = parameters(target, drag.ordinal);
        bool loft = false;
        for (const auto& axis : drag.axes)
            if (axis) {
                apply_parameter(value, axis->parameter, axis->value);
                loft |= axis->parameter == CoasterParameter::loft;
            }
        auto& local = target.geometry_state.at(drag.ordinal);
        auto& flags = target.geometry.at(drag.ordinal).geometry_flags;
        local.controls[0] = value.loft;
        local.angles[3] = value.heading;
        local.angles[1] = value.bank;
        local.angles[2] = value.incline;
        local.controls[4] = value.loop_rotation;
        flags = (flags & ~7u)
              | (value.hold_heading ? 1u : 0u)
              | (value.hold_incline ? 2u : 0u)
              | (value.hold_bank ? 4u : 0u);
        const auto& owner = transaction.world.placements.at(local.placement);
        if (loft && !(core::pylon(owner).flags & 0x4000u))
            target.default_attachment_height = (core::pylon(owner).height_maximum - core::pylon(owner).height_minimum)
                                                 * std::clamp(value.loft, 0.f, 1.f)
                                             + core::pylon(owner).height_minimum;
        core::mark_owner(transaction.world, owner);
        core::finish(transaction, r);
        if (loft && update_feedback) {
            camera_point = drag.previous_attachment.value_or(original_attachment);
            next_attachment = core::profile_position(m, transaction.world, target.geometry_state.at(drag.ordinal));
        }
        if (loft)
            output.piece_cost =
                fixed::subtract(core::rounded_cost(static_cast<float>(
                                    core::piece_cost(transaction.world, r, drag.ordinal, false, false))),
                                old_cost);
        capture_preview(output, transaction, r);
    }
    output.interaction_cost = output.piece_cost < 0 ? fixed::subtract(output.piece_cost, 250539758) : output.piece_cost;
    geometry_preview = std::move(output);
    if (camera_point) {
        fit_world_view_camera_to_coaster_attachment(c, *camera_point);
        parameter_drag->previous_attachment = next_attachment;
    }
    if (update_feedback)
        construction.state.cost = geometry_preview->interaction_cost;
}

void NativeCoasterEditor::delete_context_geometry(AttractionWorld& c, std::uint32_t slot) {
    auto& s = *state_;
    if (!s.map)
        return;
    const auto hit = mesh_node(c, *s.map, slot);
    if (!hit)
        return;
    //Native557130 selects the saved mesh, starts a range and commits the
    //same endpoint. The shared range kernel owns station guards and records.
    s.current = *hit;
    delete_range(c, static_cast<std::uint16_t>(*hit), static_cast<std::uint16_t>(*hit), true);
    s.restore_build_tools_after_delete(c);
}

bool NativeCoasterEditor::geometry_pointer(AttractionWorld& c,
                                           std::uint32_t slot,
                                           std::array<std::int32_t, 2> pixel,
                                           CoasterGeometryPointerPhase phase) {
    auto& s = *state_;
    if (!geometry_interaction_active() || c.construction.action_recording_disabled)
        return false;
    if (!s.active_tool)
        return true;
    auto& r = s.ride(c);
    if (!s.geometry_action_enabled)
        return true;
    const auto hit = mesh_node(c, *s.map, slot);
    if (s.parameter_drag
        && (phase == CoasterGeometryPointerPhase::release || phase == CoasterGeometryPointerPhase::cancel)) {
        s.finish_parameter_drag(c, true);
        return true;
    }
    if (s.parameter_drag) {
        auto& drag = *s.parameter_drag;
        std::array<float, 2> delta{};
        for (std::size_t axis = 0; axis < 2; ++axis)
            delta[axis] = (drag.start_pixel[axis] - static_cast<float>(pixel[axis])) * drag.sensitivity[axis];
        if (drag.camera_relative) {
            const float angle = c.view.camera.heading_radians - drag.track_heading;
            const auto sine =
                simulation_sine_at_index(static_cast<std::int32_t>(angle * simulation_sine_samples_per_radian));
            const auto cosine = simulation_sine_at_index(
                static_cast<std::int32_t>((angle + 1.5707964f) * simulation_sine_samples_per_radian));
            delta = { cosine * delta[0] - sine * delta[1], cosine * delta[1] + sine * delta[0] };
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
            if (auto& entry = drag.axes[axis]) {
                const auto requested = entry->range.value + delta[axis];
                entry->value = requested < entry->range.minimum ? entry->range.minimum
                             : requested > entry->range.maximum ? entry->range.maximum
                                                                : requested;
                //488230 reanchors the clamped screen axis so reversal starts
                //immediately; camera-relative paired wobble keeps its initial frame.
                if (!drag.camera_relative && entry->value != requested)
                    drag.start_pixel[axis] =
                        (entry->value - entry->range.value) / entry->range.per_pixel + static_cast<float>(pixel[axis]);
            }
        s.rebuild_parameter_preview(c, true);
        return true;
    }
    const auto tool = *s.active_tool;
    using T = CoasterEditorTool;
    if (phase == CoasterGeometryPointerPhase::cancel) {
        s.clear_range(c);
        s.geometry_anchor.reset();
        return true;
    }
    if (tool == T::delete_track_section && s.geometry_anchor && phase == CoasterGeometryPointerPhase::hover) {
        s.clear_range(c);
        if (!hit) {
            s.range_anchor = *s.geometry_anchor;
            r.selection_texture_bias = 1;
            mark(r);
            return true;
        }
        auto anchor = *s.geometry_anchor, cursor = *hit;
        const bool same_endpoint = anchor == cursor;
        if (anchor == cursor) {
            if (fixed_owner(world(c), r, anchor))
                return true;
            const auto flags = r.geometry[anchor].geometry_flags;
            auto before = anchor ? anchor - 1 : anchor;
            auto after = std::min(anchor + 1, r.geometry.size() - 1);
            if (flags & 64u) {
                if (!(flags & 128u))
                    before = before ? before - 1 : before;
                else
                    after = std::min(after + 1, r.geometry.size() - 1);
            }
            anchor = before;
            cursor = after;
        } else if (!anchor)
            anchor = r.geometry.size() - 1;
        else if (!cursor)
            cursor = r.geometry.size() - 1;
        if (!same_endpoint) {
            if (anchor > cursor)
                std::swap(anchor, cursor);
            if ((r.geometry[anchor].geometry_flags & (64u | 128u)) == (64u | 128u) && anchor)
                --anchor;
            if ((r.geometry[cursor].geometry_flags & (64u | 128u)) == 64u && cursor + 1 < r.geometry.size())
                ++cursor;
        }
        s.range_anchor = anchor;
        s.range_cursor = cursor;
        const auto low = std::min(anchor, cursor);
        const auto high = std::max(anchor, cursor);
        for (auto i = low; i < high; ++i)
            r.geometry[i].geometry_flags |= 0x1000000u;
        r.selection_texture_bias = 1;
        mark(r);
        return true;
    }
    if (phase == CoasterGeometryPointerPhase::release) {
        if (!hit)
            return true;
        if (tool == T::delete_track_section && s.geometry_anchor) {
            const auto anchor = *s.geometry_anchor;
            s.geometry_anchor.reset();
            if (anchor == *hit && fixed_owner(world(c), r, anchor)) {
                s.clear_range(c);
                return true;
            }
            delete_range(c, static_cast<std::uint16_t>(anchor), static_cast<std::uint16_t>(*hit), true);
            s.restore_build_tools_after_delete(c);
        } else if (tool == T::delete_single_support) {
            if (!fixed_owner(world(c), r, *hit)) {
                delete_range(c, static_cast<std::uint16_t>(*hit), static_cast<std::uint16_t>(*hit), true);
                s.restore_build_tools_after_delete(c);
            }
        } else if (tool == T::stack_track_up || tool == T::stack_track_down) {
            const auto& owner = world(c).placements.at(r.geometry_state.at(*hit).placement);
            if (owner.flags & 2u)
                return true;
            const auto level = core::level(owner, { s.ride_index(c), *hit });
            const bool up = tool == T::stack_track_up;
            if ((up && level + 1 >= owner.links.size()) || (!up && !level))
                return true;
            const auto peer = owner.links.at(up ? level + 1 : level - 1);
            if (peer && peer->ride == s.ride_index(c)) {
                checked(swap_coaster_stack_levels(c, meshes(c), *s.map, *hit, peer->node));
                record(c,
                       { up ? LayoutActionCode::coaster_stack_up : LayoutActionCode::coaster_stack_down,
                         LayoutWord{ static_cast<std::uint16_t>(*hit) } });
            }
        } else if (tool == T::insert_support)
            s.insert_intermediate_support(c, *hit);
        return true;
    }
    if (phase != CoasterGeometryPointerPhase::press || !hit)
        return true;
    //The picked geometry is native g_current_coaster_geometry_object; it is
    //separate from the runtime registry's current gap/placement anchor.
    if (tool == T::delete_track_section) {
        s.geometry_anchor = *hit;
        s.range_anchor = s.range_cursor = *hit;
        r.selection_texture_bias = 1;
        return true;
    }
    State::ParameterDrag drag;
    drag.map = *s.map;
    drag.mesh_slot = slot;
    drag.ordinal = *hit;
    drag.geometry_count = r.geometry.size();
    drag.start_pixel = { static_cast<float>(pixel[0]), static_cast<float>(pixel[1]) };
    drag.track_heading = r.geometry_state.at(*hit).angles[3];
    const auto axis = [&](CoasterParameter parameter, std::size_t index) {
        auto range = checked(coaster_node_parameter_range(meshes(c), *s.map, *hit, parameter));
        if (range)
            drag.axes[index] = State::ParameterAxis{ parameter, *range, range->value };
    };
    if (tool == T::adjust_support_height)
        axis(CoasterParameter::loft, 1);
    else if (tool == T::rotate_supports)
        axis(CoasterParameter::heading, 0);
    else if (tool == T::pivot_track) {
        if (r.geometry.at(*hit).geometry_flags & 64u)
            axis(CoasterParameter::loop_rotation, 0);
        else {
            axis(CoasterParameter::bank, 0);
            axis(CoasterParameter::incline, 1);
            drag.camera_relative = true;
        }
    }
    if (drag.axes[0] || drag.axes[1]) {
        for (std::size_t i = 0; i < 2; ++i)
            if (drag.axes[i])
                s.drag_sensitivity[i] = drag.axes[i]->range.per_pixel;
        drag.sensitivity = s.drag_sensitivity;
        s.parameter_drag = std::move(drag);
        s.rebuild_parameter_preview(c, true);
    }
    return true;
}
} //namespace simcoaster
