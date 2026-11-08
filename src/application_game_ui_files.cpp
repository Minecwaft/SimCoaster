#include "application_game_ui_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char file_words[] {
#embed "ui_templates/app_save_load.bin"
};

bool autosave(std::u16string_view name) {
    if (name.size() != 8) {
        return false;
    }
    constexpr std::u16string_view reserved = u"autosave";
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto letter = name[i] >= u'A' && name[i] <= u'Z' ? name[i] + (u'a' - u'A') : name[i];
        if (letter != reserved[i]) {
            return false;
        }
    }
    return true;
}

std::intptr_t row_identity(std::uint64_t id) {
    static_assert(sizeof(id) == sizeof(std::intptr_t));
    return std::bit_cast<std::intptr_t>(id);
}

std::u16string two_digits(std::int32_t value) {
    return number_text(value, "{:02}");
}
} //namespace

void ApplicationGameUiController::State::open_load(bool from_profile) {
    begin(Page::load);
    profile_load = from_profile;
    coaster_mode = false;
    dialog(file_words, 34000);
    caption(required(root, 34005), text(778), 5);
    required(root, 34005).primary_shadow_text = true;
    ui.set_visible(required(root, 34002), false);
    ui.set_visible(required(root, 34006), false);
    required(root, 34004).help_index = 141;
    configure_files(false);
    ui.set_focus(root);
}

void ApplicationGameUiController::State::open_save(bool coaster_file_mode) {
    begin(Page::save);
    profile_load = false;
    coaster_mode = coaster_file_mode;
    dialog(file_words, 34000);
    caption(required(root, 34005), text(777), 5);
    required(root, 34005).primary_shadow_text = true;
    auto& entry = required(root, 34002);
    entry.font = 7;
    entry.text_limit = 32;
    entry.edit_caret_asset_hash = ui_model_asset_hash("hilight");
    entry.rejected_characters = u"\\/*?:|<>\"";
    caption(entry, text(782), 7);
    required(root, 34004).help_index = 142;
    configure_files(true);
    c.ports.ui.disable_command_families_and_set_disabled_flag(c);
    commands_disabled = true;
    ui.set_focus(&entry);
}

void ApplicationGameUiController::State::configure_files(bool saving) {
    list = &required(root, 34001);
    if (list->columns.size() != 2) {
        throw std::logic_error("Original file list has wrong schema");
    }
    list->font = 7;
    list->row_height_font_padding = 6;
    list->list_highlight_asset_hash = ui_model_asset_hash("hilight");
    list->column_presentations.resize(2);
    for (std::size_t i = 0; i < 2; ++i) {
        auto& column = list->column_presentations[i];
        column.format = UiListCellFormat::text;
        column.font = 7;
        column.horizontal_alignment = i ? 2 : 0;
        column.vertical_alignment = 1;
        auto& heading = required(list, 16u + static_cast<std::uint32_t>(i));
        caption(heading, text(i ? 780 : 779), 8, 1, 1);
        heading.asset_hash = ui_model_asset_hash("!cbut");
        heading.text_constraints = { true, true };
        heading.help_index = i ? 21u : 15u;
        for (std::size_t style = 0; style < 3; ++style) {
            heading.button_text_colors[style] = { 255, 255, 255, 255 };
        }
    }
    //These are newly constructed list controls. Native openers do not retain
    //the previous dialog's header sort key; refresh/delete retains this list.
    file_sort = 0;
    ui.set_list_sort_key(*list, file_sort, false);
    ui.set_enabled(required(root, 34004), false);
    if (saving) {
        ui.set_enabled(required(root, 34006), false);
    }
    refresh_files();
    //Native initial name setters enqueue2049; reproduce the resulting state
    //directly without an artificial notification on every presentation refresh.
    if (saving) {
        ui.set_enabled(required(root, 34004), valid_save_name());
    }
}

void ApplicationGameUiController::State::refresh_files() {
    if ((page != Page::load && page != Page::save) || !list) {
        return;
    }
    auto loaded = host.list_files(coaster_mode ? ApplicationFileKind::coaster : ApplicationFileKind::park);
    if (!loaded) {
        message(std::move(loaded.error().title), std::move(loaded.error().body), {}, {}, UiMessageButtonKind::okay);
        return;
    }
    std::set<std::uint64_t> unique;
    std::vector<UiListRow> rows;
    for (const auto& entry : *loaded) {
        if (!unique.insert(entry.identity).second) {
            throw std::logic_error("Duplicate file-list identity");
        }
        const auto& date = entry.local_modified;
        const std::array args { BfstSignedIntegerArgument { 41, date.day }, BfstSignedIntegerArgument { 42, date.month }, BfstSignedIntegerArgument { 43, date.year } };
        auto display_date = text(956, args) + u' ' + two_digits(date.hour) + u':' + two_digits(date.minute);
        rows.push_back({ row_identity(entry.identity), { entry.name, std::move(display_date) } });
    }
    entries = std::move(*loaded);
    ui.set_list_rows(*list, std::move(rows), true, false);
    const auto* selected = selection();
    ui.set_enabled(required(root, 34004), page == Page::load ? selected != nullptr : valid_save_name());
    if (page == Page::save) {
        ui.set_enabled(required(root, 34006), selected != nullptr);
    }
}

