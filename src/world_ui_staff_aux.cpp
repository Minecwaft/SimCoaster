#include "simcoaster/entity_identity.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/sam_integer.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_staff_internal.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char name_template[] = {
#embed "ui_templates/staff_name.bin"
};
constexpr unsigned char object_name_template[] = {
#embed "ui_templates/object_name.bin"
};
constexpr unsigned char wage_demand_template[] = {
#embed "ui_templates/staff_wage_demand.bin"
};
constexpr unsigned char wage_result_template[] = {
#embed "ui_templates/staff_wage_result.bin"
};
constexpr unsigned char budget_template[] = {
#embed "ui_templates/staff_training_budget.bin"
};
constexpr unsigned char context_template[] = {
#embed "ui_templates/staff_context.bin"
};
static_assert(sizeof(name_template) == 258
              && sizeof(object_name_template) == 308
              && sizeof(wage_demand_template) == 1062
              && sizeof(wage_result_template) == 552
              && sizeof(budget_template) == 1348
              && sizeof(context_template) == 586);

bool escape(const UiEvent& event) {
    return event.type == 65547
        && (event.id > 255 ? event.id >> 8 : event.id) == 27
        && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0;
}

template <class State>
void capture_room(State& s, ThingId id) {
    if (!id || id >= s.c.entities.size())
        throw std::out_of_range("Room dialog identity");
    auto* record = s.c.entities[id].payload;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object)
        throw std::out_of_range("Room dialog needs a live park object");
    s.auxiliary_staff = id;
    s.auxiliary_identity = record;
    s.auxiliary_hire_time = reinterpret_cast<ParkObjectRuntimeView*>(record)->creation_file_time;
}

//Complete native world_balance_descriptors525..563, closed by group564.
//A projected receiver must still own every field in its selected group:
//Standard.sam also contains tooltips, heatmaps, and two scalar scroll speeds.
template <class State>
void capture_staff(State& s, ThingId id) {
    if (!id || id >= s.c.entities.size())
        throw std::out_of_range("Auxiliary Staff dialog identity");
    auto* record = s.c.entities[id].payload;
    if (!record || record->retirement_pending || !is_staff_record_kind(record->kind))
        throw std::out_of_range("Auxiliary Staff dialog needs a live employee");
    s.auxiliary_staff = id;
    s.auxiliary_identity = record;
    s.auxiliary_hire_time = reinterpret_cast<StaffRuntimeRecord*>(record)->time_hired;
}

std::u16string display_name(const StaffRuntimeRecord& staff) {
    return { staff.name.begin(), std::ranges::find(staff.name, char16_t{}) };
}

template <class State>
std::u16string currency(State& s, std::int32_t value) {
    const auto magnitude = value < 0 ? fixed::negate(value) : value;
    const std::array args{ BfstSignedIntegerArgument{ value < 0 ? 44u : 39u, magnitude } };
    return s.text(value < 0 ? 958 : 957, args);
}

std::int32_t byte_meter(float value) {
    return (static_cast<std::uint8_t>(fixed::truncate_integer(value)) << 10) / 100;
}

template <class State>
void restore_input(State& s) {
    if (s.closing)
        return;
    if (s.root && s.ui.owns(s.root)) {
        s.ui.set_enabled(*s.root, true);
        s.ui.set_input_root(nullptr);
        s.ui.set_focus(s.root);
        if (s.host.activate)
            s.host.activate(s.root);
    } else {
        s.ui.set_input_root(nullptr);
        s.ui.set_focus(s.primary);
        if (s.host.deactivate)
            s.host.deactivate(nullptr);
    }
}
} //namespace

StaffRuntimeRecord* WorldStaffUiController::State::aux_live_staff() const {
    return EntityIdentity { auxiliary_staff, auxiliary_identity, auxiliary_hire_time }.get<StaffRuntimeRecord>(c.entities);
}

ParkObjectRuntimeView* WorldStaffUiController::State::aux_live_room() const {
    return EntityIdentity { auxiliary_staff, auxiliary_identity, auxiliary_hire_time }.get<ParkObjectRuntimeView>(c.entities);
}

