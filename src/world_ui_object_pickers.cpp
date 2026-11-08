#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char promotion_template[] = {
#embed "ui_templates/park_promotion_picker.bin"
};
constexpr unsigned char upgrade_template[] = {
#embed "ui_templates/park_upgrades.bin"
};
constexpr unsigned char special_template[] = {
#embed "ui_templates/park_special_ride.bin"
};

std::u16string localized(WorldRuntime& c, std::span<const std::byte> table, std::uint32_t row) {
    auto result = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383);
    if (!result) {
        throw std::runtime_error("Original special-ride caption: " + result.error().reason);
    }
    return std::move(*result);
}
} //namespace

void WorldParkObjectUiController::State::open_promotion(ThingId id, bool return_to_settings) {
    begin(Page::promotion);
    model.open_promotion(id, return_to_settings);
    page_target = id;
    root = &parse(promotion_template, 24000, panel_callback<&State::promotion_event>());
    label(24009, 46, 5);
    required(root, 24009).primary_shadow_text = true;
    sublist = &required(root, 24008);
    constexpr std::array headings { 347u };
    constexpr std::array formats { UiListCellFormat::text };
    list(*sublist, headings, formats, model.state().promotion.sort_key);
    ui.set_selected(required(root, 24004), 24005);
    refresh_promotion();
    stage_advisor(425);
    activate();
}

void WorldParkObjectUiController::State::refresh_promotion() {
    auto& view = model.state().promotion;
    if (!view.open || !root || !sublist) {
        return;
    }
    std::vector<UiListRow> rows;
    for (const auto& item : view.rows) {
        UiListRow row { item.object, { item.name } };
        row.color = item.color;
        rows.push_back(std::move(row));
    }
    ui.set_list_rows(*sublist, std::move(rows), true, false);
    const auto selected = ui.find_list_row(*sublist, view.selected);
    ui.select_list_row(*sublist, selected ? static_cast<std::int32_t>(*selected) : -1, false, false);
    ui.set_enabled(required(root, 24002), selected.has_value());
}

std::intptr_t WorldParkObjectUiController::State::promotion_event(UiControl& control, const UiEvent& event) {
    if (closing || refreshing || !root || !sublist) {
        return ui.secondary(control, event);
    }
    auto& view = model.state().promotion;
    if (event.type == 257 && event.value == 1 && event.id >= 24005 && event.id <= 24007) {
        constexpr std::array categories { ObjectUiCategory::rides, ObjectUiCategory::shops, ObjectUiCategory::sideshows };
        model.filter_promotion(categories[event.id - 24005]);
        refresh_page();
        return 1;
    }
    if (event.type == 1030) {
        model.sort_promotion(ui.list_sort_key(*sublist));
        return 1;
    }
    if (event.type == 1025) {
        view.selected = static_cast<ThingId>(ui.selected_list_identity(*sublist).value_or(0));
        ui.set_enabled(required(root, 24002), view.selected != 0);
        return 1;
    }
    if (event.type == 1026) {
        const auto selected = static_cast<ThingId>(ui.selected_list_identity(*sublist).value_or(0));
        if (selected) {
            close();
            if (host.focus_object) {
                host.focus_object(selected);
            }
        }
        return 1;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == 24002)) {
        const auto selected = static_cast<ThingId>(ui.selected_list_identity(*sublist).value_or(0));
        if (selected) {
            const auto desk = view.info_desk;
            const bool back = view.return_to_settings;
            model.choose_promotion(selected);
            if (back) {
                open_settings(desk);
                model.state().settings.staged.promoted_attraction = selected;
                model.refresh_settings();
                refresh_page();
            } else {
                close();
            }
        }
        return 1;
    }
    if (event.type == 256 && event.id == 24010) {
        help_advisor(425);
        return 1;
    }
    if (event.type == 256 && (event.id == 24001 || event.id == 4294967294u)) {
        close();
        return 1;
    }
    return common_event(control, event);
}

void WorldParkObjectUiController::State::open_upgrade(ThingId id) {
    //The trained-mechanic gate precedes dismissal of the current panel.
    model.open_upgrade(id);
    if (!model.state().upgrade.open) {
        if (const auto warning = model.state().upgrade.message_row) {
            //556E30 supplies title10/body11 and one b_okay acknowledgement.
            message_text(text(10) + u"\n\n" + text(*warning), {}, UiMessageButtonKind::okay);
        }
        return;
    }
    begin(Page::upgrades);
    model.open_upgrade(id);
    page_target = id;
    root = &parse(upgrade_template, 19000, panel_callback<&State::upgrade_event>());
    label(19003, 529, 5);
    required(root, 19003).primary_shadow_text = true;
    sublist = &required(root, 19002);
    constexpr std::array headings { 347u, 277u };
    constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::signed_currency };
    list(*sublist, headings, formats, model.state().upgrade.sort_key);
    sublist->column_presentations[1].zero_blank = true;
    sublist->column_presentations[1].horizontal_alignment = 2;
    //Native595680 does not stage an Advisor message; destruction still
    //cancels172.
    advisor_action = 172;
    refresh_upgrade();
    activate();
}

