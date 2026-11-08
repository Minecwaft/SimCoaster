#include "application_postcard_ui_internal.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
bool postcard_escape(const UiEvent& event) {
    return event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && !(static_cast<std::uint32_t>(event.value) & 0xFF0000u);
}

ApplicationPostcardUiController::State::State(WorldRuntime& context, UiRuntime& runtime, UiControl* owner, const std::filesystem::path& path, ApplicationPostcards& storage, ApplicationPostcardUiHost callbacks) : c(context), ui(runtime), parent(owner), game_root(path), store(storage), host(std::move(callbacks)), fonts(path, host.language) {
    if (!ui.owns(parent) || !host.capture_scene || !host.viewport || !host.composition_transition || !host.restore_after_capture || !host.online_status || !host.go_online || !host.send || !host.cancel_send || !host.pause || !host.report_error || !host.status_message) {
        throw std::invalid_argument("Postcard UI requires complete application/profile/render/transport " "bindings");
    }
}

ApplicationPostcardUiController::State::~State() {
    close();
    std::error_code ignored;
    for (const auto& image : temporary_images) {
        std::filesystem::remove(image, ignored);
    }
    if (!temporary_directory.empty()) {
        std::filesystem::remove(temporary_directory, ignored);
    }
}

UiControl& ApplicationPostcardUiController::State::control(std::uint32_t id) const {
    return required(root, id);
}

void ApplicationPostcardUiController::State::title(UiControl& owner, std::uint32_t id, std::uint32_t row) {
    auto& control = required(&owner, id);
    control.has_primary_text_attachment = true;
    caption(control, text(row), 5, 1, 1);
}

void ApplicationPostcardUiController::State::edit(UiControl& control, std::size_t capacity, std::uint32_t font, std::int32_t vertical) {
    caption(control, {}, font, 0, vertical);
    control.text_limit = capacity;
    control.edit_caret_asset_hash = ui_model_asset_hash("hilight");
}

void ApplicationPostcardUiController::State::list(UiControl& control, std::span<const std::uint32_t> rows) {
    control.font = 7;
    control.row_height_font_padding = 6;
    control.list_highlight_asset_hash = ui_model_asset_hash("hilight");
    control.column_presentations.assign(rows.size(), {});
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto& column = control.column_presentations[i];
        column.font = 7;
        column.vertical_alignment = 1;
        auto& header = required(&control, 16 + static_cast<std::uint32_t>(i));
        header.asset_hash = ui_model_asset_hash("!cbut");
        for (std::size_t style = 0; style < 3; ++style) {
            header.button_text_colors[style] = { 255, 255, 255, 255 };
        }
        caption(header, text(rows[i]), 8, 1, 1);
        header.text_constraints = { true, true };
    }
}

void ApplicationPostcardUiController::State::pause() {
    if (!paused) {
        host.pause(true);
        paused = true;
    }
}

void ApplicationPostcardUiController::State::release_pause() {
    if (paused && !root && !message_wrapper && !composition_polls) {
        host.pause(false);
        paused = false;
    }
}

void ApplicationPostcardUiController::State::begin(Page next) {
    close();
    if (host.panels.dismiss) {
        host.panels.dismiss();
    }
    page = next;
    profile_revision = store.profile_revision();
    pause();
}

void ApplicationPostcardUiController::State::close_panel() {
    close_book();
    auto* old = wrapper ? wrapper : root;
    auto* registered = root;
    root = wrapper = nullptr;
    if (ui.owns(old) && !old->destroying) {
        ui.destroy(old);
    }
    page = Page::none;
    preview.reset();
    hires.reset();
    preview_image.reset();
    outbox_image.reset();
    outbox_records.clear();
    actions.clear();
    dragging = false;
    if (commands_disabled) {
        c.ports.ui.restore_command_families_state(c, false);
        commands_disabled = false;
    }
    if (host.panels.deactivate && registered) {
        host.panels.deactivate(registered);
    }
    ui.set_input_root(message_wrapper);
    if (!message_wrapper && host.panels.synchronize_focus) {
        host.panels.synchronize_focus();
    }
    release_pause();
}

void ApplicationPostcardUiController::State::close() {
    if (closing) {
        return;
    }
    closing = true;
    composition_polls.reset();
    release_composition_transition();
    if (send_serial) {
        host.cancel_send(std::exchange(send_serial, 0));
    }
    close_message();
    close_panel();
    pending_send.clear();
    pending_postcard.reset();
    pending_image.reset();
    release_pause();
    closing = false;
}

void ApplicationPostcardUiController::State::close_message() {
    auto* old = std::exchange(message_wrapper, nullptr);
    if (ui.owns(old) && !old->destroying) {
        ui.destroy(old);
    }
    message_root = nullptr;
    message_cancel = message_accept = {};
}

