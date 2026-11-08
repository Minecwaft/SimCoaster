#include "simcoaster/require_bound.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_theme.hpp"
#include "world_ui_management_internal.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
std::vector<std::byte> read_resource(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    if (!input)
        throw std::runtime_error("Management UI resource unavailable: " + path.native_encoded_string());
    const auto length = input.tellg();
    if (length < 0)
        throw std::runtime_error("Management UI resource size: " + path.native_encoded_string());
    std::vector<std::byte> result(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!input)
        throw std::runtime_error("Management UI resource read: " + path.native_encoded_string());
    return result;
}

bool escape(const UiEvent& e) {
    return e.type == 65547
        && (e.id > 255 ? e.id >> 8 : e.id) == 27
        && (static_cast<std::uint32_t>(e.value) & 0xFF0000u) == 0;
}
} //namespace

WorldManagementUiController::State::State(WorldRuntime& context,
                                          UiRuntime& runtime,
                                          UiControl* parent,
                                          const std::filesystem::path& path,
                                          WorldManagementUiHost callbacks)
    : c(context),
      ui(runtime),
      primary(parent),
      game_root(path),
      host(std::move(callbacks)) {
    if (!ui.owns(primary) || host.language.empty())
        throw std::invalid_argument("Management UI requires primary root and language");
    //Native CRT initializers select one-year/current-visitors and bank balance.
    c.ui.panels.financial_statistics.graph.selected_series_mask = 1;
    c.ui.panels.park_statistics.selected_series_mask = 1;
    const auto language = game_root / "Data" / "Language" / host.language;
    challenge_generic = read_resource(language / "CHALLENGE_GENERIC.str");
    challenge_specific = read_resource(language / "CHALLENGE_SPECIFIC.str");
}

WorldManagementUiController::State::~State() {
    close();
}

UiControl& WorldManagementUiController::State::parse(std::span<const unsigned char> bytes, std::uint32_t id) {
    return parse_ui_panel(ui, primary, bytes, id, panel_callback<&State::event>(), &roots, { "Management template word ", true, "Management template root missing" });
}

World& WorldManagementUiController::State::world() const {
    return require_bound(c.referenced_world(), "Management primary World is absent");
}

BankRecord& WorldManagementUiController::State::bank() const {
    auto* p = c.entities.at(world().saved.bank_account).payload;
    if (!p || p->kind != ThingKind::bank_account)
        throw std::logic_error("Management bank is absent");
    return *reinterpret_cast<BankRecord*>(p);
}

ParkAnalyserState& WorldManagementUiController::State::analyser() const {
    auto* p = c.entities.at(world().saved.park_analyser).payload;
    if (!p || p->kind != ThingKind::park_analyser)
        throw std::logic_error("Management analyser is absent");
    return *reinterpret_cast<ParkAnalyserState*>(p);
}

ParkObjectRuntimeView& WorldManagementUiController::State::gates() const {
    const auto id = page == Page::gates && gate_target ? gate_target : world().saved.park_gates;
    auto* p = c.entities.at(id).payload;
    if (!p || p->kind != ThingKind::park_object || p->retirement_pending)
        throw std::logic_error("Management park gates are absent");
    return *reinterpret_cast<ParkObjectRuntimeView*>(p);
}

UiFontLibrary& WorldManagementUiController::State::fonts() {
    if (!local_fonts)
        local_fonts = std::make_unique<UiFontLibrary>(game_root, host.language);
    return *local_fonts;
}

void WorldManagementUiController::State::title(std::uint32_t id, std::u16string_view value) {
    auto& control = required(root, id);
    caption(control, value, 5, 0, 1);
    control.primary_shadow_text = true;
}

void
WorldManagementUiController::State::scalar(std::uint32_t id, std::int32_t value, bool currency, std::uint32_t font) {
    auto& control = required(root, id);
    control.font = font;
    control.horizontal_alignment = 0;
    control.vertical_alignment = 1;
    control.scalar_text = currency ? UiScalarTextKind::signed_currency : UiScalarTextKind::signed_integer;
    ui.set_value(control, value);
}

