#include "ui_runtime_internal.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace simcoaster {
namespace {
template <class Compare>
void order_list_rows(UiControl& control, Compare compare) {
    if (std::ranges::none_of(control.rows, [](const auto& row) {
            return row.parent_identity.has_value();
        })) {
        if (control.sort_column >= 0)
            std::ranges::stable_sort(control.rows, compare);
        return;
    }
    //8543D4 relinks root siblings only. 8556B7 exposes the resulting
    //depth-first order; children remain attached in their insertion order.
    std::unordered_map<std::intptr_t, std::size_t> identities;
    identities.reserve(control.rows.size());
    for (std::size_t i = 0; i < control.rows.size(); ++i)
        if (!identities.emplace(control.rows[i].identity, i).second)
            throw std::invalid_argument("Hierarchical UI list has duplicate identities");
    std::vector<std::vector<std::size_t>> children(control.rows.size());
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < control.rows.size(); ++i) {
        const auto parent = control.rows[i].parent_identity;
        if (!parent) {
            roots.push_back(i);
            continue;
        }
        const auto found = identities.find(*parent);
        if (found == identities.end() || found->second == i)
            throw std::invalid_argument("Hierarchical UI list has an invalid parent");
        children[found->second].push_back(i);
    }
    if (control.sort_column >= 0)
        std::ranges::stable_sort(roots, [&](auto first, auto second) {
            return compare(control.rows[first], control.rows[second]);
        });
    std::vector<std::size_t> pending(roots.rbegin(), roots.rend()), order;
    order.reserve(control.rows.size());
    while (!pending.empty()) {
        const auto at = pending.back();
        pending.pop_back();
        order.push_back(at);
        pending.insert(pending.end(), children[at].rbegin(), children[at].rend());
    }
    if (order.size() != control.rows.size())
        throw std::invalid_argument("Hierarchical UI list has a parent cycle");
    std::vector<UiListRow> ordered;
    ordered.reserve(order.size());
    for (auto at : order)
        ordered.push_back(std::move(control.rows[at]));
    control.rows = std::move(ordered);
}

bool is_pointer_event(std::int32_t type) {
    return (type >= 65537 && type <= 65545) || type == 65549 || (type >= 69636 && type <= 69639);
}

std::int32_t bounded(std::int64_t v) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(v,
                                                              std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}

std::intptr_t pack(UiPoint p) {
    return static_cast<std::intptr_t>((static_cast<std::uint32_t>(p.x) & 65535u) << 16
                                      | (static_cast<std::uint32_t>(p.y) & 65535u));
}

UiPoint unpack(std::intptr_t p) {
    return { static_cast<std::int16_t>(static_cast<std::uint32_t>(p) >> 16), static_cast<std::int16_t>(p) };
}

