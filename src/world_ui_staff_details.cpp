#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_staff_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char details_template[] {
#embed "ui_templates/staff_details.bin"
};
constexpr unsigned char finance_template[] {
#embed "ui_templates/staff_finance.bin"
};
constexpr unsigned char activity_template[] {
#embed "ui_templates/staff_activity.bin"
};
static_assert(sizeof(details_template) == 1240 && sizeof(finance_template) == 1438 && sizeof(activity_template) == 950);

struct FinanceRow {
    std::uint32_t stepper, caption, text;
};

//StaffType order, independent of the original template's visual row order.
constexpr std::array<FinanceRow, 6> finance_rows { { { 31011, 31017, 266 }, { 31010, 31016, 265 }, { 31009, 31015, 269 }, { 31013, 31020, 268 }, { 31007, 31014, 270 }, { 31012, 31018, 267 } } };

struct ActivityRow {
    std::uint32_t label, percentage, meter, state_row;
};

//Native588810: Idle followed by the six staff activity-ring channels.
constexpr std::array<ActivityRow, 7> activity_rows { { { 0x3E8A, 0x3E8B, 0x3E83, 0 }, { 0x3E96, 0x3E97, 0x3E89, 2 }, { 0x3E8E, 0x3E8F, 0x3E85, 1 }, { 0x3E92, 0x3E93, 0x3E87, 3 }, { 0x3E94, 0x3E95, 0x3E88, 8 }, { 0x3E90, 0x3E91, 0x3E86, 7 }, { 0x3E8C, 0x3E8D, 0x3E84, 4 } } };

StaffRuntimeRecord* live_staff(WorldRuntime& c, ThingId id) {
    if (!id || id >= c.entities.size()) {
        return nullptr;
    }
    auto* record = c.entities[id].payload;
    return record && !record->retirement_pending && is_staff_record_kind(record->kind) ? reinterpret_cast<StaffRuntimeRecord*>(record) : nullptr;
}

std::u16string_view staff_name(const StaffRuntimeRecord& staff) {
    const auto end = std::ranges::find(staff.name, char16_t {});
    return { staff.name.data(), static_cast<std::size_t>(end - staff.name.begin()) };
}

bool escape(const UiEvent& event) {
    return event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0;
}

std::optional<std::size_t> finance_row(std::uint32_t stepper) {
    const auto found = std::ranges::find(finance_rows, stepper, &FinanceRow::stepper);
    return found == finance_rows.end() ? std::nullopt : std::optional<std::size_t>(found - finance_rows.begin());
}

void finance_edit_state(UiRuntime& ui, UiControl& stepper, bool visible, bool focus = false) {
    auto* edit = stepper.associated_edit;
    if (!ui.owns(edit)) {
        throw std::logic_error("Staff Finance stepper has no bound edit child");
    }
    ui.set_visible(*edit, visible);
    ui.set_enabled(*edit, visible);
    if (!visible && ui.focus() == edit) {
        ui.set_focus(nullptr);
    } else if (visible && focus) {
        ui.set_focus(edit);
    }
}

//501D50 installs the primary shadow text caption with game font5.
template <class State>
void panel_caption(State& s, UiControl& control, std::u16string_view value) {
    s.caption(control, value, 5, 0, 1);
    control.primary_shadow_text = true;
}
} //namespace

