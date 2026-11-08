#include "simcoaster/require_bound.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_event.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
ParkConstructionRuntime& construction(const WorldUiPanelHost& host) {
    return require_bound<std::invalid_argument>(host.construction, "Park-object UI requires its live construction service");
}
} //namespace

WorldParkObjectUiController::State::State(WorldRuntime& context_owner, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, WorldUiPanelHost panel_host) : c(context_owner), game_root(path), ui(runtime), primary(parent), host(std::move(panel_host)), model(c, construction(host)) {
    if (!ui.owns(primary)) {
        throw std::invalid_argument("Park-object UI requires its persistent primary root");
    }
    model.set_retirement_observer([this](ThingId id) {
        retire(id);
    });
}

WorldParkObjectUiController::State::~State() {
    close();
    model.set_retirement_observer({});
}

std::u16string WorldParkObjectUiController::State::money(std::int32_t value) const {
    const bool negative = value < 0;
    const auto magnitude = negative ? 0u - std::bit_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
    const std::array argument { BfstSignedIntegerArgument { negative ? 44u : 39u, std::bit_cast<std::int32_t>(magnitude) } };
    return text(negative ? 958u : 957u, argument);
}

UiFontLibrary& WorldParkObjectUiController::State::fonts() {
    if (!local_fonts) {
        local_fonts = std::make_unique<UiFontLibrary>(game_root);
    }
    return *local_fonts;
}

void WorldParkObjectUiController::State::label(std::uint32_t id, std::uint32_t row, std::uint32_t font, std::int32_t horizontal) {
    caption(required(root, id), text(row), font, horizontal);
}

void WorldParkObjectUiController::State::scalar(std::uint32_t id, std::int32_t value, UiScalarTextKind kind) {
    auto& control = required(root, id);
    control.font = 6;
    control.vertical_alignment = 1;
    control.scalar_text = kind;
    ui.set_value(control, value);
}

void WorldParkObjectUiController::State::meter(std::uint32_t id, std::int32_t value, std::int32_t inset) {
    auto& control = required(root, id);
    control.meter_presentation = UiMeterPresentation { "ridestatbar.tga", { 0, inset } };
    ui.set_range(control, 0, 1024);
    ui.set_value(control, value);
}

void WorldParkObjectUiController::State::affixed_edit(UiControl& stepper, std::u16string_view affix, std::size_t capacity, std::uint32_t font) {
    auto& edit = required(&stepper, 3);
    stepper.associated_edit = &edit;
    caption(edit, edit.text, font, 1);
    edit.text_affix = affix;
    edit.numeric_only = true;
    edit.text_limit = capacity ? capacity - 1 : 0;
    edit.edit_caret_asset_hash = ui_model_asset_hash("lolight");
    ui.set_value(stepper, stepper.value);
}

void WorldParkObjectUiController::State::preview(UiControl& control, ThingId id, std::uint32_t animation) {
    auto& provider = previews[&control];
    if (!provider) {
        provider = std::make_unique<UiObjectPreview>(c, game_root);
    }
    (void)provider->bind(id, animation);
}

void WorldParkObjectUiController::State::stage_advisor(std::int32_t action) {
    advisor_action = action;
    if (action < 0 || !c.simulation.current) {
        return;
    }
    const auto id = c.simulation.current->saved.advisor;
    if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != ThingKind::advisor) {
        throw std::logic_error("Park-object panel Advisor owner is absent");
    }
    auto& advisor = *c.entities.payload<AdvisorRecord>(id);
    advisor.from_ui_panel = 1;
    advisor.delayed_message = action;
    advisor.delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
}

void WorldParkObjectUiController::State::help_advisor(std::int32_t action) {
    if (action >= 0) {
        queue_world_advisor_help_action(c, action);
    }
}

void WorldParkObjectUiController::State::begin(Page next) {
    close();
    if (host.dismiss) {
        host.dismiss();
    }
    page = next;
    next_refresh = ui.clock();
}