void WorldStaffUiController::State::open_context(ThingId id, UiPoint pointer, bool filtered) {
    if (pointer.x <= 0 || pointer.x >= 2048 || pointer.y <= 0 || pointer.y >= 1536)
        return;
    close_auxiliary();
    context_filtered = filtered;
    auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (!record || record->retirement_pending)
        return;
    if (record->kind == ThingKind::park_object) {
        capture_room(*this, id);
        const auto capabilities = std::to_underlying(aux_live_room()->capabilities);
        if (capabilities & std::to_underlying(ParkObjectCapabilities::training_room))
            current_context_family = 26;
        else if (capabilities & std::to_underlying(ParkObjectCapabilities::chills_you_out))
            current_context_family = 25;
        else
            throw std::invalid_argument("Staff frontend context needs a Staff/Training Room");
    } else {
        capture_staff(*this, id);
        constexpr std::array<std::int32_t, 6> families{ 8, 9, 10, 11, 13, 12 };
        current_context_family = families.at(static_cast<std::size_t>(model.context(id).role));
    }
    mark_advisor_context_menu_used(c);
    context_menu.load(c, game_root);
    context = &parse(context_template, 1000, panel_callback<&State::context_event>());
    context_menu.start(*this, pointer);
    if (root && ui.owns(root))
        ui.set_enabled(*root, false);
    refresh_context();
    ui.set_input_root(context);
    if (host.activate)
        host.activate(context);
}

void WorldStaffUiController::State::refresh_context() {
    if (!context || !ui.owns(context))
        return;
    auto* staff = aux_live_staff();
    auto* room = aux_live_room();
    if (!staff && !room) {
        ui.destroy(context);
        return;
    }

    std::vector<UiContextOption> options;
    std::u16string title;
    if (staff) {
        const auto state = model.context(auxiliary_staff);
        title = state.name;
        constexpr std::array<std::uint32_t, 6> hire_rows{ 611, 621, 630, 639, 657, 648 };
        const bool handyman_strike = state.role == StaffType::handyman && state.on_strike;
        options = { { 602, true, true },
                    { 603, state.can_set_patrol, state.can_set_patrol },
                    { 604, state.can_edit_patrol, state.can_edit_patrol },
                    { 605, state.can_clear_patrol, state.can_clear_patrol },
                    { 606, !handyman_strike, state.can_train },
                    { 607, !handyman_strike, state.can_pickup },
                    { 608, true, true },
                    { 609, !context_filtered, true },
                    { 610, true, true },
                    { hire_rows.at(static_cast<std::size_t>(state.role)), true, state.can_hire } };
        //The eleventh native row612 is a debug report with always-false
        //visibility.
    } else {
        title = park_object_ui_display_name(*room);
        const auto* definition = get_park_object_definition(c, room);
        if (!definition)
            throw std::runtime_error("Room context definition is absent");
        const auto scrap = definition->research.upgrades[0].cost_of_upgrade
                         * static_cast<std::uint32_t>(get_park_object_scrap_value_percent(c, room))
                         / 100u;
        if (*current_context_family == 25) {
            const bool closed = !room->can_load;
            const bool can_open = closed && c.mode.game != GameMode::code_3 && is_park_object_open_eligible(c, *room);
            const bool can_close = !closed && c.mode.game != GameMode::code_3;
            const auto* control = world_operations::find_object_control(c, room->definition_id);
            if (!control)
                throw std::runtime_error("Room purchase control is absent");
            auto* world = c.referenced_world();
            const auto bank_id = world->saved.bank_account;
            auto* bank_record = bank_id < c.entities.size() ? c.entities[bank_id].payload : nullptr;
            if (!bank_record || bank_record->kind != ThingKind::bank_account)
                throw std::runtime_error("Room context bank is absent");
            const auto balance = reinterpret_cast<BankRecord*>(bank_record)->balance;
            options = {
                { 575, can_open, can_open },
                { 576, can_close, can_close },
                { 516, true, true, 27, fixed::word(scrap) },
                { 577, true, true },
                { 514, true, fixed::word(control->purchase_cost) <= balance, 25, fixed::word(control->purchase_cost) }
            };
        } else {
            options = { { 516, true, true, 27, fixed::word(scrap) } };
        }
    }
    context_menu.present(*this, fonts(), title, options);
}

