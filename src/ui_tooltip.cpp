#include "simcoaster/numeric_word.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/ui_tooltip.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using TooltipPolicy = StandardUiParameters;
using TooltipOffsets = StandardTooltipOffsets; //Native World Balance descriptor99EBE0 has indexed_count291. This is not
//the UIHELPTEXT corpus size: later help rows have no offset record.
} //namespace

struct UiTooltipController::State {
    UiRuntime& ui;
    UiControl *event_root {}, *control {};
    UiFontLibrary& fonts;
    WorldRuntime& context;
    TooltipPolicy policy;
    std::array<TooltipOffsets, tooltip_offset_count> table_offsets {};
    bool enabled = true, created {}, pending {}, clipped_rate {};
    bool has_pointer {};
    UiPoint pointer {}, anchor {}, query_offsets {}, direct_offsets {};
    std::uint32_t deadline {}, next_step {}, pixel_width {}, pixel_height {};
    std::int32_t step {};
    std::u16string cached_text;

    State(UiRuntime& runtime, UiControl* parent, UiFontLibrary& library, WorldRuntime& c, const std::filesystem::path& path) : ui(runtime), event_root(parent), fonts(library), context(c) {
        if (parent && !ui.owns(parent)) {
            throw std::invalid_argument("Tooltip requires its live UI event root");
        }
        const auto& balance = ensure_world_standard_ui_balance(c, path);
        policy = balance.user_interface;
        table_offsets = balance.offsets;
    }

    std::uint32_t clock() const {
        return static_cast<std::uint32_t>(ui.clock().count());
    }

    std::uint32_t delayed() const {
        return clock() + static_cast<std::uint32_t>(policy.tooltip_delay);
    }

    void hide() {
        pending = false;
        if (ui.owns(control)) {
            ui.set_visible(*control, false);
        }
    }

    std::shared_ptr<const UiBitmapFont> font(std::uint32_t width) {
        auto result = fonts.slot(7, width);
        if (!result) {
            throw std::runtime_error(std::format("Original tooltip font: {}", result.error().reason));
        }
        return *result;
    }

    void create(std::uint32_t width, std::uint32_t height) {
        if (created) {
            return;
        }
        created = true;
        const auto line = font(width)->line_advance;
        const auto top = 1520 - static_cast<std::int32_t>(4608u * line / height + 10u);
        control = &ui.create(event_root, UiControlKind::window, 1, 0xFFF355E4u, { 574, top, 1474, 1520 });
        control->asset_hash = ui_model_asset_hash("!tooltip");
        control->font = 7;
        control->horizontal_alignment = 1;
        control->vertical_alignment = 1;
        control->text_constraints = { true, true };
        control->color = { 0, 0, 0, 255 };
        ui.set_enabled(*control, false);
        ui.set_depth(*control, 38);
        ui.set_visible(*control, false);
        ui.set_primary_callback(*control, [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
            if (event.type == 13) {
                return 0;
            }
            if (event.type == 20 && &sender == control) {
                control = nullptr;
                pending = false;
            }
            return ui.secondary(sender, event);
        });
    }

    void position(UiPoint point) {
        if (!ui.owns(control)) {
            return;
        }
        const auto width = control->rect.width(), height = control->rect.height();
        //Native5170D0 deliberately consumes per-query X/Y offsets transposed.
        const auto x = point.x + policy.tooltip_x + query_offsets.y;
        const auto y = point.y + policy.tooltip_y + query_offsets.x;
        auto area = control->rect;
        const auto set = [&](std::int32_t left, std::int32_t top) {
            area = { left, top, left + width, top + height };
        };
        match (policy.tooltip_offset_where) {
            case 0 => set(x, y - height);
            case 1 => set(x - width, y - height);
            case 2 => set(x, y);
            case 3 => set(x - width, y);
            case 4 => set(x - width / 2, y);
            case 5 => set(x - width / 2, y - height);
            case 6 => set(x - width / 2, y - height / 2);
            case 7 => set(x, y - height / 2);
            case 8 => set(x - width, y - height / 2);
            case _ => {
                //Native preserves the queried rectangle for unknown enums.
            }
        } if (area.right > 2048) {
            area.right = 2048;
            area.left = 2048 - width;
        }
        if (area.top < 0) {
            area.bottom -= area.top;
            area.top = 0;
        }
        //No left or bottom clipping: these asymmetries are actual UI behavior.
        ui.set_bounds(*control, area);
    }

    void resize(std::uint32_t width, std::uint32_t height) {
        const auto face = font(width);
        UiTextStyle style;
        style.vertical = cached_text.contains(u'\n') ? UiTextVertical::top : UiTextVertical::middle;
        const auto layout_width = 700u * width / 2048u;
        auto measured = fonts.measure(*face, cached_text, { 0, 0, static_cast<float>(layout_width), 1536 }, style);
        if (!measured) {
            throw std::runtime_error(std::format("Original tooltip layout: {}", measured.error().reason));
        }
        const auto measured_width = static_cast<std::int32_t>(measured->bounds[2] - measured->bounds[0]);
        const auto measured_height = static_cast<std::int32_t>(measured->bounds[3] - measured->bounds[1]);
        const auto logical_width = std::max(80, static_cast<std::int32_t>((static_cast<std::int64_t>(measured_width) + 12) * 2048 / width));
        const auto logical_height = std::max(80, static_cast<std::int32_t>((static_cast<std::int64_t>(measured_height) + 12) * 1536 / height));
        ui.set_bounds(*control, { 0, 0, logical_width, logical_height });
        control->attachment_rect = control->rect;
        control->horizontal_alignment = control->vertical_alignment = 1;
        control->text_constraints = { true, true };
        control->font = 7;
        ui.set_text(*control, cached_text);
        anchor = pointer;
        position(pointer);
    }

