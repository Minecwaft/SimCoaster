#pragma once
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simcoaster {
enum class UiControlKind : std::uint16_t {
    window = 1,
    button,
    scroll,
    list_box,
    edit_box,
    image,
    list_control,
    checkbuttons,
    bar,
    text_box,
    knob,
    numeric_stepper,
    child_toggle_mask
};
enum class UiMessageButtonKind : std::uint8_t {
    none = 0,
    okay = 1,
    cancel = 2
};

struct UiPoint {
    std::int32_t x {}, y {};
};

struct UiRect {
    std::int32_t left {}, top {}, right {}, bottom {};

    std::int32_t width() const {
        return right - left;
    }

    std::int32_t height() const {
        return bottom - top;
    }

    bool contains(UiPoint p) const {
        return p.x >= left && p.y >= top && p.x < right && p.y < bottom;
    }
};

struct UiEvent {
    std::int32_t type {};
    std::uint32_t id {};
    std::intptr_t value {};
};
struct UiControl;
class UiRuntime;
using UiCallback = std::function<std::intptr_t(UiControl&, const UiEvent&)>;

struct UiPeriodicEvent {
    std::int32_t event = 16;
    std::uint32_t id {};
    std::intptr_t value {};
    std::chrono::milliseconds interval {};
    std::chrono::milliseconds next {};
    bool repeating = true;
};
enum class UiHitShape {
    rectangle,
    circle,
    ellipse,
    polygon
};

struct UiHitRegion {
    UiHitShape shape = UiHitShape::rectangle;
    UiRect rect {};
    UiPoint center {}, radii {};
    std::vector<UiPoint> polygon;
};

struct UiAttachmentPlacement {
    std::uint16_t part_base {}, part_value {};
    std::int32_t depth = 1, rotation {};
    UiPoint pivot {};
};

struct UiTextConstraints {
    bool width {}, height {};
};

struct UiMeterPresentation {
    std::string texture;
    UiPoint inset {};
};
enum class UiListCellFormat : std::uint8_t {
    text,
    signed_integer,
    signed_currency,
    meter,
    model_state
};
enum class UiScalarTextKind : std::uint8_t {
    signed_integer,
    signed_currency,
    percentage
};

struct UiListCell {
    std::variant<std::u16string, std::int32_t> value { std::u16string {} };
    bool visible = true;
    std::optional<std::array<std::uint8_t, 4>> color {};
    std::uint32_t help_index = 4294967295u;
    //Numeric identity/sort semantics remain in value. Native category/date
    //attachments may display a localized caption for that same numeric cell.
    std::optional<std::u16string> display_text {};
    UiListCell() = default;

    UiListCell(std::u16string text) : value(std::move(text)) {}

    UiListCell(std::u16string_view text) : value(std::u16string { text }) {}

    UiListCell(const char16_t* text) : value(std::u16string { text }) {}

    UiListCell(std::int32_t number) : value(number) {}

    template <std::input_iterator Iterator>
    UiListCell(Iterator first, Iterator last) : value(std::u16string(first, last)) {}

    bool numeric() const {
        return std::holds_alternative<std::int32_t>(value);
    }

    std::int32_t number() const {
        return std::get<std::int32_t>(value);
    }

    std::u16string_view text() const {
        return std::get<std::u16string>(value);
    }

    //Existing Objective rows retain their ordinary string construction and
    //text/font interfaces. Numeric cells require an explicit numeric formatter.
    operator std::u16string_view() const {
        return text();
    }

    auto operator<=>(const UiListCell&) const = default;
};

struct UiListColumnPresentation {
    UiListCellFormat format = UiListCellFormat::text;
    std::optional<std::uint32_t> font;
    std::optional<std::array<std::uint8_t, 4>> color;
    std::int32_t horizontal_alignment {}, vertical_alignment = 1;
    UiTextConstraints text_constraints;
    UiPoint text_offset {};
    UiPoint model_inset {};
    UiMeterPresentation meter;
    std::uint32_t model_asset_hash {};
    bool zero_blank {};
};

struct UiListRow {
    std::intptr_t identity {};
    std::vector<UiListCell> cells;
    std::optional<std::intptr_t> parent_identity {};
    std::optional<std::array<std::uint8_t, 4>> color {};
};

struct UiAnimationStep {
    std::array<std::int32_t, 4> words {};
};

struct UiBoundsTransition {
    UiRect target, delta;
    std::int64_t remaining {};
    std::chrono::milliseconds interval {}, next {};
    bool paused {};
};

//Native coordinates are absolute logical-canvas coordinates. Creation flag
//1024 translates supplied bounds through the ancestor chain. Runtime pointers,
//ownership and callback payloads follow the native x64 host ABI.
struct UiControl {
    UiRuntime* runtime {};
    UiControl* parent {};
    std::vector<UiControl*> children;
    UiControlKind kind = UiControlKind::window;
    std::uint32_t id {}, flags {};
    UiRect rect {}, attachment_rect {}, viewport {};
    UiHitRegion hit_region;
    bool custom_hit_region {}, destroying {}, enabled = true;
    std::int32_t state {}, value {}, minimum {}, maximum = 1024;
    //Native common constructor initializes -1. Applying this control's cursor
    //uses the configured global default, independently of its parent's cursor.
    std::int32_t cursor_id = -1;
    std::int32_t selected_id = -1, render_depth = 1;
    std::uint32_t asset_hash {}, secondary_asset_hash {};
    //Native edit boxes own a third model attachment for caret/selection;
    //it is independently laid out, rather than a whole-control backdrop.
    std::uint32_t edit_caret_asset_hash {};
    UiPoint edit_text_scroll {};
    //Native858A3A replaces the text attachment's Y offset after scrolling;
    //before that first update the font-specific default offset still applies.
    bool edit_vertical_scroll_applied {};
    //Native85A488/85A646 add half the raw bitmap line advance to pointer Y
    //before the row-first nearest-position search (not half its UI-scaled size).
    std::int32_t edit_pointer_y_bias {};
    UiAttachmentPlacement primary_placement, secondary_placement;
    std::u16string text;
    //Affixed edit attachments format presentation independently of the raw
    //editable buffer; caret positions refer to the unprefixed text.
    std::u16string text_affix;
    bool append_text_affix {};
    std::optional<UiScalarTextKind> scalar_text;
    //Explicit native controller binding: an additional primary text surface
    //under the usual secondary caption, with its own bounds/offset/depth.
    bool has_primary_text_attachment {};
    bool primary_shadow_text {}, secondary_shadow_text {};
    UiTextConstraints text_constraints;
    std::u16string help_text;
    std::uint32_t font {};
    std::array<std::uint8_t, 4> color { 255, 255, 255, 255 };
    std::array<std::array<std::uint8_t, 4>, 4> button_text_colors { { { 255, 255, 255, 255 }, { 0xCD, 0xCD, 0xCD, 255 }, { 0, 0, 0, 255 }, { 255, 255, 255, 255 } } };
    UiPoint text_offset {}, active_text_offset { 1, 1 };
    bool text_offsets_explicit {};
    std::int32_t default_text_y_offset = 12;
    std::uint32_t help_index = 4294967295u, template_metadata = 4294967295u;
    std::array<void*, 16> context {};
    UiCallback primary_callback, secondary_callback;
    std::array<std::optional<UiPeriodicEvent>, 7> periodic;
    std::vector<UiRect> columns;
    std::vector<UiListColumnPresentation> column_presentations;
    std::vector<UiListRow> rows;
    //The native sort key is +(column+1) ascending, -(column+1) descending,
    //and zero when no sort column is selected. Item identity is independent
    //of its current visible/sorted row index.
    std::int32_t sort_column = -1, sort_direction {}, step_size = 1;
    std::uint32_t list_highlight_asset_hash {};
    std::optional<std::int32_t> row_height_font_padding;
    std::optional<UiMeterPresentation> meter_presentation;
    std::int32_t repeat_countdown {}, repeat_increment = 1;
    std::int32_t raw_minimum {}, raw_maximum = 4095, raw_position {}, mapped_raw_position {}, pointer_y {};
    std::int32_t horizontal_alignment {}, vertical_alignment {};
    std::u16string accepted_characters, rejected_characters;
    std::function<UiRect(const UiControl&, std::size_t)> text_position;
    UiControl* associated_edit {};
    std::size_t selection_begin {}, selection_end {}, text_limit = 32767;
    std::int32_t scroll_offset {}, row_height {}, visible_rows {};
    std::chrono::milliseconds repeat_delay { 500 }, repeat_interval { 125 };
    std::chrono::milliseconds next_repeat {};
    std::optional<std::chrono::milliseconds> next_caret_blink;
    std::uint8_t visual_flags {}, tracked_mask {};
    bool hovered {}, pressed {}, repeating {}, read_only {}, numeric_only {}, password {};
    bool initial_value_pending {};
    //Native editbox +172: locale1037 prepares a caret at the end instead
    //of selecting existing text. Independent of IME composition support.
    bool edit_prepare_caret_at_end {};
    std::vector<UiAnimationStep> animation;
    std::size_t animation_index {};
    UiRect animation_correction {};
    std::chrono::milliseconds animation_interval {}, animation_tick {};
    bool animation_playing {};
    std::optional<UiBoundsTransition> transition;

    bool visible() const {
        return (flags & 1u) != 0 && !destroying;
    }
};

struct UiTemplateError {
    std::size_t word {};
    std::string reason;
};

struct UiEventRouteFilter {
    std::int32_t id {}, value_high_byte {};
};

struct UiHelpQuery {
    std::uint32_t index = 4294967295u;
    const char16_t* text {};
};

class UiRuntime {
    struct State;
    std::unique_ptr<State> state_;
    void collect_destroyed();
    std::intptr_t default_event(UiControl&, const UiEvent&);
    std::intptr_t widget_event(UiControl&, const UiEvent&);
    void configure_scroll_thumb(UiControl&);
    void step_animation(UiControl&);
    void step_transition(UiControl&);
    std::intptr_t deliver(UiControl*, const UiEvent&, bool observe);

public:
    explicit UiRuntime(std::size_t queue_capacity = 2048);
    ~UiRuntime();
    UiRuntime(const UiRuntime&) = delete;
    UiRuntime& operator=(const UiRuntime&) = delete;
    UiControl& create(UiControl* parent, UiControlKind, std::uint32_t flags, std::uint32_t id, UiRect);
    UiControl* find(UiControl* parent, std::uint32_t id) const;
    UiControl* find_recursive(UiControl* parent, std::uint32_t id) const;
    bool owns(const UiControl*) const;
    std::uint64_t lifetime_serial(const UiControl*) const;
    std::uint64_t tree_revision() const;
    void reparent(UiControl&, UiControl* parent);
    std::span<UiControl* const> roots() const;
    void destroy(UiControl*);
    void clear();
    void move_to_tail(UiControl&);
    void set_visible(UiControl&, bool);
    void set_enabled(UiControl&, bool);
    //Native85B4AF auxiliary appearance byte; never an enabled-state change.
    void set_button_auxiliary_state(UiControl&, bool, bool notify = true);
    void set_bounds(UiControl&, UiRect);
    void translate(UiControl&, std::int32_t x, std::int32_t y);
    void set_value(UiControl&, std::int32_t, bool notify = false);
    void set_range(UiControl&, std::int32_t minimum, std::int32_t maximum);
    void set_raw_range(UiControl&, std::int32_t minimum, std::int32_t maximum);
    void set_part_base(UiControl&, std::uint16_t, bool secondary = false);
    void set_attachment_part(UiControl&, std::uint16_t);
    void set_depth(UiControl&, std::int32_t);
    //Native850B50: visible descendants, including event13 attachment overrides.
    std::int32_t visible_render_depth(const UiControl* exclude = nullptr);
    //4F4470: raise a new modal wrapper over visible controls, then fit its
    //complete descendant/attachment depth span below the native limit39.
    void place_modal_layer(UiControl&);
    void set_rotation(UiControl&, std::int32_t, bool recursive = true);
    void start_animation(UiControl&, std::span<const UiAnimationStep>, std::chrono::milliseconds);
    void stop_animation(UiControl&);
    void start_transition(UiControl&, UiRect, std::chrono::milliseconds duration, std::chrono::milliseconds interval);
    void pause_transition(UiControl&, bool);
    void stop_transition(UiControl&, bool snap_to_target = false);
    void set_selected(UiControl&, std::int32_t id, bool notify = false);
    void set_text(UiControl&, std::u16string_view, bool notify = false);
    void refresh_list(UiControl&, bool notify = true);
    void set_list_scroll_offset(UiControl&, std::int32_t, bool notify = true);
    void select_list_row(UiControl&, std::int32_t, bool notify = true, bool ensure_visible = true);
    std::int32_t list_sort_key(const UiControl&) const;
    void set_list_sort_key(UiControl&, std::int32_t, bool notify = true);
    void sort_list_rows(UiControl&, bool notify = false);
    std::optional<std::intptr_t> selected_list_identity(const UiControl&) const;
    std::optional<std::size_t> find_list_row(const UiControl&, std::intptr_t identity) const;
    std::optional<std::size_t> parent_list_row(const UiControl&, std::size_t visible_index) const;
    void set_list_rows(UiControl&, std::vector<UiListRow>, bool preserve_selection = true, bool notify = true);
    void insert_list_row(UiControl&, UiListRow, std::optional<std::size_t> before = std::nullopt, bool notify = true);
    bool update_list_row(UiControl&, std::intptr_t identity, std::span<const UiListCell>, std::uint64_t column_mask, bool notify = true);
    bool remove_list_row(UiControl&, std::intptr_t identity, bool notify = true);
    void clear_list_rows(UiControl&, bool notify = true);
    void set_primary_callback(UiControl&, UiCallback);
    void set_secondary_callback(UiControl&, UiCallback);
    std::intptr_t secondary(UiControl&, const UiEvent&);
    std::intptr_t dispatch(UiControl*, const UiEvent&);

    std::intptr_t dispatch(UiControl* c, std::int32_t event, std::uint32_t id = 0, std::intptr_t value = 0) {
        return dispatch(c, UiEvent { event, id, value });
    }

    bool post(UiControl*, const UiEvent&);

    bool post(UiControl* c, std::int32_t event, std::uint32_t id = 0, std::intptr_t value = 0) {
        return post(c, UiEvent { event, id, value });
    }

    std::size_t drain(std::size_t primary_limit = 1024, std::size_t secondary_limit = 4096);
    void clear_events();
    void set_observer(std::function<void(UiControl*, const UiEvent&)>);
    bool set_focus(UiControl*, std::uint32_t flags = 0);
    void prepare_edit_selection(UiControl&);
    void locale_is_1037(bool);
    //Native event-routing root visibility, spanning this runtime's root trees.
    void presentation_visible(bool);
    bool presentation_visible() const;
    UiControl* focus() const;
    void set_capture(UiControl*);
    UiControl* capture() const;
    UiControl* hover() const;
    UiPoint pointer_position() const;
    UiControl* hit_test(UiPoint) const;
    void set_input_root(UiControl*);
    UiControl* input_root() const;
    void set_temporary_route(UiControl*, std::span<const UiEventRouteFilter>);
    void clear_temporary_route(UiControl*);
    void pointer_move(UiPoint);
    void pointer_button(std::uint32_t button, bool down, UiPoint);
    void pointer_wheel(std::int32_t steps, UiPoint);
    //Cancel an interrupted host gesture without synthesizing a click. Pending
    //pointer traffic is discarded; completed application events keep FIFO order.
    void cancel_pointer_input();
    void key(std::uint32_t key, bool down, std::uint32_t flags = 0);
    void character(char16_t, std::uint32_t flags = 0);
    void activate(bool);
    void set_periodic(UiControl&, std::size_t slot, std::optional<UiPeriodicEvent>);
    void update(std::chrono::milliseconds);
    std::chrono::milliseconds clock() const;
    std::size_t size() const;
    std::uint64_t dropped_events() const;
    std::expected<UiControl*, UiTemplateError> parse_template(UiControl* parent, std::span<const std::byte>, UiCallback = {});

    std::expected<UiControl*, UiTemplateError> parse_template(UiControl* parent, std::span<const std::uint16_t> words, UiCallback callback = {}) {
        return parse_template(parent, std::as_bytes(words), std::move(callback));
    }
};
} //namespace simcoaster
