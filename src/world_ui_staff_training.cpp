#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_staff_internal.hpp"
#include "world_ui_staff_columns.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char training_template[] {
#embed "ui_templates/staff_training.bin"
};
constexpr unsigned char room_template[] {
#embed "ui_templates/staff_room.bin"
};
constexpr unsigned char selector_template[] {
#embed "ui_templates/staff_selector.bin"
};
constexpr unsigned char patrol_template[] {
#embed "ui_templates/staff_patrol.bin"
};
constexpr unsigned char staff_ability_template[] {
#embed "ui_templates/staff_ability.bin"
};
static_assert(sizeof(training_template) == 1480 && sizeof(selector_template) == 774 && sizeof(room_template) == 790 && sizeof(patrol_template) == 132);
constexpr std::array<std::uint32_t, 3> training_groups { 9011, 9017, 9018 };
constexpr std::array<std::uint32_t, 6> selector_buttons { 44005, 44006, 44007, 44009, 44010, 44008 };
constexpr std::array<std::uint32_t, 6> selector_titles { 245, 250, 247, 246, 249, 248 };

bool escape(const UiEvent& event) {
    return event.type == 5 || (event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0);
}

void cancel_selector_advisor(WorldRuntime& c) {
    //58E090 joins the native503B40 UI cancellation wrapper, which clears the
    //delayed-panel flag but leaves the enclosing Training Room panel_up intact.
    if (!c.simulation.current) {
        throw std::logic_error("Staff selector Advisor World is not bound");
    }
    auto* payload = c.entities.at(c.simulation.current->saved.advisor).payload;
    if (!payload || payload->kind != ThingKind::advisor) {
        throw std::logic_error("Staff selector World Advisor is not bound");
    }
    auto& advisor = *reinterpret_cast<AdvisorRecord*>(payload);
    advisor.from_ui_panel = 0;
    world_operations::cancel_advisor_action_by_id(c, &advisor, 443);
}

ThingId selected_staff(const UiControl& list, const UiEvent& event) {
    if (event.value < 0 || static_cast<std::size_t>(event.value) >= list.rows.size()) {
        return 0;
    }
    const auto identity = list.rows[static_cast<std::size_t>(event.value)].identity;
    return identity > 0 && identity < static_cast<std::intptr_t>(thing_slot_count) ? static_cast<ThingId>(identity) : 0;
}

void reparent(UiControl& control, UiControl& parent) {
    control.runtime->reparent(control, &parent);
}

ParkObjectRuntimeView& park_object(WorldRuntime& c, ThingId id) {
    if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != ThingKind::park_object) {
        throw std::logic_error("Staff room controller has no live building");
    }
    return *c.entities.payload<ParkObjectRuntimeView>(id);
}

std::u16string integer_text(std::int32_t value) {
    const auto bytes = std::to_string(value);
    return { bytes.begin(), bytes.end() };
}

std::u16string currency(WorldRuntime& c, std::int32_t value) {
    const bool negative = value < 0;
    const std::array argument { BfstSignedIntegerArgument { negative ? 44u : 39u, negative ? fixed::negate(value) : value } };
    auto text = format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, negative ? 958u : 957u, 16383, {}, argument);
    if (!text) {
        throw std::runtime_error("Staff Room scrap value: " + text.error().reason);
    }
    return std::move(*text);
}

template <class State>
void panel_caption(State& owner, UiControl& control, std::u16string_view value) {
    //501D50 creates a primary shadow-text attachment with game font5,
    //LEFT/MIDDLE alignment. This differs from ordinary secondary captions.
    owner.caption(control, value, 5, 0, 1);
    control.primary_shadow_text = true;
}
} //namespace

