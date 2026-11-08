#include "simcoaster/world_ui_mail.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char message_template[]{
#embed "ui_templates/app_message_center.bin"
};
static_assert(sizeof(message_template) == 1032);

template <std::size_t N>
std::u16string_view view(const std::array<char16_t, N>& source) {
    return { source.data(), static_cast<std::size_t>(std::ranges::find(source, char16_t{}) - source.begin()) };
}

bool escape(const UiEvent& input) {
    return input.type == 65547
        && (input.id > 255 ? input.id >> 8 : input.id) == 27
        && !(static_cast<std::uint32_t>(input.value) & 0xFF0000u);
}

std::uint32_t ui_tick(UiRuntime& ui) {
    return static_cast<std::uint32_t>(ui.clock().count());
}
} //namespace

struct WorldMailUiController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary{}, *root{};
    std::filesystem::path game_root;
    WorldMailUiHost host;
    std::unique_ptr<UiFontLibrary> fonts;
    std::map<WorldMailIdentity, std::intptr_t> identities;
    std::map<std::intptr_t, WorldMailIdentity> keys;
    std::map<WorldMailIdentity, ThingId> target_ids;
    std::map<ThingId, std::uint64_t> generations;
    std::set<WorldMailIdentity> retired_targets;
    std::intptr_t next_identity = 1;

    struct Blink {
        bool phase{};
        std::uint32_t next{};
    };

    std::map<WorldMailIdentity, Blink> blinks;
    std::optional<WorldMailIdentity> selected, preview, pending;
    bool closing{}, refreshing{}, defer{}, counter_consumed{};
    std::int32_t sort_key = -2, last_index = -1;
    std::uint32_t deadline{}, pixel_width = 1100, pixel_height = 760;
    std::optional<std::array<std::uint32_t, 2>> body_resolution;

    explicit State(WorldRuntime& context,
                   UiRuntime& runtime,
                   UiControl* parent,
                   const std::filesystem::path& path,
                   WorldMailUiHost callbacks)
        : c(context),
          ui(runtime),
          primary(parent),
          game_root(path),
          host(std::move(callbacks)) {
        if (!ui.owns(primary) || !host.dispatch)
            throw std::invalid_argument("Message Center requires its persistent root " "and complete navigation host");
    }

    ~State() {
        close();
    }

    UiControl& required(std::uint32_t id) {
        if (auto* control = ui.find_recursive(root, id))
            return *control;
        throw std::runtime_error("Original Message Center template missing control " + std::to_string(id));
    }

    std::u16string text(std::uint32_t row) {
        auto value =
            format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, row, 16383);
        if (!value)
            throw std::runtime_error("Message Center text: " + value.error().reason);
        return std::move(*value);
    }

    void caption(UiControl& control,
                 std::u16string_view value,
                 std::uint32_t font = 6,
                 std::int32_t horizontal = 0,
                 std::int32_t vertical = 1) {
        control.font = font;
        control.horizontal_alignment = horizontal;
        control.vertical_alignment = vertical;
        ui.set_text(control, value);
    }

    void bind(UiControl& node) {
        const auto widget = node.primary_callback;
        ui.set_primary_callback(node, [this, widget](auto& sender, const auto& input) -> std::intptr_t {
            if (&sender == root || (input.type >= 256 && input.type <= 2055))
                return event(sender, input);
            return widget ? widget(sender, input) : ui.secondary(sender, input);
        });
        for (auto* child : node.children)
            bind(*child);
    }

    AdvisorRecord* advisor() {
        auto* world = c.referenced_world();
        if (!world || !world->saved.advisor)
            return nullptr;
        auto* record = c.entities.at(world->saved.advisor).payload;
        if (!record || record->kind != ThingKind::advisor)
            throw std::runtime_error("Message Center Advisor is absent");
        return reinterpret_cast<AdvisorRecord*>(record);
    }

    void help(std::int32_t action) {
        if (advisor())
            queue_world_advisor_help_action(c, action);
    }

    void consume(std::optional<WorldMailIdentity> key) {
        const auto now = c.ports.clock.get_simulation_clock_time_ms(c);
        const auto due = deadline && deadline < now + 2000u;
        deadline = 0;
        if (due && !refreshing && !defer && key && consume_ui_mail(c, *key)) {
            counter_consumed = true;
            refresh_row_colors();
            ui.sort_list_rows(required(3006), false);
            ui.refresh_list(required(3006), false);
        }
    }

    void close() {
        if (closing || !root)
            return;
        closing = true;
        consume(selected);
        auto* old = std::exchange(root, nullptr);
        if (ui.owns(old)) {
            if (auto* list = ui.find_recursive(old, 3006))
                sort_key = ui.list_sort_key(*list);
            ui.destroy(old);
        }
        selected.reset();
        preview.reset();
        pending.reset();
        identities.clear();
        keys.clear();
        blinks.clear();
        target_ids.clear();
        body_resolution.reset();
        deadline = 0;
        last_index = -1;
        counter_consumed = false;
        if (auto* record = advisor()) {
            record->from_ui_panel = record->panel_up = 0;
            for (const auto action : { 431, 529 })
                world_operations::cancel_advisor_action_by_id(c, record, action);
        }
        if (old && host.panels.deactivate)
            host.panels.deactivate(old);
        if (host.panels.synchronize_focus)
            host.panels.synchronize_focus();
        closing = false;
    }

    void open() {
        if (root
            || (c.ui.command_menu_builder && c.ui.command_menu_builder->visible)
            || (host.command_menu_visible && host.command_menu_visible()))
            return;
        if (host.panels.dismiss)
            host.panels.dismiss();
        selected.reset();
        preview.reset();
        pending.reset();
        deadline = 0;
        last_index = -1;
        auto parsed = ui.parse_template(primary, std::as_bytes(std::span{ message_template }));
        if (!parsed || !*parsed)
            throw std::runtime_error("Original Message Center template could not be constructed");
        root = *parsed;
        if (root->id != 3000)
            throw std::runtime_error("Original Message Center root mismatch");
        bind(*root);
        caption(required(3003), text(861), 5);
        required(3003).primary_shadow_text = true;
        caption(required(3007), text(860), 8);
        caption(required(3008), text(859), 8);
        for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 3>{
                 { { 3011, 856 }, { 3013, 857 }, { 3015, 855 } } }) {
            caption(required(id), text(row));
            required(id).color = { 234, 239, 102, 255 };
        }
        for (const auto id : { 3012u, 3014u, 3016u })
            caption(required(id), {});
        caption(required(3018), {}, 8);
        caption(required(3020), text(211), 8);
        auto& body = required(3009);
        caption(body, {}, 7, 0, 0);
        body.text_constraints = { true, true };
        body.color = { 225, 225, 255, 255 };
        ui.set_visible(required(3010), false);
        ui.set_visible(required(3019), false);
        required(3019).help_index = 175;
        ui.set_enabled(required(3004), false);
        auto& list = required(3006);
        if (list.columns.size() != 2)
            throw std::runtime_error("Original Message Center needs two columns");
        list.font = 7;
        list.row_height_font_padding = 6;
        list.list_highlight_asset_hash = ui_model_asset_hash("hilight");
        list.column_presentations.resize(2);
        list.column_presentations[0].format = UiListCellFormat::text;
        list.column_presentations[0].font = 7;
        list.column_presentations[1].format = UiListCellFormat::signed_integer;
        list.column_presentations[1].font = 7;
        list.column_presentations[1].horizontal_alignment = 2;
        for (std::uint32_t column = 0; column < 2; ++column) {
            auto& heading = *ui.find(&list, 16 + column);
            caption(heading, text(209 + column), 8, 1);
            heading.asset_hash = ui_model_asset_hash("!cbut");
            heading.color = { 255, 255, 255, 255 };
            heading.text_constraints = { true, true };
            heading.help_index = 20 + column;
            for (std::size_t state = 0; state < 3; ++state)
                heading.button_text_colors[state] = heading.color;
        }
        ui.set_list_sort_key(list, sort_key, false);
        refresh();
        if (auto* record = advisor()) {
            record->from_ui_panel = 1;
            record->delayed_message = 431;
            record->delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
        }
        if (host.panels.activate)
            host.panels.activate(root);
    }

    void body_layout(bool reset_position) {
        if (!root)
            return;
        auto &body = required(3009), &scroll = required(3017);
        if (body.text.empty()) {
            ui.set_visible(scroll, false);
            return;
        }
        if (!fonts)
            fonts = std::make_unique<UiFontLibrary>(game_root);
        const auto font = fonts->slot(7, pixel_width);
        if (!font)
            throw std::runtime_error("Message Center body font unavailable");
        const auto rect = body.attachment_rect;
        //552E30 measures at actual raster width, then converts its integer
        //bottom back to logical units. Scroll values count lines, not pixels.
        const auto physical_width =
            static_cast<std::int32_t>(static_cast<std::int64_t>(pixel_width) * rect.width() / 2048);
        UiTextStyle style;
        style.wrap = true;
        const auto metrics =
            fonts->measure(**font, body.text, { 0, 0, static_cast<float>(physical_width), 16384 }, style);
        if (!metrics)
            throw std::runtime_error("Message Center body layout failed");
        const auto content = static_cast<std::int32_t>(
            static_cast<std::int64_t>(fixed::truncate_integer(metrics->bounds[3])) * 1536 / pixel_height);
        const auto line = static_cast<std::int32_t>(1536u * (**font).line_advance / pixel_height);
        if (line <= 0)
            throw std::runtime_error("Message Center line advance is zero");
        if (content <= rect.height())
            ui.set_visible(scroll, false);
        else {
            ui.set_visible(scroll, true);
            //Native85DCAD takes(end,start); UiRuntime takes(start,end).
            ui.set_range(scroll, 0, (line + content - rect.height()) / line);
        }
        if (reset_position) {
            const auto changed = scroll.value != 0;
            ui.set_value(scroll, 0, false);
            if (changed)
                scroll_body();
        }
        body_resolution = std::array{ pixel_width, pixel_height };
    }

    void scroll_body() {
        auto& body = required(3009);
        if (!fonts)
            fonts = std::make_unique<UiFontLibrary>(game_root);
        const auto font = fonts->slot(7, pixel_width);
        if (!font)
            throw std::runtime_error("Message Center scroll font unavailable");
        body.text_offset = { 0,
                             fixed::narrow(
                                 -std::int64_t{ 1536 } * required(3017).value * (**font).line_advance / pixel_height) };
        body.text_offsets_explicit = true;
    }

    void project_preview(std::optional<WorldMailIdentity> key) {
        auto* message = key ? find_ui_mail(c, *key) : nullptr;
        auto& body = required(3009);
        caption(required(3016), message ? view(message->text_buffer_2) : std::u16string_view{});
        caption(required(3012), message ? view(message->text_buffer_1) : std::u16string_view{});
        caption(required(3014), message ? view(message->text_buffer_3) : std::u16string_view{});
        caption(body, message ? view(message->rendered_message) : std::u16string_view{}, 7, 0, 0);
        body_layout(true);
        const auto actionable = message && message->message_code != 0;
        ui.set_visible(required(3019), actionable);
        ui.set_visible(required(3018), actionable);
        ui.set_enabled(required(3004), message != nullptr);
        ui.set_visible(required(3010), message && message->auxiliary_field != 0);
        if (message && message->auxiliary_field)
            ui.set_attachment_part(required(3010), static_cast<std::uint16_t>(message->auxiliary_field - 1));
        if (actionable)
            help(529);
    }

    void select(std::int32_t index) {
        auto& list = required(3006);
        std::optional<WorldMailIdentity> key;
        if (index >= 0 && static_cast<std::size_t>(index) < list.rows.size())
            if (const auto found = keys.find(list.rows[static_cast<std::size_t>(index)].identity);
                found != keys.end() && find_ui_mail(c, found->second))
                key = found->second;
        selected = key;
        if (preview != key) {
            project_preview(key);
            consume(preview);
            preview = key;
        }
        last_index = index;
        deadline = c.ports.clock.get_simulation_clock_time_ms(c) + 2500u;
    }

    void refresh() {
        if (!root || refreshing)
            return;
        refreshing = true;
        c.messages.tag_panel_refresh_requested = 0;
        try {
            auto& list = required(3006);
            const auto previous = selected;
            const auto previous_index = list.selected_id;
            const auto rows = world_mail_ui_rows(c);
            std::vector<UiListRow> output;
            const auto now = ui_tick(ui);
            for (const auto& row : rows) {
                auto found = identities.find(row.key);
                if (found == identities.end()) {
                    if (next_identity == std::numeric_limits<std::intptr_t>::max())
                        throw std::length_error("Message Center row identity capacity");
                    found = identities.emplace(row.key, next_identity++).first;
                    keys.emplace(found->second, row.key);
                }
                UiListCell date{ fixed::word(row.packed_date) };
                date.display_text = row.date;
                output.push_back(
                    { found->second,
                      { row.subject, std::move(date) },
                      {},
                      std::array<std::uint8_t, 4>{ row.consumed ? std::uint8_t{ 192 } : std::uint8_t{ 255 },
                                                   row.consumed ? std::uint8_t{ 192 } : std::uint8_t{ 255 },
                                                   row.consumed ? std::uint8_t{ 192 } : std::uint8_t{ 255 },
                                                   255 } });
                blinks[row.key] = { false, now };
                if (auto* message = find_ui_mail(c, row.key)) {
                    const auto action = resolve_world_mail_ui_action(c, *message);
                    if (action && action->identity)
                        target_ids[row.key] = action->target;
                }
            }
            ui.set_list_rows(list, std::move(output), false, false);
            ui.sort_list_rows(list, false);
            auto index = list.rows.empty() ? -1 : 0;
            if (previous)
                if (const auto id = identities.find(*previous); id != identities.end()) {
                    if (const auto row = ui.find_list_row(list, id->second))
                        index = static_cast<std::int32_t>(*row);
                    else if (!list.rows.empty())
                        index = std::clamp(previous_index, 0, static_cast<std::int32_t>(list.rows.size() - 1));
                }
            ui.select_list_row(list, index, false, false);
            select(index);
            ui.set_visible(required(3020), list.rows.empty());
            ui.set_enabled(required(3004), !list.rows.empty());
            ui.refresh_list(list, false);
        } catch (...) {
            refreshing = false;
            throw;
        }
        refreshing = false;
    }

    void refresh_row_colors() {
        if (!root)
            return;
        const auto now = ui_tick(ui);
        for (auto& row : required(3006).rows) {
            const auto key = keys.find(row.identity);
            if (key == keys.end())
                continue;
            const auto* message = find_ui_mail(c, key->second);
            if (!message)
                continue;
            if (message->skip_counter_update) {
                row.color = std::array<std::uint8_t, 4>{ 192, 192, 192, 255 };
                continue;
            }
            if (std::bit_cast<std::int16_t>(message->counter_slot) <= 0)
                continue;
            auto& blink = blinks[key->second];
            if (std::bit_cast<std::int32_t>(now) <= std::bit_cast<std::int32_t>(blink.next))
                continue;
            if (blink.phase)
                row.color = std::array<std::uint8_t, 4>{ 255, 255, 255, 255 };
            else
                match (message->counter_slot) {
                    case 1 => row.color = std::array<std::uint8_t, 4>{ 0, 0, 255, 255 };
                    case 2 => row.color = std::array<std::uint8_t, 4>{ 255, 0, 0, 255 };
                    case 3 => row.color = std::array<std::uint8_t, 4>{ 0, 255, 0, 255 };
                    case _ => {
                        const auto value = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(message));
                        row.color = std::array<std::uint8_t, 4>{ value, value, value, 255 };
                    }
                }
            blink.phase = !blink.phase;
            blink.next = now + 300u;
        }
    }

    void advance() {
        if (!root)
            return;
        if (pending) {
            defer = true;
            deadline = 0;
            selected.reset();
            last_index = -1;
            (void)delete_ui_mail(c, *pending);
            pending.reset();
            defer = false;
            ui.set_enabled(required(3004), true);
        }
        if (deadline && deadline <= c.ports.clock.get_simulation_clock_time_ms(c))
            consume(selected);
        if (c.messages.tag_panel_refresh_requested || (selected && !find_ui_mail(c, *selected)))
            refresh();
        refresh_row_colors();
    }

    void execute() {
        if (!selected)
            return;
        auto* message = find_ui_mail(c, *selected);
        if (!message)
            return;
        auto action = resolve_world_mail_ui_action(c, *message);
        if (action && action->identity && retired_targets.contains(*selected))
            action.reset();
        const auto generation = action ? generations[action->target] : 0;
        consume(selected);
        close();
        if (action) {
            if (action->identity
                && (action->target >= c.entities.size()
                    || c.entities[action->target].payload != action->identity
                    || action->identity->retirement_pending
                    || generations[action->target] != generation))
                return;
            host.dispatch(*action);
        }
    }

    std::intptr_t event(UiControl& sender, const UiEvent& input) {
        if (closing)
            return ui.secondary(sender, input);
        if (input.type == 5
            || escape(input)
            || (input.type == 256
                && (input.id == static_cast<std::uint32_t>(-2) || input.id == 3001 || input.id == 3002))) {
            close();
            return 1;
        }
        if (input.type == 20 && &sender == root) {
            close();
            return 1;
        }
        if ((input.type == 256 && input.id == 3004) || (input.type == 65546 && input.id == 11776)) {
            ui.set_enabled(required(3004), false);
            pending = selected;
            if (!pending)
                ui.set_enabled(required(3004), true);
            return 1;
        }
        if (input.type == 256 && input.id == 3019) {
            execute();
            return 1;
        }
        if (input.type == 256 && input.id == 3021) {
            help(431);
            return 1;
        }
        if (input.type == 1025) {
            select(static_cast<std::int32_t>(input.value));
            return 1;
        }
        if (input.type == 1030) {
            sort_key = static_cast<std::int32_t>(input.value);
            return 1;
        }
        if (input.type == 1029 && last_index != -1) {
            const auto lower = static_cast<std::int32_t>(input.id), upper = static_cast<std::int32_t>(input.value);
            if (upper > lower)
                ui.select_list_row(required(3006), std::clamp(last_index, lower, upper - 1), true, false);
            return 1;
        }
        if (input.type == 2048 && input.id == 3017) {
            scroll_body();
            return 1;
        }
        return ui.secondary(sender, input);
    }
};