    void animate() {
        const auto now = clock();
        if (pending && numeric::word(now) > numeric::word(deadline)) {
            ui.set_visible(*control, true);
            step = 0;
            next_step = now + 20u;
            ui.set_attachment_part(*control, 0);
            control->color = { 0, 0, 0, 0 };
            pending = false;
            anchor = pointer;
            position(pointer);
        }
        if (!control->visible() || step >= 7) {
            return;
        }
        if (numeric::word(now - next_step) > 500 || clipped_rate) {
            step = 7;
            ui.set_attachment_part(*control, 7);
            control->color = { 0, 0, 0, 255 };
        } else if (numeric::word(next_step) <= numeric::word(now)) {
            ++step;
            next_step = now + 20u;
            ui.set_attachment_part(*control, static_cast<std::uint16_t>(step));
            control->color = { 0, 0, 0, static_cast<std::uint8_t>(255 / (8 - step)) };
        }
    }

    void update(std::u16string_view text, UiPoint point, std::uint32_t width, std::uint32_t height, bool refresh_text = true);
};

UiTooltipController::UiTooltipController(UiRuntime& runtime, UiControl* event_root, UiFontLibrary& fonts, WorldRuntime& c, const std::filesystem::path& path) : state_(std::make_unique<State>(runtime, event_root, fonts, c, path)) {}

UiTooltipController::~UiTooltipController() {
    if (state_->ui.owns(state_->control)) {
        state_->ui.destroy(state_->control);
    }
}

UiControl* UiTooltipController::root() const {
    return state_->control;
}

void UiTooltipController::enabled(bool value) {
    auto& s = *state_;
    if (s.enabled != value) {
        s.enabled = value;
        s.hide();
    }
}

void UiTooltipController::offsets(UiPoint value) {
    state_->direct_offsets = state_->query_offsets = value;
}

void UiTooltipController::frame_rate_below_clip(bool value) {
    state_->clipped_rate = value;
}

void UiTooltipController::update(std::u16string_view text, UiPoint pointer, std::uint32_t width, std::uint32_t height) {
    state_->query_offsets = state_->direct_offsets;
    state_->update(text, pointer, width, height);
}

void UiTooltipController::update_help(UiControl* source, UiPoint pointer, std::uint32_t width, std::uint32_t height) {
    auto& s = *state_;
    if (!s.enabled) {
        return;
    }
    if (!s.ui.owns(source)) {
        s.query_offsets = {};
        s.update({}, pointer, width, height);
        return;
    }
    const auto result = s.ui.dispatch(source, 12);
    if (!result || result == -1) {
        //Native516DB0 does not submit fresh text here, but the independent
        //hover/animation timers still run. In particular, do not rearm a hidden
        //cached caption simply because the source supplied no query payload.
        s.update({}, pointer, width, height, false);
        return;
    }
    const auto query = *reinterpret_cast<const UiHelpQuery*>(result);
    if (query.text) {
        //Copy before any further event dispatch: the owner may use a reusable
        //query buffer, or its control may disappear while the tooltip is laid out.
        const std::u16string text { query.text };
        s.query_offsets = {};
        s.update(text, pointer, width, height);
        return;
    }
    if (query.index == 4294967295u) {
        s.query_offsets = {};
        s.update({}, pointer, width, height);
        return;
    }
    s.query_offsets = {};
    if (query.index < s.table_offsets.size()) {
        const auto& pair = s.table_offsets[query.index];
        const auto low_word = [](std::int32_t value) -> std::int32_t {
            return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
        };
        s.query_offsets = { low_word(pair.x), low_word(pair.y) };
    }
    auto text = format_bfst_wide_string(s.context.messages.mail_language.ui_help, s.context.messages.mail_language.conversion, query.index, 16383);
    if (!text) {
        throw std::runtime_error(std::format("Original tooltip UIHELPTEXT row {}: {}", query.index, text.error().reason));
    }
    s.update(*text, pointer, width, height);
}

void UiTooltipController::State::update(std::u16string_view text, UiPoint point, std::uint32_t width, std::uint32_t height, bool refresh_text) {
    auto& s = *this;
    if (!width || !height) {
        throw std::invalid_argument("Tooltip viewport must have nonzero dimensions");
    }
    if (!s.enabled) {
        return;
    }
    s.create(width, height);
    if (!s.ui.owns(s.control)) {
        return; //Native event20 permanently clears this controller's reset gate.
    }
    const bool moved = !s.has_pointer || point.x != s.pointer.x || point.y != s.pointer.y;
    s.pointer = point;
    s.has_pointer = true;
    if (moved) {
        if (s.control->visible()) {
            if (std::abs(point.x - s.anchor.x) > s.policy.tooltip_max_x || std::abs(point.y - s.anchor.y) > s.policy.tooltip_max_y) {
                s.hide();
            }
        } else {
            s.deadline = s.delayed();
        }
    }
    if (!refresh_text) {
        s.animate();
        return;
    }
    if (text.empty()) {
        s.hide();
        return;
    }
    const bool changed = text != s.cached_text;
    const bool resized = width != s.pixel_width || height != s.pixel_height;
    s.pixel_width = width;
    s.pixel_height = height;
    if (changed || resized) {
        s.cached_text.assign(text);
        s.resize(width, height);
        s.ui.set_visible(*s.control, false);
        s.pending = true;
        s.deadline = s.delayed();
    } else if (!s.pending && !s.control->visible()) {
        s.pending = true;
        s.deadline = s.delayed();
    }
    s.animate();
}
} //namespace simcoaster