std::intptr_t WorldStaffUiController::State::context_event(UiControl& control, const UiEvent& event) {
    if (event.type == 20 && &control == context) {
        if (host.deactivate)
            host.deactivate(context);
        context = nullptr;
        auxiliary_staff = 0;
        auxiliary_identity = nullptr;
        current_context_family.reset();
        restore_input(*this);
    } else if (context && !closing) {
        if (event.type == 30) {
            refresh_context();
        } else if (event.type == 4 || event.type == 5 || escape(event)) {
            ui.destroy(context);
            return 1;
        } else if (event.type == 256 && event.id >= 1001 && event.id <= 1015) {
            if (!context_menu.layout_ready || !ui.owns(context))
                return 1;
            const auto index = event.id - 1001;
            auto& item = required(context, event.id);
            if (!item.enabled || !context_menu.rows[index])
                return 1;
            const auto row = context_menu.rows[index];
            const auto id = auxiliary_staff;
            const auto* identity = auxiliary_identity;
            const auto generation = auxiliary_hire_time;
            const bool filtered = context_filtered;
            auto* staff = aux_live_staff();
            auto* room = aux_live_room();
            const auto role = staff ? model.context(id).role : StaffType::unclassified;
            ui.destroy(context); //Native closes before invoking the selected action.
            const auto still_live = [this, id, identity, generation]() {
                const EntityIdentity target { id, identity, generation };
                return target.get<ParkObjectRuntimeView>(c.entities) || target.get<StaffRuntimeRecord>(c.entities);
            };
            if (!still_live())
                return 1;
            if (staff) {
                match (row) {
                    case 602 => open_activity(id);
                    case 603 or 604 => {
                        model.begin_patrol(id);
                        open_patrol();
                    }
                    case 605 => {
                        model.clear_patrol(id);
                        refresh_page();
                    }
                    case 606 => open_staff_ability(id);
                    case 607 => {
                        if (filtered)
                            close();
                        model.begin_pickup(id);
                    }
                    case 608 => {
                        if (confirm_deletions) {
                            capture_staff(*this, id);
                            message(0x392u, [this, id, filtered, still_live] {
                                if (still_live()) {
                                    if (filtered)
                                        close();
                                    model.fire(id);
                                }
                            });
                        } else {
                            if (filtered)
                                close();
                            model.fire(id);
                        }
                    }
                    case 609 => open_name(id);
                    case 610 => open_locator(id);
                    case _ => open_hire(role);
                }
            } else if (room) {
                if (row == 575) {
                    open_park_object(c, *room);
                    refresh_page();
                } else if (row == 576) {
                    close_park_object(c, *room);
                    refresh_page();
                } else if (row == 577)
                    open_name(id);
                else if (row == 514 || row == 516) {
                    const bool remove = row == 516;
                    const auto execute = [this, id, remove, filtered, still_live] {
                        if (!still_live())
                            return;
                        if (filtered)
                            close();
                        if (host.request)
                            host.request(world_object_trade_action(remove, { id }));
                    };
                    if (row == 516 && confirm_deletions) {
                        capture_room(*this, id);
                        message(0x391u, execute);
                    } else
                        execute();
                }
            }
            return 1;
        }
    }
    return ui.secondary(control, event);
}