bool inside(const UiControl& c, UiPoint p) {
    if (!c.custom_hit_region)
        return c.rect.contains(p);
    const auto& h = c.hit_region;
    if (h.shape == UiHitShape::rectangle)
        return h.rect.contains(p);
    if (h.shape == UiHitShape::polygon) {
        bool odd{};
        for (std::size_t i = 0, j = h.polygon.empty() ? 0 : h.polygon.size() - 1; i < h.polygon.size(); j = i++) {
            const auto a = h.polygon[i], b = h.polygon[j];
            if ((a.y > p.y) != (b.y > p.y)
                && static_cast<double>(p.x) < static_cast<double>(b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                odd = !odd;
        }
        return odd;
    }
    const auto x = static_cast<double>(p.x) - h.center.x, y = static_cast<double>(p.y) - h.center.y;
    if (h.shape == UiHitShape::circle)
        return x * x + y * y < static_cast<double>(h.radii.x) * h.radii.x;
    if (!h.radii.x || !h.radii.y)
        return false;
    const auto sx = x * h.radii.y / h.radii.x;
    return sx * sx + y * y < static_cast<double>(h.radii.y) * h.radii.y;
}

void button_style(UiControl& c) {
    //85AF60: disabled=1, base=0, hilite=2, hidown=3, helddown=4, down=5.
    const auto style = !c.enabled            ? 1
                     : c.hovered             ? ((c.visual_flags & 1u) ? 3 : 2)
                     : (c.visual_flags & 2u) ? 4
                     : (c.visual_flags & 1u) ? 5
                                             : 0;
    c.state = style;
    //85B4AF updates both attachments before refreshing the caption.
    c.primary_placement.part_value = c.secondary_placement.part_value = static_cast<std::uint16_t>(style);
    if (style == 1)
        c.color = c.button_text_colors[3];
    else if (style == 0)
        c.color = c.button_text_colors[0];
    else if (style == 2)
        c.color = c.button_text_colors[1];
    else
        c.color = c.button_text_colors[2];
    if (style == 3 || style == 5)
        c.text_offset = c.active_text_offset;
    else if (style == 4)
        c.text_offset = { c.active_text_offset.x + 1, c.active_text_offset.y };
    else
        c.text_offset = { 0, c.default_text_y_offset };
}

std::int32_t integer(std::u16string_view text) {
    std::int64_t n{};
    bool negative{};
    std::size_t i{};
    while (i < text.size() && (text[i] == u' ' || text[i] == u'\t'))
        ++i;
    if (i < text.size() && (text[i] == u'-' || text[i] == u'+'))
        negative = text[i++] == u'-';
    while (i < text.size() && text[i] >= u'0' && text[i] <= u'9') {
        n = std::min<std::int64_t>(n * 10 + text[i++] - u'0', 2147483648LL);
    }
    return bounded(negative ? -n : n);
}

std::u16string integer_text(std::int32_t n) {
    const auto ascii = std::to_string(n);
    return { ascii.begin(), ascii.end() };
}

void assign_rect(UiControl& c, UiRect rect) {
    c.rect = c.attachment_rect = c.viewport = rect;
    if (!c.custom_hit_region)
        c.hit_region.rect = rect;
    c.primary_placement.pivot =
        c.secondary_placement.pivot = { rect.left + rect.width() / 2, rect.top + rect.height() / 2 };
}

void scroll_geometry(UiControl& c) {
    const bool horizontal = (c.flags & 16u) == 0;
    const auto track = c.attachment_rect;
    //Explicit template children retain their authored sizes/insets. Native
    //85D8B8 moves only the thumb's scrolling axis; it does not turn Options'
    //64x64 +/- buttons or45x90 thumb into full-height180-unit rectangles.
    if (auto* a = c.runtime->find(&c, 1); a && (c.flags & 128u))
        assign_rect(*a,
                    horizontal ? UiRect{ track.right, track.top, c.rect.right, track.bottom }
                               : UiRect{ track.left, c.rect.top, track.right, track.top });
    if (auto* a = c.runtime->find(&c, 2); a && (c.flags & 128u))
        assign_rect(*a,
                    horizontal ? UiRect{ c.rect.left, track.top, track.left, track.bottom }
                               : UiRect{ track.left, track.bottom, track.right, c.rect.bottom });
    if (auto* thumb = c.runtime->find(&c, 3)) {
        const auto extent = std::max(0, horizontal ? track.width() : track.height());
        const auto length = std::clamp(horizontal ? thumb->rect.width() : thumb->rect.height(), 0, extent);
        const auto travel = extent - length;
        const auto offset =
            c.maximum == c.minimum
                ? 0
                : bounded(static_cast<std::int64_t>(c.value - c.minimum) * travel / (c.maximum - c.minimum));
        auto rectangle = thumb->rect;
        if (horizontal) {
            rectangle.left = track.right - length - offset;
            rectangle.right = track.right - offset;
        } else {
            rectangle.top = track.top + offset;
            rectangle.bottom = track.top + offset + length;
        }
        c.runtime->set_bounds(*thumb, rectangle);
    }
}

void require_list(const UiRuntime& runtime, const UiControl& control) {
    if (!runtime.owns(&control)
        || (control.kind != UiControlKind::list_control && control.kind != UiControlKind::list_box))
        throw std::invalid_argument("UI list operation requires a live list");
    if (control.rows.size() > 2147483647u)
        throw std::length_error("UI list exceeds its row index domain");
}

void sync_list_layout(UiRuntime& runtime, UiControl& control, std::int32_t requested_offset, bool notify) {
    require_list(runtime, control);
    const auto count = static_cast<std::int32_t>(control.rows.size());
    const auto previous_offset = control.scroll_offset;
    control.visible_rows = control.row_height > 0 ? std::max(control.viewport.height() / control.row_height, 0) : 0;
    control.selected_id = count ? std::clamp(control.selected_id, -1, count - 1) : -1;
    const auto maximum = std::max(0, count - control.visible_rows);
    control.scroll_offset = std::clamp(requested_offset, 0, maximum);
    if (auto* scroll = runtime.find(&control, 1)) {
        scroll->visible_rows = control.visible_rows;
        runtime.set_range(*scroll, 0, maximum);
        runtime.set_value(*scroll, control.scroll_offset);
        runtime.set_enabled(*scroll, maximum > 0);
    }
    if (notify && previous_offset != control.scroll_offset && control.kind == UiControlKind::list_control)
        runtime.post(
            control.parent,
            1029,
            static_cast<std::uint32_t>(control.scroll_offset),
            std::min(bounded(static_cast<std::int64_t>(control.scroll_offset) + control.visible_rows), count - 1));
    if (notify)
        runtime.post(&control, 3);
}
} //namespace

UiRuntime::UiRuntime(std::size_t capacity) : state_(std::make_unique<State>()) {
    if (!capacity)
        throw std::invalid_argument("Zero UI event queue capacity");
    state_->capacity = capacity;
}

UiRuntime::~UiRuntime() = default;

bool UiRuntime::owns(const UiControl* p) const {
    return state_->alive(p);
}

std::uint64_t UiRuntime::lifetime_serial(const UiControl* p) const {
    return owns(p) ? state_->serials.at(p) : 0;
}

std::uint64_t UiRuntime::tree_revision() const {
    return state_->tree_revision;
}

std::span<UiControl* const> UiRuntime::roots() const {
    return state_->roots;
}

std::size_t UiRuntime::size() const {
    return state_->serials.size();
}

std::uint64_t UiRuntime::dropped_events() const {
    return state_->dropped;
}

std::chrono::milliseconds UiRuntime::clock() const {
    return state_->now;
}

UiControl&
UiRuntime::create(UiControl* parent, UiControlKind kind, std::uint32_t flags, std::uint32_t id, UiRect rect) {
    if (parent && !owns(parent))
        throw std::invalid_argument("UI parent is not live");
    if (kind < UiControlKind::window || kind > UiControlKind::child_toggle_mask)
        throw std::invalid_argument("Unknown UI control kind");
    if (flags & 1024u)
        for (auto* a = parent; a; a = a->parent) {
            rect.left = bounded(static_cast<std::int64_t>(rect.left) + a->rect.left);
            rect.right = bounded(static_cast<std::int64_t>(rect.right) + a->rect.left);
            rect.top = bounded(static_cast<std::int64_t>(rect.top) + a->rect.top);
            rect.bottom = bounded(static_cast<std::int64_t>(rect.bottom) + a->rect.top);
        }
    auto storage = std::make_unique<UiControl>();
    auto& c = *storage;
    c.runtime = this;
    c.parent = parent;
    c.kind = kind;
    c.flags = flags;
    c.id = id;
    c.rect = c.attachment_rect = c.viewport = rect;
    c.hit_region.rect = rect;
    c.enabled = (flags & 2u) == 0;
    c.render_depth = parent ? std::min(parent->render_depth + 2, 256) : 1;
    c.primary_placement.depth = c.render_depth;
    c.secondary_placement.depth = c.render_depth + 1;
    c.primary_placement.pivot =
        c.secondary_placement.pivot = { rect.left + rect.width() / 2, rect.top + rect.height() / 2 };
    if (kind == UiControlKind::knob || kind == UiControlKind::numeric_stepper)
        c.maximum = 1;
    if (kind == UiControlKind::numeric_stepper)
        c.initial_value_pending = true;
    if (kind == UiControlKind::scroll) {
        c.maximum = 0;
        c.step_size = 1;
        if (!(flags & 64u)) {
            if (flags & 16u) {
                c.attachment_rect.top += rect.width();
                c.attachment_rect.bottom -= rect.width();
            } else {
                c.attachment_rect.left += rect.height();
                c.attachment_rect.right -= rect.height();
            }
        }
    }
    if (kind == UiControlKind::edit_box) {
        c.text_limit = 255;
        c.numeric_only = (flags & 256u) != 0;
    }
    state_->serials[&c] = ++state_->next_serial;
    state_->controls.push_back(std::move(storage));
    (parent ? parent->children : state_->roots).push_back(&c);
    ++state_->tree_revision;
    if (kind == UiControlKind::button)
        button_style(c);
    //Base constructor queues21 before a derived constructor appends children.
    post(&c, 21);
    if (kind == UiControlKind::edit_box) {
        c.edit_prepare_caret_at_end = state_->locale_is_1037;
        set_focus(&c); //8583FC; notification is deferred until construction ends.
    }
    if ((kind == UiControlKind::list_box || kind == UiControlKind::list_control) && (flags & 4096u)) {
        auto& scroll = create(&c, UiControlKind::scroll, (flags & 64u) ? 80u : 16u, 1, rect);
        set_visible(scroll, true);
        set_enabled(scroll, false);
    }
    if (kind == UiControlKind::scroll && (flags & 128u)) {
        if (!(flags & 64u)) {
            create(&c, UiControlKind::button, 1, 1, rect);
            create(&c, UiControlKind::button, 1, 2, rect);
        }
        auto thumb_rect = c.attachment_rect;
        if (flags & 16u)
            thumb_rect.bottom = thumb_rect.top + 64;
        else
            thumb_rect.right = thumb_rect.left + 64;
        auto& thumb = create(&c, UiControlKind::button, 1, 3, thumb_rect);
        configure_scroll_thumb(thumb);
        scroll_geometry(c);
    }
    return c;
}

void UiRuntime::configure_scroll_thumb(UiControl& thumb) {
    thumb.primary_callback = [this](UiControl& t, const UiEvent& e) -> std::intptr_t {
        auto& s = *t.parent;
        const bool horizontal = (s.flags & 16u) == 0;
        if (e.type == 65541 && e.id == 0) {
            set_capture(&t);
            t.pressed = true;
            const auto p = unpack(e.value);
            t.pointer_y = (horizontal ? p.x : p.y) - (horizontal ? t.rect.left : t.rect.top);
        } else if (e.type == 65540 && e.id == 0) {
            t.pressed = false;
            set_capture(nullptr);
            post(&s, 2050, t.id, 0);
        } else if (e.type == 65539 && t.pressed) {
            const auto p = unpack(static_cast<std::intptr_t>(e.id));
            const auto origin = horizontal ? s.attachment_rect.left : s.attachment_rect.top;
            const auto length =
                horizontal ? s.attachment_rect.width() - t.rect.width() : s.attachment_rect.height() - t.rect.height();
            if (length > 0)
                set_value(s,
                          bounded(static_cast<std::int64_t>(horizontal ? s.maximum : s.minimum)
                                  + static_cast<std::int64_t>((horizontal ? p.x : p.y) - origin - t.pointer_y)
                                        * (horizontal ? s.minimum - s.maximum : s.maximum - s.minimum)
                                        / length),
                          true);
            post(&s, 2049, t.id, s.value);
        }
        return default_event(t, e);
    };
}

UiControl* UiRuntime::find(UiControl* parent, std::uint32_t id) const {
    if (!owns(parent))
        return nullptr;
    for (auto* child : parent->children)
        if (owns(child) && child->id == id)
            return child;
    return nullptr;
}

UiControl* UiRuntime::find_recursive(UiControl* parent, std::uint32_t id) const {
    if (!owns(parent))
        return nullptr;
    if (parent->id == id)
        return parent;
    for (auto* child : parent->children)
        if (auto* found = find_recursive(child, id))
            return found;
    return nullptr;
}

void UiRuntime::collect_destroyed() {
    if (state_->depth || state_->draining)
        return;
    std::erase_if(state_->controls, [&](const auto& p) {
        if (!p->destroying)
            return false;
        state_->serials.erase(p.get());
        return true;
    });
}

void UiRuntime::destroy(UiControl* c) {
    if (!owns(c) || state_->destruction_notifications.contains(c))
        return;
    ++state_->depth;
    state_->destruction_notifications.insert(c);
    std::exception_ptr failure;
    try {
        dispatch(c, 20);
    } catch (...) {
        failure = std::current_exception();
    }
    state_->destruction_notifications.erase(c);
    c->destroying = true;
    ++state_->tree_revision;
    const auto children = c->children;
    for (auto* child : children)
        try {
            destroy(child);
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    c->children.clear();
    auto& siblings = c->parent ? c->parent->children : state_->roots;
    std::erase(siblings, c);
    for (auto** p : { &state_->focused,
                      &state_->captured,
                      &state_->hovered,
                      &state_->interaction,
                      &state_->input_root,
                      &state_->route })
        if (*p == c)
            *p = nullptr;
    for (auto& p : state_->down)
        if (p == c)
            p = nullptr;
    for (auto& t : state_->tracked)
        if (t.target == c)
            t = {};
    std::erase_if(state_->primary, [c](const auto& e) {
        return e.target == c;
    });
    std::erase_if(state_->secondary, [c](const auto& e) {
        return e.target == c;
    });
    --state_->depth;
    collect_destroyed();
    if (failure)
        std::rethrow_exception(failure);
}

void UiRuntime::clear() {
    const auto roots = state_->roots;
    std::exception_ptr failure;
    for (auto* r : roots)
        try {
            destroy(r);
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    clear_events();
    if (failure)
        std::rethrow_exception(failure);
}

void UiRuntime::reparent(UiControl& control, UiControl* parent) {
    if (!owns(&control) || (parent && !owns(parent)))
        throw std::invalid_argument("Reparenting a non-live UI control");
    if (control.parent == parent)
        return;
    for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == &control)
            throw std::invalid_argument("UI parent cycle");
    auto& previous = control.parent ? control.parent->children : state_->roots;
    std::erase(previous, &control);
    control.parent = parent;
    (parent ? parent->children : state_->roots).push_back(&control);
    ++state_->tree_revision;
}

void UiRuntime::move_to_tail(UiControl& c) {
    auto& siblings = c.parent ? c.parent->children : state_->roots;
    if (siblings.empty() || siblings.back() == &c)
        return;
    auto* former = siblings.back();
    std::erase(siblings, &c);
    siblings.push_back(&c);
    ++state_->tree_revision;
    post(&c, 24);
    post(former, 25);
}

void UiRuntime::set_visible(UiControl& c, bool shown) {
    if (!owns(&c))
        return;
    const bool changed = c.visible() != shown;
    c.flags = shown ? c.flags | 1u : c.flags & ~1u;
    if (changed) {
        //84BAF3 -> 84BA21: local flags and effective-visibility notification
        //are separate. Showing beneath a hidden ancestor sends no event. A
        //visible transition prunes locally hidden subtrees; hiding visits all
        //descendants without clearing their local flags. Notify primary
        //callbacks directly, never widget defaults or queued-event observers.
        bool effective = true;
        for (auto* ancestor = &c; ancestor; ancestor = ancestor->parent)
            if (!owns(ancestor) || !ancestor->visible()) {
                effective = false;
                break;
            }
        const auto notify = [&](auto&& self, UiControl* control) -> void {
            if (!owns(control) || (shown && !control->visible()))
                return;
            if (auto callback = control->primary_callback)
                callback(*control, UiEvent{ 17, shown ? 1u : 0u, 0 });
            if (!owns(control))
                return;
            const auto children = control->children;
            for (auto* child : children)
                if (owns(child) && child->parent == control)
                    self(self, child);
        };
        //Visibility callbacks may destroy/reparent controls or open another
        //panel. Retain storage for this complete traversal, as in dispatch().
        ++state_->depth;
        try {
            if (!shown || effective)
                notify(notify, &c);
            pointer_move(state_->pointer);
            --state_->depth;
        } catch (...) {
            --state_->depth;
            collect_destroyed();
            throw;
        }
        collect_destroyed();
    }
}

void UiRuntime::set_enabled(UiControl& c, bool enabled) {
    if (!owns(&c))
        return;
    const bool changed = c.enabled != enabled || ((c.flags & 2u) == 0) != enabled;
    c.enabled = enabled;
    c.flags = enabled ? c.flags & ~2u : c.flags | 2u;
    if (c.kind == UiControlKind::button)
        button_style(c);
    if (changed && c.kind == UiControlKind::scroll) {
        const auto children = c.children;
        for (auto* child : children)
            if (owns(child))
                set_enabled(*child, enabled);
    }
    if (changed) {
        dispatch(&c, 23, enabled ? 1u : 0u);
        pointer_move(state_->pointer);
    }
}

void UiRuntime::set_button_auxiliary_state(UiControl& c, bool value, bool notify) {
    if (!owns(&c))
        return;
    if (c.kind != UiControlKind::button)
        throw std::invalid_argument("Auxiliary button state requires a button");
    if (c.hovered == value)
        return;
    c.hovered = value;
    if (notify)
        post(&c, 1);
    button_style(c);
}

void UiRuntime::set_bounds(UiControl& c, UiRect rect) {
    if (!owns(&c))
        return;
    const auto old = c.rect;
    if (old.left == rect.left && old.top == rect.top && old.right == rect.right && old.bottom == rect.bottom)
        return;
    if (old.width() == rect.width() && old.height() == rect.height()) {
        translate(c,
                  bounded(static_cast<std::int64_t>(rect.left) - old.left),
                  bounded(static_cast<std::int64_t>(rect.top) - old.top));
        pointer_move(state_->pointer);
        return;
    }
    const UiRect d{ bounded(static_cast<std::int64_t>(rect.left) - old.left),
                    bounded(static_cast<std::int64_t>(rect.top) - old.top),
                    bounded(static_cast<std::int64_t>(rect.right) - old.right),
                    bounded(static_cast<std::int64_t>(rect.bottom) - old.bottom) };
    c.rect = rect;
    const auto adjust = [&](UiRect& r, const UiRect& delta) {
        r.left = bounded(static_cast<std::int64_t>(r.left) + delta.left);
        r.top = bounded(static_cast<std::int64_t>(r.top) + delta.top);
        r.right = bounded(static_cast<std::int64_t>(r.right) + delta.right);
        r.bottom = bounded(static_cast<std::int64_t>(r.bottom) + delta.bottom);
    };
    adjust(c.attachment_rect, d);
    adjust(c.viewport, d);
    if (!c.custom_hit_region)
        c.hit_region.rect = rect;
    //84AAD3: paired edge flags resize a correction-free candidate. Remaining
    //single-edge flags translate the live subtree, preserving its dimensions.
    constexpr std::uint32_t left = 65536, right = 131072, top = 262144, bottom = 524288;
    const auto children = c.children;
    for (auto* child : children)
        if (owns(child)) {
            auto candidate = child->rect;
            const auto a = child->animation_correction;
            const UiRect inverse{ bounded(-static_cast<std::int64_t>(a.left)),
                                  bounded(-static_cast<std::int64_t>(a.top)),
                                  bounded(-static_cast<std::int64_t>(a.right)),
                                  bounded(-static_cast<std::int64_t>(a.bottom)) };
            adjust(candidate, inverse);
            auto flags = child->flags;
            if ((flags & (left | right | top | bottom)) == (left | right | top | bottom)) {
                adjust(candidate, d);
                set_bounds(*child, candidate);
            } else {
                if ((flags & (left | right)) == (left | right)) {
                    if (d.left || d.right) {
                        adjust(candidate, { d.left, 0, d.right, 0 });
                        set_bounds(*child, candidate);
                    }
                    flags &= ~(left | right);
                } else if ((flags & (top | bottom)) == (top | bottom)) {
                    if (d.top || d.bottom) {
                        adjust(candidate, { 0, d.top, 0, d.bottom });
                        set_bounds(*child, candidate);
                    }
                    flags &= ~(top | bottom);
                }
                if (owns(child) && (flags & left) && d.left)
                    translate(*child, d.left, 0);
                if (owns(child) && (flags & right) && d.right)
                    translate(*child, d.right, 0);
                if (owns(child) && (flags & top) && d.top)
                    translate(*child, 0, d.top);
                if (owns(child) && (flags & bottom) && d.bottom)
                    translate(*child, 0, d.bottom);
            }
        }
    dispatch(&c, 18, static_cast<std::uint32_t>(rect.width()), rect.height());
    pointer_move(state_->pointer);
}

void UiRuntime::translate(UiControl& c, std::int32_t x, std::int32_t y) {
    const auto move = [&](UiRect& r) {
        r.left = bounded(static_cast<std::int64_t>(r.left) + x);
        r.right = bounded(static_cast<std::int64_t>(r.right) + x);
        r.top = bounded(static_cast<std::int64_t>(r.top) + y);
        r.bottom = bounded(static_cast<std::int64_t>(r.bottom) + y);
    };
    move(c.rect);
    move(c.attachment_rect);
    move(c.viewport);
    move(c.hit_region.rect);
    c.hit_region.center.x = bounded(static_cast<std::int64_t>(c.hit_region.center.x) + x);
    c.hit_region.center.y = bounded(static_cast<std::int64_t>(c.hit_region.center.y) + y);
    for (auto& p : c.hit_region.polygon) {
        p.x = bounded(static_cast<std::int64_t>(p.x) + x);
        p.y = bounded(static_cast<std::int64_t>(p.y) + y);
    }
    if (c.kind == UiControlKind::list_control)
        for (auto& column : c.columns) {
            column.left = bounded(static_cast<std::int64_t>(column.left) + x);
            column.right = bounded(static_cast<std::int64_t>(column.right) + x);
        }
    for (auto* placement : { &c.primary_placement, &c.secondary_placement }) {
        placement->pivot.x = bounded(static_cast<std::int64_t>(placement->pivot.x) + x);
        placement->pivot.y = bounded(static_cast<std::int64_t>(placement->pivot.y) + y);
    }
    const auto children = c.children;
    for (auto* child : children)
        if (owns(child))
            translate(*child, x, y);
    dispatch(&c, 19, static_cast<std::uint32_t>(x), y);
}

void UiRuntime::set_part_base(UiControl& c, std::uint16_t base, bool second) {
    (second ? c.secondary_placement : c.primary_placement).part_base = base;
}

void UiRuntime::set_attachment_part(UiControl& c, std::uint16_t part) {
    c.primary_placement.part_value = c.secondary_placement.part_value = part;
}

void UiRuntime::set_depth(UiControl& c, std::int32_t depth) {
    const auto native_depth = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(depth));
    c.render_depth = std::min<std::int32_t>(native_depth, 256);
    c.primary_placement.depth = c.render_depth;
    c.secondary_placement.depth = c.render_depth + 1;
    //84D284 passes the original signed-WORD input+2, before the receiver's
    //upper clamp. Every child narrows that next input to signed WORD again.
    const auto children = c.children;
    for (auto* child : children)
        if (owns(child))
            set_depth(*child, static_cast<std::int32_t>(native_depth) + 2);
}

void UiRuntime::set_rotation(UiControl& c, std::int32_t angle, bool recursive) {
    c.primary_placement.rotation = c.secondary_placement.rotation = angle;
    if (recursive)
        for (auto* child : c.children)
            if (owns(child))
                set_rotation(*child, angle, true);
}

std::int32_t UiRuntime::visible_render_depth(const UiControl* exclude) {
    std::int32_t maximum = -1;
    const auto scan = [&](auto&& self, UiControl* control) -> void {
        if (control == exclude || !owns(control) || !control->visible())
            return;
        const auto children = control->children;
        maximum = std::max(maximum, static_cast<std::int32_t>(dispatch(control, 13)));
        for (auto* child : children)
            self(self, child);
    };
    const auto snapshot = state_->roots;
    for (auto* root : snapshot)
        scan(scan, root);
    return maximum;
}

void UiRuntime::place_modal_layer(UiControl& wrapper) {
    if (!owns(&wrapper))
        return;
    //Native4F4470 queries before parsing the new wrapper's descendants.
    //Excluding that subtree here preserves the same existing-tree query.
    const auto depth = std::max(visible_render_depth(&wrapper), wrapper.render_depth + 1) + 1;
    set_depth(wrapper, depth);
    const auto maximum = visible_render_depth();
    if (maximum > 39)
        set_depth(wrapper, wrapper.render_depth + 39 - maximum);
}

void UiRuntime::stop_animation(UiControl& c) {
    if (!c.animation_playing)
        return;
    const auto d = c.animation_correction;
    auto rect = c.rect;
    rect.left = bounded(static_cast<std::int64_t>(rect.left) - d.left);
    rect.top = bounded(static_cast<std::int64_t>(rect.top) - d.top);
    rect.right = bounded(static_cast<std::int64_t>(rect.right) - d.right);
    rect.bottom = bounded(static_cast<std::int64_t>(rect.bottom) - d.bottom);
    c.animation_playing = false;
    c.animation_index = 0;
    c.animation_correction = {};
    c.animation.clear();
    set_bounds(c, rect);
}

void
UiRuntime::start_animation(UiControl& c, std::span<const UiAnimationStep> steps, std::chrono::milliseconds interval) {
    if (steps.empty() || interval.count() < 0)
        throw std::invalid_argument("Invalid UI animation program");
    for (const auto& step : steps)
        if (step.words[0] == -9997 && (step.words[1] < 0 || static_cast<std::size_t>(step.words[1]) >= steps.size()))
            throw std::invalid_argument("UI animation jump outside owned program");
    std::vector<UiAnimationStep> program{ steps.begin(), steps.end() };
    stop_animation(c);
    if (!owns(&c))
        return;
    c.animation = std::move(program);
    c.animation_index = 0;
    c.animation_interval = interval;
    c.animation_tick = state_->now;
    c.animation_playing = true;
}

void UiRuntime::step_animation(UiControl& c) {
    if (!c.animation_playing || state_->now - c.animation_tick <= c.animation_interval)
        return;
    if (c.animation_index >= c.animation.size()) {
        stop_animation(c);
        throw std::out_of_range("UI animation index outside owned program");
    }
    c.animation_tick = state_->now;
    const auto step = c.animation[c.animation_index].words;
    if (step[0] == -9999) {
        stop_animation(c);
        return;
    }
    if (step[0] == -9998) {
        ++c.animation_index;
        set_rotation(c, step[1]);
        return;
    }
    if (step[0] == -9997) {
        c.animation_index = static_cast<std::size_t>(step[1]);
        return;
    }
    if (step[0] == -9996) {
        ++c.animation_index;
        set_attachment_part(c, static_cast<std::uint16_t>(step[1]));
        return;
    }
    auto rect = c.rect;
    auto& a = c.animation_correction;
    rect.left = bounded(static_cast<std::int64_t>(rect.left) + step[0]);
    rect.top = bounded(static_cast<std::int64_t>(rect.top) + step[1]);
    rect.right = bounded(static_cast<std::int64_t>(rect.right) + step[2]);
    rect.bottom = bounded(static_cast<std::int64_t>(rect.bottom) + step[3]);
    a.left = bounded(static_cast<std::int64_t>(a.left) + step[0]);
    a.top = bounded(static_cast<std::int64_t>(a.top) + step[1]);
    a.right = bounded(static_cast<std::int64_t>(a.right) + step[2]);
    a.bottom = bounded(static_cast<std::int64_t>(a.bottom) + step[3]);
    ++c.animation_index;
    set_bounds(c, rect);
}

void UiRuntime::start_transition(UiControl& c,
                                 UiRect target,
                                 std::chrono::milliseconds duration,
                                 std::chrono::milliseconds interval) {
    if (interval.count() <= 0 || duration < interval)
        throw std::invalid_argument("Invalid UI bounds transition period");
    const auto count = duration / interval;
    UiBoundsTransition transition;
    transition.target = target;
    transition.remaining = count;
    transition.interval = interval;
    transition.next = state_->now + interval;
    transition.delta = { bounded((static_cast<std::int64_t>(target.left) - c.rect.left) / count),
                         bounded((static_cast<std::int64_t>(target.top) - c.rect.top) / count),
                         bounded((static_cast<std::int64_t>(target.right) - c.rect.right) / count),
                         bounded((static_cast<std::int64_t>(target.bottom) - c.rect.bottom) / count) };
    c.transition = transition;
}

void UiRuntime::pause_transition(UiControl& c, bool paused) {
    if (c.transition)
        c.transition->paused = paused;
}

void UiRuntime::stop_transition(UiControl& c, bool snap) {
    if (!c.transition)
        return;
    const auto target = c.transition->target;
    c.transition.reset();
    if (snap)
        set_bounds(c, target);
}

void UiRuntime::step_transition(UiControl& c) {
    if (!c.transition || c.transition->paused)
        return;
    if (!c.transition->remaining) {
        const auto target = c.transition->target;
        c.transition.reset();
        set_bounds(c, target);
        post(&c, 29);
        return;
    }
    const auto d = c.transition->delta;
    --c.transition->remaining;
    auto rect = c.rect;
    rect.left = bounded(static_cast<std::int64_t>(rect.left) + d.left);
    rect.top = bounded(static_cast<std::int64_t>(rect.top) + d.top);
    rect.right = bounded(static_cast<std::int64_t>(rect.right) + d.right);
    rect.bottom = bounded(static_cast<std::int64_t>(rect.bottom) + d.bottom);
    set_bounds(c, rect);
}

void UiRuntime::set_range(UiControl& c, std::int32_t lo, std::int32_t hi) {
    c.minimum = lo;
    c.maximum = hi;
    if (c.kind == UiControlKind::scroll) {
        //Native85DCAD stores start/end and makes the unit step point toward start.
        if (lo < hi && c.step_size > 0)
            c.step_size = -c.step_size;
        else if (lo >= hi && c.step_size < 0)
            c.step_size = bounded(-static_cast<std::int64_t>(c.step_size));
    }
    set_value(c, c.value);
}

void UiRuntime::set_raw_range(UiControl& c, std::int32_t lo, std::int32_t hi) {
    c.raw_minimum = lo;
    c.raw_maximum = hi;
    c.raw_position = std::clamp(c.raw_position, std::min(lo, hi), std::max(lo, hi));
}

void UiRuntime::set_value(UiControl& c, std::int32_t value, bool notify) {
    const auto old = c.value;
    if (c.kind == UiControlKind::bar)
        value = std::clamp(value, 0, 1024);
    else if (c.kind == UiControlKind::scroll
             || c.kind == UiControlKind::knob
             || c.kind == UiControlKind::numeric_stepper)
        value = std::clamp(value, std::min(c.minimum, c.maximum), std::max(c.minimum, c.maximum));
    c.value = value;
    if (c.kind == UiControlKind::image) {
        c.primary_placement.part_value = static_cast<std::uint16_t>(value);
        c.secondary_placement.part_value = static_cast<std::uint16_t>(value);
    }
    if (c.kind == UiControlKind::button) {
        c.visual_flags = static_cast<std::uint8_t>(value);
        button_style(c);
    }
    if (c.kind == UiControlKind::child_toggle_mask)
        for (unsigned i = 0; i < 31; ++i)
            if (auto* child = find(&c, 0x11100000u | i)) {
                const auto part = (static_cast<std::uint32_t>(value) & (1u << i)) ? 1u : 0u;
                child->primary_placement.part_value = static_cast<std::uint16_t>(part);
                child->secondary_placement.part_value = static_cast<std::uint16_t>(part);
            }
    if (c.kind == UiControlKind::knob && c.minimum != c.maximum) {
        c.mapped_raw_position = bounded(
            static_cast<std::int64_t>(c.raw_minimum)
            + static_cast<std::int64_t>(value - c.minimum) * (c.raw_maximum - c.raw_minimum) / (c.maximum - c.minimum));
        c.raw_position = c.mapped_raw_position;
        c.secondary_placement.rotation = c.mapped_raw_position;
    }
    if (c.kind == UiControlKind::scroll)
        scroll_geometry(c);
    if (c.kind == UiControlKind::numeric_stepper && owns(c.associated_edit))
        set_text(*c.associated_edit, integer_text(value));
    if (old != value || c.initial_value_pending) {
        c.initial_value_pending = false;
        auto* parent = c.parent;
        const auto element_id = c.id;
        const bool tell_parent = notify
                              || c.kind == UiControlKind::scroll
                              || c.kind == UiControlKind::knob
                              || c.kind == UiControlKind::numeric_stepper;
        if (c.kind != UiControlKind::bar && c.kind != UiControlKind::scroll && c.kind != UiControlKind::knob)
            dispatch(&c, 128, static_cast<std::uint32_t>(value));
        if (tell_parent && owns(&c))
            post(parent, 2048, element_id, value);
    }
}

void UiRuntime::set_selected(UiControl& c, std::int32_t id, bool notify) {
    if (c.kind == UiControlKind::list_control || c.kind == UiControlKind::list_box) {
        select_list_row(c, id, notify);
        return;
    }
    const auto old = c.selected_id;
    //Native85AD05 leaves an unchanged selection completely alone. In
    //particular, a panel refresh must not clear another tab's held-down bit.
    if (old == id)
        return;
    if (c.kind == UiControlKind::checkbuttons && id != -1 && !find(&c, static_cast<std::uint32_t>(id)))
        return;
    if (c.kind == UiControlKind::checkbuttons)
        if (auto* previous = find(&c, static_cast<std::uint32_t>(old))) {
            previous->visual_flags = 0;
            button_style(*previous);
        }
    c.selected_id = id;
    if (c.kind == UiControlKind::checkbuttons)
        if (auto* selected = find(&c, static_cast<std::uint32_t>(id))) {
            selected->visual_flags = 1;
            button_style(*selected);
        }
    if (notify && old != id) {
        if (old != -1)
            post(c.parent, 257, static_cast<std::uint32_t>(old), 0);
        if (id != -1)
            post(c.parent, 257, static_cast<std::uint32_t>(id), 1);
    }
}

void UiRuntime::set_text(UiControl& c, std::u16string_view text, bool notify) {
    const std::u16string copy{ text.substr(0, c.text_limit) };
    if (copy == c.text)
        return;
    c.text = copy;
    //85859D refreshes the buffer/selection without resetting the attachment's
    //scroll offsets. The active caret then adjusts the retained viewport.
    c.selection_begin = c.selection_end = c.text.size();
    if (c.numeric_only) {
        const auto old = c.value;
        c.value = integer(c.text);
        if (notify && c.value != old)
            post(c.parent, 2055, c.id, c.value);
    }
    if (notify)
        post(c.parent, 2049, c.id, 0);
}

void UiRuntime::refresh_list(UiControl& c, bool notify) {
    sync_list_layout(*this, c, c.scroll_offset, notify);
}

void UiRuntime::set_list_scroll_offset(UiControl& c, std::int32_t offset, bool notify) {
    //857C8A/856A90: scrolling does not move or reveal the selected item.
    sync_list_layout(*this, c, offset, notify);
}

void UiRuntime::select_list_row(UiControl& c, std::int32_t index, bool notify, bool ensure_visible) {
    require_list(*this, c);
    const auto previous = c.selected_id;
    const auto count = static_cast<std::int32_t>(c.rows.size());
    c.selected_id = count ? std::clamp(index, -1, count - 1) : -1;
    auto offset = c.scroll_offset;
    const auto capacity = c.row_height > 0 ? std::max(c.viewport.height() / c.row_height, 0) : 0;
    if (ensure_visible && c.selected_id >= 0 && capacity > 0) {
        if (c.selected_id < offset)
            offset = c.selected_id;
        else if (c.selected_id >= static_cast<std::int64_t>(offset) + capacity)
            offset = c.selected_id - capacity + 1;
    }
    sync_list_layout(*this, c, offset, notify);
    if (notify && previous != c.selected_id)
        post(c.parent, c.kind == UiControlKind::list_box ? 513 : 1025, c.id, c.selected_id);
}

std::optional<std::intptr_t> UiRuntime::selected_list_identity(const UiControl& c) const {
    require_list(*this, c);
    if (c.selected_id < 0 || static_cast<std::size_t>(c.selected_id) >= c.rows.size())
        return std::nullopt;
    return c.rows[static_cast<std::size_t>(c.selected_id)].identity;
}

std::optional<std::size_t> UiRuntime::find_list_row(const UiControl& c, std::intptr_t identity) const {
    require_list(*this, c);
    const auto found = std::ranges::find(c.rows, identity, &UiListRow::identity);
    return found == c.rows.end() ? std::nullopt : std::optional{ static_cast<std::size_t>(found - c.rows.begin()) };
}

std::optional<std::size_t> UiRuntime::parent_list_row(const UiControl& c, std::size_t visible_index) const {
    require_list(*this, c);
    if (visible_index >= c.rows.size())
        return std::nullopt;
    const auto parent = c.rows[visible_index].parent_identity;
    return parent ? find_list_row(c, *parent) : std::nullopt;
}

std::int32_t UiRuntime::list_sort_key(const UiControl& c) const {
    require_list(*this, c);
    if (c.sort_column < 0)
        return 0;
    const auto one_based = bounded(static_cast<std::int64_t>(c.sort_column) + 1);
    return c.sort_direction ? -one_based : one_based;
}

void UiRuntime::sort_list_rows(UiControl& c, bool notify) {
    require_list(*this, c);
    const auto identity = selected_list_identity(c);
    const auto previous_selection = c.selected_id;
    if (c.sort_column >= 0) {
        const auto column = static_cast<std::size_t>(c.sort_column);
        if (column >= c.columns.size())
            throw std::out_of_range("UI list sort column");
        const bool numeric =
            column < c.column_presentations.size()
                ? c.column_presentations[column].format != UiListCellFormat::text
                : !c.rows.empty() && column < c.rows.front().cells.size() && c.rows.front().cells[column].numeric();
        for (const auto& row : c.rows)
            if (column >= row.cells.size() || row.cells[column].numeric() != numeric)
                throw std::invalid_argument("UI list sort cell/column type mismatch");
        order_list_rows(c, [&](const auto& a, const auto& b) {
            const auto &first = a.cells[column], &second = b.cells[column];
            if (numeric)
                return c.sort_direction ? first.number() > second.number() : first.number() < second.number();
            return c.sort_direction ? first.text() > second.text() : first.text() < second.text();
        });
    } else
        order_list_rows(c, [](const auto&, const auto&) {
            return false;
        });
    if (identity)
        c.selected_id = static_cast<std::int32_t>(*find_list_row(c, *identity));
    refresh_list(c, notify);
    if (notify && previous_selection != c.selected_id)
        post(c.parent, 1025, c.id, c.selected_id);
}

void UiRuntime::set_list_sort_key(UiControl& c, std::int32_t key, bool notify) {
    require_list(*this, c);
    const auto previous_key = list_sort_key(c);
    const auto previous_offset = c.scroll_offset;
    const auto magnitude = key < 0 ? -static_cast<std::int64_t>(key) : static_cast<std::int64_t>(key);
    if (magnitude > static_cast<std::int64_t>(c.columns.size()))
        throw std::out_of_range("UI list signed sort key");
    c.sort_column = static_cast<std::int32_t>(magnitude) - 1;
    c.sort_direction = key < 0 ? 1 : 0;
    sort_list_rows(c, false);
    c.scroll_offset = previous_offset;
    refresh_list(c, notify);
    if (notify && c.selected_id >= 0)
        post(c.parent, 1025, c.id, c.selected_id);
    //8576A1 uses a signed one-based COLUMN key, not selected-row state.
    if (notify && previous_key != key)
        post(c.parent, 1030, c.id, key);
}

void UiRuntime::set_list_rows(UiControl& c, std::vector<UiListRow> rows, bool preserve_selection, bool notify) {
    require_list(*this, c);
    if (rows.size() > 2147483647u)
        throw std::length_error("UI list exceeds its row index domain");
    const auto previous_identity = selected_list_identity(c);
    const auto previous_selection = c.selected_id;
    const auto previous_offset = c.scroll_offset;
    c.rows = std::move(rows);
    c.selected_id = -1;
    if (preserve_selection && previous_identity)
        if (const auto found = find_list_row(c, *previous_identity))
            c.selected_id = static_cast<std::int32_t>(*found);
    sort_list_rows(c, false);
    c.scroll_offset = previous_offset;
    refresh_list(c, notify);
    if (notify && (previous_selection != c.selected_id || previous_identity != selected_list_identity(c)))
        post(c.parent, c.kind == UiControlKind::list_box ? 513 : 1025, c.id, c.selected_id);
}

void UiRuntime::insert_list_row(UiControl& c, UiListRow row, std::optional<std::size_t> before, bool notify) {
    require_list(*this, c);
    const auto at = before.value_or(c.rows.size());
    if (at > c.rows.size())
        throw std::out_of_range("UI list insertion index");
    if (c.rows.size() == 2147483647u)
        throw std::length_error("UI list exceeds its row index domain");
    const auto previous_selection = c.selected_id;
    c.rows.insert(c.rows.begin() + static_cast<std::ptrdiff_t>(at), std::move(row));
    if (c.selected_id >= 0 && at <= static_cast<std::size_t>(c.selected_id))
        ++c.selected_id;
    sort_list_rows(c, false);
    refresh_list(c, notify);
    if (notify && previous_selection != c.selected_id)
        post(c.parent, c.kind == UiControlKind::list_box ? 513 : 1025, c.id, c.selected_id);
}

bool UiRuntime::update_list_row(UiControl& c,
                                std::intptr_t identity,
                                std::span<const UiListCell> cells,
                                std::uint64_t column_mask,
                                bool notify) {
    const auto found = find_list_row(c, identity);
    if (!found)
        return false;
    auto& row = c.rows[*found];
    for (std::size_t column = 0; column < 64; ++column)
        if ((column_mask >> column) & 1u)
            if (column >= cells.size() || column >= row.cells.size())
                throw std::out_of_range("UI list masked update column");
    for (std::size_t column = 0; column < 64; ++column)
        if ((column_mask >> column) & 1u)
            row.cells[column] = cells[column];
    //85602C copies only the requested columns; live meter refreshes must not
    //replace item identity, text columns, selection or current sort order.
    if (notify)
        post(&c, 3);
    return true;
}

bool UiRuntime::remove_list_row(UiControl& c, std::intptr_t identity, bool notify) {
    const auto found = find_list_row(c, identity);
    if (!found)
        return false;
    const auto previous_identity = selected_list_identity(c);
    const auto previous_selection = c.selected_id;
    //85630D removes the receiver and its descendants, without consuming its
    //following root/sibling. The x64 owner keeps selection by stable identity.
    auto end = *found + 1;
    std::unordered_set<std::intptr_t> removed{ identity };
    for (; end < c.rows.size(); ++end) {
        const auto parent = c.rows[end].parent_identity;
        if (!parent || !removed.contains(*parent))
            break;
        removed.insert(c.rows[end].identity);
    }
    c.rows.erase(c.rows.begin() + static_cast<std::ptrdiff_t>(*found),
                 c.rows.begin() + static_cast<std::ptrdiff_t>(end));
    if (previous_identity) {
        const auto selected = find_list_row(c, *previous_identity);
        c.selected_id = selected       ? static_cast<std::int32_t>(*selected)
                      : c.rows.empty() ? -1
                                       : static_cast<std::int32_t>(std::min(*found, c.rows.size() - 1));
    }
    refresh_list(c, notify);
    if (notify && (previous_selection != c.selected_id || previous_identity != selected_list_identity(c)))
        post(c.parent, c.kind == UiControlKind::list_box ? 513 : 1025, c.id, c.selected_id);
    return true;
}

void UiRuntime::clear_list_rows(UiControl& c, bool notify) {
    set_list_rows(c, {}, false, notify);
}

void UiRuntime::set_primary_callback(UiControl& c, UiCallback callback) {
    if (callback)
        c.primary_callback = std::move(callback);
}

void UiRuntime::set_secondary_callback(UiControl& c, UiCallback callback) {
    c.secondary_callback = std::move(callback);
}

std::intptr_t UiRuntime::secondary(UiControl& c, const UiEvent& e) {
    if (!owns(&c))
        return 0;
    auto callback = c.secondary_callback;
    return callback ? callback(c, e) : widget_event(c, e);
}

std::intptr_t UiRuntime::dispatch(UiControl* c, const UiEvent& e) {
    return deliver(c, e, true);
}

std::intptr_t UiRuntime::deliver(UiControl* c, const UiEvent& e, bool observe) {
    if (!owns(c) || (!state_->active && is_pointer_event(e.type)))
        return 0;
    const auto pointer_epoch = state_->pointer_cancel_epoch;
    const auto serial = lifetime_serial(c);
    ++state_->depth;
    try {
        if (auto observer = observe ? state_->observer : nullptr)
            observer(c, e);
        std::intptr_t result{};
        if (lifetime_serial(c) == serial
            && (!is_pointer_event(e.type) || (state_->active && pointer_epoch == state_->pointer_cancel_epoch))) {
            auto callback = c->primary_callback;
            result = callback ? callback(*c, e) : widget_event(*c, e);
        }
        //A callback can cancel during focus/capture changes, then forward the
        //current input to a widget. Do not let that forwarding rearm the gesture.
        if (is_pointer_event(e.type) && pointer_epoch != state_->pointer_cancel_epoch)
            cancel_pointer_input();
        --state_->depth;
        collect_destroyed();
        return result;
    } catch (...) {
        if (is_pointer_event(e.type) && pointer_epoch != state_->pointer_cancel_epoch)
            cancel_pointer_input();
        --state_->depth;
        collect_destroyed();
        throw;
    }
}

bool UiRuntime::post(UiControl* c, const UiEvent& e) {
    if (!owns(c))
        return false;
    auto& q = e.type >= 1 && e.type <= 3 ? state_->secondary : state_->primary;
    if (q.size() >= state_->capacity) {
        ++state_->dropped;
        return false;
    }
    q.push_back({ c, state_->serials.at(c), e, false });
    return true;
}

void UiRuntime::clear_events() {
    state_->primary.clear();
    state_->secondary.clear();
}

void UiRuntime::set_observer(std::function<void(UiControl*, const UiEvent&)> f) {
    state_->observer = std::move(f);
}

std::size_t UiRuntime::drain(std::size_t primary_limit, std::size_t secondary_limit) {
    if (state_->draining)
        return 0;
    state_->draining = true;
    std::size_t count{};
    const auto drain_queue = [&](auto& queue, std::size_t limit, bool observe) {
        for (std::size_t i = 0; i < limit && !queue.empty(); ++i) {
            auto q = queue.front();
            queue.pop_front();
            auto* target = q.target;
            if (!q.routed && (!owns(target) || state_->serials.at(target) != q.serial))
                continue;
            const auto pointer_epoch = state_->pointer_cancel_epoch;
            //850542 observes the recorded target before routing. The shared router
            //invokes the callback directly, without a second observation.
            if (auto observer = observe ? state_->observer : nullptr)
                observer(target, q.event);
            if (is_pointer_event(q.event.type) && (!state_->active || pointer_epoch != state_->pointer_cancel_epoch))
                continue;
            if (q.routed) {
                const auto high = static_cast<std::int32_t>((static_cast<std::uint64_t>(q.event.value) >> 16) & 255u);
                const bool match = std::ranges::any_of(state_->filters, [&](const auto& filter) {
                    return filter.id == static_cast<std::int32_t>(q.event.id) && filter.value_high_byte == high;
                });
                target = match && owns(state_->route) && state_->route->visible()    ? state_->route
                       : owns(state_->focused) && state_->focused->visible()         ? state_->focused
                       : owns(state_->interaction) && state_->interaction->visible() ? state_->interaction
                                                                                     : nullptr;
            }
            if (owns(target))
                deliver(target, q.event, false);
            ++count;
        }
    };
    try {
        drain_queue(state_->primary, primary_limit, true);
        drain_queue(state_->secondary, secondary_limit, false);
        state_->draining = false;
        collect_destroyed();
        return count;
    } catch (...) {
        state_->draining = false;
        collect_destroyed();
        throw;
    }
}

bool UiRuntime::set_focus(UiControl* c, std::uint32_t flags) {
    if (c && !owns(c))
        return false;
    if (c == state_->focused)
        return false;
    auto* old = state_->focused;
    post(old, 32, flags);
    state_->focused = c;
    post(c, 31, flags);
    return true;
}

void UiRuntime::prepare_edit_selection(UiControl& c) {
    if (!owns(&c) || c.kind != UiControlKind::edit_box)
        return;
    c.selection_begin = c.edit_prepare_caret_at_end ? c.text.size() : 0;
    c.selection_end = c.text.size();
    c.state = 1;
    post(&c, 3);
}

void UiRuntime::locale_is_1037(bool value) {
    state_->locale_is_1037 = value;
}

void UiRuntime::presentation_visible(bool value) {
    state_->presentation_visible = value;
}

bool UiRuntime::presentation_visible() const {
    return state_->presentation_visible;
}

UiControl* UiRuntime::focus() const {
    return owns(state_->focused) ? state_->focused : nullptr;
}

void UiRuntime::set_capture(UiControl* c) {
    if (!c || owns(c))
        state_->captured = c;
}

UiControl* UiRuntime::capture() const {
    return owns(state_->captured) ? state_->captured : nullptr;
}

UiControl* UiRuntime::hover() const {
    return owns(state_->hovered) ? state_->hovered : nullptr;
}

UiPoint UiRuntime::pointer_position() const {
    return state_->pointer;
}

void UiRuntime::set_input_root(UiControl* c) {
    if (c && !owns(c))
        throw std::invalid_argument("Invalid UI input root");
    state_->input_root = c;
}

UiControl* UiRuntime::input_root() const {
    return state_->input_root;
}

UiControl* UiRuntime::hit_test(UiPoint point) const {
    if (!state_->presentation_visible)
        return nullptr;
    const auto recurse = [&](auto&& self, UiControl* c) -> UiControl* {
        //84BC3B gates the whole subtree by flags, but tests parent geometry only
        //AFTER children. Persistent tabs extend above/below their parent group.
        if (!owns(c) || !c->enabled || (c->flags & 2u) || !(c->flags & 0xF0000001u))
            return nullptr;
        for (auto it = c->children.rbegin(); it != c->children.rend(); ++it)
            if (auto* hit = self(self, *it))
                return hit;
        return inside(*c, point) ? c : nullptr;
    };
    if (state_->input_root)
        return recurse(recurse, state_->input_root);
    for (auto it = state_->roots.rbegin(); it != state_->roots.rend(); ++it)
        if (auto* hit = recurse(recurse, *it))
            return hit;
    return nullptr;
}

void UiRuntime::set_temporary_route(UiControl* c, std::span<const UiEventRouteFilter> filters) {
    if (!owns(c) || c == state_->focused)
        return;
    state_->route = c;
    state_->filters.assign(filters.begin(), filters.end());
}

void UiRuntime::clear_temporary_route(UiControl* c) {
    if (state_->route == c) {
        state_->route = nullptr;
        state_->filters.clear();
    }
}

void UiRuntime::pointer_move(UiPoint p) {
    state_->pointer = p;
    if (!state_->active)
        return;
    if (auto* captured = capture()) {
        post(captured, 65539, static_cast<std::uint32_t>(pack(p)));
        return;
    }
    auto *hit = hit_test(p), *old = state_->hovered;
    if (hit != old) {
        post(old, 65538, static_cast<std::uint32_t>(pack(p)));
        state_->hovered = hit;
        post(hit, 65537, static_cast<std::uint32_t>(pack(p)));
    } else
        post(hit, 65539, static_cast<std::uint32_t>(pack(p)));
}

void UiRuntime::pointer_button(std::uint32_t button, bool down, UiPoint p) {
    if (button >= 3 || !state_->active)
        return;
    pointer_move(p);
    if (down) {
        auto* target = capture() ? capture() : hover();
        state_->down[button] = target;
        post(target, 65541, button, pack(p));
    } else {
        post(state_->down[button], 65540, button, pack(p));
        state_->down[button] = nullptr;
    }
}

void UiRuntime::pointer_wheel(std::int32_t steps, UiPoint p) {
    if (state_->active)
        post(capture() ? capture() : hover(), 65549, static_cast<std::uint32_t>(steps), pack(p));
}

void UiRuntime::cancel_pointer_input() {
    ++state_->pointer_cancel_epoch;
    const auto pointer_event = [](const auto& queued) {
        return is_pointer_event(queued.event.type);
    };
    std::erase_if(state_->primary, pointer_event);
    std::erase_if(state_->secondary, pointer_event);
    state_->down = {};
    state_->tracked = {};
    state_->captured = state_->hovered = nullptr;
    for (const auto& storage : state_->controls) {
        auto& control = *storage;
        control.pressed = control.repeating = control.hovered = false;
        control.tracked_mask = 0;
        control.next_repeat = {};
        control.visual_flags = static_cast<std::uint8_t>(control.visual_flags & ~2u);
        if (control.kind == UiControlKind::button && owns(&control))
            button_style(control);
    }
}

void UiRuntime::key(std::uint32_t key_value, bool down, std::uint32_t flags) {
    if (!state_->active)
        return;
    if (state_->primary.size() >= state_->capacity) {
        ++state_->dropped;
        return;
    }
    state_->primary.push_back(
        { nullptr, 0, { down ? 65546 : 65547, key_value, static_cast<std::intptr_t>(flags) }, true });
}

void UiRuntime::character(char16_t code, std::uint32_t flags) {
    if (!state_->active || code < 32)
        return;
    if (state_->primary.size() >= state_->capacity) {
        ++state_->dropped;
        return;
    }
    state_->primary.push_back(
        { nullptr, 0, { 65548, static_cast<std::uint32_t>(code), static_cast<std::intptr_t>(flags) }, true });
}

void UiRuntime::activate(bool active) {
    if (active == state_->active)
        return;
    state_->active = active;
    if (!active)
        cancel_pointer_input();
    for (const auto& p : state_->controls)
        post(p.get(), active ? 33 : 34);
}

void UiRuntime::set_periodic(UiControl& c, std::size_t slot, std::optional<UiPeriodicEvent> event) {
    if (slot >= c.periodic.size())
        throw std::out_of_range("UI periodic slot");
    if (event) {
        if (event->interval.count() <= 0)
            throw std::invalid_argument("Nonpositive UI period");
        event->next = state_->now + event->interval;
    }
    c.periodic[slot] = std::move(event);
}

void UiRuntime::update(std::chrono::milliseconds elapsed) {
    if (elapsed.count() < 0)
        throw std::invalid_argument("Negative UI time");
    state_->now += elapsed;
    const auto controls = state_->roots;
    for (const auto& p : state_->controls)
        if (owns(p.get()))
            for (auto& slot : p->periodic)
                if (slot && slot->next <= state_->now) {
                    const auto event = *slot;
                    const auto attempts = 1 + (state_->now - slot->next) / event.interval;
                    if (event.repeating)
                        slot->next += event.interval * attempts;
                    else
                        slot.reset();
                    if (state_->active)
                        for (std::int64_t i = 0; i < (event.repeating ? attempts : 1); ++i) {
                            if (!post(p.get(), event.event, event.id, event.value + i))
                                break;
                        }
                }
    for (const auto& p : state_->controls)
        if (owns(p.get()) && p->next_caret_blink && *p->next_caret_blink <= state_->now) {
            constexpr auto interval = std::chrono::milliseconds{ 750 };
            const auto periods = 1 + (state_->now - *p->next_caret_blink) / interval;
            *p->next_caret_blink += interval * periods;
            if (state_->active)
                for (std::int64_t i = 0; i < periods; ++i)
                    if (!post(p.get(), 16, 482952, i))
                        break;
        }
    for (const auto& p : state_->controls)
        if (owns(p.get()) && p->transition && p->transition->next <= state_->now) {
            auto& transition = *p->transition;
            const auto periods = 1 + (state_->now - transition.next) / transition.interval;
            transition.next += transition.interval * periods;
            if (state_->active && !transition.paused)
                for (std::int64_t i = 0; i < periods; ++i)
                    if (!post(p.get(), 16, 524422, i))
                        break;
        }
    const auto visit = [&](auto&& self, UiControl* c) -> void {
        if (!owns(c))
            return;
        const auto children = c->children;
        dispatch(c, 30);
        for (auto* child : children)
            self(self, child);
    };
    ++state_->depth;
    try {
        for (auto* root : controls)
            visit(visit, root);
        --state_->depth;
    } catch (...) {
        --state_->depth;
        collect_destroyed();
        throw;
    }
    drain();
    collect_destroyed();
}

std::intptr_t UiRuntime::default_event(UiControl& c, const UiEvent& e) {
    const auto queue_inside = [&] {
        if (inside(c, unpack(e.value)))
            post(&c, e.type + 4096, e.id, e.value);
    };
    match (e.type) {
        case 1 => {
            const auto children = c.children;
            for (auto* child : children)
                dispatch(child, e);
            return 0;
        }
        case 2 => {
            const auto children = c.children;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                dispatch(*it, e);
            return 0;
        }
        case 3 => {
            //4F2E30 delegates to attachment slot8. Every inspected built-in model,
            //frame, slider and text/surface implementation of that slot is a no-op.
            return 0;
        }
        case 4 => {
            destroy(&c);
            return 0;
        }
        case 5 => {
            post(&c, 4);
            return 0;
        }
        case 6 => {
            set_visible(c, e.id != 0);
            return 0;
        }
        case 7 => {
            translate(c, static_cast<std::int32_t>(e.id), static_cast<std::int32_t>(e.value));
            return 0;
        }
        case 8 => {
            const auto a = unpack(static_cast<std::intptr_t>(e.id)), b = unpack(e.value);
            set_bounds(c, { a.x, a.y, b.x, b.y });
            return 0;
        }
        case 9 => {
            move_to_tail(c);
            return 0;
        }
        case 12 => {
            state_->help = { c.help_index, c.help_text.empty() ? nullptr : c.help_text.c_str() };
            if ((c.kind == UiControlKind::list_control || c.kind == UiControlKind::list_box)
                && c.row_height > 0
                && c.viewport.contains(state_->pointer)) {
                const auto row =
                    static_cast<std::size_t>(c.scroll_offset + (state_->pointer.y - c.viewport.top) / c.row_height);
                if (row < c.rows.size())
                    for (std::size_t column = 0; column < c.columns.size() && column < c.rows[row].cells.size();
                         ++column)
                        if (state_->pointer.x > c.columns[column].left
                            && state_->pointer.x < c.columns[column].right
                            && c.rows[row].cells[column].visible
                            && c.rows[row].cells[column].help_index != 4294967295u) {
                            state_->help = { c.rows[row].cells[column].help_index, nullptr };
                            break;
                        }
            }
            return reinterpret_cast<std::intptr_t>(&state_->help);
        }
        case 13 => return c.render_depth + 1;
        case 16 => {
            if (e.id == 524421)
                step_animation(c);
            else if (e.id == 524422)
                step_transition(c);
            return 0;
        }
        case 30 => {
            step_animation(c);
            return 1;
        }
        case 65537 => {
            c.hovered = true;
            return 0;
        }
        case 65538 => {
            c.hovered = false;
            return 0;
        }
        case 65539 => {
            const auto p = unpack(static_cast<std::intptr_t>(e.id));
            if (c.tracked_mask)
                for (auto& t : state_->tracked)
                    if (t.state == 1 && (std::abs(t.point.x - p.x) > 6 || std::abs(t.point.y - p.y) > 6))
                        t.state = 2;
            return 0;
        }
        case 65541 => {
            if (e.id >= 3)
                return 0;
            state_->interaction = &c;
            queue_inside();
            auto& t = state_->tracked[e.id];
            if (!t.state) {
                const auto previous = t.tick;
                t.target = &c;
                t.point = unpack(e.value);
                t.state = 1;
                c.tracked_mask = static_cast<std::uint8_t>(c.tracked_mask | (1u << e.id));
                if (!(c.flags & 8u) && previous.count() && state_->now - previous < std::chrono::milliseconds{ 500 }) {
                    post(&c, 65543, e.id, e.value);
                    t.state = 2;
                }
                t.tick = state_->now;
            }
            return 0;
        }
        case 65540 => {
            if (e.id >= 3)
                return 0;
            queue_inside();
            auto& t = state_->tracked[e.id];
            if (t.state == 1
                && (c.tracked_mask & (1u << e.id))
                && state_->now - t.tick < std::chrono::milliseconds{ 500 }) {
                post(&c, 65542, e.id, pack(t.point));
                t.tick = state_->now;
            } else
                t.tick = {};
            t.state = 0;
            t.target = nullptr;
            c.tracked_mask = static_cast<std::uint8_t>(c.tracked_mask & ~(1u << e.id));
            return 0;
        }
        case 65542 or 65543 => {
            queue_inside();
            return 0;
        }
        case _ => return 0;
    }
}

std::intptr_t UiRuntime::widget_event(UiControl& c, const UiEvent& e) {
    const auto notify = [&](std::int32_t type, std::intptr_t value) {
        post(c.parent, type, c.id, value);
    };
    if (c.kind == UiControlKind::button) {
        if (e.type == 65539)
            return 0; //Native button motion does not consume a click by distance.
        if (e.type == 65537) {
            c.hovered = true;
            if (c.pressed && state_->tracked[0].target == &c) {
                state_->tracked[0].state = 1;
                c.visual_flags |= 2u;
            }
            button_style(c);
            return 0;
        }
        if (e.type == 65538) {
            c.hovered = false;
            if (c.pressed) {
                state_->tracked[0].state = 2;
                c.visual_flags = static_cast<std::uint8_t>(c.visual_flags & ~2u);
            }
            button_style(c);
            return 0;
        }
        if (e.type == 65541 && e.id == 0 && c.enabled) {
            auto& t = state_->tracked[0];
            if (!t.state) {
                t = { &c, unpack(e.value), state_->now, 1 };
                c.tracked_mask |= 1u;
                c.pressed = true;
                c.visual_flags |= 2u;
                c.hovered = false;
                c.repeating = false;
                c.next_repeat = state_->now + c.repeat_delay;
                state_->interaction = &c;
                button_style(c);
            }
            return 0;
        }
        if (e.type == 65540 && e.id == 0) {
            const bool click = state_->tracked[0].target == &c && state_->tracked[0].state == 1 && !c.repeating;
            if (c.flags & 16u) {
                if (c.visual_flags & 2u)
                    c.visual_flags ^= 1u;
            } else
                c.visual_flags = static_cast<std::uint8_t>(c.visual_flags & ~1u);
            c.visual_flags = static_cast<std::uint8_t>(c.visual_flags & ~2u);
            c.pressed = false;
            c.hovered = state_->tracked[0].state != 2;
            c.repeating = false;
            c.tracked_mask = 0;
            state_->tracked[0] = {};
            button_style(c);
            if (click)
                notify(256, c.visual_flags);
            return 0;
        }
        if (e.type == 30 && state_->active && c.pressed && (c.flags & 32u)) {
            if (!c.repeating && state_->now >= c.next_repeat) {
                c.repeating = true;
                c.next_repeat = state_->now + c.repeat_interval;
                if (c.visual_flags & 2u)
                    notify(65545, 0);
            } else if (c.repeating && state_->now > c.next_repeat) {
                c.next_repeat = state_->now + c.repeat_interval;
                if (c.visual_flags & 2u)
                    notify(65544, 0);
            }
        }
        if (e.type == 65542 && e.id == 1)
            notify(258, 0);
    } else if (c.kind == UiControlKind::checkbuttons) {
        if (e.type == 21
            && (c.flags & 16u)
            && c.selected_id == -1
            && !c.children.empty()
            && c.children.front()->kind == UiControlKind::button)
            set_selected(c, static_cast<std::int32_t>(c.children.front()->id));
        if (e.type == 256 && find(&c, e.id)) {
            //Native85AD8C owns these notifications, including the same-ID
            //case. The selection setter itself only changes old/new buttons.
            if (e.value == 1) {
                if (c.selected_id != -1)
                    post(c.parent, 257, static_cast<std::uint32_t>(c.selected_id), 0);
                set_selected(c, static_cast<std::int32_t>(e.id), false);
                post(c.parent, 257, e.id, 1);
            } else if (c.flags & 16u) {
                set_selected(c, static_cast<std::int32_t>(e.id));
                //Clicking an already selected mandatory tab toggled its button
                //off on release. Restore it even though the group ID is unchanged.
                auto& button = *find(&c, e.id);
                button.visual_flags = 1;
                button_style(button);
            } else {
                set_selected(c, -1);
                post(c.parent, 257, e.id, 0);
            }
        }
        if (e.type == 258)
            post(c.parent, 258, e.id, c.id);
    } else if (c.kind == UiControlKind::knob) {
        if (e.type == 65541 && e.id == 0) {
            state_->interaction = &c;
            c.pressed = true;
            c.pointer_y = unpack(e.value).y;
            set_capture(&c);
            return 0;
        }
        if (e.type == 65540 && e.id == 0) {
            c.pressed = false;
            set_capture(nullptr);
            return 0;
        }
        if (e.type == 65539 && c.pressed) {
            const auto y = unpack(static_cast<std::intptr_t>(e.id)).y;
            const auto raw =
                std::clamp<std::int64_t>(static_cast<std::int64_t>(c.raw_position) + 8LL * (c.pointer_y - y),
                                         std::min(c.raw_minimum, c.raw_maximum),
                                         std::max(c.raw_minimum, c.raw_maximum));
            c.pointer_y = y;
            c.raw_position = static_cast<std::int32_t>(raw);
            if (c.raw_minimum != c.raw_maximum) {
                //85F520 deliberately does not subtract raw_minimum on its interior
                //path; only the two exact endpoints bypass that native expression.
                const auto output = raw == c.raw_maximum ? c.maximum
                                  : raw == c.raw_minimum
                                      ? c.minimum
                                      : bounded(static_cast<std::int64_t>(c.minimum)
                                                + raw * (c.maximum - c.minimum) / (c.raw_maximum - c.raw_minimum));
                set_value(c, output, true);
                c.raw_position = static_cast<std::int32_t>(raw);
            }
        }
    } else if (c.kind == UiControlKind::scroll) {
        if ((e.type == 256 || e.type == 65544 || e.type == 65545) && (e.id == 1 || e.id == 2))
            set_value(c, bounded(static_cast<std::int64_t>(c.value) + (e.id == 1 ? c.step_size : -c.step_size)), true);
        if (e.type == 65549)
            set_value(c, bounded(static_cast<std::int64_t>(c.value) - static_cast<std::int32_t>(e.id)), true);
        if (e.type == 69638) {
            const auto p = unpack(e.value);
            const bool horizontal = (c.flags & 16u) == 0;
            auto* thumb = find(&c, 3);
            const auto axis = horizontal ? p.x : p.y;
            const auto center =
                thumb ? (horizontal ? thumb->rect.left : thumb->rect.top) : (horizontal ? c.rect.left : c.rect.top);
            set_value(c,
                      bounded(static_cast<std::int64_t>(c.value)
                              + (axis < center ? -1LL : 1LL) * std::max(c.visible_rows, 1)),
                      true);
        }
    } else if (c.kind == UiControlKind::numeric_stepper) {
        if (e.type == 21)
            for (const auto id : { 1u, 2u })
                if (auto* button = find(&c, id)) {
                    button->repeat_delay = std::chrono::milliseconds{ 200 };
                    button->repeat_interval = std::chrono::milliseconds{ 50 };
                }
        if ((e.type == 256 || e.type == 65544 || e.type == 65545) && (e.id == 1 || e.id == 2)) {
            const bool up = e.id == 1;
            if (owns(c.associated_edit))
                set_value(c, integer(c.associated_edit->text));
            if (e.type == 65545) {
                c.repeat_countdown = c.step_size == 1 ? 20 : 5;
                c.repeat_increment = 1;
            }
            if (e.type != 65545 || up) {
                bool step = true;
                auto amount = c.step_size;
                if (e.type == 65544) {
                    if (c.step_size == 1) {
                        amount = c.repeat_increment;
                        if (--c.repeat_countdown <= 0) {
                            c.repeat_increment = bounded(static_cast<std::int64_t>(c.repeat_increment) * 10);
                            c.repeat_countdown = 20;
                        }
                    } else {
                        step = --c.repeat_countdown <= 0;
                        if (step)
                            c.repeat_countdown = 5;
                    }
                }
                if (step)
                    set_value(c,
                              bounded(static_cast<std::int64_t>(c.value)
                                      + (up ? static_cast<std::int64_t>(amount) : -static_cast<std::int64_t>(amount))),
                              true);
            }
        }
        if (e.type == 65549)
            set_value(c, bounded(static_cast<std::int64_t>(c.value) + static_cast<std::int32_t>(e.id)), true);
        if (e.type == 2055)
            set_value(c, static_cast<std::int32_t>(e.value), true);
        if (e.type == 2053 && e.id == 3 && (e.value == 13 || e.value == 27)) {
            if (owns(c.associated_edit)) {
                if (e.value == 13)
                    set_value(c, integer(c.associated_edit->text), true);
                else
                    set_text(*c.associated_edit, integer_text(c.value));
            }
            set_focus(nullptr);
        }
    } else if (c.kind == UiControlKind::list_control || c.kind == UiControlKind::list_box) {
        require_list(*this, c);
        if (e.type == 18 || e.type == 21)
            refresh_list(c, false);
        const auto count = static_cast<std::int32_t>(std::min<std::size_t>(c.rows.size(), 2147483647u));
        const auto capacity = c.row_height > 0 ? std::max(c.viewport.height() / c.row_height, 0) : 0;
        c.visible_rows = capacity;
        if (c.kind == UiControlKind::list_box) {
            //Kind4 has a distinct512/513 protocol, not kind7's1024+ row events.
            const auto from_point = [&](UiPoint point) {
                if (c.row_height <= 0)
                    return;
                const auto row = (point.y - c.viewport.top) / c.row_height;
                const auto candidate =
                    row >= 0 && row < capacity && c.scroll_offset + row < count ? c.scroll_offset + row : -1;
                if (candidate != c.selected_id) {
                    c.selected_id = candidate;
                    if (candidate >= 0)
                        notify(513, candidate);
                    post(&c, 3);
                }
            };
            if (e.type == 2048)
                if (auto* scroll = find(&c, 1)) {
                    if (c.scroll_offset != scroll->value) {
                        c.scroll_offset = scroll->value;
                        post(&c, 3);
                    }
                }
            if (e.type == 65539
                && (c.flags & 128u)
                && c.attachment_rect.contains(unpack(static_cast<std::intptr_t>(e.id))))
                from_point(unpack(static_cast<std::intptr_t>(e.id)));
            if (e.type == 69638) {
                if (c.flags & 128u) {
                    if (c.selected_id >= 0)
                        notify(512, c.selected_id);
                } else
                    from_point(unpack(e.value));
            }
            if (e.type == 69639 && c.selected_id >= 0)
                notify(512, c.selected_id);
            return default_event(c, e);
        }
        const auto select = [&](std::int32_t index) {
            select_list_row(c, index);
        };
        if (e.type == 2048) {
            if (auto* scroll = find(&c, 1))
                set_list_scroll_offset(c, scroll->value);
        }
        if (e.type == 65549) {
            set_list_scroll_offset(
                c,
                bounded(static_cast<std::int64_t>(c.scroll_offset) - static_cast<std::int32_t>(e.id)));
        }
        if ((e.type == 69638 || (e.type == 69639 && e.id == 0))
            && c.viewport.contains(unpack(e.value))
            && c.row_height > 0) {
            const auto p = unpack(e.value);
            const auto requested = c.scroll_offset + (p.y - c.viewport.top) / c.row_height;
            select(requested < count ? requested : -1);
            if (c.selected_id >= 0) {
                if (e.type == 69638 && e.id == 0)
                    for (std::size_t i = 0; i < c.columns.size(); ++i)
                        if (p.x > c.columns[i].left && p.x < c.columns[i].right) {
                            notify(1028, static_cast<std::intptr_t>(i));
                            break;
                        }
                const auto notification = e.type == 69639 ? 1027 : e.id ? 1026 : 1024;
                if (notification != 1024 || (c.flags & 128u))
                    notify(notification, c.selected_id);
            }
        }
        if (e.type == 65546 && (c.flags & 512u))
            match (e.id) {
                case 8960 => select(count - 1);
                case 9216 => select(0);
                case 9728 => select(std::max(c.selected_id - 1, 0));
                case 10240 => select(c.selected_id + 1);
                case _ => {}
            }
        if (e.type == 256 && (c.flags & 16u) && e.id >= 16 && e.id - 16 < c.columns.size()) {
            const auto column = static_cast<std::int32_t>(e.id - 16);
            const auto descending = column == c.sort_column && !c.sort_direction;
            set_list_sort_key(c, descending ? -(column + 1) : column + 1);
        }
        if ((e.type == 21 || e.type == 26) && (c.flags & 512u)) {
            constexpr std::array filters{ UiEventRouteFilter{ 8960, 0 },
                                          UiEventRouteFilter{ 9216, 0 },
                                          UiEventRouteFilter{ 9728, 0 },
                                          UiEventRouteFilter{ 10240, 0 } };
            set_temporary_route(&c, filters);
        }
    } else if (c.kind == UiControlKind::edit_box) {
        const std::pair previous_selection{ c.selection_begin, c.selection_end };
        const auto nearest_text_index = [&](UiPoint point) {
            if (!c.text_position)
                throw std::logic_error("Editable UI pointer input requires bound font position metrics");
            std::size_t best{};
            auto score = std::numeric_limits<std::int64_t>::max();
            for (std::size_t i = 0; i <= c.text.size(); ++i) {
                const auto r = c.text_position(c, i);
                const auto dx = std::abs(static_cast<std::int64_t>(r.left) - point.x);
                const auto dy = std::abs(static_cast<std::int64_t>(r.top) - point.y - c.edit_pointer_y_bias);
                //85A565 prioritizes the nearest text row, then insertion X. A
                //Euclidean search incorrectly jumps rows near a short line's end.
                const auto candidate = dy * 16384 + dx;
                if (candidate < score) {
                    score = candidate;
                    best = i;
                }
            }
            return best;
        };
        const auto replace = [&](std::u16string_view inserted) {
            if (c.read_only)
                return;
            auto begin = std::min(c.selection_begin, c.selection_end),
                 end = std::max(c.selection_begin, c.selection_end);
            begin = std::min(begin, c.text.size());
            end = std::min(end, c.text.size());
            const auto room = c.text_limit - std::min(c.text_limit, c.text.size() - (end - begin));
            auto text = c.text;
            text.replace(begin, end - begin, inserted.substr(0, room));
            const auto cursor = begin + std::min(room, inserted.size());
            set_text(c, text, true);
            c.selection_begin = c.selection_end = cursor;
        };
        if (e.type == 31) {
            c.state = 1;
            c.next_caret_blink = state_->now + std::chrono::milliseconds{ 750 };
            post(&c, 3);
            if (!e.id)
                prepare_edit_selection(c);
        }
        if (e.type == 32) {
            c.state = -1;
            c.next_caret_blink.reset();
            post(&c, 3);
        }
        if (e.type == 16 && e.id == 482952 && c.state != -1 && c.selection_begin == c.selection_end)
            c.state ^= 1;
        if (e.type == 26 || (e.type == 65542 && !c.read_only))
            set_focus(&c, 1);
        if (e.type == 65543 && !c.read_only)
            prepare_edit_selection(c);
        if (e.type == 65541 && e.id == 0 && !c.read_only) {
            const auto best = nearest_text_index(unpack(e.value));
            c.selection_begin = c.selection_end = best;
            c.pressed = true;
            set_capture(&c);
        }
        if (e.type == 65539 && c.pressed && c.text_position && !c.edit_prepare_caret_at_end) {
            c.selection_end = nearest_text_index(unpack(static_cast<std::intptr_t>(e.id)));
        }
        if (e.type == 65540 && e.id == 0) {
            c.pressed = false;
            if (capture() == &c)
                set_capture(nullptr);
        }
        if (e.type == 65548) {
            const auto code = static_cast<char16_t>(e.id);
            if (code >= 32
                && code != 127
                && (!c.numeric_only || (code >= u'0' && code <= u'9') || code == u'-')
                && (c.accepted_characters.empty() || c.accepted_characters.find(code) != std::u16string::npos)
                && c.rejected_characters.find(code) == std::u16string::npos)
                replace(std::u16string_view{ &code, 1 });
        }
        if (e.type == 65547) {
            const auto key_code = e.id > 255 ? e.id >> 8 : e.id;
            match (key_code) {
                case 9 => return dispatch(c.parent, 2051, c.id, e.value);
                case 13 => {
                    if (!(c.flags & 128u))
                        return dispatch(c.parent, 2050, c.id, e.value);
                }
                case 27 => return dispatch(c.parent, 2052, c.id, e.value);
                case _ => return dispatch(c.parent, 2054, c.id, key_code);
            }
        }
        if (e.type == 65546) {
            const auto key_code = e.id > 255 ? e.id >> 8 : e.id;
            const bool shift = ((static_cast<std::uint64_t>(e.value) >> 16) & 255u) == 3;
            auto position = std::min(c.selection_end, c.text.size());
            match (key_code) {
                case 8 => {
                    if (c.selection_begin == c.selection_end && position)
                        c.selection_begin = position - 1;
                    replace({});
                }
                case 46 => {
                    if (c.selection_begin == c.selection_end && position < c.text.size())
                        c.selection_end = position + 1;
                    replace({});
                }
                case 35 => position = c.text.size();
                case 36 => position = 0;
                case 37 => {
                    if (position)
                        --position;
                }
                case 39 => position = std::min(position + 1, c.text.size());
                case 38 or 40 => {
                    if (!(c.flags & 128u))
                        return dispatch(c.parent, 2053, c.id, e.id);
                    if (!c.text_position)
                        throw std::logic_error("Multiline caret input requires bound font position metrics");
                    const auto current = c.text_position(c, position);
                    const auto target_y =
                        current.top + (key_code == 38 ? -std::max(current.height(), 1) : std::max(current.height(), 1));
                    double best = std::numeric_limits<double>::infinity();
                    for (std::size_t i = 0; i <= c.text.size(); ++i) {
                        const auto r = c.text_position(c, i);
                        const auto dx = static_cast<double>(r.left - current.left),
                                   dy = static_cast<double>(r.top - target_y);
                        if (dx * dx + dy * dy < best) {
                            best = dx * dx + dy * dy;
                            position = i;
                        }
                    }
                }
                case 13 => {
                    if (c.numeric_only) {
                        c.value = integer(c.text);
                        return dispatch(c.parent, 2053, c.id, 13);
                    }
                    if (c.flags & 128u)
                        replace(u"\n");
                    return 0;
                }
                case 27 => return dispatch(c.parent, 2053, c.id, 27);
                case _ => return dispatch(c.parent, 2053, c.id, e.id);
            }
            if (key_code != 8 && key_code != 46) {
                c.selection_end = position;
                if (!shift)
                    c.selection_begin = position;
            }
        }
        if (previous_selection != std::pair{ c.selection_begin, c.selection_end } && c.state != -1)
            c.state = 1;
    }
    return default_event(c, e);
}
} //namespace simcoaster
