#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/application_tip_ui.hpp"
#include "simcoaster/company_email.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/world_research.hpp"
#include <format>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
//Original Game.exe9C8520, terminated by the outer END, not the following
//training template. Its model surfaces and native seven-part icon are kept.
constexpr unsigned char tip_template[] {
#embed "ui_templates/application_tip.bin"
};
static_assert(sizeof(tip_template) == 468);
using TipBalance = StandardTipBalance;

bool escape(const UiEvent& event) {
    return event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && !(static_cast<std::uint32_t>(event.value) & 0xFF0000u);
}
} //namespace

struct ApplicationTipUiController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary {}, *dialog {};
    ApplicationTipUiHost host;
    std::filesystem::path game_root;
    std::string language;
    std::vector<std::byte> table;
    std::optional<std::uint32_t> displayed_tip;
    bool leased {}, closing {};

    State(WorldRuntime& context, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, ApplicationTipUiHost callbacks, std::string locale) : c(context), ui(runtime), primary(parent), host(std::move(callbacks)), game_root(path), language(std::move(locale)) {
        if (!ui.owns(primary) || !host.history || !host.enabled || !host.set_enabled || !host.pause || !host.command_families_enabled || !host.resume_ui_transition || !host.panels.dismiss || !host.panels.activate || !host.panels.deactivate) {
            throw std::invalid_argument("Tip of the Day requires its complete campaign/application host");
        }
    }

    ~State() {
        close();
    }

    UiControl& required(std::uint32_t id) {
        if (auto* control = ui.find_recursive(dialog, id)) {
            return *control;
        }
        throw std::runtime_error(std::format("Original tip template missing control {}", id));
    }

    std::u16string text(std::uint32_t row) {
        if (table.empty()) {
            const auto path = (game_root / "Data/Language" / language / "TOTD.str").native_encoded_string();
            const auto contents = c.ports.assets.read_balance_asset(c, path);
            if (!contents) {
                throw std::runtime_error(std::format("Missing original Tip of the Day table {}", path));
            }
            const auto bytes = std::as_bytes(std::span(contents->data(), contents->size()));
            table.assign(bytes.begin(), bytes.end());
        }
        const auto result = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383);
        if (!result) {
            throw std::runtime_error(std::format("Original tip text {}: {}", row, result.error().reason));
        }
        return *result;
    }

    bool suppressed() {
        TipBalance balance;
        bool configured {};
        for (const auto& path : { game_root / "Data/levels/Standard.sam", game_root / "Data/levels" / c.source.selected_resource_source_name / "Standard.sam" }) {
            if (const auto input = c.ports.assets.read_balance_asset(c, path.native_encoded_string())) {
                const auto parsed = sam::parse_record<TipBalance, true>(balance, *input, configured);
                if (!parsed) {
                    throw std::runtime_error(std::format("Invalid Tip of the Day balance at byte {}", parsed.error().offset));
                }
                configured = true;
            }
        }
        if (!configured) {
            throw std::runtime_error("Missing original Tip of the Day balance");
        }
        return balance.suppression.all || balance.suppression.tips;
    }

    void release(UiControl* old) {
        displayed_tip.reset();
        if (!std::exchange(leased, false)) {
            return;
        }
        host.panels.deactivate(old);
        ui.set_input_root(primary);
        if (host.panels.synchronize_focus) {
            host.panels.synchronize_focus();
        } else {
            ui.set_focus(primary);
        }
        host.pause(false);
        host.resume_ui_transition();
        host.command_families_enabled(true);
    }

    void close() {
        if (closing) {
            return;
        }
        closing = true;
        auto* old = std::exchange(dialog, nullptr);
        if (ui.owns(old)) {
            ui.destroy(old);
        }
        release(old);
        closing = false;
    }

    void finish() {
        close();
        //592040 intentionally obtains the World cutscene record before the
        //once-only mission event. The modern helper owns the actual TagSystem
        //event and profile mission latch; no synthetic UI notification is used.
        if (c.simulation.current && c.simulation.current->saved.mission_controller) {
            (void)c.entities.at(c.simulation.current->saved.mission_controller).payload;
        }
        dispatch_initial_mission_event_once(c);
    }

    std::intptr_t event(UiControl& control, const UiEvent& value) {
        if (value.type == 20 && control.id == 61000) {
            if (dialog == &control) {
                dialog = nullptr;
            }
            release(&control);
            return ui.secondary(control, value);
        }
        if (value.type == 4) {
            close();
            return 0;
        }
        if (value.type == 5) {
            finish();
            return 1;
        }
        if (escape(value)) {
            //507FA0 sends CLOSE4, rather than Tip's DISMISS5. In particular it
            //does not send the initial-mission event performed by the Okay route.
            close();
            return 0;
        }
        if (value.type == 256) {
            if (value.id == 61001 || value.id == 4294967294u) {
                finish();
                return 0;
            }
            if (value.id == 61006) {
                next();
                return 0;
            }
            if (value.id == 61005) {
                host.set_enabled(value.value == 0); //591FF0, inverted checkbox.
            }
        } else if (value.type == 257 && value.id == 61005) {
            host.set_enabled(value.value == 1); //592040's separate root route.
        }
        return ui.secondary(control, value);
    }

    void next() {
        if (!ui.owns(dialog)) {
            return;
        }
        auto& history = host.history();
        if (history.count >= 92) {
            history.count = 0;
            history.seen.fill(0);
        }
        //The native rejection loop would never terminate for a malformed
        //imported history with all92 set but a count below92. Report that bad
        //profile explicitly; do not silently replace its persisted history.
        if (history.count < 0
            || std::ranges::all_of(
                history.seen,
                [](auto flag) {
                    return flag != 0;
                })) {
            throw std::runtime_error("Invalid original Tip of the Day history");
        }
        std::uint32_t index {};
        do {
            index = static_cast<std::uint32_t>(next_crt_random(c)) % 92u;
        } while (history.seen[index]);
        //Resolve the original text before publishing a history mutation.
        const auto body = text(index);
        history.seen[index] = 1;
        ++history.count;
        auto& body_control = required(61002);
        body_control.secondary_asset_hash = 0;
        body_control.font = 6;
        body_control.default_text_y_offset = 9;
        body_control.horizontal_alignment = body_control.vertical_alignment = 1;
        body_control.text_constraints = { true, false };
        body_control.color = { 255, 255, 255, 255 };
        ui.set_text(body_control, body);
        const auto icon = index % 7;
        required(61008).primary_placement.part_value = static_cast<std::uint16_t>(icon == 6 ? 7 : icon);
        displayed_tip = index;
    }

    bool show(bool force) {
        //Force bypasses only the stored checkbox preference. Neither the CDK
        //Mode3 guard nor CutTheCrap suppression is overridden.
        if (c.mode.game == GameMode::code_3 || suppressed() || (!host.enabled() && !force)) {
            return false;
        }
        close();
        host.command_families_enabled(false);
        host.pause(true);
        leased = true;
        try {
            host.panels.dismiss();
            const auto parsed = ui.parse_template(primary, std::as_bytes(std::span(tip_template)));
            if (!parsed) {
                throw std::runtime_error(std::format("Original tip template at word {}: {}", parsed.error().word, parsed.error().reason));
            }
            dialog = *parsed;
            if (!dialog || dialog->id != 61000) {
                throw std::runtime_error("Original Tip of the Day root is absent");
            }
            bind_ui_panel(ui, *dialog, [this](auto& sender, const auto& value) {
                return event(sender, value);
            });
            auto& title = required(61003);
            title.font = 5;
            title.default_text_y_offset = 9;
            title.primary_shadow_text = true;
            title.horizontal_alignment = 0;
            title.vertical_alignment = 1;
            ui.set_text(title, text(93));
            auto& checkbox_caption = required(61004);
            checkbox_caption.secondary_asset_hash = 0;
            checkbox_caption.font = 6;
            checkbox_caption.default_text_y_offset = 9;
            ui.set_text(checkbox_caption, text(92));
            ui.set_value(required(61005), !host.enabled(), false);
            next();
            ui.set_visible(required(61009), false);
            host.panels.activate(dialog);
            ui.set_input_root(dialog);
            ui.set_focus(dialog);
            host.command_families_enabled(false);
        } catch (...) {
            close();
            throw;
        }
        return true;
    }
};

ApplicationTipUiController::ApplicationTipUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& game_root, ApplicationTipUiHost host, std::string language) : state_(std::make_unique<State>(c, ui, primary, game_root, std::move(host), std::move(language))) {}

ApplicationTipUiController::~ApplicationTipUiController() = default;

bool ApplicationTipUiController::show(bool force) {
    return state_->show(force);
}

void ApplicationTipUiController::next() {
    state_->next();
}

void ApplicationTipUiController::close() {
    state_->close();
}

bool ApplicationTipUiController::is_open() const {
    return state_->ui.owns(state_->dialog);
}

UiControl* ApplicationTipUiController::root() const {
    return state_->dialog;
}

std::optional<std::uint32_t> ApplicationTipUiController::current_tip() const {
    return state_->displayed_tip;
}
} //namespace simcoaster
