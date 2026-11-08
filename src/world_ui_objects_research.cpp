#include "object_name_catalog.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_objects_internal.hpp"
#include <format>
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char research_template[] {
#embed "ui_templates/park_research.bin"
};
constexpr unsigned char picker_template[] {
#embed "ui_templates/park_research_picker.bin"
};
constexpr unsigned char workers_template[] {
#embed "ui_templates/park_research_edit.bin"
};
static_assert(sizeof(research_template) == 842 && sizeof(picker_template) == 522 && sizeof(workers_template) == 856);
constexpr std::array<std::uint32_t, 7> category_rows { 313, 315, 314, 316, 319, 318, 317 };
//Original93D0CC maps the simulation category to the numeric list value.
constexpr std::array<std::int32_t, 7> category_codes { 1, 3, 2, 5, 6, 0, 4 };

ResearchLabRecord& lab(WorldRuntime& c) {
    const auto* world = c.referenced_world();
    if (!world) {
        throw std::logic_error("Research UI has no primary World");
    }
    const auto id = world->saved.research_lab;
    const auto* record = c.entities.at(id).payload;
    if (!record || record->kind != ThingKind::research_lab) {
        throw std::logic_error("Research UI has no live Research Lab");
    }
    return *c.entities.payload<ResearchLabRecord>(id);
}

ResearchWorkSlot work_slot(std::int32_t slot) {
    if (slot < 0 || slot > 1) {
        throw std::out_of_range("Research UI work slot");
    }
    return static_cast<ResearchWorkSlot>(slot);
}

std::int32_t current_project(const ResearchLabRecord& owner, std::size_t slot) {
    return slot ? owner.secondary_current_project : owner.primary_current_project;
}

std::u16string localized(WorldRuntime& c, std::span<const std::byte> table, std::uint32_t row) {
    auto value = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383);
    if (!value) {
        throw std::runtime_error(std::format("Research UI localized row {}: {}", row, value.error().reason));
    }
    return std::move(*value);
}

StaffRuntimeRecord* staff(WorldRuntime& c, ThingId id, ThingId retiring = 0) {
    if (!id || id == retiring || id >= c.entities.size()) {
        return nullptr;
    }
    auto* record = c.entities[id].payload;
    if (!record || record->id != id || record->retirement_pending || !is_staff_record_kind(record->kind)) {
        return nullptr;
    }
    return reinterpret_cast<StaffRuntimeRecord*>(record);
}

std::u16string staff_name(const StaffRuntimeRecord& record) {
    return { record.name.begin(), std::ranges::find(record.name, char16_t {}) };
}

UiListRow worker_row(WorldRuntime& c, const StaffRuntimeRecord& record) {
    const auto maximum = c.staff.recruitment.balance.all.MaxExperience;
    if (!maximum) {
        throw std::domain_error("Research UI maximum staff experience is zero");
    }
    //573A80 uses an unsigned DWORD shift and division, without clamping.
    const auto experience = fixed::word((record.experience << 10) / static_cast<std::uint32_t>(maximum));
    return { record.base.header.id, { staff_name(record), experience } };
}

void title(UiRuntime& ui, UiControl& control, std::u16string_view text) {
    //Shared501D50 attaches title-font5 white text and its native primary shadow.
    control.font = 5;
    control.horizontal_alignment = 0;
    control.vertical_alignment = 1;
    control.primary_shadow_text = true;
    ui.set_text(control, text);
}

bool escape(const UiEvent& event) {
    return event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0;
}

std::optional<std::intptr_t> event_identity(const UiControl& control, const UiEvent& event) {
    if (event.value < 0 || static_cast<std::size_t>(event.value) >= control.rows.size()) {
        return {};
    }
    return control.rows[static_cast<std::size_t>(event.value)].identity;
}
} //namespace

void WorldParkObjectUiController::State::open_research() {
    if (page == Page::research && root) {
        return;
    }
    if (c.ui.command_menu_builder && c.ui.command_menu_builder->visible) {
        return;
    }
    if (!count_linked_staff_by_kind(c, std::to_underlying(ThingKind::researcher))) {
        message_text(text(328) + u"\n\n" + text(329));
        return;
    }
    (void)lab(c);
    begin(Page::research);
    root = &parse(research_template, 18000, panel_callback<&State::research_event>());
    title(ui, required(root, 18008), text(285));
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 6> { { { 18018, 290 }, { 18007, 286 }, { 18006, 288 }, { 18015, 287 }, { 18014, 289 }, { 18016, 294 } } }) {
        auto& control = required(root, id);
        caption(control, text(row), 7, 1, 1);
        control.text_constraints = { true, true };
    }
    //575AF3 stores native start100/end0. The public API accepts start/end.
    ui.set_range(required(root, 18009), 100, 0);
    refresh_research();
    stage_advisor(162);
    activate();
}