void WorldManagementUiController::State::edit(UiControl& control,
                                              std::int32_t minimum,
                                              std::int32_t maximum,
                                              std::int32_t value,
                                              std::u16string_view affix) {
    ui.set_range(control, minimum, maximum);
    control.step_size = 1;
    //Native501E70 resolves child3. IDs1/2 are the stepper's arrow buttons;
    //binary template command19 records the actual editor association.
    auto* entry = control.associated_edit;
    if (control.kind != UiControlKind::numeric_stepper
        || !ui.owns(entry)
        || entry->parent != &control
        || entry->id != 3
        || entry->kind != UiControlKind::edit_box)
        throw std::logic_error("Management numeric stepper editor missing");
    entry->font = 6;
    entry->default_text_y_offset = 9;
    entry->horizontal_alignment = entry->vertical_alignment = 1;
    //The original six-wchar buffer includes its terminating NUL.
    entry->text_limit = 5;
    entry->text_affix = affix;
    entry->edit_caret_asset_hash = ui_model_asset_hash("lolight");
    entry->numeric_only = true;
    entry->text_constraints = { true, true };
    ui.set_value(control, value);
}

void WorldManagementUiController::State::stage_advisor(std::int32_t action) {
    advisor_action = action;
    if (action < 0)
        return;
    auto* p = c.entities.at(c.simulation.current->saved.advisor).payload;
    if (!p || p->kind != ThingKind::advisor)
        throw std::logic_error("Management Advisor is absent");
    auto& advisor = *reinterpret_cast<AdvisorRecord*>(p);
    advisor.from_ui_panel = 1;
    advisor.delayed_message = action;
    advisor.delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
}

void WorldManagementUiController::State::help_advisor(std::int32_t action) {
    queue_world_advisor_help_action(c, action);
}

void WorldManagementUiController::State::cancel_advisor(std::int32_t action) {
    if (action < 0)
        return;
    if (auto* p = c.entities.at(c.simulation.current->saved.advisor).payload; p && p->kind == ThingKind::advisor) {
        auto& advisor = *reinterpret_cast<AdvisorRecord*>(p);
        advisor.from_ui_panel = advisor.panel_up = 0;
        world_operations::cancel_advisor_action_by_id(c, &advisor, action);
    }
}

void WorldManagementUiController::State::begin(Page next) {
    close();
    if (host.panels.dismiss)
        host.panels.dismiss();
    page = next;
}

void WorldManagementUiController::State::activate() {
    if (host.panels.activate)
        host.panels.activate(root);
    else {
        c.ui.active_root = root;
        ui.set_focus(root);
    }
}

void WorldManagementUiController::State::close_name() {
    auto* old = name_editor;
    if (ui.owns(old))
        ui.destroy(old);
    name_editor = nullptr;
}

void WorldManagementUiController::State::close_message() {
    auto* old = std::exchange(message_wrapper, nullptr);
    if (ui.owns(old))
        ui.destroy(old);
    message_root = nullptr;
    message_accepted = {};
}

