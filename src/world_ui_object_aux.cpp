#include "simcoaster/entity_identity.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/sam_integer.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "world_ui_objects_internal.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char context_template[] = {
#embed "ui_templates/staff_context.bin"
};
constexpr unsigned char object_name_template[] = {
#embed "ui_templates/object_name.bin"
};

bool escape(const UiEvent& e) {
    return e.type == 65547 && (e.id > 255 ? e.id >> 8 : e.id) == 27 && (static_cast<std::uint32_t>(e.value) & 0xFF0000u) == 0;
}
} //namespace

ParkObjectRuntimeView* WorldParkObjectUiController::State::auxiliary_object() const {
    return EntityIdentity { auxiliary_target, auxiliary_identity, auxiliary_generation }.get<ParkObjectRuntimeView>(c.entities);
}

void WorldParkObjectUiController::State::restore_input() {
    if (closing) {
        return;
    }
    //Destruction can nest while a later auxiliary has already taken over.
    //Never replace its exclusive route/focus with the retained main panel.
    if (ui.owns(message_wrapper)) {
        ui.set_input_root(message_wrapper);
        if (ui.owns(message_root)) {
            ui.set_focus(message_root);
        }
        return;
    }
    if (ui.owns(context)) {
        ui.set_input_root(context);
        ui.set_focus(context);
        return;
    }
    if (ui.owns(name_editor)) {
        ui.set_input_root(name_editor);
        ui.set_focus(&required(name_editor, 7000002));
        return;
    }
    ui.set_input_root(nullptr);
    if (host.synchronize_focus) {
        host.synchronize_focus();
    } else {
        ui.set_focus(c.ui.active_root && ui.owns(c.ui.active_root) ? c.ui.active_root : primary);
    }
}

void WorldParkObjectUiController::State::open_context(ThingId id, UiPoint point, bool filtered, std::int32_t family, std::uint32_t mesh_slot, CellId cell) {
    if (point.x <= 0 || point.x >= 2048 || point.y <= 0 || point.y >= 1536) {
        return;
    }
    close_auxiliary();
    auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (cell ? (!c.simulation.current || cell > map_cell_count || !c.simulation.current->map_cells.at(cell - 1u).litter_selector) : (!record || record->retirement_pending || record->kind != ThingKind::park_object)) {
        return;
    }
    if (filtered && page == Page::settings && model.state().settings.panel == ParkObjectUiPanel::mode3) {
        //52CCA0/52CDA0 always request family4/18 from this panel. Both native
        //family predicates reject non-rides; direct World context selection
        //keeps its separate category/flag routing even in mode3.
        const auto* definition = get_park_object_definition(c, reinterpret_cast<const ParkObjectRuntimeView*>(record));
        if (!definition || definition->which_ui_type != ObjectUiCategory::rides) {
            return;
        }
    }
    mark_advisor_context_menu_used(c);
    context_cell = cell;
    context_family = family;
    context_mesh = mesh_slot;
    const auto* mesh = c.resources.meshes ? c.resources.meshes->instance(mesh_slot) : nullptr;
    context_mesh_generation = mesh ? mesh->generation : 0;
    auxiliary_target = id;
    auxiliary_identity = record;
    auxiliary_generation = record ? reinterpret_cast<ParkObjectRuntimeView*>(record)->creation_file_time : 0;
    context_filtered = filtered;
    //554DC5 changes only the enabled state of the EXISTING active root.
    //Keep its ownership with Objectives/Staff/another main controller.
    context_previous_root = ui.owns(c.ui.active_root) ? c.ui.active_root : nullptr;
    context_previous_serial = ui.lifetime_serial(context_previous_root);
    context_previous_enabled = context_previous_root && context_previous_root->enabled;
    if (context_previous_root) {
        ui.set_enabled(*context_previous_root, false);
    }
    if (host.prepare_context_menu) {
        host.prepare_context_menu();
    }
    context_menu.load(c, game_root);
    context = &parse(context_template, 1000, panel_callback<&State::context_event>(), false);
    context_menu.start(*this, point);
    refresh_context();
    ui.set_input_root(context);
    ui.set_focus(context);
}