void WorldParkObjectUiController::State::refresh_research() {
    if (!root || page != Page::research) {
        return;
    }
    auto& owner = lab(c);
    for (std::size_t slot = 0; slot < 2; ++slot) {
        const auto project_index = current_project(owner, slot);
        auto& summary = required(root, slot ? 18013 : 18005);
        auto& details = required(root, slot ? 18012 : 18004);
        std::u16string heading, body;
        bool complete {};
        if (project_index < 0) {
            heading = text(291);
            body = text(306);
            details.color = { 200, 200, 200, 255 };
        } else {
            const auto& project = owner.projects.at(static_cast<std::size_t>(project_index));
            if (project.category != ResearchCategory::mission_obstacle) {
                heading = text(category_rows.at(static_cast<std::size_t>(project.category))) + u" : ";
            }
            heading += get_catalog_object_display_name(c, project.object_definition_id);
            complete = is_research_project_complete(&project);
            if (complete) {
                body = text(305);
                details.color = { 255, 230, 0, 255 };
            } else {
                const auto line = [&](std::uint32_t row, std::int32_t value) {
                    const std::array argument { BfstSignedIntegerArgument { 14, value } };
                    body += text(row, argument) + u'\n';
                };
                line(295, get_research_project_completion_percent(&project));
                const auto days = estimate_research_project_days_remaining(c, &owner, static_cast<ResearchWorkSlot>(slot));
                if (days < 0) {
                    body += text(292) + u'\n';
                } else {
                    line(296, days);
                }
                line(297, fixed::truncate_integer((static_cast<double>(project.cost) - project.research_done) * project.cash_per_brain_point));
                line(298, count_research_project_assigned_staff(&owner, static_cast<ResearchWorkSlot>(slot)));
                //576220 refreshes details before updating this cached consultant.
                if (const auto* consultant = staff(c, research_cached_consultants[slot], research_retiring)) {
                    const auto role = world_staff_type_for_used_thing(consultant->base.header);
                    const auto role_text = localized(c, c.messages.mail_language.staff_types, 2u * static_cast<std::uint32_t>(role));
                    const auto name = staff_name(*consultant);
                    const std::array arguments { BfstWideStringArgument { 15, role_text }, BfstWideStringArgument { 0, name } };
                    body += text(299, {}, arguments) + u'\n';
                }
                details.color = { 240, 240, 240, 255 };
            }
        }
        caption(summary, heading, 5, 0, 0);
        caption(details, body, 7, 0, 0);
        details.text_constraints.width = false;
        ui.set_enabled(required(root, slot ? 18015 : 18007), project_index >= 0 && !complete);
        ui.set_enabled(required(root, slot ? 18014 : 18006), project_index >= 0 && !complete);
    }
    for (std::size_t slot = 0; slot < 2; ++slot) {
        research_cached_projects[slot] = current_project(owner, slot);
        research_cached_consultants[slot] = owner.consultants[slot];
    }
    const auto workload = fixed::word(owner.workload);
    ui.set_value(required(root, 18009), workload);
    ui.set_attachment_part(required(root, 18010), static_cast<std::uint16_t>(fixed::word(owner.workload * 8u) / 100));
}

