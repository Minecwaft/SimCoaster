#include "application_postcard_ui_internal.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
constexpr unsigned char book_words[] {
#embed "ui_templates/app_address_book.bin"
};
constexpr unsigned char entry_words[] {
#embed "ui_templates/app_address_entry.bin"
};
static_assert(sizeof(book_words) == 724 && sizeof(entry_words) == 292);

std::vector<std::u16string> tokenize(std::u16string_view value) {
    std::vector<std::u16string> tokens;
    for (std::size_t cursor = 0; cursor < value.size();) {
        cursor = value.find_first_not_of(u";, \n", cursor);
        if (cursor == value.npos) {
            break;
        }
        const auto end = value.find_first_of(u";, \n", cursor);
        tokens.emplace_back(value.substr(cursor, end == value.npos ? value.size() - cursor : end - cursor));
        if (end == value.npos) {
            break;
        }
        cursor = end;
    }
    return tokens;
}
} //namespace

void ApplicationPostcardUiController::State::open_book() {
    if (!root || page != Page::composer || book) {
        return;
    }
    ui.translate(*root, 2000, 0);
    book_shifted = true;
    book = &parse(root, book_words, 49000, panel_callback<&State::book_event>());
    ui.set_depth(*book, 5);
    title(*book, 49009, 743);
    auto &stored = required(book, 49007), &recipients = required(book, 49008);
    list(stored, std::array<std::uint32_t, 1> { 744 });
    list(recipients, std::array<std::uint32_t, 1> { 745 });
    ui.set_list_sort_key(stored, 1, false);
    ui.set_list_sort_key(recipients, 1, false);
    refresh_book();
    std::vector<UiListRow> rows;
    if (recipients_edited && control(37004).text != text(742)) {
        std::intptr_t identity = 0;
        for (auto& value : tokenize(control(37004).text)) {
            rows.push_back({ identity++, { std::move(value) } });
        }
    }
    ui.set_list_rows(recipients, std::move(rows), false);
    for (const auto id : { 49002u, 49004u, 49005u, 49010u }) {
        ui.set_enabled(required(book, id), false);
    }
    ui.set_input_root(book);
    ui.set_focus(book);
}

void ApplicationPostcardUiController::State::close_book() {
    close_entry();
    auto* old = std::exchange(book, nullptr);
    if (ui.owns(old) && !old->destroying) {
        ui.destroy(old);
    }
    if (std::exchange(book_shifted, false) && ui.owns(root)) {
        ui.translate(*root, -2000, 0);
    }
    if (!closing) {
        ui.set_input_root(message_wrapper ? message_wrapper : root);
        if (root) {
            ui.set_focus(root);
        }
    }
}

void ApplicationPostcardUiController::State::refresh_book(std::optional<std::size_t> selected) {
    if (!book) {
        return;
    }
    auto& list_control = required(book, 49007);
    std::vector<UiListRow> rows;
    const auto& addresses = store.addresses();
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        auto value = postcard_from_ansi(addresses[index]);
        if (value.size() > 63) {
            value.resize(63); //56EB40/%.63S display cap; identity remains full
        }
        //stored address.
        rows.push_back({ static_cast<std::intptr_t>(index), { std::move(value) } });
    }
    ui.set_list_rows(list_control, std::move(rows), false);
    if (selected) {
        if (const auto row = ui.find_list_row(list_control, static_cast<std::intptr_t>(*selected))) {
            ui.select_list_row(list_control, static_cast<std::int32_t>(*row));
        }
    }
}

void ApplicationPostcardUiController::State::add_recipient() {
    auto &stored = required(book, 49007), &recipients = required(book, 49008);
    const auto selected = ui.selected_list_identity(stored);
    if (!selected) {
        return;
    }
    const auto row = ui.find_list_row(stored, *selected);
    if (!row) {
        return;
    }
    const auto value = std::u16string(stored.rows[*row].cells[0].text());
    if (std::ranges::any_of(recipients.rows, [&](const auto& recipient_row) {
            return recipient_row.cells[0].text() == value;
        })) {
        return;
    }
    std::intptr_t identity = 0;
    for (const auto& recipient_row : recipients.rows) {
        identity = std::max(identity, recipient_row.identity + 1);
    }
    ui.insert_list_row(recipients, { identity, { value } });
}

void ApplicationPostcardUiController::State::remove_recipient() {
    auto& recipients = required(book, 49008);
    if (const auto id = ui.selected_list_identity(recipients)) {
        ui.remove_list_row(recipients, *id);
    }
}

