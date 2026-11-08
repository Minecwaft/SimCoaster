#include "application_frontend_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char profile_template[] {
#embed "ui_templates/application_profile.bin"
};
static_assert(sizeof(profile_template) == 1520);
constexpr std::array<std::uint32_t, 10> metric_controls { 51020, 51022, 51024, 51026, 51019, 51015, 51017, 51011, 51027, 51013 };
constexpr std::array<std::uint32_t, 9> metric_labels { 51014, 51021, 51023, 51025, 51018, 51016, 51009, 51010, 51012 };
} //namespace

void ApplicationFrontendController::State::open_profiles() {
    remove(intro);
    remove(overview);
    remove(profile);
    phase = ApplicationFrontendPhase::profiles;
    dolly = 0;
    height_offset = -7;
    metric_complete = false;
    fade_ready = false;
    transition_queued = false;
    if (mode < 5) {
        mode = 5;
    }
    map_view_active = true;
    if (host.selected_resource_source() == 3) {
        suppress_template = true;
    }
    if (!lobby_bound) {
        bind_lobby(suppress_template);
    }
    profile = parse(profile_template, 51000);
    ui.set_visible(required(profile, 51037), false);
    //56ADB0 calls 501D50: primary shadow text, font 5, left/middle.
    //Font 0 is the large menu face and clips this original caption rectangle.
    caption(required(profile, 51036), text(19), 5, 0, 1);
    required(profile, 51036).primary_shadow_text = true;
    required(profile, 51036).default_text_y_offset = 9;
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
        auto& button = required(profile, 51005 + slot);
        caption(button, {}, 6, 1, 1);
        button.color = { 0, 0, 0, 255 };
        button.button_text_colors = { { { 64, 64, 64, 255 }, { 96, 96, 96, 255 }, { 64, 64, 64, 255 }, { 255, 255, 255, 255 } } };
        ui.set_enabled(button, button.enabled);
    }
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 6> labels { { { 51014, 22 }, { 51021, 23 }, { 51023, 25 }, { 51025, 24 }, { 51018, 26 }, { 51012, 29 } } };
    for (const auto [id, row] : labels) {
        caption(required(profile, id), text(row), 8, 0, 2);
    }
    caption(required(profile, 51016), theme_name(1), 8, 0, 2);
    caption(required(profile, 51009), theme_name(0), 8, 0, 2);
    caption(required(profile, 51010), theme_name(2), 8, 0, 2);
    for (const auto id : metric_controls) {
        auto& control = required(profile, id);
        //56ADB0's value attachments retain the constructor's LEFT/MIDDLE.
        caption(control, {}, 13, 0, 1);
        control.color = { 28, 89, 177, 255 };
    }
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 5> { { { 51028, 34 }, { 51029, 33 }, { 51033, 35 }, { 51035, 30 }, { 51002, 31 } } }) {
        auto& button = required(profile, id);
        caption(button, text(row), 8, 1, 1);
        button.text_constraints = { true, true };
        if (id == 51028 || id == 51029 || id == 51033) {
            button.color = { 0, 0, 0, 255 };
            button.button_text_colors = { { { 64, 64, 64, 255 }, { 96, 96, 96, 255 }, { 64, 64, 64, 255 }, { 208, 208, 208, 255 } } };
            ui.set_enabled(button, button.enabled);
        }
    }
    required(profile, 51033).help_index = 141;
    auto& form = required(profile, 51030);
    ui.set_depth(form, form.render_depth + 2);
    auto& edit = required(profile, 51032);
    caption(edit, {}, 6);
    edit.color = { 255, 255, 255, 255 };
    edit.text_constraints = { false, false };
    edit.text_limit = 32;
    edit.rejected_characters = u"\\/*?:|<>\"";
    edit.edit_caret_asset_hash = ui_model_asset_hash("hilight");
    commands(false);
    host.resume_ui_transition();
    host.persistent_visible(false);
    ui.set_input_root(profile);
    refresh_profiles();
    queue_advisor(619);
}

void ApplicationFrontendController::State::refresh_profiles(bool reset_selection) {
    profiles = host.profiles();
    if (!profile) {
        return;
    }
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
        auto& button = required(profile, 51005 + slot);
        ui.set_text(button, profiles[slot].present ? profiles[slot].name : text(20));
        button.help_index = profiles[slot].present ? 286 : 285;
    }
    if (reset_selection) {
        select_profile(selected_profile);
    }
}

void ApplicationFrontendController::State::select_profile(std::uint32_t slot, bool reset_name) {
    if (slot >= 4) {
        throw std::out_of_range("Original Profile screen exposes four slots");
    }
    selected_profile = slot;
    ui.set_selected(required(profile, 51004), static_cast<std::int32_t>(51005 + slot));
    const bool populated = profiles[slot].present;
    ui.set_visible(required(profile, 51030), !populated);
    for (const auto id : metric_labels) {
        required(profile, id).color = { 64, 64, 64, 255 };
    }
    ui.set_visible(required(profile, 51014), populated);
    for (const auto id : { 51028u, 51029u, 51033u }) {
        ui.set_visible(required(profile, id), populated);
    }
    ui.set_enabled(required(profile, 51002), populated);
    if (populated) {
        auto& edit = required(profile, 51032);
        if (ui.focus() == &edit) {
            ui.set_focus(nullptr);
        }
        ui.set_focus(profile);
        refresh_profile_summary();
    } else {
        for (const auto id : metric_controls) {
            auto& control = required(profile, id);
            control.scalar_text.reset();
            ui.set_text(control, {});
        }
        auto& edit = required(profile, 51032);
        if (reset_name) {
            ui.set_text(edit, text(32));
            ui.prepare_edit_selection(edit);
        }
        ui.set_enabled(required(profile, 51031), false);
        ui.set_focus(&edit);
    }
}

