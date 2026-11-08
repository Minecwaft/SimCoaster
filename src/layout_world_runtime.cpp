#include "simcoaster/world_object_placement.hpp"
#include "simcoaster/layout_world_runtime.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_staff_interaction.hpp"

namespace simcoaster {
namespace {
[[noreturn]] void dependency(LayoutActionCode code, std::string_view detail) {
    throw LayoutError { LayoutFailure::runtime_dependency, 0, code, std::string { detail } };
}

void select_object(WorldRuntime& c, ParkConstructionRuntime& construction, std::uint16_t cell) {
    using namespace construction_detail;
    if (!cell || cell > map_cell_count) {
        throw LayoutError { LayoutFailure::invalid_contract, 0, LayoutActionCode::set_selected_object, "selected object key is not a map cell" };
    }
    if (const auto id = find_first_park_object_id_in_map_cell(c, &map(c, cell)); id) {
        auto* selected = object(c, id);
        if (!selected) {
            throw std::logic_error("layout selection did not resolve a Type3 owner");
        }
        c.view.secondary_cursor_record_snapshot = &selected->base.header;
        construction.refresh_origin(c, false);
    }
}
} //namespace

void LayoutWorldBackend::clear_interaction_status(WorldRuntime&) {
    construction_.state.status_latch = false;
}

void LayoutWorldBackend::dispatch(WorldRuntime& c, const LayoutAction& action) {
    using enum LayoutActionCode;
    const auto code = action.code;
    const auto coaster = [&] {
        if (!coaster_) {
            dependency(code, "native coaster input/editor owner is not bound " "(48CEA0/48C640/660560); geometry leaf APIs are insufficient");
        }
        coaster_->dispatch(c, construction_, action);
    };
    match (code) {
        case set_mode => {
            const auto value = std::get<LayoutWord>(action.payload).value;
            if (value == 21 || value == 22) {
                coaster();
            } else {
                construction_.set_mode(c, value, false);
                if (value == 20 || value == 14 || value == 19) {
                    construction_.refresh_origin(c, false);
                    construction_.initialize_context_mode(c);
                }
            }
            return;
        }
        case set_mode_nr => {
            const auto value = std::get<LayoutWord>(action.payload).value;
            if (value == 21 || value == 22) {
                coaster();
            } else {
                construction_.set_mode_preserving_path(c, value, false);
            }
            return;
        }
        case set_ride => {
            const auto value = std::get<LayoutRide>(action.payload);
            world_operations::apply_set_ride_action(c, &value.definition, value.secondary);
            return;
        }
        case set_rotation => {
            construction_.state.rotation_degrees = std::get<LayoutWord>(action.payload).value;
            return;
        }
        case set_track => {
            c.construction.track_type = std::get<LayoutWord>(action.payload).value;
            return;
        }
        case set_queue => {
            construction_.state.queue_placement_enabled = std::get<LayoutWord>(action.payload).value != 0;
            return;
        }
        case set_selected_object => {
            select_object(c, construction_, std::get<LayoutWord>(action.payload).value);
            return;
        }
        case mouse_down or modified_mouse_down => {
            WorldCellCoordinates position;
            std::uint16_t modifiers {};
            if (code == mouse_down) {
                const auto value = std::get<LayoutCoordinates>(action.payload);
                position = { value.x, value.y };
            } else {
                const auto value = std::get<LayoutModifiedCoordinates>(action.payload);
                position = { value.x, value.y };
                modifiers = value.modifiers;
            }
            construction_.mouse_down(c, position, static_cast<WorldInputModifierMask>(modifiers), false);
            return;
        }
        case mouse_up or modified_mouse_up => {
            WorldCellCoordinates position;
            std::uint16_t modifiers {};
            if (code == mouse_up) {
                const auto value = std::get<LayoutCoordinates>(action.payload);
                position = { value.x, value.y };
            } else {
                const auto value = std::get<LayoutModifiedCoordinates>(action.payload);
                position = { value.x, value.y };
                modifiers = value.modifiers;
            }
            (void)construction_.dispatch(c, position, static_cast<WorldInputModifierMask>(modifiers), false);
            return;
        }
        case name_a or name_b => {
            const auto& value = std::get<LayoutMappedName>(action.payload);
            apply_layout_object_name(c, code, value.key, value.text);
            return;
        }
        case coaster_normal or coaster_special or coaster_loft or coaster_heading or coaster_wobble or coaster_move or coaster_stack_up or coaster_stack_down or coaster_delete_range or coaster_generate_legacy or coaster_backtrack or coaster_edit_legacy or coaster_generate or coaster_edit or coaster_speed => {
            coaster();
            return;
        }
        case legacy_name_a or legacy_name_b => return;
        case marker                         => {
            const auto extension = decode_coaster_insert_action(action);
            if (!extension) {
                throw extension.error();
            }
            if (*extension) {
                coaster();
            }
            return;
        }
        case _ => {
            if (layout_action_has_native_effect(code)) {
                throw LayoutError { LayoutFailure::invalid_contract, 0, code, "effectful action omitted from production dispatch" };
            }
            return;
        }
    }
}

void LayoutWorldBackend::reset_train_rsse(WorldRuntime& c) {
    //Public entry added by the integration patch, using the existing actual
    //TrainAnimation lifecycle backend for the active prefix (native479F70).
    if (c.resources.meshes) {
        reset_all_coaster_train_rsse(c, *c.resources.meshes);
    }
}

void LayoutWorldBackend::cancel_replay(WorldRuntime& c) {
    if (coaster_) {
        coaster_->cancel(c, construction_);
        coaster_->dispatch(c, construction_, { LayoutActionCode::coaster_edit, LayoutWord { 0 } });
    }
    construction_.set_mode(c, 0, false);
}
} //namespace simcoaster