void WorldStaffUiController::State::open_name(ThingId id, bool attached_to_panel) {
    close_auxiliary();
    auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    const bool object = record && record->kind == ThingKind::park_object;
    if (object)
        capture_room(*this, id);
    else
        capture_staff(*this, id);
    const auto words = object ? std::span<const unsigned char>{ object_name_template }
                              : std::span<const unsigned char>{ name_template };
    name_editor = &parse(words, object ? 7000000u : 80000u, panel_callback<&State::name_event>());
    if (attached_to_panel) {
        if (!root || !ui.owns(root))
            throw std::logic_error("Attached name editor has no owning panel");
        //Both template roots have absolute bounds and flags1. Parsing under
        //primary then reparenting preserves exactly the native coordinates.
        ui.reparent(*name_editor, root);
        ui.set_depth(*name_editor, root->render_depth + (object ? 17 : 10));
        if (object) {
            //5115A0 centers the two-component editor within its owning panel.
            //The Staff-specific5914A0 path retains the template position.
            const auto bounds = name_editor->rect;
            ui.translate(*name_editor,
                         root->rect.left + root->rect.width() / 2 - bounds.width() / 2 - bounds.left,
                         root->rect.top + root->rect.height() / 2 - bounds.height() / 2 - bounds.top);
        }
    } else if (object) {
        ui.set_depth(*name_editor, 32); //Standalone557380 context entry.
    }
    auto& title = required(name_editor, object ? 7000001 : 80001);
    caption(title, text(object ? 716 : 717), 5, 0, 1);
    title.secondary_shadow_text = true;
    auto& edit = required(name_editor, object ? 7000002 : 80002);
    //85854C/85859D's limit excludes the additional terminating WCHAR:
    //native32/14 therefore mean32/14 editable UTF-16 code units.
    edit.text_limit = object ? 14 : 32;
    if (object) {
        const auto components = park_object_ui_name_components(*aux_live_room());
        caption(edit, components[0], 7, 0, 1);
        auto& second = required(name_editor, 7000003);
        second.text_limit = 14;
        caption(second, components[1], 7, 0, 1);
        second.edit_caret_asset_hash = ui_model_asset_hash("hilight");
    } else {
        caption(edit, display_name(*aux_live_staff()), 7, 0, 1);
    }
    edit.edit_caret_asset_hash = ui_model_asset_hash("hilight");
    ui.prepare_edit_selection(edit);
    //An attached editor is a child of root: disabling that ancestor would
    //disable the editor itself. The modal input root restricts hit routing.
    if (!attached_to_panel && root && ui.owns(root))
        ui.set_enabled(*root, false);
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    ui.set_input_root(name_editor);
    ui.set_focus(&edit);
    //Native5914A0/591750/5115A0/557380 focus the editor but do not
    //register it as the current gameplay panel; its owning panel stays active.
}

std::intptr_t WorldStaffUiController::State::name_event(UiControl& control, const UiEvent& event) {
    if (event.type == 20 && &control == name_editor) {
        name_editor = nullptr;
        auxiliary_staff = 0;
        auxiliary_identity = nullptr;
        //Both native callbacks591310/510E20 enable unconditionally here.
        //A duplicate-warning message owns its separate nested command lease.
        c.ports.ui.restore_command_families_state(c, false);
        restore_input(*this);
    } else if (name_editor && !closing) {
        if (aux_live_room() && (event.type == 2051 || (event.type == 2050 && event.id == 7000002))) {
            ui.set_focus(&required(name_editor, event.id == 7000002 ? 7000003 : 7000002));
            return 1;
        }
        if (event.type == 2050) {
            //Preserve native callback ordering: finish the edit notification,
            //then process the queued submit against the retained target identity.
            ui.post(name_editor, 256, 4294967295u);
            return ui.secondary(control, event);
        }
        const bool accept = event.type == 256 && event.id == 4294967295u;
        const bool cancel =
            event.type == 2052 || event.type == 5 || escape(event) || (event.type == 256 && event.id == 4294967294u);
        if (accept || cancel) {
            if (accept) {
                if (aux_live_staff()) {
                    model.set_name(auxiliary_staff, required(name_editor, 80002).text);
                } else if (auto* room = aux_live_room()) {
                    const auto result = commit_park_object_ui_name(c,
                                                                   *room,
                                                                   required(name_editor, 7000002).text,
                                                                   required(name_editor, 7000003).text);
                    if (!result.accepted) {
                        if (result.warning_row) {
                            const std::array args{ BfstWideStringArgument{ 29, result.warning_name } };
                            message_text(text(*result.warning_row, {}, args));
                        }
                        return 1; //Native names remain mutated; editor stays open.
                    }
                }
            }
            ui.destroy(name_editor);
            if (!closing)
                refresh_page();
            return 1;
        }
        if (event.type == 30 && !aux_live_staff() && !aux_live_room()) {
            ui.destroy(name_editor);
            return 1;
        }
    }
    return ui.secondary(control, event);
}

