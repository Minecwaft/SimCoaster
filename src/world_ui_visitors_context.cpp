#include "simcoaster/standard_balance.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/sam_integer.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "world_ui_visitors_internal.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char context_template[] {
#embed "ui_templates/staff_context.bin"
};

bool visitor_context_cell_blocked(WorldRuntime& c, const PersonRuntimeView& person) {
    //554110 invalidates an open Visitor minimenu when the current World cell
    //acquires either flag in0xC0. This is independent of family7's type filter.
    if (!c.simulation.current) {
        return true;
    }
    const auto index = (static_cast<std::uint32_t>(person.base.y >> 8) << 7) + (person.base.x >> 8);
    return index >= map_cell_count || (c.simulation.current->map_cells.at(index).base.common.flags & 0xC0u) != 0;
}
} //namespace

void WorldVisitorsUiController::State::close_context() {
    auto* old = std::exchange(context, nullptr);
    if (ui.owns(old)) {
        ui.destroy(old);
    }
    auto* retained = std::exchange(context_previous, nullptr);
    const auto serial = std::exchange(context_previous_serial, 0);
    if (retained && serial && c.ui.active_root == retained && ui.lifetime_serial(retained) == serial) {
        ui.set_enabled(*retained, context_previous_enabled);
    }
    context_person = 0;
    context_identity = nullptr;
    ui.set_input_root(nullptr);
    if (!closing && host.synchronize_focus) {
        host.synchronize_focus();
    }
}

void WorldVisitorsUiController::State::open_context(ThingId id, UiPoint point) {
    if (point.x <= 0 || point.x >= 2048 || point.y <= 0 || point.y >= 1536) {
        return;
    }
    close_context();
    auto* record = find_ui_visitor(c, id);
    if (!record || id == retiring || !record_passes_grid_predicates(c, &record->base)) {
        return;
    }
    mark_advisor_context_menu_used(c);
    if (visitor_context_cell_blocked(c, *record)) {
        return;
    }
    context_person = id;
    context_identity = &record->base.header;
    context_arrival = record->arrival_date;
    context_previous = ui.owns(c.ui.active_root) ? c.ui.active_root : nullptr;
    context_previous_serial = ui.lifetime_serial(context_previous);
    context_previous_enabled = context_previous && context_previous->enabled;
    if (context_previous) {
        ui.set_enabled(*context_previous, false);
    }
    if (host.prepare_context_menu) {
        host.prepare_context_menu();
    }
    context_menu.load(c, game_root);
    context = &parse(context_template, 1000, panel_callback<&State::context_event>());
    context_menu.start(*this, point);
    refresh_context();
    ui.set_input_root(context);
    ui.set_focus(context);
}

void WorldVisitorsUiController::State::refresh_context() {
    auto* record = EntityIdentity { context_person, context_identity, context_arrival }.get<PersonRuntimeView, true>(c.entities);
    if (!record || context_person == retiring || visitor_context_cell_blocked(c, *record)) {
        close_context();
        return;
    }
    if (!local_fonts) {
        local_fonts = std::make_unique<UiFontLibrary>(game_root);
    }
    const auto title = visitor_ui_name(c, *record);
    const auto options = visitor_ui_context(c, *record);
    context_menu.present<true>(*this, *local_fonts, title, options);
}

std::intptr_t WorldVisitorsUiController::State::context_event(UiControl& sender, const UiEvent& input) {
    if (input.type == 20 && &sender == context) {
        close_context();
        return 1;
    }
    if (input.type == 4 || input.type == 5 || (input.type == 65547 && (input.id > 255 ? input.id >> 8 : input.id) == 27)) {
        close_context();
        return 1;
    }
    if (input.type == 256 && input.id >= 1001 && input.id <= 1004 && context) {
        auto& control = required(context, input.id);
        if (!control.enabled || !control.visible()) {
            return 1;
        }
        const auto row = context_menu.rows[input.id - 1001];
        const auto id = context_person;
        const auto* identity = context_identity;
        const auto arrival = context_arrival;
        close_context(); //Original action executes after popup destruction.
        auto* record = EntityIdentity { id, identity, arrival }.get<PersonRuntimeView, true>(c.entities);
        if (!record || id == retiring) {
            return 1;
        }
        if (row == 663) {
            open_locator(id);
        } else {
            (void)execute_visitor_ui_context(c, *record, row);
            refresh();
        }
        return 1;
    }
    return ui.secondary(sender, input);
}
} //namespace simcoaster
