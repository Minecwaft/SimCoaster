#include "application_game_ui_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
void ApplicationGameUiController::State::open_menu(bool retain) {
    if (page == Page::menu && root)
        return;
    begin(Page::menu, !retain);
    const auto status = host.status();
    const bool designer = c.mode.game == GameMode::code_3;
    host.prepare_menu(designer);
    const bool transition_world = c.simulation.current && c.simulation.current->saved.state == WorldState::transition;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> items;
    if (designer)
        items = { { 29, 51 },  { status.online_session_active ? 31u : 32u, status.online_session_active ? 767u : 766u },
                  { 33, 770 }, { 28, 771 },
                  { 34, 53 },  { 35, 772 } };
    else if (transition_world)
        items = { { 24, 768 }, { 23, 771 }, { 26, 774 }, { 27, 772 } };
    else {
        items = { { 1, 768 },
                  { 2, 769 },
                  { 6, 774 },
                  { status.online_session_active ? 7u : 8u, status.online_session_active ? 767u : 766u } };
        if (!status.online_services_active)
            items.emplace_back(5, 770);
        items.insert(items.end(), { { 10, 50 }, { 0, 771 }, { 9, 772 } });
    }
    const auto viewport = host.viewport();
    if (!viewport[0] || !viewport[1])
        throw std::logic_error("Command menu viewport is empty");
    auto font = font_library().slot(0, viewport[0]);
    if (!font)
        throw std::runtime_error(font.error().reason);
    const auto count = static_cast<std::int32_t>(items.size());
    const auto line = 1536 * static_cast<std::int32_t>((*font)->line_advance) / static_cast<std::int32_t>(viewport[1]);
    const auto total = count * line + (count - 1) * (7680 / static_cast<std::int32_t>(viewport[1]));
    auto y = 1535 / 2 - total / 2 - (transition_world ? 10 : 5 * count / 2);
    //518ED0 consumes three ordinary CRT draws even though this is only a UI ID.
    const auto first = static_cast<std::uint32_t>(next_crt_random(c));
    const auto second = static_cast<std::uint32_t>(next_crt_random(c));
    const auto random_id = first * second + static_cast<std::uint32_t>(next_crt_random(c));
    //519260/850B50 scan visible descendants, never the hidden tooltip root
    //at depth38. Reserve the menu's child/secondary layers inside clip Z.
    const auto depth = std::min(ui.visible_render_depth() + 1, 35);
    //518ED0 leaves the builder root hidden while its items are appended.
    root = &ui.create(nullptr, UiControlKind::window, 0, random_id, { 0, 0, 2047, 1535 });
    root->asset_hash = ui_model_asset_hash("lolight");
    ui.set_primary_callback(*root, [this](auto& sender, const auto& e) {
        return event(sender, e);
    });
    for (const auto [id, row] : items) {
        const auto label = text(row);
        UiTextStyle style;
        style.wrap = false;
        const auto measured = font_library().measure(**font, label, { 0, 0, 2048, 1536 }, style);
        if (!measured)
            throw std::runtime_error(measured.error().reason);
        const auto width = static_cast<std::int32_t>(
            (measured->bounds[2] - measured->bounds[0] + 1.f) * 2048.f / static_cast<float>(viewport[0]));
        const auto height = static_cast<std::int32_t>(
            (measured->bounds[3] - measured->bounds[1] + 5.f) * 1536.f / static_cast<float>(viewport[1]));
        auto& item = ui.create(root,
                               UiControlKind::window,
                               1,
                               id,
                               { 2047 / 2 - width / 2, y, 2047 / 2 - width / 2 + width, y + height });
        caption(item, label, 0, 1, 1);
        item.secondary_shadow_text = true;
        item.color = designer ? RgbaPixel{ 0, 0, 0, 255 } : RgbaPixel{ 128, 128, 128, 255 };
        hover.emplace(&item, 0);
        ui.set_primary_callback(item, [this](UiControl& sender, const UiEvent& e) -> std::intptr_t {
            if (e.type == 65537)
                hover[&sender] = 1;
            else if (e.type == 65538)
                hover[&sender] = -1;
            else if (e.type == 17)
                sender.color = { 0, 175, 190, 255 };
            else if (e.type == 30) {
                auto& state = hover[&sender];
                if (state > 0) {
                    const auto next = state + 32;
                    const auto gray = static_cast<std::uint8_t>(std::min(next, 255));
                    sender.color = { gray, gray, gray, 255 };
                    state = next > 255 ? 0 : next;
                } else if (state < 0) {
                    state = state - 32 + 255 < 0 ? 0 : state - 32;
                    sender.color = { 0, 175, 190, 255 };
                }
            } else if (e.type == 65542) {
                host.play_ui_sound(193);
                menu_action(sender.id);
                return 0;
            } else if (e.type == 20)
                hover.erase(&sender);
            return ui.secondary(sender, e);
        });
        y += height + 5;
    }
    ui.set_depth(*root, depth);
    ui.set_visible(*root, true);
    ui.set_input_root(root);
    ui.set_focus(root);
}

void ApplicationGameUiController::State::transition(ApplicationGamePayload action) {
    const auto proceed = [this, action] {
        if (std::holds_alternative<ApplicationQuitRequest>(action))
            message_resource(773, [this] {
                enqueue({ .payload = ApplicationQuitRequest{} });
            });
        else
            enqueue({ .payload = action });
    };
    if (host.status().pending_postcards) {
        //50BE60: b_cancel continues the pending transition; b_okay connects.
        //This is deliberately not a conventional Yes/No interpretation.
        message(text(749), text(981), proceed, [this] {
            enqueue({ .payload = ApplicationGoOnlineRequest{} }, true);
        });
    } else
        proceed();
}

void ApplicationGameUiController::State::menu_action(std::uint32_t action) {
    match (action) {
        case 0 or 18 or 23 or 28 => {
            close();
            return;
        }
        case 1 or 24 => {
            open_load(false);
            return; //first native mode argument is unused
        }
        case 2 => {
            open_save(false);
            return;
        }
        case 4 or 5 or 25 => {
            if (!host.status().online_services_active) {
                close();
                host.open_options();
            }
            return;
        }
        case 33 => {
            close();
            host.open_options();
            return;
        }
        case 6 or 26 => {
            transition(ApplicationChangePlayerRequest{});
            return;
        }
        case 7 or 31 => {
            enqueue({ .payload = ApplicationGoOfflineRequest{} });
            return;
        }
        case 8 or 32 => {
            enqueue({ .payload = ApplicationGoOnlineRequest{} });
            return;
        }
        case 9 or 22 or 27 => {
            transition(ApplicationQuitRequest{});
            return;
        }
        case 10 => {
            close();
            transition(ApplicationChooseCoasterSourceRequest{});
            return;
        }
        case 29 => {
            open_coaster_save();
            return;
        }
        case 34 => {
            enqueue({ .payload = ApplicationLeaveCoasterDesignerRequest{} });
            return;
        }
        case 35 => {
            close();
            message_resource(773, [this] {
                enqueue({ .payload = ApplicationQuitRequest{} });
            });
            return;
        }
        case _ => throw std::out_of_range("Unknown original command-menu action");
    }
}

std::intptr_t ApplicationGameUiController::State::menu_event(UiControl& sender, const UiEvent& e) {
    if (e.type == 1048577) {
        menu_action(e.id);
        return 0;
    }
    //find_command_action_by_key returns -1 for an unmatched key, so the
    //native !result test does NOT dismiss on every unrecognized key.
    if (e.type == 65547 && (e.id > 255 ? e.id >> 8 : e.id) == 27) {
        close();
        return 0;
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