void WorldManagementUiController::State::close() {
    if (closing)
        return;
    closing = true;

    struct CloseGuard {
        bool& value;

        ~CloseGuard() {
            value = false;
        }
    } release_close_guard{ closing };

    close_message();
    close_name();
    auto* old_root = root;
    if (page == Page::options && !options_committed)
        commit_options(false);
    if (std::exchange(options_pause_owned, false))
        host.pause_options(false);
    c.ui.panels.financial_statistics.graph.open = false;
    c.ui.panels.loans.open = false;
    c.ui.panels.park_statistics.open = false;
    if (page == Page::awards) {
        c.ui.panels.park_awards.open = false;
        c.ui.panels.park_awards.highlighted_type = -1;
    }
    if (page == Page::challenges)
        cancel_challenge_advisors();
    cancel_advisor(advisor_action);
    advisor_action = -1;
    for (const auto& [body, entry] : scrolling) {
        (void)entry;
        if (ui.owns(body) && host.bind_scrollable_text)
            host.bind_scrollable_text(*body, {});
    }
    for (auto* control : roots)
        if (ui.owns(control))
            ui.destroy(control);
    roots.clear();
    scrolling.clear();
    root = graph_control = list_control = nullptr;
    gate_target = 0;
    page = Page::none;
    if (host.panels.deactivate)
        host.panels.deactivate(old_root);
    else if (c.ui.active_root == old_root)
        c.ui.active_root = nullptr;
    ui.set_input_root(nullptr);
    if (!ui.focus() || ui.focus() == old_root)
        ui.set_focus(primary);
}

std::intptr_t WorldManagementUiController::State::event(UiControl& sender, const UiEvent& e) {
    if (closing || refreshing || !root)
        return ui.secondary(sender, e);
    if (e.type == 20 && &sender == root) {
        close();
        return ui.secondary(sender, e);
    }
    if (e.type == 4 || e.type == 5 || escape(e)) {
        close();
        return e.type == 5;
    }
    match (page) {
        case Page::finance or Page::loans or Page::shares => return finance_event(sender, e);
        case Page::gates or Page::awards or Page::statistics => return park_event(sender, e);
        case Page::challenges => return challenge_event(sender, e);
        case Page::options => return options_event(sender, e);
        case Page::none => return ui.secondary(sender, e);
        case _ => {}
    }
    return ui.secondary(sender, e);
}

void WorldManagementUiController::State::message(std::u16string heading,
                                                 std::u16string body,
                                                 std::function<void()> accepted) {
    close_message();
    split_ui_message_title(heading, body);
    modal_focus.capture(ui, ui.focus());
    message_scope.acquire(c.simulation.current->saved.state != WorldState::transition);
    message_accepted = std::move(accepted);
    message_wrapper = &create_ui_message_wrapper(ui);
    const auto parsed = ui.parse_template(message_wrapper, std::as_bytes(ui_message_template()));
    if (!parsed)
        throw std::runtime_error(parsed.error().reason);
    message_root = &required(message_wrapper, 60000);
    bind(*message_root, [this](UiControl& sender, const UiEvent& e) -> std::intptr_t {
        if (e.type == 20 && &sender == message_root) {
            message_root = nullptr;
            message_wrapper = nullptr;
            message_accepted = {};
            message_scope.release();
            if (!closing) {
                ui.set_input_root(nullptr);
                ui.set_focus(modal_focus.resolve(ui, primary));
            }
            return ui.secondary(sender, e);
        }
        if ((e.type == 256 && (e.id == 60001 || e.id == 60002 || e.id == 60003))
            || e.type == 4
            || e.type == 5
            || escape(e)) {
            auto action = e.type == 256 && e.id == 60002 ? std::move(message_accepted) : std::function<void()>{};
            close_message();
            if (action)
                action();
            return 0;
        }
        return ui.secondary(sender, e);
    });
    ui.set_primary_callback(*message_wrapper, [this](UiControl& sender, const UiEvent& e) {
        if (e.type == 4) {
            ui.destroy(&sender);
            return std::intptr_t{};
        }
        return ui.secondary(sender, e);
    });
    message_controls = bind_ui_message_controls(bindings, message_root, "Original management template has no control ");
    present_message<false>(message_wrapper, message_root, message_controls, heading, body, message_accepted);
}

void WorldManagementUiController::State::refresh() {
    if (!root || closing)
        return;
    match (page) {
        case Page::finance => refresh_finance();
        case Page::loans => refresh_loans();
        case Page::shares => {
            if (c.finance.shares_panel_refresh_pending)
                refresh_shares();
        }
        case Page::awards => refresh_awards();
        case _ => {}
    }
}

