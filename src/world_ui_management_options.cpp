#include "simcoaster/world_advisor.hpp"
#include "world_ui_management_internal.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char option_words[] {
#embed "ui_templates/app_options.bin"
};

std::u16string percent(std::int32_t value) {
    return number_text(std::max(0, value), " {} %");
}
} //namespace

void WorldManagementUiController::State::open_options() {
    if (page == Page::options && root) {
        return;
    }
    if (!host.read_options || !host.write_options || !host.read_graphics || !host.write_graphics || !host.graphics_capabilities || !host.pause_options) {
        throw std::logic_error("Options requires live application settings/device bindings");
    }
    close();
    options_snapshot = options_current = host.read_options();
    graphics_snapshot = graphics_current = host.read_graphics();
    graphics_caps = host.graphics_capabilities();
    //WARP remains a real renderer on hosts without a hardware adapter.
    //The device list names hardware adapters, not this software fallback.
    if (graphics_current.hardware && graphics_caps.devices.empty()) {
        throw std::logic_error("Options selected unavailable hardware renderer");
    }
    options_committed = false;
    host.pause_options(true);
    try {
        //561618 interrupts speech after acquiring the pause and before dismissal.
        //The shared dismissal visits this controller too: adopt its pause only
        //after that visit, so it cannot release the new Options owner's lease.
        stop_active_advisor_response(c, AdvisorStopTargetAction::parameter_request);
        if (host.panels.dismiss) {
            host.panels.dismiss();
        }
    } catch (...) {
        host.pause_options(false);
        throw;
    }
    page = Page::options;
    options_pause_owned = true;
    try {
        root = &parse(option_words, 95000);
        title(95078, text(783));
        configure_options();
        activate();
    } catch (...) {
        //Resource/presentation failure must not retain the pause acquired above.
        const auto error = std::current_exception();
        try {
            close();
        } catch (...) {
            if (std::exchange(options_pause_owned, false)) {
                host.pause_options(false);
            }
        }
        std::rethrow_exception(error);
    }
}

void WorldManagementUiController::State::configure_options() {
    for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 32> { { { 95076, 784 }, { 95077, 785 }, { 95075, 786 }, { 95023, 800 }, { 95026, 787 }, { 95025, 788 }, { 95027, 789 }, { 95029, 793 }, { 95033, 790 }, { 95031, 792 }, { 95032, 791 }, { 95006, 807 }, { 95008, 806 }, { 95010, 804 }, { 95012, 805 }, { 95046, 812 },
                                                                                            { 95045, 813 }, { 95047, 814 }, { 95048, 815 }, { 95049, 67 },  { 95050, 68 },  { 95053, 820 }, { 95052, 821 }, { 95054, 822 }, { 95057, 823 }, { 95056, 824 }, { 95058, 825 }, { 95072, 69 },  { 95021, 801 }, { 95034, 795 }, { 95073, 0 },   { 95004, 807 } } }) {
        if (row) {
            caption(required(root, id), text(row), 6, 0, 1);
        }
    }
    for (const auto id : { 95075u, 95076u, 95077u }) {
        auto& button = required(root, id);
        button.horizontal_alignment = button.vertical_alignment = 1;
        button.text_constraints = { true, true };
        for (std::size_t style = 0; style < 3; ++style) {
            button.button_text_colors[style] = { 255, 255, 255, 255 };
        }
    }
    //Native55FAE2/56042A takes (end0,start100); the public API is start/end.
    for (const auto id : { 95035u, 95039u, 95016u, 95017u, 95018u, 95019u }) {
        ui.set_range(required(root, id), 100, 0);
    }
    ui.set_range(required(root, 95070), 400, 10);
    ui.set_value(required(root, 95035), 100 * graphics_current.quality / 2);
    ui.set_value(required(root, 95039), graphics_current.resolution == 2 ? 50 : graphics_current.resolution == 3 ? 99 : 0);
    ui.set_value(required(root, 95016), options_current.sound_quality);
    ui.set_value(required(root, 95017), options_current.music_volume.value);
    ui.set_value(required(root, 95018), options_current.sfx_volume.value);
    ui.set_value(required(root, 95019), options_current.speech_volume.value);
    options_current.game_speed_percent = static_cast<std::int16_t>(std::clamp<std::int32_t>(options_current.game_speed_percent, 10, 400));
    ui.set_value(required(root, 95070), options_current.game_speed_percent);
    ui.set_selected(required(root, 95062), options_current.isometric_on ? 95064 : 95063);
    ui.set_selected(required(root, 95065), options_current.rmb_scroll_on ? 95066 : 95067);
    ui.set_selected(required(root, 95036), graphics_current.hardware ? 95037 : 95038);
    ui.set_selected(required(root, 95040), graphics_current.video_device ? 95042 : 95041);
    const auto renderer_choices = graphics_caps.show_renderer_choices && !graphics_caps.devices.empty();
    ui.set_visible(required(root, 95024), renderer_choices);
    ui.set_visible(required(root, 95036), renderer_choices);
    ui.set_enabled(required(root, 95036), renderer_choices);
    ui.set_visible(required(root, 95040), !graphics_caps.devices.empty());
    ui.set_visible(required(root, 95031), graphics_caps.devices.size() > 1);
    ui.set_visible(required(root, 95042), graphics_caps.devices.size() > 1);
    options_page = 95020;
    ui.set_selected(required(root, 95074), 95076);
    for (const auto id : { 95020u, 95003u, 95043u }) {
        ui.set_visible(required(root, id), id == 95020);
    }
    options_current.music_enabled = options_current.music_volume.value != 0;
    host.write_options(options_current, ManagementOptionsChange::live_audio);
    refresh_options();
}

