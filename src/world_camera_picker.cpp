#include "simcoaster/world_camera_picker.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_surface.hpp"
#include <format>
#include <stdexcept>

namespace simcoaster {
namespace {
MapCell* cell(WorldRuntime& c, CellId id) {
    if (!c.simulation.current || !id || id > map_cell_count) {
        return nullptr;
    }
    return &c.simulation.current->map_cells.at(static_cast<std::size_t>(id) - 1u);
}

ThingId object_at(WorldRuntime& c, const MapCell& source) {
    const auto zero = static_cast<std::uint32_t>(source.base.index) - 1u;
    const auto x = static_cast<std::int32_t>(zero & 127u);
    const auto y = static_cast<std::int32_t>(zero >> 7);
    const auto* object = find_world_park_object_at_map_cell(c, x, y);
    return object ? object->base.header.id : 0;
}

ParkObjectRuntimeView* ride(WorldRuntime& c, ThingId id) {
    if (!id || id >= c.entities.size()) {
        return nullptr;
    }
    auto* record = c.entities[id].payload;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object) {
        return nullptr;
    }
    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(record);
    const auto* definition = get_park_object_definition(c, object);
    //Native5E7AA0 returns LOWWORD AddOn.UpgradesId from the definition. It is
    //unrelated to the live object's mUpgradeParent identity.
    return definition && static_cast<std::uint16_t>(definition->add_on.upgrades_id) == 0 && definition->which_ui_type == ObjectUiCategory::rides ? object : nullptr;
}

bool terrain_hover(WorldRuntime& c, const MapCell& source) {
    const auto type = source.base.common.type;
    if (type == 30 && !source.is_obstacle) {
        return true;
    }
    const auto* track = resolve_track_cell_for_grid_entry(c, &source.base);
    return !world_track_reference_matches_type_set(c, source.base.index) && !world_cell_is_track_child(&track->base) && type != 2 && type != 5 && type != 7 && type != 8 && !(source.base.common.flags & 64u);
}

void install_base(WorldRuntime& c, bool online_alternate) {
    cancel_world_staff_interaction(c);
    auto& state = c.staff.interaction;
    state.handler = { DefaultWorldInteraction { online_alternate }, false };
    c.construction.handler = &state.handler;
    state.cursor_presentation = 0;
}

void set_help(WorldRuntime& c, RideTrackPickInteraction& state, std::uint32_t row) {
    state.help_row = row;
    if (!row) {
        state.help_text.clear();
        return;
    }
    const std::array argument { BfstWideStringArgument { 0, state.display_name } };
    const auto strings = state.hovered_object ? std::span<const BfstWideStringArgument> { argument } : std::span<const BfstWideStringArgument> {};
    auto text = format_bfst_wide_string(c.messages.mail_language.ui_help, c.messages.mail_language.conversion, row, 16383, strings);
    if (!text) {
        throw std::runtime_error(std::format("Camera picker UIHELPTEXT: {}", text.error().reason));
    }
    state.help_text = std::move(*text);
}
} //namespace

void activate_world_camera_picker(WorldRuntime& c, std::function<void()> reset_ui) {
    if (!reset_ui) {
        throw std::invalid_argument("Camera picker requires its native UI reset owner");
    }
    cancel_world_staff_interaction(c);
    auto& state = c.staff.interaction;
    RideTrackPickInteraction picker;
    picker.reset_ui = std::move(reset_ui);
    state.handler = { std::move(picker), false };
    c.construction.handler = &state.handler;
    state.cursor_presentation = 19; //Native4D6140; mode code belongs to toolbar.
}

void cancel_world_camera_picker(WorldRuntime& c) {
    if (world_camera_picker_state(c)) {
        install_base(c, false); //Native501490 resets to base1, even online.
    }
}

const RideTrackPickInteraction* world_camera_picker_state(const WorldRuntime& c) {
    return std::get_if<RideTrackPickInteraction>(&c.staff.interaction.handler.value);
}

void update_world_camera_picker(WorldRuntime& c, StaffInteractionCursor cursor) {
    c.staff.interaction.cursor = cursor;
    auto* state = std::get_if<RideTrackPickInteraction>(&c.staff.interaction.handler.value);
    if (!state) {
        return;
    }
    state->hovered_object = 0;
    state->hover_eligible = false;
    state->display_name.clear();
    const auto* source = cell(c, cursor.cell);
    if (!source) {
        set_help(c, *state, 0);
        return;
    }
    const auto id = object_at(c, *source);
    if (id) {
        if (const auto* object = ride(c, id)) {
            state->hovered_object = id;
            state->display_name = park_object_ui_display_name(*object);
            state->hover_eligible = true;
            c.staff.interaction.cursor_presentation = 19;
            set_help(c, *state, 163);
            return;
        }
    } else if (terrain_hover(c, *source)) {
        state->hover_eligible = true;
        c.staff.interaction.cursor_presentation = 19;
        set_help(c, *state, 162);
        return;
    }
    c.staff.interaction.cursor_presentation = 8;
    set_help(c, *state, 313);
}

void world_camera_picker_button(WorldRuntime& c, WorldPointerButton button, bool down) {
    auto* state = std::get_if<RideTrackPickInteraction>(&c.staff.interaction.handler.value);
    if (!state || !down) {
        return; //Native left/right state-not1 callbacks are empty leaves.
    }
    if (button == WorldPointerButton::right) {
        const auto reset = state->reset_ui;
        //The UI callback can replace/destroy this variant; retain its callable
        //independently and never read the old presentation state afterward.
        reset();
        return;
    }
    c.staff.interaction.cursor_presentation = 1;
    const auto* source = cell(c, c.staff.interaction.cursor.cell);
    if (!source) {
        return;
    }
    const auto id = object_at(c, *source);
    if (id) {
        if (auto* object = ride(c, id)) {
            c.ports.meshes.prepare_park_object_animation(c, object, 0, 0);
            world_operations::queue_scored_advisor_action(c, 452);
            install_base(c, c.mode.game == GameMode::online);
        }
        return;
    }
    //Click's mode3/flag64 outer gate is separate from503FD0's hover gate;
    //notably a type30/no-obstacle hover can still be rejected here.
    if ((source->base.common.flags & 64u) && c.mode.game == GameMode::code_3) {
        return;
    }
    if (!terrain_hover(c, *source)) {
        return;
    }
    auto* track = resolve_track_cell_for_grid_entry(c, &source->base);
    if (world_cell_is_track_child(&track->base)) {
        const auto parent = track->base.common.parent_id;
        if (!parent || parent > map_cell_count) {
            throw std::logic_error("Camera picker track child has no live parent");
        }
        track = &c.simulation.current->track_cells.at(parent - 1u);
    }
    if (track->base.common.type == 25) {
        return;
    }
    //This service retains4675A0's additional0/1/3/9/30 surface gate and its
    //real camera, UI, mesh-resource and F/T sprite reload ownership.
    c.ports.camera.toggle_world_view_point_of_interest(c);
    install_base(c, false);
}

void retire_world_camera_picker(WorldRuntime& c, ThingId id) {
    auto* state = std::get_if<RideTrackPickInteraction>(&c.staff.interaction.handler.value);
    if (!state || state->hovered_object != id) {
        return;
    }
    state->hovered_object = 0;
    state->hover_eligible = false;
    state->display_name.clear();
    state->help_row = 0;
    state->help_text.clear();
}
} //namespace simcoaster
