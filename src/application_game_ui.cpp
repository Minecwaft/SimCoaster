#include "application_game_ui_internal.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
bool escape(const UiEvent& e) {
    return e.type == 65547
        && (e.id > 255 ? e.id >> 8 : e.id) == 27
        && !(static_cast<std::uint32_t>(e.value) & 0xFF0000u);
}
} //namespace

ApplicationGameUiController::State::State(WorldRuntime& context,
                                          UiRuntime& runtime,
                                          UiControl* parent,
                                          const std::filesystem::path& path,
                                          ApplicationGameUiHost callbacks)
    : c(context),
      ui(runtime),
      primary(parent),
      game_root(path),
      host(std::move(callbacks)) {
    if (!ui.owns(primary)
        || host.language.empty()
        || !host.status
        || !host.viewport
        || !host.list_files
        || !host.pause
        || !host.prepare_menu
        || !host.open_options
        || !host.play_ui_sound)
        throw std::invalid_argument("Application UI requires complete session bindings");
}

ApplicationGameUiController::State::~State() {
    close();
}

UiFontLibrary& ApplicationGameUiController::State::font_library() {
    if (!fonts)
        fonts = std::make_unique<UiFontLibrary>(game_root, host.language);
    return *fonts;
}

void ApplicationGameUiController::State::begin(Page next, bool dismiss, bool pause) {
    close();
    if (dismiss && host.panels.dismiss)
        host.panels.dismiss();
    page = next;
    if (pause && host.status().gameplay_running) {
        host.pause(true);
        owns_pause = true;
    }
}

void ApplicationGameUiController::State::close_message() {
    auto* old = std::exchange(message_wrapper, nullptr);
    if (ui.owns(old))
        ui.destroy(old);
    message_root = nullptr;
    message_input_root = nullptr;
    message_input_serial = 0;
    cancel_message = accept_message = {};
}

void ApplicationGameUiController::State::close() {
    if (closing)
        return;
    closing = true;
    close_message();
    auto* old = wrapper ? wrapper : root;
    if (ui.owns(old))
        ui.destroy(old);
    wrapper = root = list = nullptr;
    hover.clear();
    entries.clear();
    pending.reset();
    requests.clear();
    page = Page::none;
    coaster = 0;
    coaster_identity = nullptr;
    if (std::exchange(commands_disabled, false))
        c.ports.ui.restore_command_families_state(c, false);
    if (std::exchange(owns_pause, false))
        host.pause(false);
    ui.set_input_root(nullptr);
    if (host.panels.synchronize_focus)
        host.panels.synchronize_focus();
    else
        ui.set_focus(primary);
    closing = false;
}

void ApplicationGameUiController::State::dialog(std::span<const unsigned char> bytes, std::uint32_t id) {
    //4F4470: the full-canvas wrapper owns lolight and the parsed root.
    wrapper = &ui.create(nullptr, UiControlKind::window, 1, 1, { 0, 0, 2047, 1535 });
    wrapper->secondary_asset_hash = ui_model_asset_hash("lolight");
    auto parsed = ui.parse_template(wrapper, std::as_bytes(bytes));
    if (!parsed)
        throw std::runtime_error("Application template: " + parsed.error().reason);
    root = &required(wrapper, id);
    bind(*root, [this](auto& sender, const auto& e) {
        return event(sender, e);
    });
    ui.set_primary_callback(*wrapper, [this](UiControl& sender, const UiEvent& e) {
        if (e.type == 4) {
            close();
            return std::intptr_t{};
        }
        return ui.secondary(sender, e);
    });
    ui.place_modal_layer(*wrapper);
    ui.set_input_root(wrapper);
    ui.set_focus(root);
}

void ApplicationGameUiController::State::message(std::u16string heading,
                                                 std::u16string body,
                                                 std::function<void()> cancel,
                                                 std::function<void()> accepted,
                                                 UiMessageButtonKind first) {
    close_message();
    //4F47F0 splits any untitled message at its first double newline, including
    //the Profile screen's Quit confirmation, not only message_resource calls.
    split_ui_message_title(heading, body);
    modal_focus.capture(ui, ui.focus());
    message_input_root = ui.input_root();
    message_input_serial = ui.lifetime_serial(message_input_root);
    message_scope.acquire(c.simulation.current && c.simulation.current->saved.state != WorldState::transition);
    message_scope.acquire_gameplay(host.status().gameplay_running, host.pause);
    cancel_message = std::move(cancel);
    accept_message = std::move(accepted);
    message_wrapper = &create_ui_message_wrapper(ui);
    const auto parsed = ui.parse_template(message_wrapper, std::as_bytes(ui_message_template()));
    if (!parsed)
        throw std::runtime_error(parsed.error().reason);
    message_root = &required(message_wrapper, 60000);
    bind(*message_root, [this](UiControl& sender, const UiEvent& e) -> std::intptr_t {
        if (e.type == 20 && &sender == message_root) {
            message_root = message_wrapper = nullptr;
            cancel_message = accept_message = {};
            message_scope.release();
            if (!closing) {
                ui.set_input_root(message_input_serial && ui.lifetime_serial(message_input_root) == message_input_serial
                                      ? message_input_root
                                  : wrapper ? wrapper
                                            : root);
                ui.set_focus(modal_focus.resolve(ui, primary));
            }
            return ui.secondary(sender, e);
        }
        const bool choice = e.type == 256 && (e.id == 60001 || e.id == 60002 || e.id == 60003);
        if (choice || e.type == 4 || e.type == 5 || escape(e)) {
            auto action = !choice       ? std::function<void()>{}
                        : e.id == 60002 ? std::move(accept_message)
                                        : std::move(cancel_message);
            close_message();
            if (action)
                action();
            return 0;
        }
        return ui.secondary(sender, e);
    });
    ui.set_primary_callback(*message_wrapper, [this](UiControl& sender, const UiEvent& e) {
        if (e.type == 4) {
            close_message();
            return std::intptr_t{};
        }
        return ui.secondary(sender, e);
    });
    //501D50 creates a primary shadow caption with font5/LEFT/MIDDLE.
    message_controls = bind_ui_message_controls(bindings, message_root, "Original application control absent: ");
    present_message(message_wrapper, message_root, message_controls, heading, body, accept_message, first);
}