void WorldStaffUiController::State::open_wage(ThingId id, bool result) {
    auto& wage = model.state().wage;
    if (!wage.open || wage.staff != id)
        throw std::logic_error("Wage dialog requires a seeded native demand");
    if (wage_dialog && ui.owns(wage_dialog))
        return;
    capture_staff(*this, id);
    if (result && !wage.showing_result)
        (void)model.show_wage_result();
    wage_dialog = &parse(result ? std::span<const unsigned char> { wage_result_template } : std::span<const unsigned char> { wage_demand_template }, result ? 45700u : 827777u, panel_callback<&State::wage_event>());
    ui.set_depth(*wage_dialog, 24);
    auto* staff = aux_live_staff();
    if (result) {
        caption(required(wage_dialog, 45706), text(254), 0);
        caption(required(wage_dialog, 45703), text(162), 6, 0);
        caption(required(wage_dialog, 45704), display_name(*staff), 6, 0);
        const std::array args{ BfstSignedIntegerArgument{ 13, wage.demanded_raise } };
        auto& response = required(wage_dialog, 45702);
        caption(response, text(wage.response_row, args), 6);
        response.text_constraints = { true, true };
        auto& choices = required(wage_dialog, 45708);
        ui.set_visible(choices, wage.outcome == StaffWageOutcome::rejected);
        caption(required(&choices, 45711), text(258), 6, 0);
        caption(required(&choices, 45712), text(259), 6, 0);
        ui.set_selected(choices, 45709);
        preview(required(wage_dialog, 45705), id);
        stage_advisor(433);
    } else {
        caption(required(wage_dialog, 827801), text(251), 0);
        caption(required(wage_dialog, 827783), display_name(*staff), 6, 0);
        auto& offer_label = required(wage_dialog, 827799);
        caption(offer_label, text(252), 6);
        offer_label.text_constraints = { true, true };
        const std::array args{ BfstSignedIntegerArgument{ 13, wage.offered_raise } };
        auto& description = required(wage_dialog, 827782);
        caption(description, text(253, args), 6);
        description.text_constraints = { true, true };
        required(wage_dialog, 827779).help_index = 14;
        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 7> labels{ { { 827790, 190 },
                                                                                   { 827794, 196 },
                                                                                   { 827792, 193 },
                                                                                   { 827793, 195 },
                                                                                   { 827788, 191 },
                                                                                   { 827795, 197 },
                                                                                   { 827798, 198 } } };
        for (const auto [control_id, row] : labels)
            caption(required(wage_dialog, control_id), text(row), 6, 0);
        constexpr std::array<std::uint32_t, 4> agencies{ 110, 111, 112, 113 };
        caption(required(wage_dialog, 827796), text(agencies.at(staff->agency)), 6, 0);
        caption(required(wage_dialog, 827789), currency(*this, fixed::word(staff->salary)), 6, 0);
        auto ability_text = format_bfst_wide_string(c.messages.mail_language.abilities,
                                                    c.messages.mail_language.conversion,
                                                    static_cast<std::uint32_t>(staff->first_known_ability),
                                                    16383);
        if (!ability_text)
            throw std::runtime_error("Unable to format wage dialog ability");
        caption(required(wage_dialog, 827797), *ability_text, 6, 0);
        meter(required(wage_dialog, 827786), "happygrad.tga");
        meter(required(wage_dialog, 827787));
        meter(required(wage_dialog, 827785));
        auto& stepper = required(wage_dialog, 827781);
        ui.set_range(stepper, c.staff.recruitment.balance.all.MinWage, c.staff.recruitment.balance.all.MaxWage);
        stepper.step_size = c.staff.recruitment.balance.all.WageStep;
        stepper.font = 6;
        affixed_edit(stepper, u"$ ", 6, 6);
        ui.set_value(stepper, wage.offered_raise, true);
        model.set_wage_offer(stepper.value);
        if (stepper.associated_edit)
            stepper.associated_edit->text_limit = 6;
        preview(required(wage_dialog, 827800), id);
        refresh_wage();
        ui.set_periodic(*wage_dialog,
                        0,
                        UiPeriodicEvent{ 16,
                                         524416,
                                         0,
                                         std::chrono::milliseconds{ 2000 },
                                         ui.clock() + std::chrono::milliseconds{ 2000 } });
        stage_advisor(432);
    }
    if (root && ui.owns(root))
        ui.set_enabled(*root, false);
    ui.set_input_root(wage_dialog);
    if (host.activate)
        host.activate(wage_dialog);
}

