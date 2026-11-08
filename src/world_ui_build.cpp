#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/world_ui_build.hpp"
#include "object_name_catalog.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_object_preview.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "world_ui_build_catalog.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char build_template[] {
#embed "ui_templates/attraction_build.bin"
};
constexpr unsigned char coaster_template[] {
#embed "ui_templates/coaster_ready_made.bin"
};
static_assert(sizeof(build_template) == 1414 && sizeof(coaster_template) == 418);
constexpr std::array<std::int32_t, 5> categories { 0, 1, 2, 3, 5 };
constexpr std::array<std::uint32_t, 5> category_buttons { 13023, 13022, 13021, 13024, 13025 };
constexpr std::array<std::uint32_t, 4> weather_controls { 13007, 13008, 13009, 13017 };

std::uint32_t draw_index(std::size_t value) {
    if (!std::in_range<std::uint32_t>(value)) {
        throw std::length_error("Build UI draw index capacity exceeded");
    }
    return static_cast<std::uint32_t>(value);
}
} //namespace

struct WorldBuildUiController::State {
    enum class Page {
        none,
        build,
        ready_made
    };
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary {}, *root {}, *list {};
    std::filesystem::path game_root;
    WorldUiPanelHost host;
    Page page {};
    const World* bound_world {};
    std::string bound_source;
    std::vector<UiControl*> owners;
    std::int32_t category = -1, sort_key = 1;
    std::uint16_t selected {}, base_ride {};
    bool closing {};
    UiObjectPreview preview;
    TextureImagePtr footprint;
    std::array<std::uint32_t, 2> footprint_resolution {};
    std::map<std::string, std::vector<std::byte>> language;
    std::vector<ReadyMadeCoasterItem> designs;
    std::vector<std::string> diagnostics;

    State(WorldRuntime& context, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, WorldUiPanelHost panel_host) : c(context), ui(runtime), primary(parent), game_root(path), host(std::move(panel_host)), preview(c, path) {
        if (!ui.owns(primary)) {
            throw std::invalid_argument("Build UI requires its persistent primary root");
        }
    }

    ~State() {
        close();
    }

    UiControl& required(std::uint32_t id) {
        auto* control = ui.find_recursive(root, id);
        if (!control) {
            throw std::runtime_error(std::format("Original Build template missing control {}", id));
        }
        return *control;
    }

    std::u16string localized(std::span<const std::byte> data, std::uint32_t row) {
        const auto result = format_bfst_wide_string(data, c.messages.mail_language.conversion, row, 16383);
        if (!result) {
            throw std::runtime_error(std::format("Build UI text {}: {}", row, result.error().reason));
        }
        return *result;
    }

    std::u16string text(std::uint32_t row) {
        return localized(c.messages.mail_language.ui_text, row);
    }

    const std::vector<std::byte>& table(std::string name) {
        if (const auto found = language.find(name); found != language.end()) {
            return found->second;
        }
        const auto path = (game_root / "Data/Language/American" / name).native_encoded_string();
        const auto contents = c.ports.assets.read_balance_asset(c, path);
        if (!contents) {
            throw std::runtime_error(std::format("Missing original Build language table: {}", path));
        }
        const auto raw = std::as_bytes(std::span { contents->data(), contents->size() });
        return language.emplace(std::move(name), std::vector<std::byte> { raw.begin(), raw.end() }).first->second;
    }

    void caption(UiControl& control, std::u16string_view value, std::uint32_t font = 6, std::int32_t alignment = 0) {
        control.font = font;
        control.horizontal_alignment = alignment;
        control.vertical_alignment = 1;
        ui.set_text(control, value);
    }

    AdvisorRecord* advisor() {
        const auto* world = c.simulation.current;
        if (!world || !world->saved.advisor) {
            return nullptr;
        }
        auto* header = c.entities.at(world->saved.advisor).payload;
        if (!header || header->kind != ThingKind::advisor) {
            throw std::logic_error("Build UI Advisor identity is invalid");
        }
        return reinterpret_cast<AdvisorRecord*>(header);
    }