void WorldParkObjectUiController::State::refresh_upgrade() {
    auto& view = model.state().upgrade;
    if (!view.open || !root || !sublist) {
        return;
    }
    std::vector<UiListRow> rows;
    for (const auto& item : view.rows) {
        rows.push_back({ item.key, { item.name, item.cost } });
    }
    ui.set_list_rows(*sublist, std::move(rows), true, false);
    const auto selected = ui.selected_list_identity(*sublist);
    const auto row = selected ? std::ranges::find(view.rows, static_cast<std::int32_t>(*selected), &ParkObjectUpgradeUiRow::key) : view.rows.end();
    ui.set_enabled(required(root, 19001), row != view.rows.end() && row->affordable && row->kind != ParkObjectUpgradeKind::information);
}

std::intptr_t WorldParkObjectUiController::State::upgrade_event(UiControl& control, const UiEvent& event) {
    if (closing || refreshing || !root || !sublist) {
        return ui.secondary(control, event);
    }
    auto& view = model.state().upgrade;
    if (event.type == 1030) {
        view.sort_key = ui.list_sort_key(*sublist);
        return 1;
    }
    if (event.type == 1025) {
        view.selected = static_cast<std::int32_t>(ui.selected_list_identity(*sublist).value_or(0));
        refresh_page();
        return 1;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == 19001)) {
        if (const auto selected = ui.selected_list_identity(*sublist)) {
            if (model.apply_upgrade(static_cast<std::int32_t>(*selected))) {
                close();
            }
        }
        return 1;
    }
    if (event.type == 256 && event.id == 19005) {
        help_advisor(172);
        return 1;
    }
    if (event.type == 256 && (event.id == 19004 || event.id == 4294967294u)) {
        close();
        return 1;
    }
    return common_event(control, event);
}

void WorldParkObjectUiController::State::open_special(ThingId id) {
    begin(Page::special);
    model.open_special(id);
    page_target = id;
    root = &parse(special_template, 20000, panel_callback<&State::special_event>());
    const auto& view = model.state().special;
    const std::array name { BfstWideStringArgument { 0, view.name } };
    caption(required(root, 20013), text(116, {}, name), 5); //5864BA: TITLE.
    caption(required(root, 20004), text(984), 8);
    required(root, 20004).horizontal_alignment = 1;
    label(20008, 234, 8);
    caption(required(root, 20009), localized(c, c.messages.mail_language.ui_staff_states, 8), 8);
    label(20010, 983, 8);
    auto& explanation = required(root, 20011);
    caption(explanation, text(115), 8);
    explanation.text_constraints = { true, false };
    sublist = &required(root, 20006);
    constexpr std::array headings { 234u, 0u, 983u };
    constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::text, UiListCellFormat::text };
    list(*sublist, headings, formats, 3);
    auto& preview_control = required(root, 20012);
    preview_control.help_index = 301;
    ui.set_primary_callback(preview_control, [this](auto& sender, const auto& event) -> std::intptr_t {
        if (event.type == 13) {
            return static_cast<std::intptr_t>(sender.render_depth + 10);
        }
        if (event.type == 65542 && host.focus_object) {
            host.focus_object(page_target);
        }
        if (event.type == 20) {
            previews.erase(&sender);
        }
        return ui.secondary(sender, event);
    });
    refresh_special();
    stage_advisor(422);
    activate();
}

void WorldParkObjectUiController::State::refresh_special() {
    auto& view = model.state().special;
    if (!view.open || !root || !sublist) {
        return;
    }
    ui.set_enabled(required(root, 20004), view.can_activate);
    ui.set_visible(required(root, 20011), !view.abilities_activated);
    if (view.ticket_cost) {
        const std::array arguments { BfstSignedIntegerArgument { 50, view.ticket_cost }, BfstSignedIntegerArgument { 51, view.ticket_balance } };
        caption(required(root, 20007), text(view.ticket_cost == 1 ? 986u : 987u, arguments), 7);
    }
    std::vector<UiListRow> rows;
    for (const auto& item : view.requirements) {
        const auto known = item.ability_activated;
        auto employee = item.name;
        if (!item.staff) {
            employee = u" " + localized(c, c.messages.mail_language.abilities, 0);
        }
        rows.push_back({ static_cast<std::intptr_t>(item.slot), { item.role, known ? item.ability_name : text(114), std::move(employee) } });
    }
    //58689D/5873EB and the two native forwarding wrappers all use55E020:
    //rebuilding clears selection and restores employee-column ascending.
    ui.clear_list_rows(*sublist, false);
    ui.set_list_sort_key(*sublist, 3, false);
    ui.set_list_rows(*sublist, std::move(rows), false, false);
    preview(required(root, 20012), view.object);
}

std::intptr_t WorldParkObjectUiController::State::special_event(UiControl& control, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(control, event);
    }
    if (event.type == 256 && event.id == 20004) {
        if (model.activate_special()) {
            close();
        }
        return 1;
    }
    if (event.type == 256 && event.id == 20002) {
        help_advisor(422);
        return 1;
    }
    if (event.type == 256 && (event.id == 20001 || event.id == 4294967294u)) {
        close();
        return 1;
    }
    return common_event(control, event);
}
} //namespace simcoaster