void WorldStaffUiController::State::open_room(ThingId room) {
    begin(Page::staff_room);
    page_target = room;
    model.open_staff_room(room);
    root = &parse(
        room_template,
        26000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    constexpr std::array<std::uint32_t, 3> headings { 162, 166, 163 };
    constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::text, UiListCellFormat::meter };
    const auto& roster = c.ui.panels.staff_room;
    const auto key = static_cast<std::int32_t>(roster.sort_column + 1) * (roster.sort_direction == UiListSortDirection::ascending ? 1 : -1);
    auto& list_control = required(root, 26007);
    list(list_control, headings, formats, key, 7);
    list_control.column_presentations[1].horizontal_alignment = 1;
    list_control.column_presentations[1].text_constraints = { true, true };
    list_control.column_presentations[2].meter = { "ridestatbar.tga", { 0, 10 } };
    caption(required(root, 26010), text(148), 6, 0, 1);
    caption(required(root, 26011), text(164), 6, 0, 1);
    auto& status = required(root, 26014);
    caption(status, {}, 8);
    status.color = { 0, 0, 0, 255 };
    ui.set_enabled(status, false);
    ui.set_depth(status, status.render_depth + 11);
    auto& preview_control = required(root, 26013);
    preview_control.help_index = 301;
    ui.set_primary_callback(preview_control, [this](UiControl& sender, const UiEvent& event) {
        //510500/510B70 bind the containing preview
        //control, not the graphic child26008.
        if (event.type == 13) {
            return std::intptr_t { sender.render_depth + 10 };
        }
        if (event.type == 65542) {
            if (host.focus_object) {
                host.focus_object(model.state().staff_room);
            }
            return std::intptr_t {};
        }
        return ui.secondary(sender, event);
    });
    ui.set_primary_callback(required(root, 26015), [this](UiControl& sender, const UiEvent& event) {
        if (event.type == 65537) {
            sender.color = { 239, 234, 24, 255 };
        } else if (event.type == 65538) {
            sender.color = { 255, 255, 255, 255 };
        } else if (event.type == 65542 && !event.id) {
            open_name(model.state().staff_room, true);
        }
        return ui.secondary(sender, event);
    });
    refresh_room();
    ui.set_periodic(*root, 0, UiPeriodicEvent { 16, 524416, 0, std::chrono::milliseconds { 2000 } });
    stage_advisor(247);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_room() {
    if (page != Page::staff_room || !root || !model.state().staff_room) {
        return;
    }
    auto& room = park_object(c, model.state().staff_room);
    panel_caption(*this, required(root, 26015), park_object_ui_display_name(room));
    //58DFC0/510500 store preview26013 at+1C and statistics26008 at+18.
    //The old analyst field names inverted those IDs;58CD80 reads+1C bounds.
    object_preview(required(root, 26013), room.base.header.id);
    std::vector<UiListRow> rows;
    for (const auto& row : c.ui.panels.staff_room.rows) {
        rows.push_back({ row.staff, { row.name, row.role, row.energy_meter } });
    }
    auto& roster = required(root, 26007);
    project_rows(roster, std::move(rows), 4);
    ui.set_enabled(required(root, 26003), ui.selected_list_identity(roster).has_value());
    auto& closed = required(root, 26004);
    closed.visual_flags = room.can_load ? 0 : 1;
    closed.help_index = room.can_load ? 12u : 11u;
    caption(required(root, 26009), integer_text(c.staff.recruitment.balance.all.StaffRoomCapacity), 6, 0, 1);
    const auto* definition = get_park_object_definition(c, &room);
    if (!definition) {
        throw std::logic_error("Staff Room has no object definition");
    }
    const auto scrap = definition->research.upgrades[0].cost_of_upgrade * static_cast<std::uint32_t>(get_park_object_scrap_value_percent(c, &room)) / 100u;
    caption(required(root, 26012), currency(c, fixed::word(scrap)), 6, 0, 1);
    const auto state = classify_park_object_ui_status(c, room, 2);
    const auto presentation = park_object_ui_status_presentation(state);
    auto& status = required(root, 26014);
    caption(status, text(presentation.text_row), 8);
    status.color = presentation.color;
    ui.set_visible(status, state != ParkObjectUiStatus::open && state != ParkObjectUiStatus::park_closed && (definition->which_ui_type != ObjectUiCategory::features || state == ParkObjectUiStatus::dead));
    std::size_t navigation_count = 0;
    //50F660 counts the stricter4F98F0 facility filter for arrow visibility.
    //Native4FAA70/4FBFA0 navigation below intentionally checks only Chills.
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (!node->payload || node->payload->kind != ThingKind::park_object) {
            continue;
        }
        const auto& candidate = *reinterpret_cast<const ParkObjectRuntimeView*>(node->payload);
        const auto* catalog = get_park_object_definition(c, &candidate);
        if (catalog && static_cast<std::uint16_t>(catalog->add_on.upgrades_id) == 0 && catalog->which_ui_type == ObjectUiCategory::facilities && (std::to_underlying(candidate.capabilities) & std::to_underlying(ParkObjectCapabilities::chills_you_out)) != 0) {
            ++navigation_count;
        }
    }
    ui.set_visible(required(root, 26002), navigation_count >= 2);
    ui.set_visible(required(root, 26006), navigation_count >= 2);
}