    void stage_advisor(std::int32_t action) {
        if (auto* owner = advisor()) {
            owner->from_ui_panel = 1;
            owner->delayed_message = action;
            owner->delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
        }
    }

    void cancel_advisor(std::int32_t action) {
        if (auto* owner = advisor()) {
            owner->from_ui_panel = owner->panel_up = 0;
            world_operations::cancel_advisor_action_by_id(c, owner, action);
        }
    }

    void reject() {
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 29, 0, 0, 0);
    }

    std::int32_t cash() {
        if (!c.simulation.current) {
            throw std::logic_error("Build UI has no World");
        }
        const auto* header = c.entities.at(c.simulation.current->saved.bank_account).payload;
        if (!header || header->kind != ThingKind::bank_account) {
            throw std::logic_error("Build UI has no live bank");
        }
        return reinterpret_cast<const BankRecord*>(header)->balance;
    }

    void parse(std::span<const unsigned char> bytes, std::uint32_t id) {
        const auto previous = primary->children;
        const auto result = ui.parse_template(primary, std::as_bytes(bytes));
        if (!result) {
            throw std::runtime_error(std::format("Original Build template word {}: {}", result.error().word, result.error().reason));
        }
        for (auto* child : primary->children) {
            if (std::ranges::find(previous, child) == previous.end()) {
                owners.push_back(child);
                bind_ui_panel(
                    ui,
                    *child,
                    [this](auto& sender, const auto& value) {
                        return event(sender, value);
                    },
                    child->id == id);
                if (child->id == id) {
                    root = child;
                }
            }
        }
        if (!root) {
            throw std::runtime_error("Original Build template has no requested root");
        }
    }

    void close() {
        if (closing) {
            return;
        }
        closing = true;
        auto* old_root = root;
        const auto old_page = page;
        preview.clear();
        footprint.reset();
        selected = 0;
        root = list = nullptr;
        page = Page::none;
        bound_world = nullptr;
        const auto old_owners = std::exchange(owners, {});
        for (auto* control : old_owners) {
            if (ui.owns(control)) {
                ui.destroy(control);
            }
        }
        if (old_page != Page::none) {
            cancel_advisor(old_page == Page::build ? 150 : 623);
        }
        if (old_root && host.deactivate) {
            host.deactivate(old_root);
        }
        closing = false;
    }

    void begin(Page next) {
        close();
        if (host.dismiss) {
            host.dismiss();
        }
        page = next;
        bound_world = c.referenced_world();
        bound_source = c.source.selected_resource_source_name;
    }

    bool reconcile() {
        if (!root) {
            return false;
        }
        if (bound_world != c.referenced_world() || bound_source != c.source.selected_resource_source_name) {
            close();
            return false;
        }
        return true;
    }

    void configure_list(UiControl& control, std::span<const std::uint32_t> headings, std::span<const UiListCellFormat> formats) {
        control.font = 7;
        control.row_height_font_padding = 6;
        control.list_highlight_asset_hash = ui_model_asset_hash("hilight");
        control.column_presentations.resize(formats.size());
        for (std::size_t i = 0; i < formats.size(); ++i) {
            auto& column = control.column_presentations[i];
            column.format = formats[i];
            column.font = 7;
            column.horizontal_alignment = i == 1 ? 2 : 0;
            column.vertical_alignment = 1;
            column.zero_blank = i == 1;
            if (formats[i] == UiListCellFormat::model_state) {
                column.model_asset_hash = ui_model_asset_hash("i_boxtick");
            }
            if (auto* heading = ui.find(&control, static_cast<std::uint32_t>(16 + i))) {
                caption(*heading, text(headings[i]), 8, 1);
                //503090 installs the original beveled button model independently
                //of the template, then refreshes the secondary caption after setting
                //all three enabled-state text colors to opaque white.
                heading->asset_hash = ui_model_asset_hash("!cbut");
                heading->color = { 255, 255, 255, 255 };
                heading->text_constraints = { true, true };
                for (std::size_t style = 0; style < 3; ++style) {
                    heading->button_text_colors[style] = heading->color;
                }
            }
        }
        ui.set_list_sort_key(control, page == Page::build ? sort_key : 1, false);
    }

    void rebuild() {
        std::vector<UiListRow> rows;
        for (const auto& item : build_catalog_items(c, static_cast<ObjectUiCategory>(category))) {
            UiListRow row { item.definition, { item.name, item.cost, item.ownership, item.recent_research } };
            if (item.unique_owned) {
                row.color = RgbaPixel { 150, 150, 150, 255 };
            }
            row.cells[2].help_index = item.ownership == 1 ? 283 : 284;
            if (item.recent_research == 2) {
                row.cells[3].help_index = 282;
            }
            rows.push_back(std::move(row));
            const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, item.definition);
            if (definition->which_track_type == 3) {
                const auto ready = scan_ready_made_coasters(c, game_root, item.definition, diagnostics);
                if (!ready.empty()) {
                    //Exactly one synthetic child opens the full ready-made picker. Its
                    //opaque key is the first matching filename hash, not a definition.
                    UiListRow child { static_cast<std::intptr_t>(ready.front().path_hash), { text(16), 0, 0, 0 } };
                    child.parent_identity = item.definition;
                    child.cells[2].help_index = 284;
                    rows.push_back(std::move(child));
                }
            }
        }
        ui.set_list_rows(*list, std::move(rows), false, false);
        ui.set_list_sort_key(*list, sort_key, false);
        const auto first = list->rows.empty() ? -1 : 0;
        ui.select_list_row(*list, first, false, false);
        select(first);
    }

    void category_details() {
        caption(required(13030), text(271u + static_cast<std::uint32_t>(std::ranges::find(categories, category) - categories.begin())), 5);
        required(13030).primary_shadow_text = true;
        ui.set_selected(required(13020), static_cast<std::int32_t>(category_buttons.at(static_cast<std::size_t>(std::ranges::find(categories, category) - categories.begin()))), false);
        caption(required(13010), category == 0 || category == 2 ? text(280) : u"");
        caption(required(13013), category == 0 ? text(282) : u"");
        caption(required(13016), category == 0 ? text(283) : u"");
        caption(required(13014), category == 0 ? text(281) : u"");
        for (const auto id : { 13004u, 13005u, 13006u }) {
            auto& bar = required(id);
            const auto use_meter = category == 0 || (category == 2 && id == 13004);
            bar.meter_presentation = use_meter ? std::optional { UiMeterPresentation { "ridestatbar.tga", { 0, 0 } } } : std::nullopt;
            caption(bar, {});
            ui.set_range(bar, 0, 1024);
            ui.set_value(bar, 0);
        }
        caption(required(13015), {});
    }

    void open_build(std::int32_t requested, std::uint16_t restore) {
        if (root) {
            return;
        }
        if (requested == -1) {
            requested = category == -1 ? 0 : category;
        }
        if (std::ranges::find(categories, requested) == categories.end()) {
            throw std::invalid_argument("Unsupported original Build category");
        }
        begin(Page::build);
        category = requested;
        parse(build_template, 13000);
        list = &required(13026);
        constexpr std::array<std::uint32_t, 4> headings { 276, 277, 278, 278 };
        constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::signed_currency, UiListCellFormat::model_state, UiListCellFormat::model_state };
        configure_list(*list, headings, formats);
        constexpr std::array<std::uint32_t, 4> helps { 15, 27, 28, 29 };
        for (std::size_t i = 0; i < helps.size(); ++i) {
            required(static_cast<std::uint32_t>(16 + i)).help_index = helps[i];
        }
        required(13001).help_index = 13;
        caption(required(13011), text(279));
        caption(required(13012), {});
        caption(required(13018), {});
        required(13018).text_constraints = { true, true };
        caption(required(13029), {}, 6, 1);
        required(13029).text_constraints = { true, true };
        //57AC20 calls501FE0 once for each standalone bar. The constructor's
        //full control rectangle includes ten logical units above and below the
        //actual strip; category changes replace the attachment, not its bounds.
        for (const auto id : { 13004u, 13005u, 13006u }) {
            auto& bar = required(id);
            bar.attachment_rect = bar.rect;
            bar.attachment_rect.top += 10;
            bar.attachment_rect.bottom -= 10;
        }
        ui.set_depth(required(13028), required(13028).render_depth + 12);
        ui.set_depth(required(13029), required(13029).render_depth + 12);
        category_details();
        rebuild();
        if (restore) {
            if (const auto index = ui.find_list_row(*list, restore)) {
                ui.select_list_row(*list, static_cast<std::int32_t>(*index), false);
                select(static_cast<std::int32_t>(*index));
            }
        }
        stage_advisor(150);
        if (host.activate) {
            host.activate(root);
        }
    }

    void select(std::int32_t visible_row) {
        selected = 0;
        if (list && visible_row >= 0 && static_cast<std::size_t>(visible_row) < list->rows.size()) {
            const auto parent = ui.parent_list_row(*list, visible_row);
            const auto& row = list->rows[parent ? *parent : visible_row];
            selected = static_cast<std::uint16_t>(row.identity);
        }
        preview.clear();
        footprint.reset();
        constexpr std::array<std::uint32_t, 12> details { 13012, 13004, 13005, 13006, 13007, 13008, 13009, 13017, 13015, 13018, 13028, 13029 };
        for (const auto id : details) {
            ui.set_visible(required(id), selected != 0);
        }
        if (!selected) {
            return;
        }
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, selected);
        if (!definition || !definition->balance) {
            throw std::logic_error("Build selection lost its definition");
        }
        const auto& balance = *definition->balance;
        const auto* control = find_world_object_control(c.simulation.current->object_controls, selected);
        caption(required(13012), control && control->live_instance_count ? number_text(control->live_instance_count) : text(284));
        if (category == 0 || category == 2) {
            ui.set_value(required(13004), balance.UsageInfo.ExcitementLevel * 1024 / 100);
        }
        if (category == 0) {
            const auto& upgrade = balance.Upgrades[0];
            const auto speed_ratio = static_cast<double>(upgrade.InitSpeed) / balance.UsageInfo.MaxSpeed;
            const auto scaled_speed = static_cast<float>(speed_ratio * static_cast<double>(.89999998f));
            //57BD00 compares locally loaded InitSpeed with RedLineSpeed. No
            //incoming floating parameter or undefined condition belongs here.
            const auto speed = upgrade.InitSpeed < upgrade.RedLineSpeed ? static_cast<double>(scaled_speed) + static_cast<double>(.1f) : (static_cast<double>(scaled_speed) + static_cast<double>(1.1f)) * .5;
            const auto capacity = static_cast<double>(upgrade.InitCapacity) / balance.UsageInfo.MaxCapacity * static_cast<double>(.89999998f) + static_cast<double>(.1f);
            const auto wear = (capacity + speed) * upgrade.WearRate * 4.5;
            const auto truncated = std::isfinite(wear) ? fixed::truncate_integer(wear) : 0;
            ui.set_value(required(13005), (100 - static_cast<std::uint8_t>(truncated)) * 1024 / 100);
            ui.set_value(required(13006), balance.Info.WorkingLife * 1024 / 100);
            caption(required(13015), number_text(static_cast<std::uint32_t>(upgrade.RedLineCapacity)));
        }
        const auto& weather = balance.WeatherModifier;
        const std::array values { weather.RainPercentage, weather.SnowPercentage, weather.HeatWavePercentage, weather.StormPercentage };
        constexpr std::array<std::uint32_t, 4> below { 270, 272, 269, 271 };
        constexpr std::array<std::uint32_t, 4> above { 266, 268, 265, 267 };
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto& metric = required(weather_controls[i]);
            ui.set_visible(metric, values[i] != 0 && values[i] != 100);
            if (values[i] != 0 && values[i] != 100) {
                ui.set_attachment_part(metric, values[i] > 100 ? 0 : 1);
                metric.help_index = values[i] > 100 ? above[i] : below[i];
            }
        }
        if (const auto mapping = std::ranges::find(object_name_catalog::rows, selected, &object_name_catalog::Row::definition_id); mapping != object_name_catalog::rows.end()) {
            caption(required(13018), localized(table("OBJECT_DESC.str"), mapping->description));
        } else {
            caption(required(13018), {});
        }
        caption(required(13029), localized(table("ITEMTYPES.str"), static_cast<std::uint32_t>(balance.Info.RideTypeStringIndex)), 6, 1);
        (void)preview.bind_definition(selected, balance.UsageInfo.RequiresWatering ? 12 : 5);
    }

    void open_ready_made(std::uint16_t base) {
        if (root) {
            return;
        }
        begin(Page::ready_made);
        base_ride = base;
        parse(coaster_template, 111000);
        list = &required(111002);
        caption(required(111003), text(14), 5);
        required(111003).primary_shadow_text = true;
        ui.set_enabled(required(111001), false);
        constexpr std::array<std::uint32_t, 2> headings { 12, 13 };
        constexpr std::array formats { UiListCellFormat::text, UiListCellFormat::signed_currency };
        configure_list(*list, headings, formats);
        required(16).help_index = 15;
        required(17).help_index = 27;
        designs = scan_ready_made_coasters(c, game_root, base, diagnostics);
        std::vector<UiListRow> rows;
        for (const auto& design : designs) {
            rows.push_back({ static_cast<std::intptr_t>(design.path_hash), { design.name, design.cost } });
        }
        ui.set_list_rows(*list, std::move(rows), false, false);
        ui.set_list_sort_key(*list, 1, false);
        if (!list->rows.empty()) {
            ui.select_list_row(*list, 0, false);
            ui.set_enabled(required(111001), true);
        }
        stage_advisor(623);
        if (host.activate) {
            host.activate(root);
        }
    }

    bool activate(std::int32_t index) {
        if (!list || index < 0 || static_cast<std::size_t>(index) >= list->rows.size()) {
            return false;
        }
        const auto row = list->rows[index];
        if (row.cells.size() < 2 || !row.cells[1].numeric()) {
            throw std::logic_error("Build row has no numeric purchase quote");
        }
        const auto cost = row.cells[1].number();
        if (cost > cash()) {
            reject();
            return false;
        }
        if (page == Page::build) {
            if (const auto parent = ui.parent_list_row(*list, index)) {
                const auto base = static_cast<std::uint16_t>(list->rows[*parent].identity);
                close();
                open_ready_made(base);
                return true;
            }
            const auto definition = static_cast<std::uint16_t>(row.identity);
            const auto* record = find_object_definition_by_id(&c.resources.object_catalog, definition);
            const auto* control = find_world_object_control(c.simulation.current->object_controls, definition);
            if (!record || !record->balance || !control) {
                throw std::logic_error("Activated Build row lost its catalog owner");
            }
            if (record->balance->UsageInfo.SingleInstance == 1 && control->live_instance_count) {
                reject();
                return false;
            }
            if (!host.begin_object_build) {
                throw std::logic_error("Build placement host is not bound");
            }
            if (!host.begin_object_build(definition)) {
                reject();
                return false;
            }
            close();
            return true;
        }
        //Native52D030 rechecks both filename-derived caption and path hash.
        //Hash alone is an opaque UI identity, not permission to load another file.
        const auto fresh = scan_ready_made_coasters(c, game_root, base_ride, diagnostics);
        const auto found = std::ranges::find_if(fresh, [&](const auto& candidate) {
            return candidate.path_hash == static_cast<std::uint32_t>(row.identity) && row.cells[0].text() == candidate.name;
        });
        if (found == fresh.end()) {
            return false;
        }
        if (!host.begin_coaster_design) {
            throw std::logic_error("Ready-made placement host is not bound");
        }
        if (!host.begin_coaster_design(found->path, cost)) {
            return false;
        }
        close();
        return true;
    }

    std::intptr_t event(UiControl& sender, const UiEvent& value) {
        if (closing || !root) {
            return ui.secondary(sender, value);
        }
        if (!reconcile()) {
            return 0;
        }
        if ((value.type == 4 || value.type == 20 || value.type == 5) && &sender == root) {
            close();
            return 0;
        }
        if (&sender == root && value.type == 65547 && (value.id > 255 ? value.id >> 8 : value.id) == 27 && (static_cast<std::uint32_t>(value.value) & 0xFF0000u) == 0) {
            close();
            return 0;
        }
        if (page == Page::build) {
            if (value.type == 257 && value.value == 1) {
                const auto found = std::ranges::find(category_buttons, value.id);
                if (found != category_buttons.end()) {
                    category = categories[found - category_buttons.begin()];
                    category_details();
                    rebuild();
                    return 0;
                }
            }
            if (value.type == 1025) {
                select(static_cast<std::int32_t>(value.value));
                return 0;
            }
            if (value.type == 1030) {
                sort_key = static_cast<std::int32_t>(value.value);
                return 0;
            }
            if (value.type == 256) {
                if (value.id == 13002 || value.id == 4294967294u) {
                    close();
                    return 0;
                }
                if (value.id == 13001) {
                    (void)activate(list->selected_id);
                    return 0;
                }
                if (value.id == 13031) {
                    queue_world_advisor_help_action(c, 150);
                    return 0;
                }
            }
        } else {
            if (value.type == 1025) {
                ui.set_enabled(required(111001), value.value != -1);
                return 0;
            }
            if (value.type == 256) {
                if (value.id == 111004 || value.id == 4294967294u) {
                    close();
                    open_build(-1, 0);
                    return 0;
                }
                if (value.id == 111001) {
                    (void)activate(list->selected_id);
                    return 0;
                }
                if (value.id == 111005) {
                    queue_world_advisor_help_action(c, 623);
                    return 0;
                }
            }
        }
        if (value.type == 1024) {
            (void)activate(static_cast<std::int32_t>(value.value));
            return 0;
        }
        return ui.secondary(sender, value);
    }

    void make_footprint(std::uint32_t pixel_width, std::uint32_t pixel_height) {
        const auto& area = required(13028).attachment_rect;
        const auto width = static_cast<std::uint32_t>(std::max<std::int64_t>(1, std::int64_t { area.width() } * pixel_width / 2048));
        const auto height = static_cast<std::uint32_t>(std::max<std::int64_t>(1, std::int64_t { area.height() } * pixel_height / 1536));
        if (footprint && footprint_resolution == std::array { width, height }) {
            return;
        }
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, selected);
        if (!definition || !definition->balance || !definition->balance->Info.Shape) {
            throw std::logic_error("Build footprint has no original placement shape");
        }
        if (width > 8192 || height > 8192) {
            throw std::length_error("Build footprint surface is too large");
        }
        auto surface = std::make_shared<TextureImage>();
        surface->width = width;
        surface->height = height;
        surface->alpha = TextureAlpha::translucent;
        surface->source_mode = TextureSourceMode::alpha_low;
        surface->pixels.assign(std::size_t { width } * height, RgbaPixel {});
        const auto& shape = *definition->balance->Info.Shape;
        const auto cell_width = static_cast<std::int32_t>(width / 8);
        const auto cell_height = static_cast<std::int32_t>(height / 8);
        //Native66D2D0 reduces the actual shape to blue footprint, green entrance,
        //orange exit.57C590 draws inclusive rectangles into an8x8 surface grid,
        //with the sourceY axis rising from the bottom of the bitmap.
        for (std::int32_t x = 0; x < shape.width; ++x) {
            for (std::int32_t y = 0; y < shape.height; ++y) {
                const auto code = shape.cells.at(static_cast<std::size_t>(y * 40 + x)).code;
                RgbaPixel color {};
                if (code == 1 || code == 4 || code == 23) {
                    color = { 30, 170, 255, 128 };
                } else if (code == 9) {
                    color = { 15, 220, 50, 128 };
                } else if (code == 10) {
                    color = { 220, 100, 15, 128 };
                } else {
                    continue;
                }
                const auto left = x * cell_width;
                const auto right = std::min((x + 1) * cell_width, static_cast<std::int32_t>(width) - 1);
                const auto bottom = static_cast<std::int32_t>(height) - y * cell_height - 1;
                const auto top = std::max(0, bottom - cell_height);
                for (auto py = top; py <= bottom && py < static_cast<std::int32_t>(height); ++py) {
                    for (auto px = left; px <= right; ++px) {
                        surface->pixels.at(static_cast<std::size_t>(py) * width + px) = color;
                    }
                }
            }
        }
        footprint = std::move(surface);
        footprint_resolution = { width, height };
    }

    void append_footprint(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
        if (!selected) {
            return;
        }
        make_footprint(width, height);
        const auto r = control.attachment_rect;
        clip = { std::max({ clip.left, r.left, 0 }), std::max({ clip.top, r.top, 0 }), std::min({ clip.right, r.right, 2048 }), std::min({ clip.bottom, r.bottom, 1536 }) };
        if (clip.width() <= 0 || clip.height() <= 0) {
            return;
        }
        const auto base = draw_index(draw.vertices.size());
        //57C590/84B7F1 draw into the SECONDARY surface attachment;511F90
        //clamps its depth before6FCB00 submits it using projected mode2.
        const auto z = ui_attachment_depth(std::max(control.secondary_placement.depth, 1)) * .1f;
        const auto left = static_cast<float>(r.left), top = static_cast<float>(r.top), right = static_cast<float>(r.right), bottom = static_cast<float>(r.bottom);
        draw.vertices.insert(draw.vertices.end(), { UiDrawVertex { { left, top, z }, { 0, 0 } }, UiDrawVertex { { right, top, z }, { 1, 0 } }, UiDrawVertex { { right, bottom, z }, { 1, 1 } }, UiDrawVertex { { left, bottom, z }, { 0, 1 } } });
        UiDrawCommand command;
        command.texture = footprint;
        //Native57C590 clears to transparent0 and paints cells with alpha128.
        //Tiled surface submission6FCD75 enables blend+alpha test+depth writes;
        //rejecting transparent texels preserves objects behind the empty cells.
        command.material.flags = 0x07060556u;
        command.depth_buffered = true;
        command.first_index = draw_index(draw.indices.size());
        command.index_count = 6;
        command.clip = { static_cast<float>(clip.left), static_cast<float>(clip.top), static_cast<float>(clip.right), static_cast<float>(clip.bottom) };
        draw.indices.insert(draw.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        draw.commands.push_back(std::move(command));
    }
};

