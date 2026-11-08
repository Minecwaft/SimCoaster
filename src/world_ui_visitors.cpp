#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "world_ui_visitors_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
WorldVisitorsUiController::State::State(WorldRuntime& context_owner, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, WorldUiPanelHost panel_host) : c(context_owner), ui(runtime), primary(parent), game_root(path), host(std::move(panel_host)) {
    if (!ui.owns(primary)) {
        throw std::invalid_argument("Visitors UI requires its persistent primary root");
    }
}

WorldVisitorsUiController::State::~State() {
    close();
}

std::u16string WorldVisitorsUiController::State::thought(std::uint32_t row) const {
    auto value = format_bfst_wide_string(c.messages.mail_language.ui_thoughts, c.messages.mail_language.conversion, row, 16383);
    if (!value) {
        throw std::runtime_error("Visitors thought: " + value.error().reason);
    }
    return std::move(*value);
}

void WorldVisitorsUiController::State::label(std::uint32_t id, std::uint32_t row, std::uint32_t font, std::int32_t horizontal) {
    caption(required(root, id), text(row), font, horizontal);
}

void WorldVisitorsUiController::State::scalar(std::uint32_t id, std::int32_t value) {
    auto& control = required(root, id);
    control.font = 6;
    control.scalar_text = UiScalarTextKind::signed_integer;
    ui.set_value(control, value);
}

void WorldVisitorsUiController::State::meter(std::uint32_t id, std::string_view texture, std::int32_t inset) {
    auto& control = required(root, id);
    control.meter_presentation = UiMeterPresentation { std::string(texture), { 0, inset } };
    ui.set_range(control, 0, 1024);
}

void WorldVisitorsUiController::State::stage(std::int32_t action) {
    advisor = action;
    auto* world = c.referenced_world();
    if (!world) {
        return;
    }
    const auto id = world->saved.advisor;
    if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != ThingKind::advisor) {
        throw std::logic_error("Visitors Advisor is absent");
    }
    auto& record = *c.entities.payload<AdvisorRecord>(id);
    record.from_ui_panel = 1;
    record.delayed_message = action;
    record.delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
}

void WorldVisitorsUiController::State::help() {
    const auto action = page == Page::profile ? 249 : advisor;
    if (action >= 0 && c.referenced_world()) {
        queue_world_advisor_help_action(c, action);
    }
}

void WorldVisitorsUiController::State::begin(Page next) {
    close();
    if (host.dismiss) {
        host.dismiss();
    }
    page = next;
    next_refresh = ui.clock();
}

void WorldVisitorsUiController::State::activate() {
    if (host.activate) {
        host.activate(root);
    }
}

void WorldVisitorsUiController::State::close() {
    if (closing) {
        return;
    }
    closing = true;
    close_context();
    auto* old = root;
    root = nullptr;
    if (page == Page::list && ui.owns(old)) {
        auto& list = required(old, 55004);
        list_sort = ui.list_sort_key(list);
        list_selected = list.selected_id;
    }
    if (page == Page::locator) {
        c.view.flags &= ~4096u;
    }
    if (preview) {
        preview->clear();
    }
    if (ui.owns(old)) {
        ui.destroy(old);
    }
    if (advisor >= 0) {
        for (auto* world : { c.simulation.current, c.referenced_world() }) {
            if (world && world->saved.advisor && world->saved.advisor < c.entities.size()) {
                auto* header = c.entities[world->saved.advisor].payload;
                if (header && header->kind == ThingKind::advisor) {
                    auto& record = *reinterpret_cast<AdvisorRecord*>(header);
                    record.from_ui_panel = record.panel_up = 0;
                    world_operations::cancel_advisor_action_by_id(c, &record, advisor);
                }
            }
        }
    }
    advisor = -1;
    target = 0;
    target_identity = nullptr;
    list_identities.clear();
    page = Page::none;
    if (old && host.deactivate) {
        host.deactivate(old);
    }
    ui.set_input_root(nullptr);
    if (host.synchronize_focus) {
        host.synchronize_focus();
    } else if (ui.owns(primary)) {
        ui.set_focus(primary);
    }
    closing = false;
}

PersonRuntimeView* WorldVisitorsUiController::State::person() const {
    auto* record = EntityIdentity { target, target_identity, target_arrival }.get<PersonRuntimeView, true>(c.entities);
    return target != retiring ? record : nullptr;
}