std::intptr_t WorldStaffUiController::State::room_event(UiControl& sender, const UiEvent& event) {
    if (refreshing) {
        return ui.secondary(sender, event);
    }
    if (event.type == 20 && &sender == root) {
        model.close_staff_room();
        return ui.secondary(sender, event);
    }
    if (escape(event) || (event.type == 256 && (event.id == 26001 || event.id == static_cast<std::uint32_t>(-2)))) {
        close();
        return 0;
    }
    if (event.type == 16 && event.id == 524416) {
        model.refresh();
        refresh_room();
        return 0;
    }
    if (event.type == 65546 && (event.id == 9728 || event.id == 10240)) {
        //510C00 forwards the packed native previous/next commands to the same
        //navigation methods as the arrow buttons.
        return room_event(sender, UiEvent { 256, event.id == 9728 ? 26002u : 26006u, 0 });
    }
    auto& roster = required(root, 26007);
    if (event.type == 1025) {
        ui.set_enabled(required(root, 26003), event.value != -1);
        return 0;
    }
    if (event.type == 1030) {
        const auto key = static_cast<std::int32_t>(event.value);
        if (key) {
            c.ui.panels.staff_room.sort_column = static_cast<std::int16_t>((key < 0 ? -static_cast<std::int64_t>(key) : key) - 1);
            c.ui.panels.staff_room.sort_direction = key < 0 ? UiListSortDirection::descending : UiListSortDirection::ascending;
        } else {
            c.ui.panels.staff_room.sort_column = -1;
        }
        return 0;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == 26003)) {
        const auto chosen = event.type == 1024 ? selected_staff(roster, event) : static_cast<ThingId>(ui.selected_list_identity(roster).value_or(0));
        if (chosen) {
            open_details(chosen);
        }
        return 0;
    }
    if (event.type == 256) {
        if (event.id == 26004) {
            auto& room = park_object(c, model.state().staff_room);
            if (event.value == 1) {
                close_park_object(c, room);
            } else {
                open_park_object(c, room);
            }
            refresh_room();
            return 0;
        }
        if (event.id == 26005) {
            open_context(model.state().staff_room, { sender.rect.left, sender.rect.bottom });
            return 0;
        }
        if (event.id == 26016) {
            if (c.referenced_world()) {
                queue_world_advisor_help_action(c, 247);
            }
            return 0;
        }
        if (event.id == 26002 || event.id == 26006) {
            if (context && ui.owns(context)) {
                ui.dispatch(context, 4);
            }
            std::vector<ThingId> matches;
            for (auto* node = c.entities.active_head; node; node = node->active_next) {
                if (!node->payload || node->payload->kind != ThingKind::park_object) {
                    continue;
                }
                const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(node->payload);
                if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::chills_you_out)) != 0) {
                    matches.push_back(object.base.header.id);
                }
            }
            const auto position = std::ranges::find(matches, model.state().staff_room);
            if (!matches.empty() && position != matches.end()) {
                const auto index = static_cast<std::size_t>(position - matches.begin());
                page_target = matches[(index + (event.id == 26002 ? matches.size() - 1 : 1)) % matches.size()];
                model.open_staff_room(page_target);
                refresh_room();
            }
            return 0;
        }
    }
    return ui.secondary(sender, event);
}