WorldMailUiController::WorldMailUiController(WorldRuntime& c,
                                             UiRuntime& ui,
                                             UiControl* primary,
                                             const std::filesystem::path& path,
                                             WorldMailUiHost host)
    : state_(std::make_unique<State>(c, ui, primary, path, std::move(host))) {}

WorldMailUiController::~WorldMailUiController() = default;

bool WorldMailUiController::command(WorldUiCommand command) {
    if (command != WorldUiCommand::mail)
        return false;
    open();
    return true;
}

void WorldMailUiController::open() {
    state_->open();
}

bool WorldMailUiController::is_open() const {
    return state_->root != nullptr;
}

void WorldMailUiController::close() {
    state_->close();
}

void WorldMailUiController::advance() {
    state_->advance();
}

void WorldMailUiController::handle_event(const WorldEvent& event) {
    if (const auto* retirement = std::get_if<RetirementEvent>(&event.value)) {
        auto& s = *state_;
        ++s.generations[retirement->retired_id];
        for (const auto& [key, id] : s.target_ids)
            if (id == retirement->retired_id)
                s.retired_targets.insert(key);
    }
}

void WorldMailUiController::append(UiDrawList&, UiControl& control, UiRect, std::uint32_t width, std::uint32_t height) {
    if (!width || !height)
        throw std::invalid_argument("Message Center requires nonzero raster dimensions");
    auto& s = *state_;
    s.pixel_width = width;
    s.pixel_height = height;
    if (s.root && control.id == 3009 && s.body_resolution != std::optional{ std::array{ width, height } })
        s.body_layout(false);
}
} //namespace simcoaster