void WorldStaffUiController::State::open_details(ThingId id) {
    if (!live_staff(c, id)) {
        return;
    }
    begin(Page::details);
    page_target = id;
    model.open_details(id);
    root = &parse(
        details_template,
        7000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 8> labels { { { 7016, 190 }, { 7023, 197 }, { 7019, 198 }, { 7014, 193 }, { 7015, 195 }, { 7018, 196 }, { 7013, 194 }, { 7021, 192 } } };
    for (const auto& [control, row] : labels) {
        caption(required(root, control), text(row), 6, 0, 1);
    }
    for (const auto control : { 7017u, 7022u, 7024u, 7020u }) {
        caption(required(root, control), {}, 6, 0, 1);
    }
    meter(required(root, 7010), "happygrad.tga");
    for (const auto control : { 7009u, 7008u, 7011u }) {
        meter(required(root, control));
    }
    auto& thought = required(root, 7027);
    caption(thought, {}, 6);
    thought.text_constraints = { true, true };
    auto& wage_caption = required(root, 7025);
    caption(wage_caption, text(191), 6);
    wage_caption.text_constraints = { true, true };
    auto& salary = required(root, 7006);
    const auto& balance = c.staff.recruitment.balance.all;
    ui.set_range(salary, balance.MinWage, balance.MaxWage);
    salary.step_size = balance.WageStep;
    affixed_edit(salary, u"$ ");
    auto& name = required(root, 7029);
    panel_caption(*this, name, {});
    ui.set_primary_callback(name, panel_callback<&State::details_event>());
    auto& surface = required(root, 7026);
    surface.help_index = 302;
    ui.set_primary_callback(surface, panel_callback<&State::details_event>());
    ui.set_periodic(*root, 0, UiPeriodicEvent { 16, 524416, 0, std::chrono::milliseconds { 4000 } });
    refresh_details();
    stage_advisor(154);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_details() {
    if (page != Page::details || !root) {
        return;
    }
    const auto& view = model.state().details;
    if (!view.open || !live_staff(c, view.staff)) {
        close();
        return;
    }
    page_target = view.staff;
    ui.set_visible(required(root, 7002), view.can_navigate);
    ui.set_visible(required(root, 7005), view.can_navigate);
    //Native5908C0 reloads salary only when selecting an employee. The model's
    //staged value retains that contract during native5902E0 metrics refreshes.
    auto& salary = required(root, 7006);
    if (salary.value != view.staged_wage) {
        ui.set_value(salary, view.staged_wage);
    }
    caption(required(root, 7029), view.row.name, 5, 0, 1);
    caption(required(root, 7017), view.tenure, 6, 0, 1);
    caption(required(root, 7022), view.status, 6, 0, 1);
    caption(required(root, 7024), view.agency, 6, 0, 1);
    caption(required(root, 7020), view.row.ability, 6, 0, 1);
    ui.set_value(required(root, 7009), view.row.experience_meter);
    ui.set_value(required(root, 7010), view.row.happiness_meter);
    ui.set_value(required(root, 7011), view.motivation_meter);
    ui.set_value(required(root, 7008), view.row.energy_meter);
    ui.set_attachment_part(required(root, 7012), static_cast<std::uint16_t>(view.activity_icon));
    auto& thought = required(root, 7027);
    caption(thought, view.thought, 6);
    thought.help_index = static_cast<std::uint32_t>(view.thought_help);
    ui.set_attachment_part(required(root, 7028), static_cast<std::uint16_t>(view.thought_icon));
    preview(required(root, 7026), view.staff);
}

std::intptr_t WorldStaffUiController::State::details_event(UiControl& sender, const UiEvent& event) {
    if (refreshing || closing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 5 || escape(event) || (event.type == 256 && (event.id == 7001 || event.id == static_cast<std::uint32_t>(-2)))) {
        close();
        return 0;
    }
    const auto id = model.state().details.staff;
    if (!live_staff(c, id)) {
        close();
        return 0;
    }
    if (sender.id == 7029) {
        if (event.type == 65537) {
            sender.color = { 239, 234, 24, 255 };
        } else if (event.type == 65538) {
            sender.color = { 255, 255, 255, 255 };
        } else if (event.type == 65542 && event.id == 0) {
            open_name(id, true);
        }
        return ui.secondary(sender, event);
    }
    if (sender.id == 7026) {
        if (event.type == 13) {
            return sender.render_depth + 10;
        }
        if (event.type == 65542) {
            if (host.focus_staff) {
                host.focus_staff(id, false);
            }
            return 0;
        }
    }
    if (event.type == 16 && event.id == 524416) {
        model.refresh();
        refresh_details();
        return 0;
    }
    if (event.type == 2048) {
        //Native590DD0 has this real fallback, including its shared CRT stream.
        model.state().details.staged_wage = event.id == 7006 ? fixed::narrow(event.value) : next_crt_random(c) % 1000;
    }
    const bool previous = event.type == 256 && event.id == 7002;
    const bool next = event.type == 256 && event.id == 7005;
    const bool key_previous = event.type == 65546 && (event.id == 9728 || (event.id <= 255 && (event.id << 8) == 9728));
    const bool key_next = event.type == 65546 && (event.id == 10240 || (event.id <= 255 && (event.id << 8) == 10240));
    if (previous || next || key_previous || key_next) {
        model.navigate_details(previous || key_previous);
        refresh_details();
        return 0;
    }
    if (event.type == 256) {
        if (event.id == 7003) {
            model.apply_details_salary();
            close();
            return 0;
        }
        if (event.id == 7004) {
            auto& button = required(root, 7004);
            open_context(id, { button.rect.left, button.rect.bottom }, true);
            return 0;
        }
        if (event.id == 7030) {
            queue_world_advisor_help_action(c, 154);
            return 0;
        }
    }
    return ui.secondary(sender, event);
}

void WorldStaffUiController::State::open_finance() {
    begin(Page::finance);
    model.open_finance();
    auto& view = model.state().finance;
    model.set_finance_mode(view.mode);
    view.enabled.fill(false);
    finance_presentation_mode.reset();
    root = &parse(
        finance_template,
        31000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    panel_caption(*this, required(root, 31019), text(260));
    auto& group = required(root, 31003);
    caption(group, text(261), 8);
    group.text_constraints = { true, true };
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3> modes { { { 31006, 262 }, { 31004, 263 }, { 31005, 264 } } };
    for (const auto& [control, row] : modes) {
        auto& option = required(root, control);
        caption(option, text(row), 11);
        option.text_constraints = { true, true };
    }
    for (const auto& row : finance_rows) {
        auto& label = required(root, row.caption);
        caption(label, text(row.text), 6);
        label.text_constraints = { true, true };
        ui.set_primary_callback(required(root, row.stepper), panel_callback<&State::finance_event>());
    }
    refresh_finance();
    stage_advisor(434);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_finance() {
    if (page != Page::finance || !root || !model.state().finance.open) {
        return;
    }
    auto& view = model.state().finance;
    const bool reconfigure = finance_presentation_mode != view.mode;
    finance_presentation_mode = view.mode;
    const auto& balance = c.staff.recruitment.balance.all;
    for (std::size_t i = 0; i < finance_rows.size(); ++i) {
        auto& stepper = required(root, finance_rows[i].stepper);
        if (reconfigure) {
            const auto minimum = view.mode == StaffFinanceAdjustment::amount ? balance.MinWage : view.mode == StaffFinanceAdjustment::add ? -5000 : -100;
            const auto maximum = view.mode == StaffFinanceAdjustment::amount ? balance.MaxWage : view.mode == StaffFinanceAdjustment::add ? 5000 : 100;
            ui.set_range(stepper, minimum, maximum);
            stepper.step_size = view.mode == StaffFinanceAdjustment::amount ? balance.WageStep : 1;
            const bool percentage = view.mode == StaffFinanceAdjustment::percent;
            affixed_edit(stepper, percentage ? u" %" : u"$ ", 6, 6, { 255, 255, 255, 255 }, percentage);
        }
        if (stepper.value != view.values[i]) {
            ui.set_value(stepper, view.values[i]);
        }
        auto& toggle = required(&stepper, 31008);
        if ((toggle.visual_flags & 1u) != static_cast<unsigned>(view.enabled[i])) {
            ui.set_value(toggle, view.enabled[i] ? 1 : 0);
        }
        finance_edit_state(ui, stepper, view.enabled[i]);
    }
    const auto selected = view.mode == StaffFinanceAdjustment::amount ? 31006 : view.mode == StaffFinanceAdjustment::add ? 31004 : 31005;
    ui.set_selected(required(root, 31003), selected, false);
}

std::intptr_t WorldStaffUiController::State::finance_event(UiControl& sender, const UiEvent& event) {
    if (refreshing || closing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 5 || escape(event) || (event.type == 256 && (event.id == 31001 || event.id == static_cast<std::uint32_t>(-2)))) {
        close();
        return 1;
    }
    auto& view = model.state().finance;
    if (const auto index = finance_row(sender.id)) {
        auto& toggle = required(&sender, 31008);
        if (event.type == 21 || (event.type == 256 && event.id == 31008 && event.value == 0)) {
            view.enabled[*index] = false;
            finance_edit_state(ui, sender, false);
        } else if (event.type == 65542 && !toggle.visual_flags) {
            view.enabled[*index] = true;
            finance_edit_state(ui, sender, true, true);
            ui.set_value(toggle, 1);
        } else if (event.type == 256) {
            view.enabled[*index] = true;
            finance_edit_state(ui, sender, true);
            if (event.id != 31008) {
                ui.set_value(toggle, 1);
            }
        }
        return ui.secondary(sender, event);
    }
    if (event.type == 2048) {
        if (const auto index = finance_row(event.id)) {
            view.values[*index] = fixed::narrow(event.value);
            return 0;
        }
    }
    if (event.type == 257 && event.value == 1) {
        std::optional<StaffFinanceAdjustment> mode;
        if (event.id == 31006) {
            mode = StaffFinanceAdjustment::amount;
        } else if (event.id == 31004) {
            mode = StaffFinanceAdjustment::add;
        } else if (event.id == 31005) {
            mode = StaffFinanceAdjustment::percent;
        }
        if (mode) {
            model.set_finance_mode(*mode);
            //Repeated native policy selection also resets each field to zero
            //(clamped to SAM minimum for the absolute salary policy).
            finance_presentation_mode.reset();
            refresh_finance();
            return 0;
        }
    }
    if (event.type == 256 && event.id == 31002) {
        model.apply_finance();
        close();
        return 1;
    }
    if (event.type == 256 && event.id == 31021) {
        queue_world_advisor_help_action(c, 434);
        return 0;
    }
    return ui.secondary(sender, event);
}

void WorldStaffUiController::State::open_activity(ThingId id) {
    if (!live_staff(c, id)) {
        return;
    }
    begin(Page::activity);
    page_target = id;
    model.open_activity(id);
    root = &parse(
        activity_template,
        16000,
        [this](auto& sender, const auto& event) {
            return this->event(sender, event);
        },
        &page_roots);
    const auto path = (game_root / "Data/Language/American/STAFFSTATES.str").native_encoded_string();
    const auto states = c.ports.assets.read_balance_asset(c, path);
    if (!states) {
        throw std::runtime_error("Missing Staff Activity language resource: " + path);
    }
    const auto bytes = std::as_bytes(std::span { states->data(), states->size() });
    for (std::size_t i = 0; i < activity_rows.size(); ++i) {
        const auto& row = activity_rows[i];
        auto label = format_bfst_wide_string(bytes, c.messages.mail_language.conversion, row.state_row, 16383);
        if (!label) {
            throw std::runtime_error("Staff Activity state text: " + label.error().reason);
        }
        caption(required(root, row.label), *label, 6, 0, 1);
        auto& percentage = required(root, row.percentage);
        caption(percentage, {}, 6, 0, 1);
        percentage.scalar_text = UiScalarTextKind::percentage;
        meter(required(root, row.meter), i == 1 ? "happygrad.tga" : "ridestatbar.tga");
    }
    const auto* staff = live_staff(c, id);
    if (!staff) {
        throw std::logic_error("Staff Activity owner retired while opening");
    }
    const std::array name { BfstWideStringArgument { 49, staff_name(*staff) } };
    panel_caption(*this, required(root, 0x3E98), text(971, {}, name));
    ui.set_periodic(*root, 0, UiPeriodicEvent { 16, 524416, 0, std::chrono::milliseconds { 2000 } });
    refresh_activity();
    stage_advisor(430);
    if (host.activate) {
        host.activate(root);
    }
}

void WorldStaffUiController::State::refresh_activity() {
    if (page != Page::activity || !root) {
        return;
    }
    const auto& view = model.state().activity;
    const auto* staff = live_staff(c, view.staff);
    if (!view.open || !staff) {
        close();
        return;
    }
    page_target = view.staff;
    const std::array name { BfstWideStringArgument { 49, staff_name(*staff) } };
    panel_caption(*this, required(root, 0x3E98), text(971, {}, name));
    for (std::size_t i = 0; i < activity_rows.size(); ++i) {
        const auto percentage = fixed::truncate_integer(view.percentages[i]);
        ui.set_value(required(root, activity_rows[i].percentage), percentage);
        //588810 shifts the DWORD before unsigned division. Preserve the
        //wrapping intermediate; the native meter clamps the resulting value.
        const auto progress = (static_cast<std::uint32_t>(percentage) << 10) / 100u;
        ui.set_value(required(root, activity_rows[i].meter), fixed::word(progress));
    }
}

std::intptr_t WorldStaffUiController::State::activity_event(UiControl& sender, const UiEvent& event) {
    if (refreshing || closing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 5 || escape(event) || (event.type == 256 && (event.id == 16001 || event.id == static_cast<std::uint32_t>(-2)))) {
        close();
        return 1;
    }
    if (event.type == 16 && event.id == 524416) {
        model.refresh();
        refresh_activity();
        return 0;
    }
    if (event.type == 256 && event.id == 16025) {
        queue_world_advisor_help_action(c, 430);
        return 0;
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