void WorldStaffUiController::State::open_training(ThingId room) {
    begin(Page::training);
    page_target = room;
    model.open_training_room(room);
    root = &parse(
        training_template,
        9000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    ability = ui.find_recursive(primary, 9021);
    if (!ability) {
        throw std::runtime_error("Original Training Room template has no detached ability root9021");
    }
    bind(*ability, panel_callback<&State::ability_event>());
    ui.set_visible(*ability, false);
    panel_caption(*this, required(root, 9004), text(226));
    auto& head = required(root, 9006);
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 5> { { { 9008, 227 }, { 9010, 228 }, { 9007, 229 }, { 9009, 230 }, { 9019, 231 } } }) {
        auto& label = required(&head, id);
        caption(label, text(row), 8);
        label.text_constraints = { true, true };
    }
    auto& choices = required(ability, 9022);
    constexpr std::array<std::uint32_t, 1> headings { 706 };
    constexpr std::array formats { UiListCellFormat::text };
    list(choices, headings, formats, 1, 7);
    auto& ability_heading = required(&choices, 16);
    ability_heading.color = { 255, 255, 255, 255 };
    for (auto& color : ability_heading.button_text_colors) {
        color = ability_heading.color;
    }
    refresh_training();
    ui.set_periodic(*root, 0, UiPeriodicEvent { 16, 524416, 0, std::chrono::milliseconds { 2000 } });
    stage_advisor(158);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_training() {
    if (page != Page::training || !root || !model.state().training.open) {
        return;
    }
    const auto& display = c.ui.panels.training_room;
    for (std::size_t visible = 0; visible < training_groups.size(); ++visible) {
        auto& group = required(root, training_groups[visible]);
        const auto& row = display.rows[visible];
        ui.set_visible(group, row.visible);
        if (!row.visible) {
            continue;
        }
        group.context[0] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(row.pending_index) + 1);
        caption(required(&group, 9014), row.name, 6, 0, 1);
        caption(required(&group, 9016), row.role, 6);
        auto& ability_button = required(&group, 9012);
        caption(ability_button, row.ability, 6);
        ability_button.text_constraints.width = true;
        for (auto& color : ability_button.button_text_colors) {
            color = { 255, 255, 255, 255 };
        }
        auto& progress = required(&group, 9015);
        if (row.progress_text_row == 934) {
            const std::array arguments { BfstSignedIntegerArgument { 33, row.progress_percent } };
            caption(progress, text(934, arguments), 6);
        } else {
            caption(progress, text(933), 6);
        }
        progress.text_constraints = { true, true };
        ui.set_enabled(required(&group, 9013), !ability_slot);
    }
    ui.set_visible(required(root, 9019), display.empty_notice_visible);
    ui.set_enabled(required(root, 9005), display.add_staff_enabled);
    if (ability_slot) {
        refresh_ability();
    }
}

void WorldStaffUiController::State::open_selector() {
    if (page != Page::training || !root) {
        return;
    }
    close_ability();
    close_selector();
    std::vector<ThingId> excluded;
    for (const auto& pending : c.staff.interaction.training_pending) {
        if (pending.staff) {
            excluded.push_back(pending.staff);
        }
    }
    constexpr std::array excluded_roles { StaffType::entertainer, StaffType::researcher };
    model.open_selector(excluded, excluded_roles);
    ui.translate(*root, 2000, 0);
    selector_shifted = true;
    const auto parsed = ui.parse_template(root, std::as_bytes(std::span { selector_template }), panel_callback<&State::selector_event>());
    if (!parsed) {
        throw std::runtime_error("Staff selector template: " + parsed.error().reason);
    }
    selector = ui.find(root, 44000);
    if (!selector) {
        throw std::runtime_error("Original staff selector root44000 is absent");
    }
    bind(*selector, panel_callback<&State::selector_event>());
    auto& rows = required(selector, 44011);
    list(rows, StaffSelectorColumns::headings, StaffSelectorColumns::formats, model.state().selector.sort_key, 7);
    rows.column_presentations[2].meter = { "ridestatbar.tga", { 0, 8 } };
    rows.column_presentations[3].meter = { "happygrad.tga", { 0, 8 } };
    caption(required(selector, 44012), text(701), 6);
    required(selector, 44012).text_constraints = { true, true };
    ui.set_periodic(*selector, 3, UiPeriodicEvent { 16, 524419, 0, std::chrono::milliseconds { 2000 } });
    refresh_selector();
    ui.set_focus(selector);
}

