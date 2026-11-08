#include "ui_runtime_internal.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
//Exact embedded binary stream opcodes from84DEEA. Text-template keywords
//form a different format and do not determine these payload widths.
enum class Command : std::uint16_t {
    create,
    primary_asset,
    secondary_asset,
    attachment_bounds,
    hit_region,
    end,
    button_one,
    button_two,
    thumb,
    optional_scroll,
    viewport,
    columns,
    column_button,
    knob_range,
    button_one_alias,
    button_two_alias,
    indexed_image,
    help,
    metadata,
    stepper_editor
};

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t cursor {};

    [[noreturn]] void fail(std::string reason) const {
        throw UiTemplateError { cursor / 2, std::move(reason) };
    }

    std::uint16_t word() {
        if (cursor > bytes.size() || bytes.size() - cursor < 2) {
            fail("Truncated WORD payload");
        }
        const auto value = static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[cursor]) | std::to_integer<unsigned>(bytes[cursor + 1]) << 8);
        cursor += 2;
        return value;
    }

    std::int32_t signed_word() {
        return std::bit_cast<std::int16_t>(word());
    }

    std::uint32_t dword() {
        const auto lo = word();
        return lo | static_cast<std::uint32_t>(word()) << 16;
    }

    UiRect rect() {
        const auto l = signed_word(), t = signed_word(), r = signed_word(), b = signed_word();
        return { l, t, r, b };
    }

    UiPoint point() {
        const auto x = signed_word(), y = signed_word();
        return { x, y };
    }
};

std::int32_t coordinate_add(std::int32_t a, std::int32_t b) {
    const auto value = static_cast<std::int64_t>(a) + b;
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        throw UiTemplateError { 0, "Coordinate overflow" };
    }
    return static_cast<std::int32_t>(value);
}

void translate_rect(UiRect& r, const UiControl* parent) {
    for (auto* a = parent; a; a = a->parent) {
        r.left = coordinate_add(r.left, a->rect.left);
        r.right = coordinate_add(r.right, a->rect.left);
        r.top = coordinate_add(r.top, a->rect.top);
        r.bottom = coordinate_add(r.bottom, a->rect.top);
    }
}
} //namespace

