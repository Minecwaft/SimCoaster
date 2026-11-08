#include "simcoaster/world_ui_shortcut_actions.hpp"
#include "simcoaster/advisor_balance.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/park_authoring.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
enum class InteractionOwner {
    base,
    construction,
    other
};

InteractionOwner interaction_owner(ParkRuntime& park) {
    auto& c = park.context();
    initialize_world_staff_interaction(c);
    if (!std::holds_alternative<DefaultWorldInteraction>(c.staff.interaction.handler.value)) {
        return InteractionOwner::other;
    }
    if (park.construction().state.shortcut_rearm_mode) {
        return InteractionOwner::construction;
    }
    const auto& coaster = park.coaster_editor();
    if (coaster.active_map()) {
        match (coaster.input_route()) {
            case CoasterEditorInputRoute::map_action    => return InteractionOwner::construction;
            case CoasterEditorInputRoute::geometry      => return InteractionOwner::other;
            case CoasterEditorInputRoute::world_default => {
                //Insert Support retains numeric21 after restoring native base1.
                //The numeric mode alone must not manufacture a kind3 path owner.
                return InteractionOwner::base;
            }
            case _ => {
            }
        }
    }
    park.authoring().synchronize_from_world();
    if (park.authoring().tool() != ParkAuthoringTool::inspect) {
        return InteractionOwner::construction;
    }
    return world_staff_interaction_kind(c) == WorldInteractionHandlerKind::default_ ? InteractionOwner::base : InteractionOwner::other;
}

bool prebuilt_rotation_locked(WorldRuntime& c) {
    const auto* definition = c.construction.selected_object_definition_id ? find_object_definition_by_id(&c.resources.object_catalog, c.construction.selected_object_definition_id) : nullptr;
    //673089 tests definition+316. Original descriptor86, together with
    //418F60's scalar pointer map and43C2D0's base+12, identifies this as
    //Prebuilt.IsPrebuiltCoaster; it is not a generic nonrotatable flag.
    return definition && definition->balance && definition->balance->Prebuilt.IsPrebuiltCoaster == 1;
}

void set_rotation(ParkRuntime& park, std::int32_t angle, bool force) {
    auto& c = park.context();
    if (prebuilt_rotation_locked(c)) {
        return;
    }
    auto& state = park.construction().state;
    if (force || angle != state.previous_rotation_degrees) {
        const auto recorded = park.action_recording().record(c, { LayoutActionCode::set_rotation, LayoutWord { static_cast<std::uint16_t>(angle) } });
        if (!recorded) {
            throw std::runtime_error(describe_layout_error(recorded.error()));
        }
    }
    state.previous_rotation_degrees = state.rotation_degrees;
    state.rotation_degrees = angle;
}

void rotate(ParkRuntime& park, std::uint32_t increment) {
    auto& c = park.context();
    const auto old = park.construction().state.rotation_degrees;
    const auto sum = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(old) + increment);
    set_rotation(park, sum % 360, true);
    //42F6B0 writes manager+35; the typed native eligibility UDT establishes
    //the third byte, independently of the rotation setter's prebuilt guard.
    get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[2] = 0;
    park.authoring().synchronize_from_world();
}

void speed(ParkRuntime& park, std::int32_t difference, bool reset) {
    const auto& options = park.preferences();
    const auto wrapped = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::int32_t>(options.game_speed_percent) + difference));
    const auto value = reset ? std::int16_t { 100 } : std::clamp<std::int16_t>(wrapped, 10, 400);
    park.set_game_speed_percent(value); //Native clock multiplier is0.0125.
}

void activate_advisor(ParkRuntime& park) {
    auto& c = park.context();
    if (!c.simulation.current) {
        throw std::logic_error("Advisor shortcut requires the current World");
    }
    const auto id = c.simulation.current->saved.advisor;
    auto* header = id ? c.entities.at(id).payload : nullptr;
    if (!header || header->kind != ThingKind::advisor) {
        throw std::logic_error("Advisor shortcut has no live Advisor record");
    }
    auto& advisor = *reinterpret_cast<AdvisorRecord*>(header);
    auto& candidate = advisor.special_candidate;
    if (!candidate.valid) {
        return;
    }
    //718AC9 owns the same lazy policy bit consumed by the existing queue.
    if (!(c.advisor.advisor_candidate_initialized & 1u)) {
        c.advisor.advisor_candidate_initialized |= 1u;
        world_operations::load_advisor_behavior_configuration(c);
    }
    if (!c.advisor.balance) {
        throw std::logic_error("Advisor shortcut has no loaded behavior policy");
    }
    candidate.score = c.advisor.balance->HelpMessage.ContextHelpScore;
    candidate.score_overridden = candidate.override_only_once = 1;
    (void)queue_advisor_action_candidate(c, advisor, candidate);
}