std::intptr_t WorldParkObjectUiController::State::research_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 2048 && event.id == 18009) {
        auto& owner = lab(c);
        set_world_research_lab_work_rate(&owner, static_cast<std::uint32_t>(event.value));
        ui.set_attachment_part(required(root, 18010), static_cast<std::uint16_t>(fixed::word(owner.workload * 8u) / 100));
        return 1;
    }
    if (event.type == 256) {
        match (event.id) {
            case 18006 or 18014 => {
                const auto slot = event.id == 18006 ? ResearchWorkSlot::primary : ResearchWorkSlot::secondary;
                message_text(text(334) + u"\n\n" + text(335), [this, slot] {
                    clear_research_lab_project_slot(c, &lab(c), slot);
                });
                return 1;
            }
            case 18007 or 18015 => {
                open_research_edit(event.id == 18007 ? 0 : 1);
                return 1;
            }
            case 18018 => {
                auto& owner = lab(c);
                for (std::int32_t slot = 0; slot < 2; ++slot) {
                    if (current_project(owner, static_cast<std::size_t>(slot)) < 0 || is_research_project_slot_complete(&owner, work_slot(slot))) {
                        if (is_research_project_slot_complete(&owner, work_slot(slot))) {
                            clear_research_lab_project_slot(c, &owner, work_slot(slot));
                        }
                        research_slot = slot;
                        open_research_picker();
                        return 1;
                    }
                }
                message_text(text(336) + u"\n\n" + text(337));
                return 1;
            }
            case 18019 => {
                help_advisor(162);
                return 1;
            }
            case 18001 or 18002 or 4294967294u => {
                close();
                return 1;
            }
            case _ => {
            }
        }
    }
    return common_event(sender, event);
}

void WorldParkObjectUiController::State::open_research_picker() {
    (void)work_slot(research_slot);
    begin(Page::research_picker);
    research_picker_initialized = false;
    root = &parse(picker_template, 18100, panel_callback<&State::research_picker_event>());
    title(ui, required(root, 18103), text(307));
    sublist = &required(root, 18104);
    constexpr std::array headings { 308u, 309u, 312u, 311u, 310u };
    constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::signed_integer, UiListCellFormat::text, UiListCellFormat::meter, UiListCellFormat::signed_currency };
    list(*sublist, headings, formats, research_sort_key);
    for (std::size_t i = 1; i < formats.size(); ++i) {
        sublist->column_presentations[i].horizontal_alignment = 1;
    }
    sublist->column_presentations[2].text_constraints = { true, true };
    sublist->column_presentations[3].meter = { "happygrad.tga", { 0, 8 } };
    for (auto* heading : sublist->children) {
        if (heading->id >= 16 && heading->id <= 20) {
            heading->color = { 255, 255, 255, 255 };
            for (std::size_t style = 0; style < 3; ++style) {
                heading->button_text_colors[style] = heading->color;
            }
            heading->help_index = 295u + heading->id - 16u;
        }
    }
    refresh_research_picker();
    if (!sublist->rows.empty()) {
        ui.select_list_row(*sublist, 0, false);
    }
    research_project = static_cast<std::int32_t>(ui.selected_list_identity(*sublist).value_or(-1));
    if (research_project >= 0) {
        const auto definition = lab(c).projects.at(static_cast<std::size_t>(research_project)).object_definition_id;
        const auto mapping = std::ranges::find(object_name_catalog::rows, definition, &object_name_catalog::Row::definition_id);
        caption(required(root, 18106), mapping == object_name_catalog::rows.end() ? std::u16string {} : localized(c, c.messages.mail_language.object_descriptions, mapping->description), 7, 0, 0);
        required(root, 18106).text_constraints.width = false;
    }
    ui.set_enabled(required(root, 18102), research_project >= 0);
    stage_advisor(490);
    activate();
}

void WorldParkObjectUiController::State::refresh_research_picker() {
    if (!root || !sublist || page != Page::research_picker) {
        return;
    }
    //574EC0 runs at construction, not each periodic tick. The original picker
    //retains its initial project catalog and list ordering while it is open.
    if (research_picker_initialized) {
        return;
    }
    research_picker_initialized = true;
    auto& owner = lab(c);
    if (owner.num_projects > owner.projects.size()) {
        throw std::length_error("Research UI project catalog exceeds capacity");
    }
    const auto slot = work_slot(research_slot);
    float maximum = 1.f;
    for (std::uint32_t i = 0; i < owner.num_projects; ++i) {
        if (is_research_project_selectable(c, slot, static_cast<std::int32_t>(i)) && owner.projects[i].cost > maximum) {
            maximum = owner.projects[i].cost;
        }
    }
    std::vector<UiListRow> rows;
    for (std::uint32_t i = 0; i < owner.num_projects; ++i) {
        if (!is_research_project_selectable(c, slot, static_cast<std::int32_t>(i))) {
            continue;
        }
        const auto& project = owner.projects[i];
        const auto role = map_research_staff_requirement_to_staff_type_index(project.staff_required);
        auto requirement = localized(c, role == StaffType::unsupported_index ? c.messages.mail_language.abilities : c.messages.mail_language.staff_types, role == StaffType::unsupported_index ? 0u : 2u * static_cast<std::uint32_t>(role));
        if (role != StaffType::unsupported_index && project.staff_ability != ThemeAbility::none) {
            requirement += u" - " + localized(c, c.messages.mail_language.abilities, static_cast<std::uint32_t>(project.staff_ability));
        }
        const auto category = static_cast<std::size_t>(project.category);
        UiListCell category_cell { category_codes.at(category) };
        category_cell.display_text = text(category_rows.at(category));
        rows.push_back({ static_cast<std::intptr_t>(i), { get_catalog_object_display_name(c, project.object_definition_id), std::move(category_cell), requirement, fixed::truncate_integer(static_cast<double>(project.cost) * 1024. / maximum), fixed::truncate_integer(static_cast<double>(project.cash_per_brain_point) * project.cost) } });
    }
    ui.set_list_rows(*sublist, std::move(rows), false, false);
}