void WorldParkObjectUiController::State::refresh_context() {
    if (!context || !ui.owns(context)) {
        return;
    }
    auto* object = auxiliary_object();
    auto* cell = context_cell && c.simulation.current && context_cell <= map_cell_count ? &c.simulation.current->map_cells.at(context_cell - 1u) : nullptr;
    if (!object && !cell) {
        ui.destroy(context);
        return;
    }
    ParkObjectContextUiState state;
    if (context_family == 6 || context_family == 16 || cell) {
        state.menu_family = context_family;
        state.name = cell ? text(!cell->litter_selector ? 511u : cell->litter_selector == 7 ? 519u : cell->litter_selector == 8 ? 520u : 521u) : park_object_ui_display_name(*object);
    } else {
        state = model.context(auxiliary_target);
        if (context_family) {
            state.menu_family = context_family;
        }
    }

    std::vector<UiContextOption> options;
    const auto title = state.name;
    const auto append = [&](std::uint32_t row, bool visible = true, bool enabled = true, std::uint32_t key = 0, std::int32_t value = 0) {
        options.push_back({ row, visible, enabled, key, value });
    };
    //Native option order comes from the32-byte records in context table9BDC60.
    const auto add_open = [&](std::uint32_t a, std::uint32_t b) {
        append(a, state.can_open, state.can_open);
        append(b, state.can_close, state.can_close);
    };
    const auto remove = [&] {
        const auto* definition = get_park_object_definition(c, object);
        if (!definition) {
            throw std::runtime_error("Park-object menu definition is absent");
        }
        const auto scrap = definition->research.upgrades[0].cost_of_upgrade * static_cast<std::uint32_t>(get_park_object_scrap_value_percent(c, object)) / 100u;
        append(516, true, state.can_delete, 27, fixed::word(scrap));
    };
    const auto copy = [&] {
        const auto* control = world_operations::find_object_control(c, object->definition_id);
        if (!control) {
            throw std::runtime_error("Park-object menu purchase control is absent");
        }
        append(514, true, state.can_copy, 25, fixed::word(control->purchase_cost));
    };
    const auto service = [&](std::uint32_t a, std::uint32_t b) {
        append(a, true, state.can_request_service);
        append(b, true, state.can_cancel_service);
    };
    match (state.menu_family) {
        case 33 => {
            append(517, true, c.simulation.current->saved.first_handyman && !cell->litter_collector);
            append(518, true, cell->litter_collector != 0);
        }
        case 16 => {
            auto& manager = get_or_create_theme_global_info_manager(c);
            const auto* theme = manager.selected_theme_info ? get_or_create_theme_global_info_for_ordinal(c, manager, manager.selected_theme_info->source_ordinal) : nullptr;
            append(591, true, theme && manager.mission_research.ending_theme == static_cast<std::int32_t>(theme->source_ordinal));
            append(592, true, can_proceed_with_current_objective(c) && object->definition_id == static_cast<std::uint16_t>(manager.mission_research.obstacle_type));
            //Four native developer rows have constant-false visibility.
        }
        case 6 => {
            const auto* world = c.resources.meshes ? c.resources.meshes->coasters() : nullptr;
            const auto* ride = world ? world->find(object->mesh_instance_id) : nullptr;
            if (!ride) {
                ui.destroy(context);
                return;
            }
            const auto flags = ride->train_state.placement_flags;
            const bool placement = (flags & 1u) && !(flags & 2u);
            const auto* mesh = c.resources.meshes->instance(context_mesh);
            const bool saved = mesh && mesh->generation == context_mesh_generation;
            std::optional<std::uint32_t> owner_flags;
            if (saved) {
                for (const auto& node : ride->geometry_state) {
                    if (node.mesh_slot == context_mesh) {
                        owner_flags = world->placements.at(node.placement).flags;
                        break;
                    }
                }
            }
            const bool movable = owner_flags && !(*owner_flags & 16u);
            const bool section = placement && (!saved || (owner_flags && (*owner_flags & 16u)));
            append(677, !placement);
            append(678, placement && movable);
            append(679, section);
            for (auto row : { 684u, 685u, 681u, 686u, 682u, 683u }) {
                append(row, true, movable);
            }
            append(680, !placement);
            append(687, !placement);
        }
        case 14 or 15 => {
            //The two native entrance/ticket-booth families share the same single
            //unconditional, enabled option (9BD260):597 ->53A990 Park Gates.
            append(597);
        }
        case 4 or 18 => {
            const bool ordinary = state.menu_family == 4;
            const bool visible = ordinary ? c.mode.game != GameMode::code_3 : state.unregistered_special;
            append(524, visible, state.can_request_service);
            append(525, visible, state.can_cancel_service);
            append(526, visible, object->state != ParkObjectState::condemned);
            append(527, state.can_edit_track, state.can_edit_track);
            append(529, visible, state.can_upgrade);
            add_open(530, 531);
            if (ordinary && !park_object_definition_is_marked_special(c, object)) {
                remove();
            }
            append(532, ordinary || visible, state.can_ride);
            append(533, visible);
            append(534);
            if (ordinary && visible) {
                copy();
            }
            append(535, visible);
            //522/523 are always-false developer COS actions.
        }
        case 19 => {
            const bool can_open = state.closed && !c.referenced_world()->saved.park_closed;
            append(547, can_open, can_open);
            append(548, state.can_close, state.can_close);
            remove();
            append(549);
            append(550);
            copy();
            append(551);
        }
        case 20 => {
            add_open(552, 553);
            remove();
            append(554);
            append(555);
            copy();
            append(556);
        }
        case 21 => {
            service(517, 518);
            add_open(559, 560);
            remove();
            append(561);
            append(562);
            copy();
        }
        case 22 => {
            service(517, 518);
            remove();
            append(565);
            copy();
            //566 is always-false debug
        }
        case 23 => {
            append(567);
            add_open(568, 569);
            remove();
            append(570);
            copy();
        }
        case 24 => {
            append(571, state.closed && !c.referenced_world()->saved.park_closed, state.can_open);
            append(572, state.can_close, state.can_close);
            remove();
            append(573);
        }
        case 25 => {
            add_open(575, 576);
            remove();
            append(577);
            copy();
        }
        case 26 or 27 => remove();
        case 28       => {
            remove();
            append(581);
            copy();
        }
        case 29 => {
            remove();
            append(582);
            copy();
            append(583, state.can_request_gardener, state.can_request_gardener);
            append(585, state.can_request_janitor, state.can_request_janitor);
            append(584, state.can_cancel_service, state.can_cancel_service);
        }
        case 30 => {
            remove();
            append(589);
            copy();
            append(590, true, state.can_ride);
        }
        case _ => throw std::runtime_error("Unknown native park-object context family");
    } context_menu.present(*this, fonts(), title, options);
}

