#include "application_postcard_ui_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <format>
#include <algorithm>

namespace simcoaster {
namespace {
constexpr unsigned char outbox_words[] {
#embed "ui_templates/app_postcard_outbox.bin"
};
static_assert(sizeof(outbox_words) == 738);
} //namespace

void ApplicationPostcardUiController::State::open_outbox() {
    if (root || store.index().records.empty()) {
        return;
    }
    begin(Page::outbox);
    auto records = store.records();
    if (!records) {
        error(records.error());
        close();
        return;
    }
    outbox_records = std::move(*records);
    wrapper = &ui.create(nullptr, UiControlKind::window, 1, 1, { 0, 0, 2047, 1535 });
    wrapper->secondary_asset_hash = ui_model_asset_hash("lolight");
    root = &parse(wrapper, outbox_words, 570000, panel_callback<&State::outbox_event>());
    ui.set_primary_callback(*wrapper, [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
        if (event.type == 4) {
            close_panel();
            return 0;
        }
        return ui.secondary(sender, event);
    });
    ui.place_modal_layer(*wrapper);
    title(*root, 570010, 756);
    auto& list_control = control(570007);
    list(list_control, std::array<std::uint32_t, 4> { 757, 758, 759, 760 });
    list_control.column_presentations[1].horizontal_alignment = 1;
    list_control.column_presentations[1].text_constraints = { true, true };
    for (std::size_t i = 2; i < 4; ++i) {
        auto& column = list_control.column_presentations[i];
        column.format = UiListCellFormat::model_state;
        column.model_asset_hash = ui_model_asset_hash("i_boxtick");
    }
    constexpr std::array<std::uint32_t, 4> help { 20, 21, 55, 56 };
    for (std::size_t i = 0; i < help.size(); ++i) {
        required(&list_control, 16 + static_cast<std::uint32_t>(i)).help_index = help[i];
    }
    auto& body = control(570001);
    caption(body, {}, 6, 0, 0);
    body.text_constraints = { true, false };
    body.text_offset = { 0, 0 };
    body.text_offsets_explicit = true;
    for (const auto& record : outbox_records) {
        actions[record.id] = PostcardRowAction::send;
    }
    refresh_outbox();
    if (host.panels.activate) {
        host.panels.activate(root);
    }
    ui.set_input_root(wrapper);
    ui.set_focus(root);
}

void ApplicationPostcardUiController::State::refresh_outbox() {
    auto& list_control = control(570007);
    std::vector<UiListRow> rows;
    for (const auto& record : outbox_records) {
        const std::array date { BfstSignedIntegerArgument { 41, record.date.day }, BfstSignedIntegerArgument { 42, record.date.month }, BfstSignedIntegerArgument { 43, record.date.year } };
        const auto action = actions.at(record.id);
        rows.push_back({ static_cast<std::intptr_t>(record.id), { postcard_from_ansi(record.subject), text(956, date), action == PostcardRowAction::send ? 1 : 0, action == PostcardRowAction::erase ? 1 : 0 } });
    }
    ui.set_list_rows(list_control, std::move(rows), true);
}

void ApplicationPostcardUiController::State::outbox_preview() {
    const auto id = ui.selected_list_identity(control(570007));
    if (!id) {
        return;
    }
    const auto selected = std::ranges::find(outbox_records, static_cast<std::uint32_t>(*id), &PostcardRecord::id);
    if (selected == outbox_records.end()) {
        return;
    }
    std::u16string recipients;
    for (const auto& recipient : selected->recipients) {
        recipients += postcard_from_ansi(recipient);
        recipients += u'\n';
    }
    ui.set_text(control(570001), recipients);
    outbox_scroll_dirty = true;
    ui.set_value(control(570002), 0);
    control(570001).text_offset = { 0, 0 };
    auto image = load_postcard_jpeg(selected->image_path);
    if (!image) {
        outbox_image.reset();
        error(image.error());
        return;
    }
    outbox_image = std::make_shared<TextureImage>(std::move(*image));
}

void ApplicationPostcardUiController::State::outbox_scroll() {
    if (!root || page != Page::outbox) {
        return;
    }
    auto &body = control(570001), &scroll = control(570002);
    const auto selected = fonts.slot(6, pixel_width);
    if (!selected) {
        throw std::runtime_error(std::format("Original outbox body font: {}", selected.error().reason));
    }
    UiTextStyle style;
    style.wrap = true;
    style.scale = { 2048.f / static_cast<float>(pixel_width), 1536.f / static_cast<float>(pixel_height) };
    const auto& rect = body.attachment_rect;
    const auto measured = fonts.measure(**selected, body.text, { 0, 0, static_cast<float>(rect.width()), static_cast<float>(rect.height()) }, style);
    if (!measured) {
        throw std::runtime_error(std::format("Original outbox recipient layout: {}", measured.error().reason));
    }
    const auto overflow = std::max(0, fixed::truncate_integer(measured->bounds[3]) - rect.height());
    ui.set_visible(scroll, overflow > 0);
    if (overflow) {
        ui.set_range(scroll, 0, overflow);
    }
    body.text_offset = { 0, -static_cast<std::int16_t>(scroll.value) };
    body.text_offsets_explicit = true;
    outbox_scroll_dirty = false;
    outbox_resolution = std::array { pixel_width, pixel_height };
}

void ApplicationPostcardUiController::State::apply_outbox() {
    if (profile_revision != store.profile_revision()) {
        close();
        return;
    }
    std::vector<std::pair<std::uint32_t, PostcardRowAction>> choices(actions.begin(), actions.end());
    auto applied = store.apply(choices);
    if (!applied) {
        error(applied.error());
        return;
    }
    const bool sending = std::ranges::any_of(choices, [](const auto& value) {
        return value.second == PostcardRowAction::send;
    });
    if (sending) {
        const auto status = host.online_status();
        if (status.session_active && !status.permission) {
            host.status_message(73);
            return;
        }
    }
    auto deleted = store.process_deletions();
    if (!deleted) {
        error(deleted.error());
        return;
    }
    close_panel();
    if (sending) {
        start_send();
    }
}

std::intptr_t ApplicationPostcardUiController::State::outbox_event(UiControl& sender, const UiEvent& event) {
    if (event.type == 20 && &sender == root) {
        close_panel();
        return 0;
    }
    if (event.type == 4 || event.type == 5 || postcard_escape(event)) {
        close_panel();
        return 0;
    }
    if (event.type == 1025 && event.id == 570007 && event.value != -1) {
        outbox_preview();
    } else if (event.type == 1028 && event.value > 1) {
        const auto id = ui.selected_list_identity(control(570007));
        if (id) {
            auto& action = actions.at(static_cast<std::uint32_t>(*id));
            const auto selected = event.value == 2 ? PostcardRowAction::send : PostcardRowAction::erase;
            action = action == selected ? PostcardRowAction::none : selected;
            refresh_outbox();
        }
    } else if (event.type == 2048 && event.id == 570002) {
        control(570001).text_offset = { 0, -static_cast<std::int16_t>(event.value) };
        control(570001).text_offsets_explicit = true;
    } else if (event.type == 256) {
        if (event.id == 570004 || event.id == 570005 || event.id == 570006) {
            const auto action = event.id == 570004 ? PostcardRowAction::send : event.id == 570005 ? PostcardRowAction::erase : PostcardRowAction::none;
            for (auto& [id, value] : actions) {
                (void)id;
                value = action;
            }
            refresh_outbox();
        } else if (event.id == 570008 || event.id == 570009) {
            apply_outbox();
            return 0;
        }
    }
    return ui.secondary(sender, event);
}
} //namespace simcoaster