std::intptr_t WorldParkObjectUiController::State::research_picker_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root || !sublist) {
        return ui.secondary(sender, event);
    }
    if (event.type == 1030) {
        research_sort_key = ui.list_sort_key(*sublist);
        return 1;
    }
    if (event.type == 1025) {
        research_project = static_cast<std::int32_t>(event_identity(*sublist, event).value_or(-1));
        if (research_project >= 0) {
            const auto definition = lab(c).projects.at(static_cast<std::size_t>(research_project)).object_definition_id;
            const auto mapping = std::ranges::find(object_name_catalog::rows, definition, &object_name_catalog::Row::definition_id);
            caption(required(root, 18106), mapping == object_name_catalog::rows.end() ? std::u16string {} : localized(c, c.messages.mail_language.object_descriptions, mapping->description), 7, 0, 0);
        }
        ui.set_enabled(required(root, 18102), research_project >= 0);
        return 1;
    }
    if (event.type == 1024 || (event.type == 256 && event.id == 18102)) {
        const auto selected = event.type == 1024 ? event_identity(*sublist, event) : ui.selected_list_identity(*sublist);
        if (!selected || *selected < 0) {
            return 1;
        }
        const auto project = static_cast<std::int32_t>(*selected);
        auto& owner = lab(c);
        const auto& requirement = owner.projects.at(static_cast<std::size_t>(project));
        const auto role = map_research_staff_requirement_to_staff_type_index(requirement.staff_required);
        research_consultant = role == StaffType::unsupported_index ? 0 : find_staff_matching_research_project_requirement(c, &owner, project);
        research_cached_projects.at(static_cast<std::size_t>(research_slot)) = project;
        research_cached_consultants.at(static_cast<std::size_t>(research_slot)) = research_consultant;
        if (role != StaffType::unsupported_index && !research_consultant) {
            const auto role_text = localized(c, c.messages.mail_language.staff_types, 2u * static_cast<std::uint32_t>(role));
            const auto ability = requirement.staff_ability == ThemeAbility::none ? std::u16string {} : localized(c, c.messages.mail_language.abilities, static_cast<std::uint32_t>(requirement.staff_ability));
            const std::array arguments { BfstWideStringArgument { 15, role_text }, BfstWideStringArgument { 17, ability } };
            const auto explanation = text(330) + u"\n\n" + text(331, {}, arguments);
            open_research_picker();
            message_text(explanation);
        } else {
            open_research_edit(research_slot, project);
        }
        return 1;
    }
    if (event.type == 256) {
        if (event.id == 18101 || event.id == 4294967294u) {
            close();
            open_research();
            return 1;
        }
        if (event.id == 18105) {
            help_advisor(490);
            return 1;
        }
    }
    return common_event(sender, event);
}