const ApplicationFileEntry* ApplicationGameUiController::State::selection() const {
    if (!list) {
        return nullptr;
    }
    const auto selected = ui.selected_list_identity(*list);
    if (!selected) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(entries, [&](const auto& entry) {
        return row_identity(entry.identity) == *selected;
    });
    return found == entries.end() ? nullptr : &*found;
}

bool ApplicationGameUiController::State::valid_save_name() const {
    const auto& name = required(root, 34002).text;
    return !name.empty() && name.size() <= 32 && !autosave(name) && name.find_first_of(u"\\/*?:|<>\"") == name.npos;
}

void ApplicationGameUiController::State::save(bool overwrite) {
    if (!valid_save_name()) {
        return;
    }
    const auto name = required(root, 34002).text;
    //Native548390 compares ordinary save basenames case-sensitively, while the
    //protected autosave name is case-insensitive. The host separately enforces
    //actual filesystem collision rules and reports any failed write honestly.
    const auto found = std::ranges::find(entries, name, &ApplicationFileEntry::name);
    if (!overwrite && found != entries.end()) {
        const auto serial = ui.lifetime_serial(root);
        const std::array args { BfstWideStringArgument { 31, name } };
        message({}, text(781, {}, args), {}, [this, serial] {
            if (ui.lifetime_serial(root) == serial) {
                save(true);
            }
        });
        return;
    }
    ApplicationFileWrite destination { .basename = name, .overwrite = overwrite };
    if (found != entries.end()) {
        destination.file_identity = found->identity;
    }
    if (coaster_mode) {
        ApplicationSaveCoasterRequest request;
        static_cast<ApplicationFileWrite&>(request) = std::move(destination);
        if (auto* object = first_ride()) {
            request.coaster = object->base.header.id;
            request.coaster_identity = &object->base.header;
            request.coaster_generation = object->creation_file_time;
        }
        enqueue({ .payload = std::move(request) }, true);
    } else {
        enqueue({ .payload = ApplicationSaveParkRequest { std::move(destination) } }, true);
    }
}

void ApplicationGameUiController::State::delete_selection() {
    const auto* selected = selection();
    if (!selected) {
        return;
    }
    const auto entry = *selected;
    const auto serial = ui.lifetime_serial(root);
    message(text(4), text(5), {}, [this, serial, entry] {
        if (ui.lifetime_serial(root) != serial) {
            return;
        }
        ApplicationFileSelection file { entry.name, entry.identity };
        if (coaster_mode) {
            enqueue({ .payload = ApplicationDeleteCoasterRequest { std::move(file) } }, true);
        } else {
            enqueue({ .payload = ApplicationDeleteParkRequest { std::move(file) } }, true);
        }
    });
}

void ApplicationGameUiController::State::load() {
    const auto* entry = selection();
    if (!entry) {
        return;
    }
    enqueue({ .payload = ApplicationLoadParkRequest { .basename = entry->name, .file_identity = entry->identity, .from_profile = profile_load } }, true);
}

std::intptr_t ApplicationGameUiController::State::file_event(UiControl& sender, const UiEvent& e) {
    if (e.type == 1030) {
        file_sort = ui.list_sort_key(*list);
        return 0;
    }
    if ((e.type == 1024 || e.type == 1025) && e.id == 34001) {
        const auto* selected = selection();
        if (page == Page::load) {
            ui.set_enabled(required(root, 34004), selected != nullptr);
            if (selected && e.type == 1024) {
                load();
            }
        } else {
            ui.set_enabled(required(root, 34006), selected != nullptr);
            if (selected) {
                ui.set_text(required(root, 34002), selected->name, true);
            }
            if (selected && e.type == 1024) {
                save();
            }
        }
        return 0;
    }
    if (page == Page::save && e.type == 2049) {
        ui.set_enabled(required(root, 34004), valid_save_name());
        return 0;
    }
    if (page == Page::save && e.type == 2050) {
        save();
        return 0;
    }
    if (e.type == 2052) {
        close();
        return 0;
    }
    if (e.type == 256) {
        if (e.id == 34003 || e.id == 4294967294u) {
            if (page == Page::load) {
                enqueue({ .payload = ApplicationCancelLoadRequest { .from_profile = profile_load } });
            } else {
                close();
            }
            return 0;
        }
        if (e.id == 34004) {
            if (page == Page::load) {
                load();
            } else {
                save();
            }
            return 0;
        }
        if (e.id == 34006 && page == Page::save) {
            delete_selection();
            return 0;
        }
    }
    return ui.secondary(sender, e);
}
} //namespace simcoaster