void ApplicationPostcardUiController::State::message(std::u16string heading, std::u16string body, std::function<void()> cancel, std::function<void()> accepted) {
    close_message();
    pause();
    split_ui_message_title(heading, body);
    modal_focus.capture(ui, ui.focus());
    message_scope.acquire(c.simulation.current && c.simulation.current->saved.state != WorldState::transition);
    message_cancel = std::move(cancel);
    message_accept = std::move(accepted);
    message_wrapper = &create_ui_message_wrapper(ui);
    message_root = &parse(message_wrapper, ui_message_template(), 60000, [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
        if (event.type == 20 && &sender == message_root) {
            message_root = message_wrapper = nullptr;
            message_cancel = message_accept = {};
            message_scope.release();
            if (!closing) {
                ui.set_input_root(entry ? entry : book ? book : wrapper ? wrapper : root);
                ui.set_focus(modal_focus.resolve(ui, root ? root : parent));
            }
            release_pause();
            return 0;
        }
        if (send_serial) {
            return ui.secondary(sender, event);
        }
        const bool choice = event.type == 256 && (event.id == 60001 || event.id == 60002 || event.id == 60003);
        if (choice || event.type == 4 || event.type == 5 || postcard_escape(event)) {
            auto action = event.id == 60002 ? std::move(message_accept) : std::move(message_cancel);
            close_message();
            if (action) {
                action();
            }
            return 0;
        }
        return ui.secondary(sender, event);
    });
    message_controls = bind_ui_message_controls(bindings, message_root, "Original postcard template missing control ");
    present_message<false>(message_wrapper, message_root, message_controls, heading, body, message_accept);
}

void ApplicationPostcardUiController::State::resource_message(std::uint32_t row, std::function<void()> accepted, std::function<void()> cancel) {
    message({}, text(row), std::move(cancel), std::move(accepted));
}

void ApplicationPostcardUiController::State::error(const PostcardError& problem) {
    host.report_error(problem);
}

void ApplicationPostcardUiController::State::advance() {
    if ((root || message_wrapper || composition_polls) && profile_revision != store.profile_revision()) {
        close();
        return;
    }
    if (page == Page::composer && root && preview_deadline > 0 && fixed::word(static_cast<std::uint32_t>(ui.clock().count())) > fixed::add(preview_deadline, 1000)) {
        preview_deadline = -1;
        refresh_preview();
    }
}

void ApplicationPostcardUiController::State::append(UiDrawList& draw, const UiControl& target, UiRect clip, std::uint32_t width, std::uint32_t height) {
    pixel_width = std::max(width, 1u);
    pixel_height = std::max(height, 1u);
    if (page == Page::composer && ui.owns(root) && &target == &control(37001) && preview_image && !book) {
        append_postcard_surface(draw, preview_image, target, clip);
    } else if (page == Page::outbox && ui.owns(root)) {
        if (target.id == 570001 && (outbox_resolution != std::optional { std::array { pixel_width, pixel_height } } || outbox_scroll_dirty)) {
            outbox_scroll();
        }
        if (&target == &control(570003)) {
            append_postcard_surface(draw, outbox_image, target, clip);
        }
    }
}

ApplicationPostcardUiController::ApplicationPostcardUiController(WorldRuntime& c, UiRuntime& ui, UiControl* parent, const std::filesystem::path& path, ApplicationPostcards& store, ApplicationPostcardUiHost host) : state_(std::make_unique<State>(c, ui, parent, path, store, std::move(host))) {}

ApplicationPostcardUiController::~ApplicationPostcardUiController() = default;

void ApplicationPostcardUiController::open_composer() {
    state_->open_composer();
}

void ApplicationPostcardUiController::open_outbox() {
    state_->open_outbox();
}

void ApplicationPostcardUiController::open_address_book() {
    state_->open_book();
}

void ApplicationPostcardUiController::close() {
    state_->close();
}

bool ApplicationPostcardUiController::is_open() const {
    return state_->root || state_->message_wrapper || state_->composition_polls;
}

UiControl* ApplicationPostcardUiController::root() const {
    return state_->root ? state_->root : state_->message_root;
}

void ApplicationPostcardUiController::advance() {
    state_->advance();
}

void ApplicationPostcardUiController::poll_composition() {
    state_->poll_composition();
}

void ApplicationPostcardUiController::append(UiDrawList& draw, const UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    state_->append(draw, control, clip, width, height);
}

void ApplicationPostcardUiController::complete_send(std::uint64_t serial, bool success, std::int32_t detail) {
    state_->complete_send(serial, success, detail);
}
} //namespace simcoaster