void WorldStaffUiController::State::refresh_selector() {
    if (!selector || !ui.owns(selector)) {
        return;
    }
    const auto& view = model.state().selector;
    auto& rows = required(selector, 44011);
    std::vector<UiListRow> data;
    for (const auto& row : view.rows) {
        data.push_back({ row.staff, StaffSelectorColumns::cells(row) });
    }
    //58E420 periodically replaces Experience, Happiness and Activity only
    //(mask28), preserving the existing row order and identity.
    project_rows(rows, std::move(data), 28);
    if (ui.list_sort_key(rows) != view.sort_key) {
        ui.set_list_sort_key(rows, view.sort_key, false);
    }
    for (std::size_t i = 0; i < selector_buttons.size(); ++i) {
        ui.set_enabled(required(selector, selector_buttons[i]), view.available[i]);
    }
    auto& group = required(selector, 44004);
    if (view.filter >= StaffType::handyman && view.filter <= StaffType::gardener) {
        const auto index = static_cast<std::size_t>(view.filter);
        ui.set_selected(group, static_cast<std::int32_t>(selector_buttons[index]), false);
        panel_caption(*this, required(selector, 44013), text(selector_titles[index]));
    } else {
        ui.set_selected(group, -1, false);
        panel_caption(*this, required(selector, 44013), text(695));
    }
    ui.set_enabled(required(selector, 44002), view.selected != 0);
}

void WorldStaffUiController::State::close_selector() {
    auto* old = std::exchange(selector, nullptr);
    model.state().selector.open = false;
    model.state().selector.selected = 0;
    if (old && ui.owns(old)) {
        ui.destroy(old);
        cancel_selector_advisor(c);
    }
    if (selector_shifted && root && ui.owns(root)) {
        ui.translate(*root, -2000, 0);
    }
    selector_shifted = false;
    if (root && ui.owns(root)) {
        ui.set_focus(root);
    }
}

void WorldStaffUiController::State::open_ability(std::size_t slot) {
    if (page != Page::training || !ability || !ui.owns(ability)) {
        return;
    }
    close_ability();
    model.begin_training_ability_picker(slot);
    ability_slot = slot;
    UiControl* button_control = nullptr;
    for (const auto id : training_groups) {
        auto& group = required(root, id);
        if (reinterpret_cast<std::uintptr_t>(group.context[0]) == slot + 1) {
            button_control = &required(&group, 9012);
            break;
        }
    }
    if (!button_control) {
        throw std::logic_error("Training Room ability picker has no row for its pending slot");
    }
    reparent(*ability, *button_control);
    const auto& rows = required(ability, 9022);
    ui.translate(*ability, button_control->rect.right - rows.rect.left, button_control->rect.top - rows.rect.top);
    ui.set_visible(*ability, true);
    button_control->visual_flags |= 1u;
    refresh_ability();
    refresh_training();
    ui.set_focus(ability);
}

void WorldStaffUiController::State::refresh_ability() {
    if (!ability_slot || !ability || !ui.owns(ability)) {
        return;
    }
    const auto slot = *ability_slot;
    const auto id = c.staff.interaction.training_pending.at(slot).staff;
    if (!id) {
        close_ability();
        return;
    }
    std::vector<UiListRow> rows;
    for (const auto& entry : model.training_abilities(id)) {
        rows.push_back({ static_cast<std::intptr_t>(entry.ability), { entry.name } });
    }
    project_rows(required(ability, 9022), std::move(rows), 0);
}