void WorldStaffUiController::State::refresh_wage() {
    auto* staff = aux_live_staff();
    if (!staff || !wage_dialog || !ui.owns(wage_dialog) || model.state().wage.showing_result)
        return;
    ui.set_value(required(wage_dialog, 827786), byte_meter(staff->happiness_score));
    ui.set_value(required(wage_dialog, 827787), byte_meter(staff->motivation));
    const auto maximum = c.staff.recruitment.balance.all.MaxExperience;
    if (!maximum)
        throw std::domain_error("Wage preview maximum experience is zero");
    ui.set_value(required(wage_dialog, 827785), fixed::word(staff->experience << 10) / maximum);
    FileTime now{};
    calculate_world_calendar_filetime(c, &c.referenced_world()->calendar, &now);
    const auto current = std::uint64_t{ now.low } | (std::uint64_t{ now.high } << 32);
    const auto days = std::bit_cast<std::int64_t>(current - staff->time_hired) / 864000000000LL;
    const std::array args{ BfstSignedIntegerArgument{ 19, fixed::narrow(days) } };
    caption(required(wage_dialog, 827791), text(952, args), 6, 0);
}

std::intptr_t WorldStaffUiController::State::wage_event(UiControl& control, const UiEvent& event) {
    auto& wage = model.state().wage;
    if (event.type == 20 && &control == wage_dialog) {
        if (host.deactivate)
            host.deactivate(wage_dialog);
        //Retirement clears the model before notifying the frontend. The live
        //control still identifies which Advisor/settlement teardown applies.
        const bool result = control.id == 45700;
        const auto choice = result ? required(wage_dialog, 45708).selected_id : -1;
        auto* old = wage_dialog;
        wage_dialog = nullptr;
        std::erase_if(previews, [&](const auto& entry) {
            for (auto* node = entry.first; node; node = node->parent)
                if (node == old)
                    return true;
            return false;
        });
        if (result && aux_live_staff() && wage.open)
            model.resolve_wage_result(choice == 45709   ? StaffRejectedWageChoice::accept_full_demand
                                      : choice == 45710 ? StaffRejectedWageChoice::dismiss
                                                        : StaffRejectedWageChoice::leave_unchanged);
        auxiliary_staff = 0;
        auxiliary_identity = nullptr;
        if (!result && !wage_transition_pending)
            wage = {};
        cancel_ui_advisor_action(c, result ? 433 : 432, false);
        restore_input(*this);
        return ui.secondary(control, event);
    }
    if (!wage_dialog || closing)
        return ui.secondary(control, event);
    if (!wage.open || !aux_live_staff()) {
        ui.destroy(wage_dialog);
        return 1;
    }
    if (event.type == 16 && event.id == 524416) {
        refresh_wage();
        return 1;
    }
    if (event.type == 2048 && !wage.showing_result) {
        model.set_wage_offer(static_cast<std::int32_t>(event.value));
        return 1;
    }
    if (event.type == 256 && event.id == (wage.showing_result ? 45713u : 827802u)) {
        queue_world_advisor_help_action(c, wage.showing_result ? 433 : 432);
        return 1;
    }
    if (wage.showing_result) {
        if (event.type == 5 || escape(event) || (event.type == 256 && (event.id == 45701 || event.id == 45707))) {
            ui.destroy(wage_dialog);
            return 1;
        }
    } else if (event.type == 256) {
        if (event.id == 827779) {
            const auto id = wage.staff;
            auxiliary_staff = 0;
            model.fire(id);
            if (wage_dialog && ui.owns(wage_dialog))
                ui.destroy(wage_dialog);
            return 1;
        }
        if (event.id == 827778 || event.id == 4294967294u || event.id == 827780 || event.id == 4294967295u) {
            if (event.id == 827778 || event.id == 4294967294u)
                model.set_wage_offer(0);
            const auto id = wage.staff;
            wage_transition_pending = true;
            try {
                ui.destroy(wage_dialog);
            } catch (...) {
                wage_transition_pending = false;
                throw;
            }
            wage_transition_pending = false;
            (void)model.show_wage_result();
            open_wage(id, true);
            return 1;
        }
    }
    if (!wage.showing_result && (event.type == 5 || escape(event))) {
        ui.destroy(wage_dialog);
        return 1;
    }
    return ui.secondary(control, event);
}

