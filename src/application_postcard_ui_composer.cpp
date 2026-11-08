#include "application_postcard_ui_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <chrono>
#include <ctime>
#include <format>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char composer_words[] {
#embed "ui_templates/app_postcard_composer.bin"
};
static_assert(sizeof(composer_words) == 436);

void tag(WorldRuntime& c, std::int32_t code) {
    if (!c.simulation.event_registry) {
        return;
    }
    WorldEvent event { TagMailEvent { code, 0, 0, -1 } };
    dispatch_world_event_to_subscribers(c, c.simulation.event_registry, &event, deliver_world_record_event, nullptr);
}
} //namespace

void ApplicationPostcardUiController::State::open_composer() {
    if (root || composition_polls) {
        return;
    }
    close();
    (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 149, 0, 0, 0);
    page = Page::composer;
    profile_revision = store.profile_revision();
    try {
        pause();
        composition_hidden = true;
        host.composition_transition(true);
        c.ports.ui.clear_control_particle_group(c);
        stop_active_advisor_response(c, AdvisorStopTargetAction::parameter_request);
        composition_polls = 3;
    } catch (...) {
        close();
        throw;
    }
}

void ApplicationPostcardUiController::State::release_composition_transition() {
    if (std::exchange(composition_hidden, false)) {
        host.composition_transition(false);
    }
}

void ApplicationPostcardUiController::State::poll_composition() {
    if (!composition_polls || *composition_polls <= 0) {
        return;
    }
    if (profile_revision != store.profile_revision()) {
        close();
        return;
    }
    if (--*composition_polls) {
        return;
    }
    try {
        begin_composer();
        composition_polls.reset();
    } catch (...) {
        close();
        throw;
    }
}

void ApplicationPostcardUiController::State::begin_composer() {
    //571EB0 captures the last actual secondary surface before restoring a POI/
    //ride view or showing its HUD. The host performs that platform boundary.
    auto scene = host.capture_scene();
    if (!scene) {
        close();
        error(scene.error());
        return;
    }
    release_composition_transition();
    host.restore_after_capture();
    const auto status = host.online_status();
    if (status.session_active && !status.permission) {
        close();
        host.status_message(73);
        return;
    }
    if (host.panels.dismiss) {
        host.panels.dismiss();
    }
    try {
        preview = std::make_unique<PostcardImageComposer>(game_root, host.language, fonts, *scene, false);
        hires = std::make_unique<PostcardImageComposer>(game_root, host.language, fonts, *scene, true);
        root = &parse(parent, composer_words, 37000, panel_callback<&State::composer_event>());
        title(*root, 37007, 739);
        edit(control(37004), 511, 11);
        control(37004).text_constraints = { true, false };
        edit(control(37003), 95, 11);
        control(37003).text_constraints = { false, false };
        edit(control(37002), 255, 11);
        control(37002).text_constraints = { true, false };
        ui.set_text(control(37004), text(742));
        ui.set_text(control(37003), text(740));
        ui.set_text(control(37002), text(741));
        body_edited = subject_edited = recipients_edited = false;
        preview_deadline = 0;
        ui.set_enabled(control(37006), false);
        auto& surface = control(37001);
        ui.set_primary_callback(surface, [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
            if (event.type == 65541) {
                dragging = true;
                drag_previous = { static_cast<std::int32_t>(static_cast<std::uint32_t>(event.value) >> 16), static_cast<std::int32_t>(static_cast<std::uint32_t>(event.value) & 65535u) };
                ui.set_capture(&sender);
            } else if (event.type == 65539 && dragging && ui.capture() == &sender) {
                const UiPoint point { static_cast<std::int32_t>(event.id >> 16), static_cast<std::int32_t>(event.id & 65535u) };
                drag_offset.x = std::max(0, drag_offset.x + static_cast<std::int32_t>(pixel_width) * (drag_previous.x - point.x) / 2048);
                drag_offset.y = std::max(0, drag_offset.y + static_cast<std::int32_t>(pixel_height) * (drag_previous.y - point.y) / 1536);
                drag_previous = point;
                //Native5706C0's offset globals have no rendering readers. Preserve
                //input/capture state; moving the captured photo would invent a
                //crop.
            } else if (event.type == 65540) {
                dragging = false;
                if (ui.capture() == &sender) {
                    ui.set_capture(nullptr);
                }
            }
            return ui.secondary(sender, event);
        });
        preview_image = std::make_shared<TextureImage>(preview->image());
        c.ports.ui.disable_command_families_and_set_disabled_flag(c);
        commands_disabled = true;
        if (host.panels.activate) {
            host.panels.activate(root);
        }
        ui.set_focus(root);
        ui.set_input_root(root);
    } catch (...) {
        close();
        throw;
    }
}

void ApplicationPostcardUiController::State::refresh_preview(bool draw_text) {
    if (!preview || !root) {
        return;
    }
    preview->compose(control(37002).text, draw_text);
    preview_image = std::make_shared<TextureImage>(preview->image());
}