void WorldParkObjectUiController::State::open_research_edit(std::int32_t slot, std::int32_t project) {
    (void)work_slot(slot);
    auto& owner = lab(c);
    const bool from_picker = project >= 0;
    if (!from_picker) {
        project = current_project(owner, static_cast<std::size_t>(slot));
    }
    if (project < 0) {
        return;
    }
    (void)owner.projects.at(static_cast<std::size_t>(project));
    const auto consultant = from_picker ? research_consultant : owner.consultants.at(static_cast<std::size_t>(slot));
    begin(Page::research_edit);
    research_slot = slot;
    research_project = project;
    research_return_to_picker = from_picker;
    research_consultant = consultant;
    research_workers.clear();
    research_available.clear();
    research_identities.clear();
    const auto& selected = slot ? owner.secondary_staff : owner.primary_staff;
    const auto& opposite = slot ? owner.primary_staff : owner.secondary_staff;
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next) {
        const auto* record = entry->payload;
        if (!record || record->kind != ThingKind::researcher || record->retirement_pending || record->id == research_retiring || std::ranges::find(opposite, record->id) != opposite.end()) {
            continue;
        }
        research_identities.emplace(record->id, record);
        (std::ranges::find(selected, record->id) != selected.end() ? research_workers : research_available).push_back(record->id);
    }
    root = &parse(workers_template, 900100, panel_callback<&State::research_edit_event>());
    const auto name = get_catalog_object_display_name(c, owner.projects.at(static_cast<std::size_t>(project)).object_definition_id);
    const std::array name_argument { BfstWideStringArgument { 16, name } };
    title(ui, required(root, 900103), text(321, {}, name_argument));
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 2> { { { 900112, 322 }, { 900113, 323 } } }) {
        auto& heading = required(root, id);
        caption(heading, text(row), 8, 1, 1);
        heading.color = { 0, 0, 0, 255 };
        heading.text_constraints = { true, true };
    }
    constexpr std::array headings { 162u, 193u };
    constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::meter };
    for (const auto id : { 900109u, 900111u }) {
        auto& control = required(root, id);
        list(control, headings, formats, 0);
        for (auto* heading : control.children) {
            if (heading->id == 16 || heading->id == 17) {
                heading->color = { 255, 255, 255, 255 };
                for (std::size_t style = 0; style < 3; ++style) {
                    heading->button_text_colors[style] = heading->color;
                }
                heading->help_index = heading->id == 16 ? 15 : 22;
            }
        }
    }
    sublist = &required(root, 900109);
    //Build membership once. Periodic projection never discards staged moves.
    for (const auto id : research_available) {
        ui.insert_list_row(*sublist, worker_row(c, *staff(c, id)), {}, false);
    }
    auto& assigned = required(root, 900111);
    for (const auto id : research_workers) {
        ui.insert_list_row(assigned, worker_row(c, *staff(c, id)), {}, false);
    }
    if (!sublist->rows.empty()) {
        ui.select_list_row(*sublist, 0, false);
    }
    if (!assigned.rows.empty()) {
        ui.select_list_row(assigned, 0, false);
    }
    refresh_research_edit();
    stage_advisor(444);
    activate();
}

void WorldParkObjectUiController::open_research_assignment() {
    //Native5728C0 uses the Thinking Space panel's cached primary project and
    //consultant, including this debug caller. An unopened/invalid cache is an
    //approved missing-target failure, not an implicit choice of another project.
    auto& owner = lab(state_->c);
    const auto project = state_->research_cached_projects[0];
    if (project < 0 || static_cast<std::uint32_t>(project) >= owner.num_projects) {
        throw std::runtime_error("Research assignment has no valid cached primary project; open Research and select a project first");
    }
    state_->research_consultant = state_->research_cached_consultants[0];
    state_->open_research_edit(0, project);
    state_->research_return_to_picker = false;
}

void WorldParkObjectUiController::State::refresh_research_edit() {
    if (!root || page != Page::research_edit) {
        return;
    }
    const auto still_owned = [&](ThingId id) {
        const auto found = research_identities.find(id);
        const auto* record = staff(c, id, research_retiring);
        return found != research_identities.end() && record && &record->base.header == found->second;
    };
    std::erase_if(research_workers, [&](ThingId id) {
        return !still_owned(id);
    });
    std::erase_if(research_available, [&](ThingId id) {
        return !still_owned(id);
    });
    for (const auto id : { 900109u, 900111u }) {
        auto& control = required(root, id);
        const auto rows = control.rows;
        for (const auto& row : rows) {
            const auto worker = static_cast<ThingId>(row.identity);
            if (!still_owned(worker)) {
                ui.remove_list_row(control, row.identity, false);
            } else {
                const auto updated = worker_row(c, *staff(c, worker));
                ui.update_list_row(control, row.identity, updated.cells, 2, false);
            }
        }
    }
    auto& status_panel = required(root, 900105);
    auto& consultant_caption = required(&status_panel, 900106);
    if (!research_consultant) {
        caption(consultant_caption, text(293), 8, 0, 0);
        ui.set_depth(consultant_caption, 30);
    } else if (const auto* consultant = staff(c, research_consultant, research_retiring)) {
        const auto role = world_staff_type_for_used_thing(consultant->base.header);
        const auto role_text = localized(c, c.messages.mail_language.staff_types, 2u * static_cast<std::uint32_t>(role));
        const auto name = staff_name(*consultant);
        const auto ability = consultant->first_known_ability == ThemeAbility::none ? std::u16string {} : localized(c, c.messages.mail_language.abilities, static_cast<std::uint32_t>(consultant->first_known_ability));
        const std::array arguments { BfstWideStringArgument { 15, role_text }, BfstWideStringArgument { 0, name }, BfstWideStringArgument { 17, ability } };
        caption(consultant_caption, text(324, {}, arguments), 8, 0, 0);
    } else {
        caption(consultant_caption, {}, 8, 0, 0);
    }
    const auto count = count_research_project_assigned_staff(&lab(c), work_slot(1 - research_slot));
    const std::array count_argument { BfstSignedIntegerArgument { 14, count } };
    caption(required(&status_panel, 900107), count ? text(326, count_argument) : std::u16string {}, 8, 0, 0);
}