void ApplicationGameUiController::State::message_resource(std::uint32_t row, std::function<void()> accepted) {
    const auto first = accepted ? UiMessageButtonKind::cancel : UiMessageButtonKind::okay;
    message({}, text(row), {}, std::move(accepted), first);
}

void ApplicationGameUiController::State::enqueue(ApplicationGameRequest request, bool retain) {
    if (pending)
        throw std::logic_error("Application UI operation already pending");
    request.serial = next_request++;
    if (!retain)
        close();
    else {
        pending = request;
        if (ui.owns(root))
            ui.set_enabled(*root, false);
    }
    requests.push_back(std::move(request));
}

std::intptr_t ApplicationGameUiController::State::event(UiControl& sender, const UiEvent& e) {
    if (closing)
        return ui.secondary(sender, e);
    if (e.type == 20 && &sender == root) {
        close();
        return ui.secondary(sender, e);
    }
    if (pending)
        return ui.secondary(sender, e);
    if (e.type == 4 || e.type == 5 || escape(e)) {
        if (page == Page::load)
            enqueue({ .payload = ApplicationCancelLoadRequest{ .from_profile = profile_load } });
        else
            close();
        return e.type == 5;
    }
    match (page) {
        case Page::menu => return menu_event(sender, e);
        case Page::load or Page::save => return file_event(sender, e);
        case Page::coaster_name => return coaster_event(sender, e);
        case Page::none => return ui.secondary(sender, e);
        case _ => {}
    }
    return ui.secondary(sender, e);
}

ApplicationGameUiController::ApplicationGameUiController(WorldRuntime& c,
                                                         UiRuntime& ui,
                                                         UiControl* primary,
                                                         const std::filesystem::path& path,
                                                         ApplicationGameUiHost host)
    : state_(std::make_unique<State>(c, ui, primary, path, std::move(host))) {}

ApplicationGameUiController::~ApplicationGameUiController() = default;

void ApplicationGameUiController::open_menu(bool retain) {
    state_->open_menu(retain);
}

void ApplicationGameUiController::request_quit_confirmation() {
    state_->transition(ApplicationQuitRequest{});
}

void ApplicationGameUiController::open_load(bool profile) {
    state_->open_load(profile);
}

void ApplicationGameUiController::open_save(bool coaster) {
    state_->open_save(coaster);
}

void ApplicationGameUiController::open_coaster_save() {
    state_->open_coaster_save();
}

void ApplicationGameUiController::close() {
    state_->close();
}

bool ApplicationGameUiController::is_open() const {
    return state_->root || state_->message_root;
}

bool ApplicationGameUiController::menu_visible() const {
    return state_->page == State::Page::menu && state_->root;
}

UiControl* ApplicationGameUiController::root() const {
    return state_->root;
}

std::vector<ApplicationGameRequest> ApplicationGameUiController::take_requests() {
    return std::exchange(state_->requests, {});
}

void ApplicationGameUiController::complete(std::uint64_t serial, std::optional<ApplicationUiError> error) {
    auto& s = *state_;
    if (!s.pending || s.pending->serial != serial)
        return;
    const bool deleted = std::holds_alternative<ApplicationDeleteParkRequest>(s.pending->payload)
                      || std::holds_alternative<ApplicationDeleteCoasterRequest>(s.pending->payload);
    const bool online = std::holds_alternative<ApplicationGoOnlineRequest>(s.pending->payload);
    s.pending.reset();
    if (s.ui.owns(s.root))
        s.ui.set_enabled(*s.root, true);
    if (error) {
        s.message(std::move(error->title), std::move(error->body), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    if (deleted)
        s.refresh_files();
    else if (online && s.page == State::Page::menu)
        s.ui.set_focus(s.root);
    else
        s.close();
}

void ApplicationGameUiController::refresh_files() {
    state_->refresh_files();
}

void ApplicationGameUiController::report_error(ApplicationUiError error) {
    state_->message(std::move(error.title), std::move(error.body), {}, {}, UiMessageButtonKind::okay);
}

void ApplicationGameUiController::show_message(std::u16string body, bool question, std::function<void()> accepted) {
    state_->message({}, std::move(body), {}, question ? std::move(accepted) : std::function<void()>{});
}

void ApplicationGameUiController::handle_event(const WorldEvent& e) {
    if (e.type() == 33 && state_->coaster && e.get<RetirementEvent>().retired_id == state_->coaster)
        state_->close();
}
} //namespace simcoaster