void WorldStaffUiController::State::close_ability(bool restore) {
    model.close_training_ability_picker(restore);
    ability_slot.reset();
    if (ability && ui.owns(ability)) {
        if (ability->parent && ability->parent != primary) {
            ability->parent->visual_flags &= static_cast<std::uint8_t>(~1u);
        }
        ui.set_visible(*ability, false);
        reparent(*ability, *primary);
        ui.clear_list_rows(required(ability, 9022), false);
    }
    if (root && ui.owns(root)) {
        ui.set_focus(root);
    }
}

std::intptr_t WorldStaffUiController::State::selector_event(UiControl& sender, const UiEvent& event) {
    if (refreshing) {
        return ui.secondary(sender, event);
    }
    if (event.type == 20 && &sender == selector) {
        selector = nullptr;
        if (selector_shifted && root && ui.owns(root)) {
            ui.translate(*root, -2000, 0);
        }
        selector_shifted = false;
        model.state().selector.open = false;
        model.state().selector.selected = 0;
        cancel_selector_advisor(c);
        if (root && ui.owns(root)) {
            ui.set_focus(root);
        }
        return ui.secondary(sender, event);
    }
    if (!selector) {
        return ui.secondary(sender, event);
    }
    if (event.type == 16 && event.id == 524419) {
        model.refresh();
        refresh_selector();
        return 0;
    }
    if (escape(event) || (event.type == 256 && (event.id == 44001 || event.id == static_cast<std::uint32_t>(-2)))) {
        close_selector();
        return 0;
    }
    if (event.type == 257 && event.value == 1) {
        const auto found = std::ranges::find(selector_buttons, event.id);
        if (found != selector_buttons.end()) {
            model.filter_selector(static_cast<StaffType>(found - selector_buttons.begin()));
            refresh_selector();
            return 0;
        }
    }
    if (event.type == 1030) {
        model.sort_selector(static_cast<std::int32_t>(event.value));
        return 0;
    }
    if (event.type == 1025) {
        const auto id = selected_staff(required(selector, 44011), event);
        model.state().selector.selected = id;
        refresh_selector();
        return 0;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == 44002)) {
        if (event.type == 1024) {
            model.state().selector.selected = selected_staff(required(selector, 44011), event);
        }
        const auto id = model.state().selector.selected;
        if (!id) {
            return 0;
        }
        const auto add = [this, id] {
            //The modal callback retains a World identity only while retirement
            //notification can synchronously clear the selector/confirmation.
            if (!model.state().selector.open || model.state().selector.selected != id) {
                return;
            }
            close_selector();
            (void)model.stage_training_staff(id);
            refresh_training();
        };
        if (id >= c.entities.size() || !c.entities[id].payload || !is_staff_record_kind(c.entities[id].payload->kind)) {
            return 0;
        }
        if (c.entities.payload<const StaffRuntimeRecord>(id)->first_known_ability != ThemeAbility::none) {
            message(715, add);
        } else {
            add();
        }
        return 0;
    }
    return ui.secondary(sender, event);
}

std::intptr_t WorldStaffUiController::State::ability_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == ability) {
        ability = nullptr;
        model.close_training_ability_picker();
        ability_slot.reset();
        return ui.secondary(sender, event);
    }
    if (escape(event)) {
        close_ability();
        refresh_training();
        return 0;
    }
    if (event.type == 1024 && ability_slot && ability) {
        auto& rows = required(ability, 9022);
        if (event.value < 0 || static_cast<std::size_t>(event.value) >= rows.rows.size()) {
            return 0;
        }
        const auto selected = static_cast<ThemeAbility>(rows.rows[static_cast<std::size_t>(event.value)].identity);
        const auto result = model.stage_training_ability(*ability_slot, selected);
        close_ability(result == StaffTrainingSelectionResult::already_known);
        refresh_training();
        if (result == StaffTrainingSelectionResult::already_known) {
            message(714);
        }
        return 0;
    }
    return ui.secondary(sender, event);
}