void ApplicationFrontendController::State::refresh_profile_summary() {
    const auto& data = profiles.at(selected_profile);
    if (!data.present) {
        return;
    }
    ui.set_text(required(profile, 51020), format(c.messages.mail_language.promotions, static_cast<std::uint32_t>(data.promotion)));
    numeric(required(profile, 51022), data.share_percentage, UiScalarTextKind::percentage);
    numeric(required(profile, 51024), data.share_price, UiScalarTextKind::signed_currency);
    numeric(required(profile, 51026), data.shares_owned, UiScalarTextKind::signed_integer);
    //Native DWORD multiply wraps before the signed currency attachment sees it.
    const auto portfolio = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(data.share_price) * std::bit_cast<std::uint32_t>(data.shares_owned));
    numeric(required(profile, 51019), portfolio, UiScalarTextKind::signed_currency);
    numeric(required(profile, 51015), data.golden_tickets, UiScalarTextKind::signed_integer);
    constexpr std::array<std::uint32_t, 3> controls { 51011, 51017, 51027 };
    for (std::size_t theme = 0; theme < controls.size(); ++theme) {
        const auto& state = data.themes[theme];
        const std::array<BfstSignedIntegerArgument, 2> args { { { 1, state.completed_challenges }, { 2, 10 } } };
        ui.set_text(required(profile, controls[theme]), state.available ? text(27, {}, args) : text(28));
    }
    ui.set_text(required(profile, 51013), data.last_mission == -1 ? text(36) : format(table("OPERATION.str"), static_cast<std::uint32_t>(data.last_mission)) + u", " + theme_name(static_cast<int>(data.last_theme)));
    required(profile, 51028).help_index = data.last_mission == -1 ? 351 : 287;
    ui.set_enabled(required(profile, 51033), data.last_mission != -1);
}

void ApplicationFrontendController::State::create_profile() {
    if (!profile || phase != ApplicationFrontendPhase::profiles || profiles[selected_profile].present) {
        return;
    }
    auto name = required(profile, 51032).text;
    while (!name.empty() && name.back() == u' ') {
        name.pop_back();
    }
    if (name.empty()) {
        return;
    }
    if (!host.create_profile(selected_profile, name)) {
        diagnostics.push_back("Original profile creation was rejected by persistent owner");
    }
    refresh_profiles();
}

void ApplicationFrontendController::State::profile_action(std::uint32_t id) {
    if (phase != ApplicationFrontendPhase::profiles) {
        return;
    }
    const auto slot = selected_profile;
    if (id == 51028) {
        enter_profile();
        return;
    }
    if (id == 51029 && profiles[slot].present) {
        const auto expected = profiles[slot];
        const std::array<BfstWideStringArgument, 1> name { { { 0, expected.name } } };
        std::weak_ptr<int> alive = lifetime;
        host.message(text(915, name), true, [this, alive, slot, expected] {
            if (alive.expired()) {
                return;
            }
            const auto current = host.profiles()[slot];
            if (current.present && current.generation == expected.generation && current.name == expected.name) {
                if (!host.delete_profile(slot, expected.generation)) {
                    diagnostics.push_back("Original profile deletion was rejected by persistent owner");
                }
                if (profile) {
                    refresh_profiles();
                }
            }
        });
        return;
    }
    if (id == 51035) {
        std::weak_ptr<int> alive = lifetime;
        host.message(text(773), true, [this, alive] {
            if (!alive.expired()) {
                remove(profile);
                commands(true);
                host.request_quit();
            }
        });
        return;
    }
    if ((id == 51033 || id == 51002) && profiles[slot].present && host.activate_profile(slot)) {
        if (id == 51033) {
            host.open_load_game();
        } else {
            host.open_coaster_source_picker();
        }
    }
}

void ApplicationFrontendController::State::enter_profile() {
    if (!profile || !profiles[selected_profile].present || !host.activate_profile(selected_profile)) {
        return;
    }
    profile_source = host.selected_resource_source();
    if (profile_source == -1 || profile_source == 3) {
        profile_source = 1;
        host.select_resource_source(1);
    }
    if (profile_source < 0 || profile_source > 2) {
        throw std::runtime_error("Invalid profile resource-source index");
    }
    ui.set_visible(*profile, false);
    ui.set_input_root(parent);
    ui.set_focus(nullptr);
    constexpr std::array<std::uint16_t, 3> resource_modes { 2, 0, 1 };
    select_mode(resource_modes[static_cast<std::size_t>(profile_source)], false);
    phase = ApplicationFrontendPhase::profile_transition;
}
} //namespace simcoaster