std::expected<UiControl*, UiTemplateError> UiRuntime::parse_template(UiControl* parent, std::span<const std::byte> bytes, UiCallback callback) {
    if (parent && !owns(parent)) {
        return std::unexpected(UiTemplateError { 0, "Parent is not live" });
    }
    if (bytes.size() % 2) {
        return std::unexpected(UiTemplateError { bytes.size() / 2, "Odd template byte extent" });
    }
    if (bytes.size() > 16777216) {
        return std::unexpected(UiTemplateError { 0, "Template exceeds bounded input extent" });
    }
    //No callback is invoked during construction: creation21 is queued. Preserve
    //existing parent properties/tree links and queues, so every rejected stream
    //rolls back without firing destruction notifications or exposing new nodes.
    std::vector<std::pair<UiControl*, UiControl>> old_controls;
    old_controls.reserve(state_->controls.size());
    for (const auto& control : state_->controls) {
        old_controls.emplace_back(control.get(), *control);
    }
    const auto old_count = state_->controls.size();
    const auto old_roots = state_->roots;
    const auto old_primary = state_->primary, old_secondary = state_->secondary;
    const auto old_serial = state_->next_serial, old_dropped = state_->dropped;
    const std::array old_input { state_->focused, state_->captured, state_->hovered, state_->interaction, state_->input_root, state_->route };
    const auto rollback = [&] {
        for (std::size_t i = old_count; i < state_->controls.size(); ++i) {
            state_->serials.erase(state_->controls[i].get());
        }
        state_->controls.resize(old_count);
        for (auto& [target, snapshot] : old_controls) {
            *target = std::move(snapshot);
        }
        state_->roots = old_roots;
        state_->primary = old_primary;
        state_->secondary = old_secondary;
        state_->next_serial = old_serial;
        state_->dropped = old_dropped;
        state_->focused = old_input[0];
        state_->captured = old_input[1];
        state_->hovered = old_input[2];
        state_->interaction = old_input[3];
        state_->input_root = old_input[4];
        state_->route = old_input[5];
    };
    Reader input { bytes };
    try {
        const auto parse = [&](auto&& self, UiControl* owner, std::size_t depth) -> UiControl* {
            if (depth > 256) {
                input.fail("Template nesting exceeds control-depth limit");
            }
            UiControl* last {};
            const auto require_parent = [&]() -> UiControl& {
                if (!owner) {
                    input.fail("Property command has no parent");
                }
                return *owner;
            };
            const auto child = [&](UiControlKind kind, std::uint32_t flags, std::uint32_t id, UiRect rect) -> UiControl& {
                if (state_->controls.size() - old_count >= 65536) {
                    input.fail("Template control count exceeds limit");
                }
                auto& created = create(owner, kind, flags, id, rect);
                last = &created;
                return created;
            };
            for (;;) {
                match (static_cast<Command>(input.word())) {
                    case Command::create => {
                        const auto raw_kind = input.word();
                        if (raw_kind < 1 || raw_kind > 13) {
                            input.fail("Unknown control kind");
                        }
                        const auto flags = input.dword(), id = input.dword();
                        const auto rect = input.rect();
                        auto& created = child(static_cast<UiControlKind>(raw_kind), flags, id, rect);
                        self(self, &created, depth + 1);
                    }
                    case Command::primary_asset => {
                        auto& c = require_parent();
                        c.asset_hash = input.dword();
                        c.primary_placement.part_value = 0;
                    }
                    case Command::secondary_asset => {
                        auto& c = require_parent();
                        c.secondary_asset_hash = input.dword();
                        c.secondary_placement.part_value = 0;
                    }
                    case Command::attachment_bounds => {
                        auto& c = require_parent();
                        c.attachment_rect = input.rect();
                        if (c.flags & 1024u) {
                            translate_rect(c.attachment_rect, c.parent);
                        }
                    }
                    case Command::hit_region => {
                        auto& c = require_parent();
                        UiHitRegion hit;
                        const auto shape = input.word();
                        match (shape) {
                            case 1 => {
                                hit.shape = UiHitShape::rectangle;
                                hit.rect = input.rect();
                            }
                            case 2 => {
                                hit.shape = UiHitShape::circle;
                                hit.center = input.point();
                                hit.radii.x = input.signed_word();
                                hit.radii.y = hit.radii.x;
                            }
                            case 3 => {
                                hit.shape = UiHitShape::ellipse;
                                hit.center = input.point();
                                hit.radii = input.point();
                            }
                            case 4 => {
                                hit.shape = UiHitShape::polygon;
                                const auto count = input.signed_word();
                                if (count < 0) {
                                    input.fail("Negative polygon count");
                                }
                                hit.polygon.reserve(static_cast<std::size_t>(count));
                                for (std::int32_t i = 0; i < count; ++i) {
                                    hit.polygon.push_back(input.point());
                                }
                            }
                            case _ => input.fail("Unknown hit-region shape");
                        } c.hit_region = std::move(hit);
                        c.custom_hit_region = true;
                    }
                    case Command::end                                                                                         => return last;
                    case Command::button_one or Command::button_one_alias or Command::button_two or Command::button_two_alias => {
                        const auto opcode = static_cast<Command>(std::to_integer<unsigned>(bytes[input.cursor - 2]) | std::to_integer<unsigned>(bytes[input.cursor - 1]) << 8);
                        const auto id = (opcode == Command::button_one || opcode == Command::button_one_alias) ? 1u : 2u;
                        const auto rect = input.rect();
                        auto& created = child(UiControlKind::button, 33, id, rect);
                        self(self, &created, depth + 1);
                    }
                    case Command::thumb => {
                        const auto rect = input.rect();
                        auto& created = child(UiControlKind::button, 1, 3, rect);
                        if (owner && owner->kind == UiControlKind::scroll) {
                            configure_scroll_thumb(created);
                        } else {
                            input.fail("Thumb command requires a Scroll parent");
                        }
                        self(self, &created, depth + 1);
                    }
                    case Command::optional_scroll => {
                        auto& c = require_parent();
                        if ((c.kind == UiControlKind::list_box || c.kind == UiControlKind::list_control) && !find(&c, 1)) {
                            const auto rect = input.rect();
                            auto& created = child(UiControlKind::scroll, 16, 1, rect);
                            self(self, &created, depth + 1);
                        }
                    }
                    case Command::viewport => {
                        auto& c = require_parent();
                        if (c.kind == UiControlKind::list_box || c.kind == UiControlKind::list_control) {
                            c.viewport = input.rect();
                        }
                    }
                    case Command::columns => {
                        auto& c = require_parent();
                        if (c.kind != UiControlKind::list_control) {
                            input.fail("Columns command requires ListControl");
                        }
                        const auto count = input.signed_word();
                        if (count < 0) {
                            input.fail("Negative column count");
                        }
                        c.columns.clear();
                        c.columns.reserve(static_cast<std::size_t>(count));
                        c.column_presentations.assign(static_cast<std::size_t>(count), {});
                        for (std::int32_t i = 0; i < count; ++i) {
                            const auto p = input.point();
                            c.columns.push_back({ p.x, 0, p.y, 1 });
                        }
                    }
                    case Command::column_button => {
                        const auto index = input.signed_word();
                        const auto rect = input.rect();
                        auto& created = child(UiControlKind::button, 1, static_cast<std::uint32_t>(index + 16), rect);
                        self(self, &created, depth + 1);
                    }
                    case Command::knob_range => {
                        auto& c = require_parent();
                        if (c.kind != UiControlKind::knob) {
                            input.fail("Raw range requires Knob");
                        }
                        const auto lo = input.signed_word(), hi = input.signed_word();
                        set_raw_range(c, lo, hi);
                    }
                    case Command::indexed_image => {
                        const auto index = input.signed_word();
                        const auto rect = input.rect();
                        auto& created = child(UiControlKind::image, 1, static_cast<std::uint32_t>(index) | 0x11100000u, rect);
                        self(self, &created, depth + 1);
                    }
                    case Command::help           => require_parent().help_index = input.dword();
                    case Command::metadata       => require_parent().template_metadata = input.dword();
                    case Command::stepper_editor => {
                        auto& c = require_parent();
                        if (c.kind != UiControlKind::numeric_stepper) {
                            input.fail("Editor association requires NumericStepper");
                        }
                        const auto rect = input.rect();
                        auto& created = child(UiControlKind::edit_box, 257, 3, rect);
                        c.associated_edit = &created;
                    }
                    case _ => input.fail("Unknown template command");
                }
            }
        };
        auto* result = parse(parse, parent, 0);
        if (input.cursor != bytes.size()) {
            input.fail("Bytes remain after outer END");
        }
        if (result && callback) {
            set_primary_callback(*result, std::move(callback));
        }
        return result;
    } catch (const UiTemplateError& error) {
        rollback();
        return std::unexpected(error);
    } catch (...) {
        rollback();
        throw;
    }
}
} //namespace simcoaster
