#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/application_debug_selector.hpp"
#include "application_debug_pages.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char selector_template[]{
#embed "ui_templates/application_debug_selector.bin"
};
constexpr unsigned char inspection_template[]{
#embed "ui_templates/application_debug_inspection.bin"
};
constexpr unsigned char buttons_template[]{
#embed "ui_templates/application_debug_buttons.bin"
};
static_assert(sizeof(selector_template) == 1450
              && sizeof(inspection_template) == 432
              && sizeof(buttons_template) == 3402);
constexpr std::array<std::u16string_view, 12> page_names{ u"BULLFROG", u"NICK",  u"BEN",    u"JOHN",
                                                          u"JANSUZ",   u"ED",    u"DAVE",   u"CHRIS",
                                                          u"RICHARD",  u"LEIGH", u"WILL B", u"OLD PANEL" };

bool escape(const UiEvent& e) {
    return e.type == 65547
        && (e.id > 255 ? e.id >> 8 : e.id) == 27
        && !(static_cast<std::uint32_t>(e.value) & 0xFF0000u);
}
} //namespace

std::u16string_view application_debug_page_name(ApplicationDebugPage p) {
    return page_names.at(static_cast<std::size_t>(p));
}

struct ApplicationDebugSelectorController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl* primary{};
    UiControl* selector{};
    UiControl* inspection{};
    UiControl* buttons{};
    ApplicationDebugSelectorHost host;
    std::optional<ApplicationDebugPage> page;
    std::optional<std::string> error;
    bool closing{};

    State(WorldRuntime& context, UiRuntime& runtime, UiControl* p, ApplicationDebugSelectorHost h)
        : c(context),
          ui(runtime),
          primary(p),
          host(std::move(h)) {
        if (!ui.owns(p)
            || !host.panels.dismiss
            || !host.panels.activate
            || !host.panels.deactivate
            || !host.persistent
            || !host.execute
            || !host.report_error)
            throw std::invalid_argument("Developer tools require owned UI, process " "state and command routing");
        bind_application_debug_state(c, *host.persistent);
    }

    ~State() {
        close();
    }

    void focus() {
        auto* root = ui.owns(selector)   ? selector
                   : ui.owns(inspection) ? inspection
                   : ui.owns(buttons)    ? buttons
                                         : primary;
        ui.set_input_root(root);
        if (root != primary)
            ui.set_focus(root);
        else if (host.panels.synchronize_focus)
            host.panels.synchronize_focus();
        else
            ui.set_focus(primary);
    }

    void drop(UiControl*& owned) {
        auto* old = std::exchange(owned, nullptr);
        if (ui.owns(old))
            ui.destroy(old);
        if (old)
            host.panels.deactivate(old);
    }

    void close() {
        if (closing)
            return;
        closing = true;
        drop(selector);
        drop(inspection);
        drop(buttons);
        page.reset();
        focus();
        closing = false;
    }

    void close_page() {
        drop(selector);
        page.reset();
        focus();
    }

    void fail(std::string why) {
        error = std::move(why);
        host.report_error(*error);
    }

    UiControl& required(UiControl* root, std::uint32_t id) {
        auto* p = ui.find_recursive(root, id);
        if (!p)
            throw std::runtime_error("Developer control is absent: " + std::to_string(id));
        return *p;
    }

    UiControl* parse(std::span<const unsigned char> bytes, UiControl* parent = nullptr) {
        auto p = ui.parse_template(parent ? parent : primary, std::as_bytes(bytes));
        if (!p)
            throw std::runtime_error(
                "Developer template word " + std::to_string(p.error().word) + ": " + p.error().reason);
        if (!*p)
            throw std::runtime_error("Developer template has no root");
        return *p;
    }

    void captions() {
        for (std::size_t i = 0; i < 12; ++i) {
            auto& label = required(selector, 100025u + static_cast<unsigned>(i));
            auto text = page ? debug_detail::pages[static_cast<std::size_t>(*page)][i].caption : page_names[i];
            if (page == ApplicationDebugPage::leigh) {
                if (i == 8 && host.persistent->force_special)
                    text = u"Complete things for Special rides";
                if (i == 9 && host.persistent->always_win)
                    text = u"Do Challenges normally";
            }
            label.secondary_asset_hash = 0;
            label.font = 6;
            label.default_text_y_offset = 9;
            ui.set_text(label, text);
        }
    }

    std::intptr_t event(UiControl& control, const UiEvent& e) {
        if (e.type == 20) {
            if (selector == &control) {
                selector = nullptr;
                page.reset();
                host.panels.deactivate(&control);
                focus();
            }
            return ui.secondary(control, e);
        }
        if (e.type == 5 || escape(e) || (e.type == 256 && e.id == 4294967295u)) {
            close_page();
            return 0;
        }
        if (e.type == 256 && e.id >= 100001 && e.id <= 100012) {
            const auto index = e.id - 100001;
            if (!page) {
                page = static_cast<ApplicationDebugPage>(index);
                captions();
                error.reset();
                return 0;
            }
            const auto slot = debug_detail::pages[static_cast<std::size_t>(*page)][index];
            const auto page_name = application_debug_page_name(*page);
            const auto action = "Developer "
                              + std::string{ page_name.begin(), page_name.end() }
                              + " command "
                              + std::to_string(index + 1)
                              + ": ";
            if (slot.close == debug_detail::Close::before)
                close_page();
            try {
                auto result = std::expected<void, std::string>{};
                if (slot.command == ApplicationDebugCommand::inspection)
                    open_inspection();
                else if (slot.command == ApplicationDebugCommand::button_window)
                    open_buttons();
                else if (slot.command != ApplicationDebugCommand::none)
                    result = host.execute(slot.command);
                if (!result)
                    fail(action + result.error());
                else
                    error.reset();
                if (slot.close == debug_detail::Close::after)
                    close_page();
                if (ui.owns(selector))
                    captions();
            } catch (const std::exception& ex) {
                fail(action + ex.what());
            }
            return slot.forward && ui.owns(&control) ? ui.secondary(control, e) : slot.result;
        }
        return ui.secondary(control, e);
    }

    void bind_aux(UiControl& control, UiControl* root, bool inspection_kind) {
        const auto widget = control.primary_callback;
        ui.set_primary_callback(
            control,
            [this, root, inspection_kind, widget](UiControl& sender, const UiEvent& e) -> std::intptr_t {
                if (&sender == root && e.type == 20) {
                    auto*& owned = inspection_kind ? inspection : buttons;
                    if (owned == root) {
                        owned = nullptr;
                        host.panels.deactivate(root);
                        focus();
                    }
                    return ui.secondary(sender, e);
                }
                if ((e.type == 256 && (e.id == 4294967294u || (!inspection_kind && e.id == 500001)))
                    || (inspection_kind && (e.type == 5 || escape(e)))) {
                    if (inspection_kind)
                        drop(inspection);
                    else
                        drop(buttons);
                    focus();
                    return inspection_kind && e.type == 5 ? 1 : 0;
                }
                if (inspection_kind && e.type == 1030)
                    host.persistent->sort_key = static_cast<std::int32_t>(e.value);
                if (inspection_kind && (e.type == 65546 || e.type == 65547) && host.key && host.key(e))
                    return 1;
                if (&sender == root || (e.type >= 256 && e.type <= 2055))
                    return ui.secondary(sender, e);
                return widget ? widget(sender, e) : ui.secondary(sender, e);
            });
        for (auto* child : control.children)
            bind_aux(*child, root, inspection_kind);
    }

    void open_buttons() {
        drop(buttons);
        host.panels.dismiss();
        try {
            buttons = parse(buttons_template);
            bind_aux(*buttons, buttons, false);
            host.panels.activate(buttons);
            ui.set_depth(*buttons, buttons->render_depth + 10);
            focus();
        } catch (...) {
            drop(buttons);
            focus();
            throw;
        }
    }

    void open_inspection() {
        if (ui.owns(inspection))
            return;
        host.panels.dismiss();
        try {
            inspection = parse(inspection_template, host.inspection_parent);
            auto& list = required(inspection, 623974932);
            list.font = 7;
            list.row_height_font_padding = 6;
            list.list_highlight_asset_hash = ui_model_asset_hash("hilight");
            list.column_presentations.resize(5);
            constexpr std::array names{ u"Name", u"Type", u"Xps", u"Ability", u"Progress" };
            constexpr std::array helps{ 15u, 17u, 15u, 17u, 17u };
            for (unsigned i = 0; i < 5; ++i) {
                auto& col = list.column_presentations[i];
                col.font = 7;
                col.format = (i == 2 || i == 4) ? UiListCellFormat::signed_integer : UiListCellFormat::text;
                auto& heading = required(&list, 16 + i);
                heading.font = 8;
                heading.asset_hash = ui_model_asset_hash("!cbut");
                heading.default_text_y_offset = 9;
                heading.horizontal_alignment = heading.vertical_alignment = 1;
                heading.text_constraints = { true, true };
                //503090 requests gray128, then its three style setters immediately
                //refresh enabled text to opaque white. Disabled styling is retained.
                heading.color = { 255, 255, 255, 255 };
                heading.help_index = helps[i];
                for (unsigned s = 0; s < 3; ++s)
                    heading.button_text_colors[s] = heading.color;
                ui.set_text(heading, names[i]);
            }
            std::vector<UiListRow> rows;
            //Native enumeration is active-list order, staff mask1, TRAINING8 only.
            for (auto* link = c.entities.active_head; link; link = link->active_next) {
                auto* payload = link->payload;
                if (!payload || payload->retirement_pending || !is_staff_record_kind(payload->kind))
                    continue;
                const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(payload);
                if (staff.state == StaffState::training)
                    rows.push_back({ staff.base.header.id, { u"SOMETHING", u"SOMETHING", 42, u"SOMETHING", 24 } });
            }
            if (!rows.empty()) {
                const std::array argument{ BfstSignedIntegerArgument{ 6, 24 } };
                auto percent = format_bfst_wide_string(c.messages.mail_language.ui_text,
                                                       c.messages.mail_language.conversion,
                                                       959,
                                                       63,
                                                       {},
                                                       argument);
                if (!percent)
                    throw std::runtime_error("Developer percentage: " + percent.error().reason);
                for (auto& row : rows)
                    row.cells[4].display_text = *percent;
            }
            ui.set_list_rows(list, std::move(rows), false, false);
            ui.set_list_sort_key(list, host.persistent->sort_key, false);
            if (!list.rows.empty())
                ui.select_list_row(list, 0, false);
            auto& title = required(inspection, 623974933);
            title.font = 5;
            title.primary_shadow_text = true;
            title.horizontal_alignment = 0;
            title.vertical_alignment = 1;
            title.color = { 255, 255, 255, 255 };
            ui.set_text(title, u"Debug Panel");
            if (auto* container = ui.find_recursive(inspection, 623974929)) {
                auto& text = required(container, 623974930);
                text.font = 6;
                text.default_text_y_offset = 9;
                ui.set_text(text, u"Foo test 64");
                auto& number = required(container, 623974931);
                number.font = 6;
                number.default_text_y_offset = 9;
                number.scalar_text = UiScalarTextKind::signed_integer;
                number.value = 128;
            }
            bind_aux(*inspection, inspection, true);
            host.panels.activate(inspection);
            focus();
        } catch (...) {
            drop(inspection);
            focus();
            throw;
        }
    }

    bool open() {
        if (!c.debug.cheat_commands.cheat_commands_enabled)
            return false;
        if (ui.owns(selector))
            return true;
        close();
        host.panels.dismiss();
        try {
            selector = parse(selector_template);
            if (selector->id != 100000)
                throw std::runtime_error("Developer selector root is absent");
            bind_ui_panel(ui, *selector, [this](auto& sender, const auto& value) {
                return event(sender, value);
            });
            captions();
            error.reset();
            host.panels.activate(selector);
            focus();
        } catch (const std::exception& ex) {
            close();
            fail(std::string{ "Opening developer selector: " } + ex.what());
            return false;
        }
        return true;
    }
};

ApplicationDebugSelectorController::ApplicationDebugSelectorController(WorldRuntime& c,
                                                                       UiRuntime& ui,
                                                                       UiControl* p,
                                                                       ApplicationDebugSelectorHost h)
    : state_(std::make_unique<State>(c, ui, p, std::move(h))) {}

ApplicationDebugSelectorController::~ApplicationDebugSelectorController() = default;

bool ApplicationDebugSelectorController::open() {
    return state_->open();
}

void ApplicationDebugSelectorController::close() {
    state_->close();
}

bool ApplicationDebugSelectorController::is_open() const {
    return state_->ui.owns(state_->selector) || state_->ui.owns(state_->inspection) || state_->ui.owns(state_->buttons);
}

UiControl* ApplicationDebugSelectorController::root() const {
    return state_->ui.owns(state_->selector)   ? state_->selector
         : state_->ui.owns(state_->inspection) ? state_->inspection
                                               : state_->buttons;
}

const std::optional<std::string>& ApplicationDebugSelectorController::last_error() const {
    return state_->error;
}
} //namespace simcoaster