void ApplicationPostcardUiController::State::submit() {
    if (!root || page != Page::composer || send_serial) {
        return;
    }
    if (!body_edited) {
        ui.set_text(control(37002), text(755));
    }
    if (!subject_edited) {
        ui.set_text(control(37003), text(754));
    }
    refresh_preview();
    auto recipients = parse_postcard_recipients(control(37004).text);
    if (!recipients) {
        resource_message(752);
        return;
    }
    hires->compose(control(37002).text);
    auto album = store.update_album(hires->image());
    if (!album) {
        error(album.error());
        return;
    }
    PostcardRecord postcard;
    postcard.subject = postcard_to_ansi(control(37003).text);
    const auto status = host.online_status();
    postcard.from = status.session_active ? status.from : "nobody@nowhere.net";
    for (const auto& recipient : *recipients) {
        postcard.recipients.push_back(postcard_to_ansi(recipient));
    }
    const auto now = std::time(nullptr);
    std::tm local {};
    if (localtime_s(&local, &now) != 0) {
        throw std::runtime_error("Cannot read postcard local date");
    }
    postcard.date = { local.tm_mday, local.tm_mon + 1, local.tm_year + 1900 };
    if (temporary_directory.empty()) {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary_directory = std::filesystem::temp_directory_path() / std::format("SimCoaster-postcards-{}", token);
        std::error_code failure;
        std::filesystem::create_directory(temporary_directory, failure);
        if (failure) {
            error({ temporary_directory, failure.message() });
            return;
        }
    }
    postcard.image_path = temporary_directory / std::format("{:08}.jpg", temporary_images.size());
    auto saved = save_postcard_jpeg(postcard.image_path, preview->image());
    if (!saved) {
        error(saved.error());
        return;
    }
    temporary_images.push_back(postcard.image_path);
    pending_postcard = std::move(postcard);
    pending_image = std::make_shared<TextureImage>(preview->image());
    close_panel();
    if (status.session_active) {
        start_send();
    } else {
        message(
            text(749),
            text(750),
            [this] {
                store_offline();
            },
            [this] {
                host.go_online();
            });
    }
}

void ApplicationPostcardUiController::State::store_offline() {
    if (!pending_postcard || !pending_image || profile_revision != store.profile_revision()) {
        return;
    }
    auto stored = store.store(*pending_postcard, *pending_image);
    if (!stored) {
        error(stored.error());
        return;
    }
    pending_postcard.reset();
    pending_image.reset();
    tag(c, 30);
}

void ApplicationPostcardUiController::State::start_send() {
    if (send_serial) {
        return;
    }
    const auto status = host.online_status();
    if (status.session_active && !status.permission) {
        host.status_message(73);
        return;
    }
    pending_send.clear();
    sending_from_outbox = !pending_postcard;
    if (pending_postcard) {
        pending_send.push_back(*pending_postcard);
    } else {
        for (const auto id : store.index().send_queue) {
            auto record = store.read(id);
            if (!record) {
                error(record.error());
                pending_send.clear();
                return;
            }
            pending_send.push_back(std::move(*record));
        }
    }
    if (pending_send.empty()) {
        return;
    }
    //Actual Mail::Connect rejects a zero-recipient sender as detail8. Do not
    //misreport the delimiter-only UI quirk as successful delivery.
    if (std::ranges::any_of(pending_send, [](const auto& record) {
            return record.recipients.empty();
        })) {
        pending_send.clear();
        return;
    }
    send_profile_revision = store.profile_revision();
    send_serial = next_send++;
    message({}, text(761));
    ui.set_visible(required(message_root, 60001), false);
    ui.set_visible(required(message_root, 60003), false);
    host.send(send_serial, pending_send);
}

void ApplicationPostcardUiController::State::complete_send(std::uint64_t serial, bool success, std::int32_t detail) {
    if (!serial || serial != send_serial || send_profile_revision != store.profile_revision()) {
        return;
    }
    send_serial = 0;
    close_message();
    if (success) {
        if (sending_from_outbox) {
            for (const auto& record : pending_send) {
                auto erased = store.sent(record.id);
                if (!erased) {
                    error(erased.error());
                    break;
                }
            }
        }
        tag(c, 29);
    } else {
        std::optional<std::uint32_t> row;
        if (detail == 0) {
            row = 51;
        } else if (detail == 1) {
            row = 56;
        } else if (detail == 2) {
            row = 57;
        } else if (detail == 3 || detail == 4) {
            row = 58;
        }
        if (row) {
            host.status_message(*row);
            tag(c, 31);
        }
    }
    pending_send.clear();
    pending_postcard.reset();
    pending_image.reset();
    release_pause();
}

std::intptr_t ApplicationPostcardUiController::State::composer_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == root) {
        close_panel();
        return 0;
    }
    if (event.type == 4 || event.type == 5 || postcard_escape(event)) {
        close_panel();
        return event.type == 5;
    }
    if (event.type == 2049) {
        if (event.id == 37002) {
            body_edited = true;
            preview_deadline = fixed::word(static_cast<std::uint32_t>(ui.clock().count()));
        } else if (event.id == 37003) {
            subject_edited = true;
        } else if (event.id == 37004) {
            recipients_edited = true;
            ui.set_enabled(control(37006), parse_postcard_recipients(control(37004).text).has_value());
        }
    } else if (event.type == 256) {
        if (event.id == 37006) {
            submit();
            return 0;
        }
        if (event.id == 37008) {
            open_book();
            return 0;
        }
        if (event.id == 37005 || event.id == 4294967294u) {
            close_panel();
            return 0;
        }
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