void WorldVisitorsUiController::State::select(ThingId id) {
    auto* record = find_ui_visitor(c, id);
    target = record && id != retiring ? id : 0;
    target_identity = target ? &record->base.header : nullptr;
    target_arrival = target ? record->arrival_date : 0;
}

void WorldVisitorsUiController::State::retire(ThingId id) {
    const auto previous = retiring;
    retiring = id;
    if (preview) {
        preview->retire(id);
    }
    if (context_person == id) {
        close_context();
    }
    list_identities.erase(id);
    if (target == id) {
        select(0);
        if (page == Page::profile) {
            close();
        } else if (page == Page::locator) {
            locator_filter = 0;
            c.view.flags &= ~4096u;
            refresh_locator();
        }
    }
    if (page == Page::list && root) {
        ui.remove_list_row(required(root, 55004), id, false);
        refresh_list_details();
    } else if (page == Page::statistics) {
        refresh_statistics();
    }
    retiring = previous;
}

void WorldVisitorsUiController::State::refresh() {
    if (!root || closing || refreshing) {
        return;
    }
    refreshing = true;
    try {
        match (page) {
            case Page::profile    => refresh_profile();
            case Page::list       => refresh_list();
            case Page::statistics => refresh_statistics();
            case Page::locator    => refresh_locator();
            case _                => {
            }
        }
    } catch (...) {
        refreshing = false;
        throw;
    }
    refreshing = false;
}

WorldVisitorsUiController::WorldVisitorsUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& path, WorldUiPanelHost host) : state_(std::make_unique<State>(c, ui, primary, path, std::move(host))) {}

WorldVisitorsUiController::~WorldVisitorsUiController() = default;

bool WorldVisitorsUiController::command(WorldUiCommand command) {
    match (command) {
        case WorldUiCommand::visitor_statistics => {
            open_statistics();
            return true;
        }
        case WorldUiCommand::visitor_list => {
            open_list();
            return true;
        }
        case WorldUiCommand::visitor_locator => {
            open_locator();
            return true;
        }
        case _ => return false;
    }
}

bool WorldVisitorsUiController::open() const {
    return state_->root || state_->context;
}

void WorldVisitorsUiController::close() {
    state_->close();
}

void WorldVisitorsUiController::open_statistics() {
    state_->open_statistics();
}

void WorldVisitorsUiController::open_list() {
    state_->open_list();
}

void WorldVisitorsUiController::open_profile(ThingId id) {
    state_->open_profile(id);
}

void WorldVisitorsUiController::open_locator(ThingId id) {
    state_->open_locator(id);
}

void WorldVisitorsUiController::open_context(ThingId id, UiPoint p) {
    state_->open_context(id, p);
}

bool WorldVisitorsUiController::dismiss_outside(UiPoint p) {
    if (!state_->context || state_->context->rect.contains(p)) {
        return false;
    }
    state_->close_context();
    return true;
}

void WorldVisitorsUiController::close_context() {
    state_->close_context();
}

void WorldVisitorsUiController::preferences(const UiPreferences& p) {
    state_->animate_menus = p.animate_menus != 0;
}

void WorldVisitorsUiController::handle_event(const WorldEvent& e) {
    if (const auto* retirement = std::get_if<RetirementEvent>(&e.value)) {
        state_->retire(retirement->retired_id);
    } else if (std::holds_alternative<PersonCreatedEvent>(e.value) && state_->root) {
        if (state_->page == State::Page::list) {
            state_->refresh_list();
        } else if (state_->page == State::Page::locator) {
            state_->refresh_locator();
        }
    }
}

void WorldVisitorsUiController::advance() {
    auto& s = *state_;
    if (s.context) {
        s.refresh_context();
    }
    if (s.page == State::Page::locator) {
        s.refresh_locator();
        if ((s.c.view.flags & 4096u) && s.person() && s.host.focus_person) {
            s.host.focus_person(s.target, true);
        }
    }
    if (s.preview) {
        (void)s.preview->refresh();
    }
    if (s.root && s.ui.clock() >= s.next_refresh) {
        s.next_refresh = s.ui.clock() + std::chrono::milliseconds(s.page == State::Page::profile ? 4000 : 2000);
        s.refresh();
    }
}

ThingId WorldVisitorsUiController::locator_target() const {
    return state_->page == State::Page::locator && state_->person() ? state_->target : 0;
}

void WorldVisitorsUiController::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    state_->append(draw, control, clip, width, height);
}
} //namespace simcoaster