WorldBuildUiController::WorldBuildUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& root, WorldUiPanelHost host) : state_(std::make_unique<State>(c, ui, primary, root, std::move(host))) {}

WorldBuildUiController::~WorldBuildUiController() = default;

bool WorldBuildUiController::command(WorldUiCommand command) {
    if (command != WorldUiCommand::attraction_build) {
        return false;
    }
    state_->open_build(-1, 0);
    return true;
}

void WorldBuildUiController::open_build(std::int32_t category, std::uint16_t selected) {
    state_->open_build(category, selected);
}

void WorldBuildUiController::open_ready_made(std::uint16_t base) {
    state_->open_ready_made(base);
}

bool WorldBuildUiController::open() const {
    return state_->root != nullptr;
}

void WorldBuildUiController::close() {
    state_->close();
}

void WorldBuildUiController::advance() {
    if (state_->reconcile()) {
        state_->preview.advance();
    }
}

void WorldBuildUiController::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    auto& s = *state_;
    //Drawing may suppress stale art, but must never destroy the control tree
    //currently being traversed. Frame/input reconciliation owns that closure.
    if (!s.root || s.bound_world != s.c.referenced_world() || s.bound_source != s.c.source.selected_resource_source_name || s.page != State::Page::build || !control.visible()) {
        return;
    }
    if (&control == &s.required(13027) && s.selected) {
        s.preview.append(draw, control, clip, width, height, 10);
    } else if (&control == &s.required(13028)) {
        s.append_footprint(draw, control, clip, width, height);
    }
}

const std::vector<std::string>& WorldBuildUiController::diagnostics() const {
    auto& s = *state_;
    for (const auto& message : s.preview.diagnostics()) {
        if (std::ranges::find(s.diagnostics, message) == s.diagnostics.end()) {
            s.diagnostics.push_back(message);
        }
    }
    return s.diagnostics;
}
} //namespace simcoaster
