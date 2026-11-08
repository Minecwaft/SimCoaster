#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_staff_internal.hpp"
#include "world_ui_staff_columns.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char hire_template[] {
#embed "ui_templates/staff_hire.bin"
};
constexpr unsigned char list_template[] {
#embed "ui_templates/staff_list.bin"
};
constexpr unsigned char locator_template[] {
#embed "ui_templates/staff_locator.bin"
};
static_assert(sizeof(hire_template) == 846 && sizeof(list_template) == 1048 && sizeof(locator_template) == 406);
//All arrays are indexed by StaffType, independently of each template's
//visual role order. Gardener precedes Guard and Researcher in the tabs.
constexpr std::array<std::uint32_t, 6> hire_buttons { 4005, 4006, 4007, 4009, 4010, 4008 };
constexpr std::array<std::uint32_t, 6> list_buttons { 30014, 30015, 30016, 30018, 30019, 30017 };
constexpr std::array<std::uint32_t, 6> locator_buttons { 32004, 32003, 32007, 32006, 32008, 32005 };
constexpr std::array<std::uint32_t, 6> role_headings { 245, 250, 247, 246, 249, 248 };
constexpr std::array<std::uint32_t, 6> happiness_headings { 239, 240, 242, 241, 243, 244 };

bool concrete(StaffType role) {
    return role >= StaffType::handyman && role <= StaffType::gardener;
}

std::optional<StaffType> role_for(const auto& buttons, std::uint32_t id) {
    const auto found = std::ranges::find(buttons, id);
    return found == buttons.end() ? std::nullopt : std::optional { static_cast<StaffType>(found - buttons.begin()) };
}

std::optional<std::intptr_t> event_identity(const UiControl& list, const UiEvent& event) {
    if (event.value < 0 || static_cast<std::size_t>(event.value) >= list.rows.size()) {
        return std::nullopt;
    }
    return list.rows[static_cast<std::size_t>(event.value)].identity;
}

StaffRuntimeRecord* live_staff(WorldRuntime& c, ThingId id) {
    if (!id || id >= c.entities.size()) {
        return nullptr;
    }
    auto* record = c.entities[id].payload;
    return record && !record->retirement_pending && is_staff_record_kind(record->kind) ? reinterpret_cast<StaffRuntimeRecord*>(record) : nullptr;
}

void queue_advisor(WorldRuntime& c, std::int32_t action) {
    auto* world = c.referenced_world();
    if (!world) {
        return;
    }
    const auto id = world->saved.advisor;
    if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != ThingKind::advisor) {
        throw std::logic_error("Staff panel Advisor owner is absent");
    }
    const auto candidate = world_operations::make_advisor_action_candidate(c, action);
    (void)world_operations::queue_advisor_action_candidate(c, c.entities.payload<AdvisorRecord>(id), candidate);
}
} //namespace