void WorldParkObjectUiController::State::activate() {
    if (host.activate) {
        host.activate(root);
    }
}

void WorldParkObjectUiController::State::close_auxiliary() {
    for (auto** slot : { &message_wrapper, &context, &name_editor }) {
        if (*slot && ui.owns(*slot)) {
            ui.destroy(*slot);
        }
        *slot = nullptr;
    }
    message_root = nullptr;
    message_accepted = {};
    auxiliary_target = 0;
    confirmation_target = 0;
    auxiliary_identity = nullptr;
}

void WorldParkObjectUiController::State::close() {
    if (closing) {
        return;
    }
    closing = true;
    auto* old_root = root;
    close_auxiliary();
    const auto owners = roots;
    for (auto* control : owners) {
        if (ui.owns(control)) {
            ui.destroy(control);
        }
    }
    roots.clear();
    previews.clear();
    graph_control = nullptr;
    root = sublist = nullptr;
    model.close_all();
    page = Page::none;
    page_target = 0;
    presented_category.reset();
    presented_finance.reset();
    if (advisor_action >= 0) {
        cancel_ui_advisor_action(c, advisor_action);
    }
    advisor_action = -1;
    if (old_root && host.deactivate) {
        host.deactivate(old_root);
    }
    closing = false;
    restore_input();
}

void WorldParkObjectUiController::State::retire(ThingId id) {
    if (host.retiring) {
        host.retiring(id);
    }
    for (auto& [control, provider] : previews) {
        (void)control;
        provider->retire(id);
    }
    std::erase(research_workers, id);
    std::erase(research_available, id);
    research_identities.erase(id);
    if (research_consultant == id) {
        research_consultant = 0;
    }
    for (auto& consultant : research_cached_consultants) {
        if (consultant == id) {
            consultant = 0;
        }
    }
    if (auxiliary_target == id || confirmation_target == id) {
        close_auxiliary();
    }
    if (page_target == id && !closing) {
        close();
        return;
    }
    if (!closing) {
        const auto previous_retiring = research_retiring;
        research_retiring = id;
        try {
            refresh_page();
        } catch (...) {
            research_retiring = previous_retiring;
            throw;
        }
        research_retiring = previous_retiring;
    }
}

void WorldParkObjectUiController::State::open_object(ThingId id) {
    const auto route = model.route(id);
    match (route.panel) {
        case ParkObjectUiPanel::none       => return;
        case ParkObjectUiPanel::staff_room => {
            if (!host.open_staff_room) {
                throw std::logic_error("Park-object Staff Room route has no owner");
            }
            host.open_staff_room(route.target);
            return;
        }
        case ParkObjectUiPanel::training_room => {
            if (!host.open_training_room) {
                throw std::logic_error("Park-object Training Room route has no owner");
            }
            host.open_training_room(route.target);
            return;
        }
        case ParkObjectUiPanel::research => {
            open_research();
            return;
        }
        case ParkObjectUiPanel::special_ride => {
            open_special(route.target);
            return;
        }
        case _ => {
            open_settings(route.target);
            return;
        }
    }
}

void WorldParkObjectUiController::State::refresh_page() {
    if (closing || refreshing) {
        return;
    }
    refreshing = true;
    try {
        match (page) {
            case Page::settings        => refresh_settings();
            case Page::list            => refresh_list();
            case Page::finance         => refresh_finance();
            case Page::statistics      => refresh_statistics();
            case Page::opinions        => refresh_opinions();
            case Page::promotion       => refresh_promotion();
            case Page::upgrades        => refresh_upgrade();
            case Page::special         => refresh_special();
            case Page::research        => refresh_research();
            case Page::research_picker => refresh_research_picker();
            case Page::research_edit   => refresh_research_edit();
            case _                     => {
            }
        } if (context) {
            refresh_context();
        }
        refreshing = false;
    } catch (...) {
        refreshing = false;
        throw;
    }
}