void WorldParkObjectUiController::State::execute_context(std::uint32_t row) {
    if (context_cell) {
        const auto id = context_cell;
        ui.destroy(context);
        if (row == 517) {
            call_world_cell_handyman(c, id);
        } else if (row == 518) {
            cancel_world_cell_litter_assignment(c, id);
        }
        return;
    }
    auto* object = auxiliary_object();
    if (!object) {
        if (context) {
            ui.destroy(context);
        }
        return;
    }
    const auto id = auxiliary_target;
    const auto* identity = auxiliary_identity;
    const auto generation = auxiliary_generation;
    const bool filtered = context_filtered;
    const auto saved_mesh = context_mesh;
    const auto saved_mesh_generation = context_mesh_generation;
    ui.destroy(context); //Native action callbacks run after popup destruction.
    const auto live = [this, id, identity, generation] {
        return EntityIdentity { id, identity, generation }.get<ParkObjectRuntimeView>(c.entities) != nullptr;
    };
    if (!live()) {
        return;
    }
    using Tool = CoasterEditorEntryTool;
    const auto tool = match (row) -> std::optional<Tool> {
        case 677 => Tool::build_supports;
        case 684 => Tool::adjust_support_height;
        case 685 => Tool::rotate_supports;
        case 681 => Tool::pivot_track;
        case 686 => Tool::move_supports;
        case 682 => Tool::stack_track_up;
        case 683 => Tool::stack_track_down;
        case 680 => Tool::build_complex_loop;
        case 687 => Tool::build_loop_the_loop;
        case _   => std::nullopt;
    };
    if (tool) {
        if (host.open_coaster_tool) {
            host.open_coaster_tool(id, *tool);
        }
        return;
    }
    match (row) {
        case 678 => {
            const auto* mesh = c.resources.meshes ? c.resources.meshes->instance(saved_mesh) : nullptr;
            if (mesh && mesh->generation == saved_mesh_generation && host.delete_coaster_geometry) {
                host.delete_coaster_geometry(id, saved_mesh);
            }
        }
        case 679 => {
            if (host.edit_object_track) {
                (void)host.edit_object_track(id);
            }
        }
        case 591 => {
            if (host.open_objectives) {
                host.open_objectives();
            }
        }
        case 592 => {
            WorldEvent event { ObstacleRemovedEvent { c.simulation.current->saved.current_obstacle } };
            dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
        }
        case 597 => {
            if (host.request) {
                host.request(WorldCommandAction { WorldUiCommand::park_gates, 0 });
            }
        }
        case 524 or 517 or 583 or 585 => {
            model.request_service(id, true);
            refresh_page();
        }
        case 525 or 518 or 584 => {
            model.request_service(id, false);
            refresh_page();
        }
        case 530 or 547 or 552 or 559 or 568 or 571 or 575 => {
            model.set_closed(id, false);
            refresh_page();
        }
        case 531 or 548 or 553 or 560 or 569 or 572 or 576 => {
            model.set_closed(id, true);
            refresh_page();
        }
        case 533 or 549 or 554 or 561 or 573                                    => open_statistics(id);
        case 535 or 551 or 556                                                  => open_opinions(id);
        case 534 or 550 or 555 or 562 or 565 or 570 or 577 or 581 or 582 or 589 => open_name(id, false);
        case 529                                                                => open_upgrade(id);
        case 567                                                                => open_promotion(id, false);
        case 526                                                                => {
            if (filtered) {
                close();
            }
            if (host.edit_object_queue) {
                (void)host.edit_object_queue(id);
            }
        }
        case 527 => {
            if (filtered) {
                close();
            }
            if (host.edit_object_track) {
                (void)host.edit_object_track(id);
            }
        }
        case 532 or 590 => {
            close();
            if (host.ride_object) {
                host.ride_object(id);
            }
        }
        case 514 or 516 => {
            const bool remove = row == 516;
            const auto execute = [this, id, identity, generation, filtered, remove, live] {
                if (!live()) {
                    return;
                }
                if (filtered) {
                    close();
                }
                if (host.request) {
                    host.request(world_object_trade_action(remove, { id, identity, generation }));
                }
            };
            if (row == 516 && confirm_deletions) {
                message(913, execute);
                confirmation_target = id;
            } else {
                execute();
            }
        }
        case _ => throw std::runtime_error("Unhandled native park-object menu action");
    }
}