void WorldManagementUiController::State::scroll_text(UiControl& body, std::uint32_t scrollbar, std::u16string value) {
    auto& scroll = required(&body, scrollbar);
    caption(body, value, 7, 0, 0);
    body.text_constraints = { true, true };
    if (!host.bind_scrollable_text)
        throw std::logic_error("Management requires shared scrollable-body presentation");
    if (!scrolling.contains(&body))
        host.bind_scrollable_text(body, scrollbar);
    scrolling[&body] = { &scroll, body.attachment_rect };
    if (value.empty()) {
        ui.set_range(scroll, 0, 0);
        ui.set_value(scroll, 0);
        ui.set_visible(scroll, false);
    }
    body.text_offset = { 0, -scroll.value };
}

WorldManagementUiController::WorldManagementUiController(WorldRuntime& c,
                                                         UiRuntime& ui,
                                                         UiControl* primary,
                                                         const std::filesystem::path& path,
                                                         WorldManagementUiHost host)
    : state_(std::make_unique<State>(c, ui, primary, path, std::move(host))) {}

WorldManagementUiController::~WorldManagementUiController() = default;

bool WorldManagementUiController::command(WorldUiCommand command) {
    match (command) {
        case WorldUiCommand::financial_statistics => state_->open_finance();
        case WorldUiCommand::loans => state_->open_loans();
        case WorldUiCommand::shares => state_->open_shares();
        case WorldUiCommand::park_gates => state_->open_gates();
        case WorldUiCommand::park_awards => state_->open_awards({});
        case WorldUiCommand::park_statistics => state_->open_statistics();
        case WorldUiCommand::challenges => state_->open_challenges({});
        case _ => return false;
    }
    return true;
}

void WorldManagementUiController::open_options() {
    state_->open_options();
}

void WorldManagementUiController::open_challenges(std::optional<ChallengeScriptIndex> script) {
    state_->open_challenges(script);
}

void WorldManagementUiController::open_awards(std::optional<std::int32_t> type) {
    state_->open_awards(type);
}

void WorldManagementUiController::close() {
    state_->close();
}

bool WorldManagementUiController::open() const {
    return state_->root != nullptr;
}

bool WorldManagementUiController::simulation_paused() const {
    return state_->message_root || (state_->page == State::Page::options && state_->root);
}

UiControl* WorldManagementUiController::root() const {
    return state_->root;
}

void WorldManagementUiController::advance(std::chrono::milliseconds) {
    state_->refresh();
}

void WorldManagementUiController::handle_event(const WorldEvent& e) {
    if (e.type() == 33
        && state_->page == State::Page::gates
        && e.get<RetirementEvent>().retired_id == state_->gate_target)
        state_->close();
}

void WorldManagementUiController::append(UiDrawList& draw, UiRect clip, std::uint32_t width, std::uint32_t height) {
    auto& s = *state_;
    if (!s.root)
        return;
    s.pixel_width = width;
    s.pixel_height = height;
    if (!s.graph_control || !s.ui.owns(s.graph_control))
        return;
    //Graphs are submitted separately from normal control traversal. Preserve
    //that traversal's ancestor visibility contract when camera/selector modes
    //retain a management page underneath a hidden persistent root.
    for (auto* owner = s.graph_control; owner; owner = owner->parent)
        if (!owner->visible())
            return;
    if (!s.graph_presenter)
        s.graph_presenter = std::make_unique<UiGraphPresenter>(s.fonts());
    const auto& graph = s.page == State::Page::finance    ? s.c.ui.panels.financial_statistics.graph
                      : s.page == State::Page::statistics ? s.c.ui.panels.park_statistics
                                                          : s.shares_graph;
    s.graph_presenter->append(draw, *s.graph_control, graph, s.graph_styles, clip, width, height);
}
} //namespace simcoaster