void WorldParkObjectUiController::State::refresh() {
    for (auto& [control, provider] : previews) {
        if (ui.owns(control)) {
            provider->advance();
        }
    }
    if (ui.clock() >= next_refresh) {
        next_refresh = ui.clock() + std::chrono::milliseconds { page == Page::statistics ? 10000 : page == Page::settings || page == Page::special ? 4000 : 2000 };
        model.refresh();
        refresh_page();
    } else if (context) {
        refresh_context();
    }
}

std::intptr_t WorldParkObjectUiController::State::common_event(UiControl& control, const UiEvent& event) {
    if (!closing && event.type == 20 && &control == root) {
        close();
        return 1;
    }
    const auto key = event.id > 255 ? event.id >> 8 : event.id;
    if (!closing && (event.type == 5 || (event.type == 65547 && key == 27 && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0))) {
        close();
        return 1;
    }
    return ui.secondary(control, event);
}

WorldParkObjectUiController::WorldParkObjectUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& root, WorldUiPanelHost host) : state_(std::make_unique<State>(c, ui, primary, root, std::move(host))) {}

WorldParkObjectUiController::~WorldParkObjectUiController() = default;

bool WorldParkObjectUiController::command(WorldUiCommand command) {
    match (command) {
        case WorldUiCommand::attraction_list => {
            state_->open_list();
            return true;
        }
        case WorldUiCommand::attraction_finance => {
            state_->open_finance();
            return true;
        }
        case WorldUiCommand::research => {
            state_->open_research();
            return true;
        }
        case _ => return false;
    }
}

bool WorldParkObjectUiController::open() const {
    return state_->page != State::Page::none || state_->context || state_->name_editor || state_->message_wrapper;
}

void WorldParkObjectUiController::close() {
    state_->close();
}

void WorldParkObjectUiController::open_object(ThingId id) {
    state_->open_object(id);
}

void WorldParkObjectUiController::open_context(ThingId id, UiPoint p, bool filtered) {
    state_->open_context(id, p, filtered);
}

void WorldParkObjectUiController::open_world_context(ThingId id, UiPoint point, std::int32_t family, std::uint32_t mesh) {
    state_->open_context(id, point, false, family, mesh);
}

void WorldParkObjectUiController::open_cell_context(CellId id, UiPoint point) {
    state_->open_context(0, point, false, 33, 0, id);
}

void WorldParkObjectUiController::open_statistics(ThingId id) {
    state_->open_statistics(id);
}

void WorldParkObjectUiController::open_opinions(ThingId id) {
    state_->open_opinions(id);
}

void WorldParkObjectUiController::open_upgrade(ThingId id) {
    state_->open_upgrade(id);
}

void WorldParkObjectUiController::open_research() {
    state_->open_research();
}

void WorldParkObjectUiController::preferences(const UiPreferences& p) {
    state_->confirm_deletions = p.confirm_delete_on != 0;
    state_->animate_menus = p.animate_menus != 0;
}

void WorldParkObjectUiController::advance() {
    state_->refresh();
}

void WorldParkObjectUiController::retire(const ThingHeader& record) {
    state_->model.retire(record);
}

void WorldParkObjectUiController::handle_event(const WorldEvent& event) {
    state_->model.handle_event(event);
}

bool WorldParkObjectUiController::simulation_paused() const {
    return state_->message_scope.advisor_paused();
}

WorldParkObjectUiRuntime& WorldParkObjectUiController::model() {
    return state_->model;
}

void WorldParkObjectUiController::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    state_->append(draw, control, clip, width, height);
}

bool WorldParkObjectUiController::dismiss_outside(UiPoint point) {
    if (!state_->context || state_->context->rect.contains(point)) {
        return false;
    }
    state_->ui.destroy(state_->context);
    state_->context = nullptr;
    state_->restore_input();
    return true;
}

void WorldParkObjectUiController::close_context() {
    auto& s = *state_;
    if (!s.context) {
        return;
    }
    if (s.context && s.ui.owns(s.context)) {
        s.ui.destroy(s.context);
    }
    s.context = nullptr;
    s.restore_input();
}
} //namespace simcoaster