std::intptr_t WorldParkObjectUiController::State::context_event(UiControl& control, const UiEvent& event) {
    if (event.type == 20 && &control == context) {
        context = nullptr;
        context_cell = 0;
        context_family = 0;
        context_mesh = 0;
        context_mesh_generation = 0;
        const auto* retained = std::exchange(context_previous_root, nullptr);
        const auto serial = std::exchange(context_previous_serial, 0);
        if (retained && serial && c.ui.active_root == retained && ui.lifetime_serial(retained) == serial) {
            ui.set_enabled(*c.ui.active_root, context_previous_enabled);
        }
        auxiliary_target = 0;
        auxiliary_identity = nullptr;
        restore_input();
    } else if (context && !closing) {
        if (event.type == 30) {
            refresh_context();
        } else if (event.type == 4 || event.type == 5 || escape(event)) {
            ui.destroy(context);
            return 1;
        } else if (event.type == 256 && event.id >= 1001 && event.id <= 1015) {
            if (context_menu.layout_ready && required(context, event.id).enabled && context_menu.rows[event.id - 1001]) {
                execute_context(context_menu.rows[event.id - 1001]);
            }
            return 1;
        }
    }
    return ui.secondary(control, event);
}

void WorldParkObjectUiController::State::open_name(ThingId id, bool attached) {
    close_auxiliary();
    auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object) {
        return;
    }
    auxiliary_target = id;
    auxiliary_identity = record;
    auxiliary_generation = reinterpret_cast<ParkObjectRuntimeView*>(record)->creation_file_time;
    name_editor = &parse(object_name_template, 7000000, panel_callback<&State::name_event>(), false);
    if (attached) {
        if (!root || !ui.owns(root)) {
            throw std::logic_error("Attached object name has no owning panel");
        }
        ui.reparent(*name_editor, root);
        ui.set_depth(*name_editor, root->render_depth + 17);
        const auto bounds = name_editor->rect;
        ui.translate(*name_editor, root->rect.left + root->rect.width() / 2 - bounds.width() / 2 - bounds.left, root->rect.top + root->rect.height() / 2 - bounds.height() / 2 - bounds.top);
    } else {
        ui.set_depth(*name_editor, 32);
    }
    auto& title = required(name_editor, 7000001);
    caption(title, text(716), 5);
    title.secondary_shadow_text = true;
    const auto names = park_object_ui_name_components(*auxiliary_object());
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto& edit = required(name_editor, 7000002 + static_cast<std::uint32_t>(i));
        edit.text_limit = 14;
        caption(edit, names[i], 7);
        edit.edit_caret_asset_hash = ui_model_asset_hash("hilight");
    }
    auto& edit = required(name_editor, 7000002);
    edit.selection_begin = 0;
    edit.selection_end = edit.text.size();
    //Standalone557380 retains the main panel's enabled/registered state;
    //only the name editor's command and keyboard-focus lifetime changes.
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    ui.set_input_root(name_editor);
    ui.set_focus(&edit);
}