void WorldStaffUiController::State::open_hire(StaffType role) {
    //Native53F840 does nothing while its singleton window already exists,
    //even if another opener supplies a different initial category.
    if (page == Page::hire && ui.owns(root)) {
        return;
    }
    //Native53F840 leaves the current panel untouched in Coaster Edit Mode.
    if (c.mode.game == GameMode::code_3) {
        return;
    }
    begin(Page::hire);
    model.open_hire(role);
    root = &parse(
        hire_template,
        4000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    auto& rows = required(root, 4011);
    list(rows, StaffHireColumns::headings, StaffHireColumns::formats, model.state().hire.sort_key, 7);
    rows.column_presentations[1].meter = { "ridestatbar.tga", { 10, 10 } };
    rows.column_presentations[2].horizontal_alignment = 1;
    constexpr std::array<std::uint32_t, 4> help { 215, 216, 217, 218 };
    for (std::size_t i = 0; i < StaffHireColumns::size; ++i) {
        auto& header = required(&rows, static_cast<std::uint32_t>(16 + i));
        header.font = 8;
        header.help_index = help[i];
    }
    auto& message_control = required(root, 4012);
    caption(message_control, {}, 7);
    message_control.text_constraints = { true, true };
    hire_candidate_identities.clear();
    refresh_hire();
    stage_advisor(443);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_hire() {
    if (page != Page::hire || !root || !model.state().hire.open) {
        return;
    }
    const auto& view = model.state().hire;
    auto& rows = required(root, 4011);
    std::vector<UiListRow> data;
    std::map<std::size_t, StaffCandidate> identities;
    auto* world = c.referenced_world();
    if (!world) {
        throw std::logic_error("Staff hiring has no World owner");
    }
    for (const auto& row : view.rows) {
        const auto& candidate = world->staff_candidates.at(row.slot);
        if (!candidate.valid || candidate.type != view.filter) {
            continue;
        }
        identities.emplace(row.slot, candidate);
        data.push_back({ static_cast<std::intptr_t>(row.slot), StaffHireColumns::cells(row) });
    }
    hire_candidate_identities = std::move(identities);
    ui.set_list_rows(rows, std::move(data), true, false);
    ui.set_list_sort_key(rows, view.sort_key, false);
    const auto selected = view.selected_slot >= 0 ? ui.find_list_row(rows, view.selected_slot) : std::nullopt;
    ui.select_list_row(rows, selected ? static_cast<std::int32_t>(*selected) : -1, false, false);
    for (std::size_t i = 0; i < hire_buttons.size(); ++i) {
        ui.set_enabled(required(root, hire_buttons[i]), view.available[i]);
    }
    ui.set_selected(required(root, 4004), concrete(view.filter) ? static_cast<std::int32_t>(hire_buttons[static_cast<std::size_t>(view.filter)]) : -1, false);
    caption(required(root, 4014), text(view.heading_row), 7);
    auto& message_control = required(root, 4012);
    ui.set_visible(message_control, view.message_row != 0);
    if (view.message_row) {
        caption(message_control, text(view.message_row), 7);
    }
    ui.set_enabled(required(root, 4002), selected.has_value());
    auto& surface = required(root, 4013);
    if (selected) {
        candidate_preview(surface, static_cast<std::size_t>(view.selected_slot));
    } else {
        previews.erase(&surface);
    }
}

void WorldStaffUiController::State::open_list(StaffType role, ThingId selected) {
    //Native51FF90 refuses to replace the active UI if no employee exists.
    if (std::ranges::none_of(c.entities, [](const auto& slot) {
            return slot.payload && !slot.payload->retirement_pending && is_staff_record_kind(slot.payload->kind);
        })) {
        return;
    }
    begin(Page::list);
    model.open_list(role, selected);
    root = &parse(
        list_template,
        30000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    auto& rows = required(root, 30012);
    list(rows, StaffListColumns::headings, StaffListColumns::formats, model.state().list.sort_key, 7);
    rows.column_presentations[1].meter = { "ridestatbar.tga", { 0, 10 } };
    rows.column_presentations[2].horizontal_alignment = 2;
    rows.column_presentations[3].horizontal_alignment = 1;
    rows.column_presentations[3].text_constraints = { true, true };
    rows.column_presentations[4].meter = { "happygrad.tga", { 0, 10 } };
    rows.column_presentations[5].meter = { "ridestatbar.tga", { 0, 10 } };
    constexpr std::array<std::uint32_t, 6> help { 15, 22, 23, 25, 26, 17 };
    for (std::size_t i = 0; i < StaffListColumns::size; ++i) {
        auto& header = required(&rows, static_cast<std::uint32_t>(16 + i));
        header.font = 8;
        header.help_index = help[i];
    }
    meter(required(root, 30004), "happygrad.tga", 8);
    meter(required(root, 30007), "happygrad.tga", 8);
    caption(required(root, 30005), {}, 6, 0, 1);
    caption(required(root, 30006), text(238), 6, 0, 1);
    caption(required(root, 30009), text(192), 6, 0, 1);
    caption(required(root, 30010), {}, 6, 0, 1);
    ui.set_periodic(*root, 3, UiPeriodicEvent { 16, 524419, 0, std::chrono::milliseconds { 2000 } });
    refresh_list();
    //Optional preselection is an employee identity, resolved AFTER sorting.
    if (selected) {
        if (const auto index = ui.find_list_row(rows, selected)) {
            ui.select_list_row(rows, static_cast<std::int32_t>(*index), false, true);
        }
    }
    stage_advisor(234);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_list() {
    if (page != Page::list || !root || !model.state().list.open) {
        return;
    }
    const auto& view = model.state().list;
    auto& rows = required(root, 30012);
    std::vector<UiListRow> data;
    for (const auto& row : view.rows) {
        if (live_staff(c, row.staff)) {
            data.push_back({ row.staff, StaffListColumns::cells(row) });
        }
    }
    //Native51F403 pushes byte32h to85602C: replace columns1/4/5 in
    //place. Metrics refresh never re-sorts the visible row records.
    project_rows(rows, std::move(data), 0x32u);
    if (ui.list_sort_key(rows) != view.sort_key) {
        ui.set_list_sort_key(rows, view.sort_key, false);
    }
    const auto selected = view.selected ? ui.find_list_row(rows, view.selected) : std::nullopt;
    ui.select_list_row(rows, selected ? static_cast<std::int32_t>(*selected) : -1, false, false);
    for (std::size_t i = 0; i < list_buttons.size(); ++i) {
        ui.set_enabled(required(root, list_buttons[i]), view.available[i]);
    }
    if (concrete(view.filter)) {
        const auto index = static_cast<std::size_t>(view.filter);
        ui.set_selected(required(root, 30013), static_cast<std::int32_t>(list_buttons[index]), false);
        caption(required(root, 30011), text(role_headings[index]), 7);
        caption(required(root, 30005), text(happiness_headings[index]), 6, 0, 1);
    } else {
        ui.set_selected(required(root, 30013), -1, false);
        caption(required(root, 30011), {}, 7);
        caption(required(root, 30005), {}, 6, 0, 1);
    }
    ui.set_value(required(root, 30004), view.role_happiness_meter);
    ui.set_value(required(root, 30007), view.all_happiness_meter);
    caption(required(root, 30010), view.status, 6, 0, 1);
    ui.set_visible(required(root, 30010), selected.has_value());
    ui.set_enabled(required(root, 30002), selected.has_value());
}

void WorldStaffUiController::State::open_locator(ThingId target) {
    if (target && !live_staff(c, target)) {
        return;
    }
    begin(Page::locator);
    page_target = target;
    model.open_locator(target);
    root = &parse(
        locator_template,
        32000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    caption(*root, {}, 6);
    root->text_constraints = { true, true };
    refresh_locator();
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_locator() {
    if (page != Page::locator || !root || !model.state().locator.open) {
        return;
    }
    const auto& view = model.state().locator;
    page_target = view.target;
    caption(*root, view.caption, 6);
    for (std::size_t i = 0; i < locator_buttons.size(); ++i) {
        ui.set_enabled(required(root, locator_buttons[i]), view.available[i]);
    }
    ui.set_selected(required(root, 32002), view.target && concrete(view.filter) ? static_cast<std::int32_t>(locator_buttons[static_cast<std::size_t>(view.filter)]) : -1, false);
    ui.set_enabled(required(root, 32009), view.target != 0);
}

std::intptr_t WorldStaffUiController::State::roster_event(UiControl& sender, const UiEvent& event) {
    if (refreshing || closing || !root) {
        return ui.secondary(sender, event);
    }
    const auto close_id = page == Page::hire ? 4001u : page == Page::list ? 30001u : 32001u;
    if (event.type == 5 || (event.type == 256 && (event.id == close_id || event.id == static_cast<std::uint32_t>(-2)))) {
        close();
        return 0;
    }
    if (page == Page::hire) {
        if (event.type == 257 && event.value == 1) {
            if (const auto role = role_for(hire_buttons, event.id)) {
                model.open_hire(*role);
                //Native clear_ui_list_rows resets first-visible to zero on a
                //category rebuild; live candidate updates retain the current scroll.
                ui.set_list_scroll_offset(required(root, 4011), 0, false);
                refresh_hire();
                return 0;
            }
        }
        if (event.type == 1030 && event.id == 4011) {
            model.sort_hire(static_cast<std::int32_t>(event.value));
            refresh_hire();
            return 0;
        }
        if (event.type == 256 && event.id == 4015) {
            queue_world_advisor_help_action(c, 443);
            return 0;
        }
        const bool hire = event.type == 1024 || (event.type == 256 && event.id == 4002);
        if (hire || event.type == 1025) {
            auto& rows = required(root, 4011);
            const auto identity = event.type == 256 ? ui.selected_list_identity(rows) : event_identity(rows, event);
            if (!identity || *identity < 0) {
                return 0;
            }
            const auto slot = static_cast<std::size_t>(*identity);
            const auto original = hire_candidate_identities.find(slot);
            auto* world = c.referenced_world();
            if (!world || slot >= world->staff_candidates.size() || original == hire_candidate_identities.end()) {
                return 0;
            }
            const auto& candidate = world->staff_candidates[slot];
            if (!candidate.valid || !matches_staff_hire_snapshot(candidate, original->second)) {
                model.refresh();
                refresh_hire();
                return 0;
            }
            model.select_hire(slot); //Protect this exact candidate against expiry.
            if (hire && model.hire_selected()) {
                close(); //Release previews before candidate-slot/placement reuse.
                queue_advisor(c, 254);
            } else {
                refresh_hire();
            }
            return 0;
        }
    } else if (page == Page::list) {
        if (event.type == 257 && event.value == 1) {
            if (const auto role = role_for(list_buttons, event.id)) {
                model.open_list(*role);
                refresh_list();
                return 0;
            }
        }
        if (event.type == 1030 && event.id == 30012) {
            model.sort_list(static_cast<std::int32_t>(event.value));
            refresh_list();
            return 0;
        }
        if (event.type == 16 && event.id == 524419) {
            model.refresh();
            refresh_list();
            return 0;
        }
        if (event.type == 256 && event.id == 30020) {
            queue_world_advisor_help_action(c, 234);
            return 0;
        }
        const bool details = event.type == 1024 || (event.type == 256 && event.id == 30002);
        if (details || event.type == 1025 || event.type == 1026) {
            auto& rows = required(root, 30012);
            const auto identity = event.type == 256 ? ui.selected_list_identity(rows) : event_identity(rows, event);
            const auto id = identity && *identity > 0 && *identity < static_cast<std::intptr_t>(thing_slot_count) ? static_cast<ThingId>(*identity) : ThingId {};
            if (id && !live_staff(c, id)) {
                model.refresh();
                refresh_list();
                return 0;
            }
            model.select_list(id);
            if (details && id) {
                open_details(id);
            } else if (event.type == 1026 && id) {
                //Native503730 performs a one-shot focus and closes the list.
                //The owning camera bridge applies the actual render interpolation.
                if (host.focus_staff) {
                    host.focus_staff(id, false);
                }
                close();
            } else {
                refresh_list();
            }
            return 0;
        }
    } else if (page == Page::locator) {
        if ((event.type == 257 && event.value == 1) || event.type == 258) {
            if (const auto role = role_for(locator_buttons, event.id)) {
                model.cycle_locator(*role, event.type == 258);
                refresh_locator();
                return 0;
            }
        }
        if (event.type == 30) {
            const auto target = model.state().locator.target;
            if (target && (c.view.flags & 4096u) && host.focus_staff) {
                host.focus_staff(target, true);
            }
        } else if (event.type == 256 && event.id == 32009) {
            if (const auto id = model.state().locator.target) {
                open_context(id, { sender.rect.left, sender.rect.bottom }, true);
            }
            return 0;
        } else if (event.type == 69638) {
            if (const auto id = model.state().locator.target) {
                open_details(id);
            }
            return 0;
        } else if (event.type == 65546 || event.type == 65547) {
            const auto key = event.id > 255 ? event.id >> 8 : event.id;
            const auto modifiers = (static_cast<std::uint64_t>(event.value) >> 16) & 255u;
            //9A86A0 uses wildcard modifiers for Up/Down and numeric-pad
            //panning, but exact modifier0 for Home/Left/Right/Add/Subtract.
            if (event.type == 65546 && modifiers && (key == 36 || key == 37 || key == 39 || key == 107 || key == 109)) {
                return ui.secondary(sender, event);
            }
            if (host.camera_key && host.camera_key(event)) {
                return 0;
            }
        }
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