void WorldManagementUiController::State::refresh_options() {
    if (page != Page::options || !root) {
        return;
    }
    caption(required(root, 95021), text(graphics_current.quality == 0 ? 801 : graphics_current.quality == 1 ? 802 : 803), 6);
    caption(required(root, 95034), text(graphics_current.resolution == 1 ? 795 : graphics_current.resolution == 2 ? 796 : 797), 6);
    caption(required(root, 95004), text(807) + percent(options_current.sound_quality), 6);
    caption(required(root, 95013), text(809) + percent(options_current.music_volume.value), 6);
    caption(required(root, 95014), text(808) + percent(options_current.sfx_volume.value), 6);
    caption(required(root, 95015), text(810) + percent(options_current.speech_volume.value), 6);
    caption(required(root, 95073), percent(options_current.game_speed_percent), 6);
    for (const auto [id, value] : std::array<std::pair<std::uint32_t, bool>, 5> { { { 95059, options_current.advisor_on != 0 }, { 95060, options_current.confirm_delete_on != 0 }, { 95061, options_current.tooltips_on != 0 }, { 95068, options_current.animate_menus != 0 }, { 95069, options_current.totd != 0 } } }) {
        auto& button = required(root, id);
        ui.set_value(button, static_cast<std::int32_t>((button.visual_flags & ~1u) | (value ? 1u : 0u)));
    }
}

void WorldManagementUiController::State::commit_options(bool commit) {
    if (!host.write_options || !host.write_graphics) {
        return;
    }
    if (!commit) {
        options_current = options_snapshot;
        graphics_current = graphics_snapshot;
        //Rollback invokes all three native volume setters after copying92bytes;
        //the Music setter derives MusicEnabled from its restored volume.
        options_current.music_enabled = options_current.music_volume.value != 0;
    }
    host.write_graphics(graphics_current, commit);
    host.write_options(options_current, commit ? ManagementOptionsChange::commit : ManagementOptionsChange::rollback);
    options_committed = true;
}

std::intptr_t WorldManagementUiController::State::options_event(UiControl& sender, const UiEvent& e) {
    if (e.type == 256) {
        if (e.id == 95001 || e.id == 4294967294u) {
            commit_options(false);
            close();
            return 0;
        }
        if (e.id == 95079) {
            commit_options(true);
            close();
            return 0;
        }
        const auto value = static_cast<std::uint8_t>(e.value == 1);
        match (e.id) {
            case 95059 => options_current.advisor_on = value;
            case 95060 => options_current.confirm_delete_on = value;
            case 95061 => options_current.tooltips_on = value;
            case 95068 => options_current.animate_menus = value;
            case 95069 => options_current.totd = value;
            case _     => return ui.secondary(sender, e);
        } host.write_options(options_current, ManagementOptionsChange::live_controls);
        refresh_options();
        return 0;
    }
    if (e.type == 257 && e.value == 1) {
        if (e.id == 95075 || e.id == 95076 || e.id == 95077) {
            options_page = e.id == 95075 ? 95043 : e.id == 95076 ? 95020 : 95003;
            for (const auto id : { 95020u, 95003u, 95043u }) {
                ui.set_visible(required(root, id), static_cast<std::int32_t>(id) == options_page);
            }
            return 0;
        }
        match (e.id) {
            case 95037 => graphics_current.hardware = true;
            case 95038 => {
                graphics_current.hardware = false;
                if (graphics_current.quality == 2) {
                    graphics_current.quality = 1;
                    ui.set_value(required(root, 95035), 100);
                }
            }
            case 95041 => graphics_current.video_device = 0;
            case 95042 => {
                if (graphics_caps.devices.size() < 2) {
                    throw std::logic_error("Options selected unavailable second device");
                }
                graphics_current.video_device = 1;
            }
            case 95063 => options_current.isometric_on = 0;
            case 95064 => options_current.isometric_on = 1;
            case 95066 => options_current.rmb_scroll_on = 1;
            case 95067 => options_current.rmb_scroll_on = 0;
            case _     => return ui.secondary(sender, e);
        } if (e.id == 95063 || e.id == 95064 || e.id == 95066 || e.id == 95067) {
            host.write_options(options_current, ManagementOptionsChange::live_controls);
        }
        refresh_options();
        return 0;
    }
    if (e.type == 2048) {
        const auto value = static_cast<std::int32_t>(e.value);
        bool audio {};
        match (e.id) {
            case 95035 => {
                graphics_current.quality = graphics_current.hardware ? (value < 33 ? 0 : value < 66 ? 1 : 2) : (value >= 50 ? 1 : 0);
            }
            case 95039 => graphics_current.resolution = value < 33 ? 1 : value <= 66 ? 2 : 3;
            case 95016 => {
                options_current.sound_quality = value;
                audio = true;
            }
            case 95017 => {
                options_current.music_volume.value = value;
                options_current.music_enabled = value != 0;
                audio = true;
            }
            case 95018 => {
                options_current.sfx_volume.value = value;
                audio = true;
            }
            case 95019 => {
                options_current.speech_volume.value = value;
                audio = true;
            }
            case 95070 => {
                options_current.game_speed_percent = static_cast<std::int16_t>(std::clamp<std::int32_t>(static_cast<std::int16_t>(value), 10, 400));
            }
            case _ => return ui.secondary(sender, e);
        } if (audio) {
            host.write_options(options_current, ManagementOptionsChange::live_audio);
        } else if (e.id == 95070) {
            host.write_options(options_current, ManagementOptionsChange::live_controls);
        }
        refresh_options();
        return 0;
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