void WorldStaffUiController::State::open_staff_ability(ThingId id) {
    if (!id || id >= c.entities.size() || !c.entities[id].payload || !is_staff_record_kind(c.entities[id].payload->kind)) {
        throw std::out_of_range("Staff ability picker needs a live employee");
    }
    const auto* employee = c.entities.payload<const StaffRuntimeRecord>(id);
    bool facility = false;
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (!node->payload || node->payload->kind != ThingKind::park_object) {
            continue;
        }
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(node->payload);
        if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::training_room)) != 0) {
            facility = true;
            break;
        }
    }
    if (!facility) {
        message(233);
        return;
    }
    if (employee->experience < 10000u) {
        message(232);
        return;
    }
    begin(Page::staff_ability);
    page_target = id;
    open_staff_training_picker(c, *c.entities.payload<StaffRuntimeRecord>(id));
    auto& picker = c.staff.interaction.training_picker;
    std::size_t training_count = 0;
    for (auto* node = c.entities.active_head; node; node = node->active_next) {
        if (!node->payload || !is_staff_record_kind(node->payload->kind)) {
            continue;
        }
        const auto state = reinterpret_cast<const StaffRuntimeRecord*>(node->payload)->state;
        if (state == StaffState::training || state == StaffState::walking_to_training_room || state == StaffState::training_paused || state == StaffState::walking_to_training_room_paused) {
            ++training_count;
        }
    }
    if (training_count >= 3) {
        picker.rows.clear();
        picker.message_row = 713;
    } else if (picker.rows.empty()) {
        constexpr std::array<std::uint32_t, 6> empty_rows { 707, 708, 711, 710, 712, 709 };
        picker.message_row = empty_rows.at(static_cast<std::size_t>(world_staff_type_for_used_thing(*c.entities[id].payload)));
    }
    root = &parse(
        staff_ability_template,
        631628,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    constexpr std::array<std::uint32_t, 1> headings { 706 };
    constexpr std::array formats { UiListCellFormat::text };
    list(required(root, 631629), headings, formats, staff_ability_sort_key, 7);
    panel_caption(*this, required(root, 631631), text(705));
    auto& empty = required(root, 631630);
    caption(empty, {}, 8);
    empty.text_constraints = { true, true };
    refresh_staff_ability();
    stage_advisor(445);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_staff_ability() {
    if (page != Page::staff_ability || !root) {
        return;
    }
    const auto& picker = c.staff.interaction.training_picker;
    std::vector<UiListRow> rows;
    for (const auto& entry : picker.rows) {
        rows.push_back({ static_cast<std::intptr_t>(entry.ability), { entry.name } });
    }
    auto& list_control = required(root, 631629);
    project_rows(list_control, std::move(rows), 0);
    if (ui.list_sort_key(list_control) != staff_ability_sort_key) {
        ui.set_list_sort_key(list_control, staff_ability_sort_key, false);
    }
    auto& empty = required(root, 631630);
    ui.set_visible(empty, picker.rows.empty());
    if (picker.rows.empty()) {
        caption(empty, text(picker.message_row), 8);
    }
    ui.set_enabled(required(root, static_cast<std::uint32_t>(-1)), ui.selected_list_identity(list_control).has_value());
}

std::intptr_t WorldStaffUiController::State::staff_ability_event(UiControl& sender, const UiEvent& event) {
    if (refreshing) {
        return ui.secondary(sender, event);
    }
    if (event.type == 20 && &sender == root) {
        close_staff_training_picker(c);
        return ui.secondary(sender, event);
    }
    if (escape(event) || (event.type == 256 && event.id == static_cast<std::uint32_t>(-2))) {
        close();
        return 0;
    }
    if (event.type == 1030) {
        staff_ability_sort_key = static_cast<std::int32_t>(event.value);
        return 0;
    }
    if (event.type == 1025) {
        ui.set_enabled(required(root, static_cast<std::uint32_t>(-1)), event.value != -1);
        return 0;
    }
    if (event.type == 256 && event.id == 631632) {
        if (c.referenced_world()) {
            queue_world_advisor_help_action(c, 445);
        }
        return 0;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == static_cast<std::uint32_t>(-1))) {
        auto& list_control = required(root, 631629);
        if (event.type == 1024 && event.value >= 0) {
            ui.select_list_row(list_control, static_cast<std::int32_t>(event.value), false);
        }
        const auto selected = ui.selected_list_identity(list_control);
        if (!selected) {
            return 0;
        }
        const auto id = c.staff.interaction.training_picker.staff;
        const auto ability_id = static_cast<ThemeAbility>(*selected);
        const auto result = select_staff_training_ability(c, ability_id);
        close();
        if (result == StaffTrainingSelectionResult::already_known) {
            message(714);
        } else if (result == StaffTrainingSelectionResult::confirmation_required) {
            message(715, [this, id, ability_id] {
                const auto& pending = c.staff.interaction.training_picker;
                if (pending.staff == id && pending.selected == ability_id && pending.confirmation_pending) {
                    (void)confirm_staff_training_ability_change(c);
                }
            });
        }
        return 0;
    }
    return ui.secondary(sender, event);
}