std::intptr_t WorldParkObjectUiController::State::name_event(UiControl& control, const UiEvent& event) {
    if (event.type == 20 && &control == name_editor) {
        name_editor = nullptr;
        auxiliary_target = 0;
        auxiliary_identity = nullptr;
        c.ports.ui.restore_command_families_state(c, false);
        restore_input();
    } else if (name_editor && !closing) {
        if (event.type == 2051 || (event.type == 2050 && event.id == 7000002)) {
            ui.set_focus(&required(name_editor, event.id == 7000002 ? 7000003 : 7000002));
            return 1;
        }
        if (event.type == 2050) {
            ui.post(name_editor, 256, 4294967295u);
            return ui.secondary(control, event);
        }
        const bool accept = event.type == 256 && event.id == 4294967295u;
        const bool cancel = event.type == 2052 || event.type == 5 || escape(event) || (event.type == 256 && event.id == 4294967294u);
        if (accept || cancel) {
            if (accept) {
                if (auto* object = auxiliary_object()) {
                    const auto result = commit_park_object_ui_name(c, *object, required(name_editor, 7000002).text, required(name_editor, 7000003).text);
                    if (!result.accepted) {
                        if (result.warning_row) {
                            const std::array args { BfstWideStringArgument { 29, result.warning_name } };
                            message_text(text(*result.warning_row, {}, args), {}, UiMessageButtonKind::okay);
                        }
                        return 1; //Original names remain mutated after a duplicate
                        //warning.
                    }
                }
            }
            ui.destroy(name_editor);
            if (!closing) {
                refresh_page();
            }
            return 1;
        }
        if (event.type == 30 && !auxiliary_object()) {
            ui.destroy(name_editor);
            return 1;
        }
    }
    return ui.secondary(control, event);
}

void WorldParkObjectUiController::State::message(std::uint32_t row, std::function<void()> accepted) {
    message_text(text(row), std::move(accepted));
}

void WorldParkObjectUiController::State::message_text(std::u16string body, std::function<void()> accepted, UiMessageButtonKind first) {
    if (message_wrapper && ui.owns(message_wrapper)) {
        ui.destroy(message_wrapper);
    }
    message_previous_focus = ui.focus();
    message_scope.acquire(!c.simulation.current || c.simulation.current->saved.state != WorldState::transition);
    message_accepted = std::move(accepted);
    const bool question = static_cast<bool>(message_accepted);
    //4F4470: only generic messages own a full-canvas wrapper and lolight
    //backdrop. Staff context/name/wage panels parse directly under primary.
    message_wrapper = &create_ui_message_wrapper(ui);
    auto callback = [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
        if (event.type == 20 && &sender == message_root) {
            message_root = nullptr;
            message_wrapper = nullptr;
            message_accepted = {};
            confirmation_target = 0;
            message_scope.release();
            if (!closing) {
                ui.set_input_root(context ? context : name_editor ? name_editor : nullptr);
                ui.set_focus(ui.owns(message_previous_focus) ? message_previous_focus : primary);
            }
            return ui.secondary(sender, event);
        }
        const bool choice = event.type == 256 && (event.id == 60001 || event.id == 60002 || event.id == 60003);
        if (choice || event.type == 4 || event.type == 5) {
            auto action = choice && event.id == 60002 ? std::move(message_accepted) : std::function<void()> {};
            auto* wrapper = message_wrapper;
            if (ui.owns(wrapper)) {
                ui.destroy(wrapper);
            }
            if (action) {
                action();
            }
            return 0;
        }
        return ui.secondary(sender, event);
    };
    const auto parsed = ui.parse_template(message_wrapper, std::as_bytes(ui_message_template()));
    if (!parsed) {
        throw std::runtime_error(std::format("Original message template: {}", parsed.error().reason));
    }
    message_root = ui.find(message_wrapper, 60000);
    if (!message_root) {
        throw std::runtime_error("Original message template has no root60000");
    }
    bind(*message_root, callback);
    ui.set_primary_callback(*message_wrapper, [this](auto& sender, const auto& event) {
        if (event.type == 4) {
            ui.destroy(&sender);
            return std::intptr_t {};
        }
        return ui.secondary(sender, event);
    });
    message_controls = bind_ui_message_controls(bindings, message_root, "Original park-object template missing control ");
    present_message(message_wrapper, message_root, message_controls, std::nullopt, body, question, first);
}
} //namespace simcoaster
