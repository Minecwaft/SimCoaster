#include "simcoaster/world_fixed_point.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char finance_words[] {
#embed "ui_templates/park_finance.bin"
};

struct Row {
    std::uint32_t value, label, text;
};

constexpr std::array rows { Row { 28007, 28014, 189 }, Row { 28009, 28015, 188 }, Row { 28010, 28016, 184 }, Row { 28011, 28017, 185 }, Row { 28012, 28018, 186 }, Row { 28013, 28020, 187 } };

std::optional<std::size_t> row_for(std::uint32_t id) {
    const auto found = std::ranges::find(rows, id, &Row::value);
    return found == rows.end() ? std::nullopt : std::optional { static_cast<std::size_t>(found - rows.begin()) };
}

void edit_state(UiRuntime& ui, UiControl& stepper, bool enabled, bool focus = false) {
    auto* edit = ui.find(&stepper, 3);
    if (!edit) {
        throw std::logic_error("Original attraction-finance row has no editor");
    }
    ui.set_visible(*edit, enabled);
    ui.set_enabled(*edit, enabled);
    if (enabled && focus) {
        ui.set_focus(edit);
    } else if (!enabled && ui.focus() == edit) {
        ui.set_focus(nullptr);
    }
}
} //namespace

void WorldParkObjectUiController::State::open_finance() {
    begin(Page::finance);
    model.open_finance();
    auto& view = model.state().finance;
    model.set_finance_mode(view.mode);
    view.enabled.fill(false);
    root = &parse(finance_words, 28000, panel_callback<&State::finance_event>());
    label(28019, 179, 5, 0);
    required(root, 28019).primary_shadow_text = true;
    label(28003, 180, 8, 1);
    required(root, 28003).text_constraints = { true, true };
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 3> { { { 28006, 181 }, { 28004, 182 }, { 28005, 183 } } }) {
        label(id, row, 11, 1);
        required(root, id).text_constraints = { true, true };
    }
    for (const auto& row : rows) {
        label(row.label, row.text, 6, 1);
        required(root, row.label).text_constraints = { true, true };
        ui.set_primary_callback(required(root, row.value), panel_callback<&State::finance_event>());
    }
    refresh_finance();
    stage_advisor(428);
    activate();
}

void WorldParkObjectUiController::State::refresh_finance() {
    if (!root || !model.state().finance.open) {
        return;
    }
    const auto& view = model.state().finance;
    const bool reconfigure = presented_finance != view.mode;
    presented_finance = view.mode;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto& stepper = required(root, rows[i].value);
        if (reconfigure) {
            const bool percentage = view.mode == ParkObjectFinanceMode::percent;
            ui.set_range(stepper, view.mode == ParkObjectFinanceMode::amount ? 0 : percentage ? -100 : -5000, percentage ? 100 : view.mode == ParkObjectFinanceMode::amount ? 10000 : 5000);
            stepper.step_size = 1;
            affixed_edit(stepper, percentage ? u" %" : u"$ ");
            required(&stepper, 3).append_text_affix = percentage;
        }
        if (stepper.value != view.values[i]) {
            ui.set_value(stepper, view.values[i]);
        }
        auto& toggle = required(&stepper, 28008);
        if ((toggle.visual_flags & 1u) != static_cast<unsigned>(view.enabled[i])) {
            ui.set_value(toggle, view.enabled[i] ? 1 : 0);
        }
        edit_state(ui, stepper, view.enabled[i]);
    }
    ui.set_selected(required(root, 28003), view.mode == ParkObjectFinanceMode::amount ? 28006 : view.mode == ParkObjectFinanceMode::add ? 28004 : 28005, false);
}

std::intptr_t WorldParkObjectUiController::State::finance_event(UiControl& sender, const UiEvent& event) {
    if (refreshing || closing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 256 && (event.id == 28001 || event.id == 4294967294u)) {
        close();
        return 1;
    }
    auto& view = model.state().finance;
    if (const auto index = row_for(sender.id)) {
        auto& toggle = required(&sender, 28008);
        if (event.type == 21 || (event.type == 256 && event.id == 28008 && !event.value)) {
            view.enabled[*index] = false;
            edit_state(ui, sender, false);
        } else if (event.type == 65542 && !toggle.visual_flags) {
            view.enabled[*index] = true;
            edit_state(ui, sender, true, true);
            ui.set_value(toggle, 1);
        } else if (event.type == 256) {
            view.enabled[*index] = true;
            edit_state(ui, sender, true);
            if (event.id != 28008) {
                ui.set_value(toggle, 1);
            }
        }
        return ui.secondary(sender, event);
    }
    if (event.type == 2048) {
        if (const auto index = row_for(event.id)) {
            view.values[*index] = fixed::narrow(event.value);
            return 1;
        }
    }
    if (event.type == 257 && event.value == 1) {
        std::optional<ParkObjectFinanceMode> mode;
        if (event.id == 28006) {
            mode = ParkObjectFinanceMode::amount;
        } else if (event.id == 28004) {
            mode = ParkObjectFinanceMode::add;
        } else if (event.id == 28005) {
            mode = ParkObjectFinanceMode::percent;
        }
        if (mode) {
            model.set_finance_mode(*mode);
            presented_finance.reset();
            refresh_finance();
            return 1;
        }
    }
    if (event.type == 256 && event.id == 28002) {
        model.apply_finance();
        close();
        return 1;
    }
    if (event.type == 256 && event.id == 28021) {
        help_advisor(428);
        return 1;
    }
    return common_event(sender, event);
}
} //namespace simcoaster