std::intptr_t WorldStaffUiController::State::training_event(UiControl& sender, const UiEvent& event) {
    if (refreshing) {
        return ui.secondary(sender, event);
    }
    if (event.type == 20 && &sender == root) {
        model.cancel_training_room();
        return ui.secondary(sender, event);
    }
    if (escape(event)) {
        close();
        return 0;
    }
    if (event.type == 16 && event.id == 524416) {
        model.refresh();
        refresh_training();
        return 0;
    }
    if (event.type == 256) {
        if (event.id == 9001 || event.id == static_cast<std::uint32_t>(-2)) {
            close();
            return 0;
        }
        if (event.id == 9002) {
            model.commit_training_room();
            close();
            return 0;
        }
        if (event.id == 9005) {
            open_selector();
            return 0;
        }
        if (event.id == 9020) {
            if (c.referenced_world()) {
                queue_world_advisor_help_action(c, 158);
            }
            return 0;
        }
        if (event.id == 9003) {
            open_context(model.state().training.room, { sender.rect.left, sender.rect.bottom });
            return 0;
        }
        if (event.id == 9012 || event.id == 9013) {
            auto* group = &sender;
            while (group && std::ranges::find(training_groups, group->id) == training_groups.end()) {
                group = group->parent;
            }
            if (!group) {
                return ui.secondary(sender, event);
            }
            const auto encoded = reinterpret_cast<std::uintptr_t>(group->context[0]);
            if (!encoded) {
                return 0;
            }
            const auto slot = static_cast<std::size_t>(encoded - 1);
            if (event.id == 9013) {
                model.remove_training_staff(slot);
                refresh_training();
            } else if (event.value == 1) {
                open_ability(slot);
            } else {
                close_ability();
                refresh_training();
            }
            return 0;
        }
    }
    return ui.secondary(sender, event);
}

void WorldStaffUiController::State::open_patrol() {
    close();
    if (host.dismiss) {
        host.dismiss();
    }
    auto* handler = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value);
    if (!handler || !handler->panel_open) {
        throw std::logic_error("Patrol modal requires the live StaffArea handler");
    }
    page = Page::patrol;
    page_target = handler->target;
    root = &parse(
        patrol_template,
        39500,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    caption(*root, text(handler->caption_row), 6);
    root->text_constraints = { true, true };
    root->cursor_id = 0;
    ui.set_focus(root);
    refresh_patrol();
}

void WorldStaffUiController::State::refresh_patrol() {
    if (page != Page::patrol || !root) {
        return;
    }
    const auto* handler = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value);
    if (!handler || !handler->panel_open) {
        close();
        return;
    }
    caption(*root, text(handler->caption_row), 6);
    ui.set_enabled(required(root, 39501), handler->confirm_enabled);
}

std::intptr_t WorldStaffUiController::State::patrol_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == root) {
        close_staff_area_selection_panel(c, false);
        return ui.secondary(sender, event);
    }
    if (escape(event) || (event.type == 256 && event.id == static_cast<std::uint32_t>(-2))) {
        close_staff_area_selection_panel(c, false);
        close();
        return 0;
    }
    if (event.type == 256 && event.id == 39501) {
        close_staff_area_selection_panel(c, true);
        close();
        return 0;
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