void WorldStaffUiController::State::open_training_budget() {
    begin(Page::training_budget);
    root = &parse(budget_template, 729, panel_callback<&State::budget_event>(), &page_roots);
    const auto budget = model.training_budget();
    budget_history6 = budget.history_6;
    budget_history3_minus4 = budget.history_3_minus_4;
    constexpr std::array<std::uint32_t, 5> steppers{ 743, 748, 746, 747, 749 };
    for (std::size_t i = 0; i < steppers.size(); ++i) {
        auto& stepper = required(root, steppers[i]);
        ui.set_range(stepper, 0, 10000);
        stepper.step_size = 25;
        stepper.font = 6;
        stepper.horizontal_alignment = stepper.vertical_alignment = 1;
        stepper.color = { 0, 0, 0, 255 };
        stepper.scalar_text = UiScalarTextKind::signed_currency;
        ui.set_value(stepper, budget.budgets[i]);
        auto& experience = required(&stepper, 744);
        meter(experience, "ridestatbar.tga", 0);
        ui.set_value(experience, budget.experience_meters[i]);
    }
    //The shipped budget panel exposes five role steppers; index5 (Gardener)
    //still participates in all totals. Do not invent a sixth template control.
    auto& totals = required(root, 734);
    const std::array<std::pair<std::uint32_t, std::int32_t>, 4> values{
        { { 738, budget.history_6 },
          { 735, budget.history_4 },
          { 736, budget.history_3_minus_4 },
          { 740, fixed::subtract(budget.history_6, fixed::add(budget.history_4, budget.history_3_minus_4)) } }
    };
    for (const auto [id, value] : values)
        caption(required(&totals, id), currency(*this, value), 6, 2);
    required(&totals, 735).color = required(&totals, 736).color = { 255, 0, 0, 255 };
    ui.set_enabled(required(root, 733), c.mode.game != GameMode::code_2);
    stage_advisor(151);
    if (host.activate)
        host.activate(root);
}

std::intptr_t WorldStaffUiController::State::budget_event(UiControl& control, const UiEvent& event) {
    if (event.type == 20 && &control == root) {
        if (host.deactivate)
            host.deactivate(root);
        std::erase(page_roots, root);
        root = nullptr;
        page = Page::none;
        return ui.secondary(control, event);
    }
    if (event.type == 2048 && !refreshing) {
        constexpr std::array<std::uint32_t, 5> ids{ 743, 748, 746, 747, 749 };
        const auto found = std::ranges::find(ids, event.id);
        if (found != ids.end()) {
            model.set_training_budget(static_cast<StaffType>(found - ids.begin()),
                                      static_cast<std::int32_t>(event.value));
            const auto budget = model.training_budget();
            auto& totals = required(root, 734);
            caption(required(&totals, 735), currency(*this, budget.total), 6, 2);
            const auto remainder =
                fixed::subtract(fixed::subtract(budget_history6, budget_history3_minus4), budget.total);
            caption(required(&totals, 740), currency(*this, remainder), 6, 2);
            return 1;
        }
    }
    if (event.type == 5 || escape(event) || (event.type == 256 && event.id == 4294967295u)) {
        close();
        return 1;
    }
    if (event.type == 256 && (event.id == 732 || event.id == 733)) {
        const auto command = event.id == 732 ? WorldUiCommand::financial_statistics : WorldUiCommand::loans;
        close();
        if (host.request)
            host.request(WorldCommandAction{ command, 0 });
        return 1;
    }
    if (event.type == 256 && event.id == 731)
        return 1;
    return ui.secondary(control, event);
}
} //namespace simcoaster