void ApplicationPostcardUiController::State::apply_recipients() {
    auto& recipients = required(book, 49008);
    std::u16string result;
    for (auto row = recipients.rows.rbegin(); row != recipients.rows.rend(); ++row) {
        const auto value = row->cells[0].text();
        //56EBE0 tests substring presence, not token equality, in reverse visible
        //order. The owned modern string removes its original512-WCHAR overflow.
        if (result.find(value) == result.npos) {
            result += u';';
            result += value;
        }
    }
    ui.set_text(control(37004), result, true);
    recipients_edited = true;
    ui.set_enabled(control(37006), parse_postcard_recipients(control(37004).text).has_value());
    close_book();
}

void ApplicationPostcardUiController::State::open_entry(std::int32_t index) {
    close_entry();
    entry_index = index;
    entry = &parse(book, entry_words, 88888, panel_callback<&State::entry_event>());
    ui.set_depth(*entry, entry->render_depth + 6);
    title(*entry, 88889, index == -1 ? 746u : 748u);
    auto& input = required(entry, 88890);
    edit(input, 511, 7, 1);
    caption(required(entry, 88891), text(747), 7, 0, 1);
    if (index >= 0 && static_cast<std::size_t>(index) < store.addresses().size()) {
        auto address = postcard_from_ansi(store.addresses()[static_cast<std::size_t>(index)]);
        if (address.size() > 63) {
            address.resize(63);
        }
        ui.set_text(input, address);
    }
    ui.set_input_root(entry);
    ui.set_focus(&input);
}

void ApplicationPostcardUiController::State::close_entry() {
    auto* old = std::exchange(entry, nullptr);
    if (ui.owns(old) && !old->destroying) {
        ui.destroy(old);
    }
    if (!closing && book) {
        ui.set_input_root(message_wrapper ? message_wrapper : book);
        ui.set_focus(book);
    }
}

void ApplicationPostcardUiController::State::commit_entry() {
    if (!entry || profile_revision != store.profile_revision()) {
        return;
    }
    const auto value = required(entry, 88890).text;
    //56E9E0 removes the original BEFORE validating the replacement. An invalid
    //edit does not roll that in-memory removal back or clear its index context.
    if (entry_index >= 0 && static_cast<std::size_t>(entry_index) < store.addresses().size()) {
        store.erase_address(static_cast<std::size_t>(entry_index));
    }
    if (!has_basic_postcard_address_syntax(value)) {
        const std::array argument { BfstWideStringArgument { 30, value } };
        message({}, text(751, {}, argument));
        return;
    }
    const auto selected = store.insert_address(postcard_to_ansi(value, 63));
    refresh_book(selected);
    close_entry();
}

std::intptr_t ApplicationPostcardUiController::State::book_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == book) {
        close_book();
        return 0;
    }
    if (event.type == 4 || event.type == 5 || postcard_escape(event)) {
        close_book();
        return 0;
    }
    if (event.type == 1025) {
        if (event.id == 49007) {
            for (const auto id : { 49002u, 49004u, 49005u }) {
                ui.set_enabled(required(book, id), event.value != -1);
            }
        } else if (event.id == 49008) {
            ui.set_enabled(required(book, 49010), event.value != -1);
        }
    } else if (event.type == 1024) {
        if (event.id == 49007) {
            add_recipient();
        } else if (event.id == 49008) {
            remove_recipient();
        }
    } else if (event.type == 256) {
        if (event.id == 49003 || event.id == 4294967294u) {
            close_book();
            return 0;
        }
        if (event.id == 49001) {
            open_entry(-1);
        } else if (event.id == 49002) {
            add_recipient();
        } else if (event.id == 49010) {
            remove_recipient();
        } else if (event.id == 49006) {
            apply_recipients();
            return 0;
        } else if (event.id == 49004 || event.id == 49005) {
            auto& list_control = required(book, 49007);
            const auto selected = ui.selected_list_identity(list_control);
            if (selected && *selected >= 0 && static_cast<std::size_t>(*selected) < store.addresses().size()) {
                const auto index = static_cast<std::size_t>(*selected);
                if (event.id == 49005) {
                    open_entry(static_cast<std::int32_t>(index));
                } else {
                    const auto address = store.addresses()[index];
                    const auto display = postcard_from_ansi(address);
                    const std::array argument { BfstWideStringArgument { 30, display } };
                    const auto revision = store.profile_revision();
                    message({}, text(753, {}, argument), {}, [this, index, address, revision] {
                        if (!book || store.profile_revision() != revision || index >= store.addresses().size() || store.addresses()[index] != address) {
                            return;
                        }
                        store.erase_address(index);
                        refresh_book();
                    });
                }
            }
        }
    }
    return ui.secondary(sender, event);
}

std::intptr_t ApplicationPostcardUiController::State::entry_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == entry) {
        close_entry();
        return 0;
    }
    if (event.type == 2050 || (event.type == 256 && event.id == 4294967295u)) {
        commit_entry();
        return 0;
    }
    if (event.type == 2052 || event.type == 4 || event.type == 5 || postcard_escape(event) || (event.type == 256 && event.id == 4294967294u)) {
        close_entry();
        return 0;
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