std::intptr_t WorldParkObjectUiController::State::research_edit_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(sender, event);
    }
    const bool add = (event.type == 256 && event.id == 900110) || (event.type == 1024 && event.id == 900109);
    const bool remove = (event.type == 256 && event.id == 900108) || (event.type == 1024 && event.id == 900111);
    if (add || remove) {
        auto& source = required(root, add ? 900109 : 900111);
        auto& destination = required(root, add ? 900111 : 900109);
        const auto selected = event.type == 1024 ? event_identity(source, event) : ui.selected_list_identity(source);
        if (selected) {
            const auto id = static_cast<ThingId>(*selected);
            const auto found = research_identities.find(id);
            const auto* record = staff(c, id, research_retiring);
            if (record && found != research_identities.end() && found->second == &record->base.header) {
                ui.insert_list_row(destination, worker_row(c, *record), {}, false);
                if (destination.rows.size() == 1) {
                    ui.select_list_row(destination, 0, false);
                }
                ui.remove_list_row(source, *selected, false);
                std::erase(add ? research_available : research_workers, id);
                (add ? research_workers : research_available).push_back(id);
            }
        }
        return 1;
    }
    if (event.type == 256) {
        if (event.id == 900104) {
            help_advisor(444);
            return 1;
        }
        if (event.id == 900101 || event.id == 4294967294u) {
            if (research_return_to_picker) {
                open_research_picker();
            } else {
                close();
                open_research();
            }
            return 1;
        }
        if (event.id == 900102) {
            refresh_research_edit();
            auto& assigned = required(root, 900111);
            if (assigned.rows.empty()) {
                const auto slot = work_slot(research_slot);
                message_text(text(334) + u"\n\n" + text(333), [this, slot] {
                    //573C00 reopens the main panel before clearing the live slot.
                    close();
                    open_research();
                    clear_research_lab_project_slot(c, &lab(c), slot);
                });
                return 1;
            }
            //Native573CD0 overflows its20-ID scratch buffer with surplus rows.
            //Keep every candidate visible; diagnose this invalid commit explicitly.
            if (assigned.rows.size() > 20) {
                throw std::length_error("Research assignment exceeds the saved20-worker slot");
            }
            std::array<ThingId, 20> ids {};
            std::size_t destination {};
            for (const auto& row : std::views::reverse(assigned.rows)) {
                ids[destination++] = static_cast<ThingId>(row.identity);
            }
            auto& owner = lab(c);
            if (research_project >= 0) {
                auto* world = c.referenced_world();
                auto* header = c.entities.at(world->saved.tag_system).payload;
                if (!header || header->kind != ThingKind::tag_system) {
                    throw std::logic_error("Research UI has no live Tag System");
                }
                mark_tag_messages_by_primary_key_for_deletion(c, reinterpret_cast<TagSystemRecord*>(header), 20);
                set_research_lab_current_project_for_slot(c, &owner, research_project, work_slot(research_slot));
                owner.consultants.at(static_cast<std::size_t>(research_slot)) = research_consultant;
                set_research_slot_worker_ids(c, &owner, work_slot(research_slot), ids);
            }
            close();
            open_research();
            return 1;
        }
    }
    if (escape(event)) {
        close();
        return 1;
    }
    return common_event(sender, event);
}
} //namespace simcoaster