bool backtrack(ParkRuntime& park) {
    auto& c = park.context();
    auto& coaster = park.coaster_editor();
    //This existing native adapter owns the edit-event-pending guard and
    //complete support backtracking. Geometry kind8 consumes without edits.
    if (!park.construction().state.shortcut_rearm_mode && coaster.coaster_key(c, 8, 65547)) {
        return true;
    }
    const auto owner = interaction_owner(park);
    auto& construction = park.construction();
    if (owner == InteractionOwner::base && !c.construction.mode) {
        const auto cursor = c.staff.interaction.cursor.cell;
        if (c.simulation.current && cursor && cursor <= map_cell_count && c.simulation.current->map_cells.at(cursor - 1u).base.common.type == 1) {
            const auto zero = static_cast<std::uint32_t>(cursor) - 1u;
            const WorldCellCoordinates tile { static_cast<std::int32_t>(zero & 127u), static_cast<std::int32_t>(zero >> 7u) };
            (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 95, tile.x * 10, 0, tile.y * 10);
            construction.set_mode(c, 50, true);
            (void)construction.dispatch(c, tile, WorldInputModifierMask::none, false);
            construction.set_mode(c, 0, true);
            set_rotation(park, 0, false);
        }
    } else if (owner == InteractionOwner::construction && c.construction.mode != 139) {
        (void)construction.backtrack_current_path(c);
    }
    park.authoring().synchronize_from_world();
    return true; //42F130 always reports the key handled.
}

bool cancel(ParkRuntime& park) {
    if (interaction_owner(park) == InteractionOwner::base) {
        return false; //42F7C1/42F851 decline native kinds0 and1.
    }
    park.authoring().cancel_interaction(park.context().construction.modifiers);
    return true;
}
} //namespace

void ParkAuthoringController::cancel_interaction(WorldInputModifierMask modifiers) {
    auto& c = park_.context();
    auto& construction = park_.construction();
    const auto rotation = construction.state.rotation_degrees;
    const auto previous_rotation = construction.state.previous_rotation_degrees;
    //Replacing native kind3 first runs4D6330. Reuse the actual authoring
    //teardown so relocation queue completion, track repair, reopen policy and
    //any detached coaster placement finish before mode0 replaces the owner.
    cancel(modifiers);
    cancel_world_staff_interaction(c);
    c.staff.interaction.handler = { DefaultWorldInteraction {}, false };
    c.construction.handler = &c.staff.interaction.handler;
    //The authoring owner clears its transient state. Native current/previous
    //rotation are process globals, so retain their distinct lifetimes until
    //mode0's673383..6733F7 reset has finished.
    construction.state.rotation_degrees = rotation;
    construction.state.previous_rotation_degrees = previous_rotation;
    set_rotation(park_, 0, false);
    const auto pointer_mode = c.staff.interaction.pointer_mode_code;
    if (pointer_mode == 2 || pointer_mode == 3) {
        construction.state.shortcut_rearm_mode = pointer_mode == 2 ? 1 : 58;
    }
    //Native673230 installs the selected handler immediately. If its cached
    //cell is absent, retain activation until the next admitted World sample.
    construction.resume_shortcut_tool(c);
    synchronize_from_world();
}

bool apply_world_ui_shortcut_action(ParkRuntime& park, WorldUiShortcutAction action, std::int32_t argument) {
    (void)argument;
    using A = WorldUiShortcutAction;
    match (action) {
        case A::rotate_clockwise => {
            rotate(park, 90);
            return true;
        }
        case A::rotate_counterclockwise => {
            rotate(park, 270);
            return true;
        }
        case A::cancel_interaction => return cancel(park);
        case A::backtrack          => return backtrack(park);
        case A::enter_delete_mode  => {
            //5018F6 selects original control66, whose requested map action is58.
            //This is the rectangle Delete handler, not path erase or tool68.
            const auto selected = park.ui().select_toolbar_tool(66);
            park.authoring().synchronize_from_world();
            return selected;
        }
        case A::decrease_game_speed => {
            speed(park, -15, false);
            return true;
        }
        case A::increase_game_speed => {
            speed(park, 15, false);
            return true;
        }
        case A::reset_game_speed => {
            speed(park, 0, true);
            return true;
        }
        case A::close_park => {
            auto& c = park.context();
            if (!c.simulation.current) {
                throw std::logic_error("Close Park shortcut requires a World");
            }
            handle_world_park_open_close(c, *c.simulation.current, false, false);
            return true;
        }
        case A::activate_advisor_special_candidate => {
            activate_advisor(park);
            return true; //4305E0 ignores718AB0's invalid-candidate return value.
        }
        case _ => return false;
    }
}
} //namespace simcoaster
