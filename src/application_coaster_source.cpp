#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/application_coaster_source.hpp"
#include "object_name_catalog.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
constexpr unsigned char source_template[] {
#embed "ui_templates/application_coaster_source.bin"
};
static_assert(sizeof(source_template) == 436);

const ResearchCatalogDefinition& definition(const WorldRuntime& c, std::uint16_t id) {
    for (const auto* entry : c.resources.object_catalog.entries) {
        if (entry && entry->id == id && entry->balance) {
            return *entry;
        }
    }
    throw std::invalid_argument("Coaster source definition is absent from selected catalog");
}

std::string lower(std::string name) {
    for (auto& letter : name) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return name;
}

std::array<std::u16string, 2> scanner_names(const std::string& filename) {
    auto name = std::filesystem::path { filename }.filename().u16string();
    if (name.size() < 4) {
        throw std::invalid_argument("Invalid original COS filename");
    }
    name.resize(name.size() - 4);
    const auto separator = name.find_first_of(u" -_");
    std::array<std::u16string, 2> result { name.substr(0, separator), {} };
    if (separator != name.npos) {
        result[1] = name.substr(separator + 1);
    }
    for (auto& part : result) {
        part.resize(std::min<std::size_t>(part.size(), 32));
    }
    return result;
}

void apply_scanner_names(CoasterDesign& design, const std::array<std::u16string, 2>& names) {
    design.name_a.fill(0);
    design.name_b.fill(0);
    for (std::size_t index = 0; index < names[0].size(); ++index) {
        design.name_a[index] = names[0][index];
    }
    for (std::size_t index = 0; index < names[1].size(); ++index) {
        design.name_b[index] = names[1][index];
    }
}
} //namespace

void validate_coaster_source_selection(const WorldRuntime& c, const ApplicationCoasterSourceSelection& selection) {
    if (selection.source_index < 0 || selection.source_index > 2 || lower(c.source.selected_resource_source_name) != theme_global_source_names[static_cast<std::size_t>(selection.source_index)]) {
        throw std::invalid_argument("Coaster source belongs to a different resource source");
    }
    const auto& entry = definition(c, selection.definition_id);
    if (!selection.definition_id || !is_coaster_catalog_definition(entry) || !entry.balance->Info.Shape) {
        throw std::invalid_argument("Invalid original coaster source definition");
    }
    const auto& shape = *entry.balance->Info.Shape;
    if (shape.width <= 0 || shape.width > 40 || shape.height <= 0 || shape.height > 40) {
        throw std::invalid_argument("Invalid original coaster source Shape");
    }
    if (selection.is_plain) {
        if (selection.design) {
            throw std::invalid_argument("Plain coaster source unexpectedly owns a COS design");
        }
        if (entry.balance->Prebuilt.IsPrebuiltCoaster) {
            const auto predefined = load_predefined_coaster_balance(entry);
            if (!predefined) {
                throw std::runtime_error("Coaster source prebuilt.sam is unavailable");
            }
            const auto& base = definition(c, static_cast<std::uint16_t>(entry.balance->Prebuilt.PrebuiltCoasterBase));
            if (!is_coaster_catalog_definition(base) || !base.balance->Info.Shape) {
                throw std::invalid_argument("Coaster source predefined base is unavailable");
            }
        }
        return;
    }
    if (!selection.design || selection.native_design_path.empty()) {
        throw std::invalid_argument("Stored coaster source has no original design");
    }
    const auto& design = *selection.design;
    if (design.definition_id != selection.definition_id
        || design.source_index != static_cast<std::uint32_t>(selection.source_index)
        || entry.balance->Prebuilt.IsPrebuiltCoaster
        || design.build_cost > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        || !design.grid_width
        || design.grid_width > 40
        || !design.grid_height
        || design.grid_height > 40
        || design.placement_offset_x < -127
        || design.placement_offset_x > 127
        || design.placement_offset_y < -127
        || design.placement_offset_y > 127) {
        throw std::invalid_argument("Invalid original COS source placement header");
    }
    for (const auto& row : design.geometry) {
        if (row.tile_offset_x < -127 || row.tile_offset_x > 127 || row.tile_offset_y < -127 || row.tile_offset_y > 127 || !std::isfinite(row.loft + row.heading + row.secondary_parameter + row.angle_b)) {
            throw std::invalid_argument("Invalid original COS source geometry");
        }
    }
}

void install_coaster_source_selection(WorldRuntime& c, const ApplicationCoasterSourceSelection& selection) {
    validate_coaster_source_selection(c, selection);
    c.construction.coaster_source_grid_bypass = 0;
    c.construction.coaster_source_is_plain = selection.is_plain ? 1 : 0;
    c.construction.coaster_design_name_object_id = selection.is_plain ? 0 : selection.definition_id;
    if (!selection.is_plain) {
        const auto& design = *selection.design;
        c.construction.coaster_source_picker_grid.width = static_cast<std::int32_t>(design.grid_width);
        c.construction.coaster_source_picker_grid.height = static_cast<std::int32_t>(design.grid_height);
        for (std::size_t index = 0; index < design.grid.size(); ++index) {
            c.construction.coaster_source_picker_grid.cells[index] = { std::bit_cast<std::int32_t>(design.grid[index].code), design.grid[index].attribute };
        }
        std::ranges::copy(design.name_a, c.construction.coaster_design_name_prefix.begin());
        std::ranges::copy(design.name_b, c.construction.coaster_design_name_suffix.begin());
    }
}

void begin_coaster_source_world_interaction(WorldRuntime& c, ParkConstructionRuntime& construction, const ApplicationCoasterSourceSelection& selection) {
    validate_coaster_source_selection(c, selection);
    if (c.mode.game != GameMode::code_3 || !c.construction.coaster_catalog) {
        throw std::logic_error("Coaster source activation requires its live designer owners");
    }
    install_coaster_source_selection(c, selection);
    //The host consumes a pending target at the native map-handler activation
    //boundary. Construction keeps all actual imported cells/geometry/accounting.
    if (selection.is_plain) {
        construction.begin_placement(c, selection.definition_id, 0);
    } else {
        c.construction.coaster_catalog->select_design(c, *selection.design, 0);
    }
}

struct ApplicationCoasterSourceController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary {}, *wrapper {}, *panel {}, *list {};
    ApplicationCoasterSourceHost host;
    std::filesystem::path root_path;
    std::string language;
    std::vector<std::byte> theme_names;
    std::vector<std::string> diagnostics;
    std::uint8_t allow_same {}, remember {};
    std::int32_t previous_source = -1, sort_key = 1;
    bool closing {}, paused {}, populated {};

    State(WorldRuntime& context, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, ApplicationCoasterSourceHost callbacks, std::string locale) : c(context), ui(runtime), primary(parent), host(std::move(callbacks)), root_path(path), language(std::move(locale)) {
        if (!ui.owns(primary) || !host.selected_source || !host.request_source || !host.begin_same_source || !host.pause || !host.gameplay_running || !host.resume_ui_transition || !host.map_source_recreation_flag || !host.cancel_profile_route) {
            throw std::invalid_argument("Coaster source picker requires its complete application host");
        }
    }

    ~State() {
        close();
    }

    UiControl& required(std::uint32_t id) {
        auto* control = ui.find_recursive(panel, id);
        if (!control) {
            throw std::runtime_error("Original Coaster Source template missing control " + std::to_string(id));
        }
        return *control;
    }

    std::u16string localized(std::span<const std::byte> data, std::uint32_t row) {
        const auto value = format_bfst_wide_string(data, c.messages.mail_language.conversion, row, 16383);
        if (!value) {
            throw std::runtime_error("Coaster source text: " + value.error().reason);
        }
        return *value;
    }

    std::u16string text(std::uint32_t row) {
        return localized(c.messages.mail_language.ui_text, row);
    }

    std::u16string theme(std::int32_t category) {
        if (theme_names.empty()) {
            const auto path = (root_path / "Data/Language" / language / "THEMENAMES.str").native_encoded_string();
            const auto source = c.ports.assets.read_balance_asset(c, path);
            if (!source) {
                throw std::runtime_error("Missing original CDK theme-name table");
            }
            const auto bytes = std::as_bytes(std::span(source->data(), source->size()));
            theme_names.assign(bytes.begin(), bytes.end());
        }
        constexpr std::array<std::uint32_t, 4> rows { 3, 0, 1, 2 };
        if (category < 0 || category >= static_cast<std::int32_t>(rows.size())) {
            throw std::invalid_argument("Invalid original CDK theme category");
        }
        return localized(theme_names, rows[static_cast<std::size_t>(category)]);
    }

    std::u16string source_name(std::uint16_t id) {
        if (find_object_definition_by_id(&c.resources.object_catalog, id)) {
            auto name = get_catalog_object_display_name(c, id);
            if (!name.empty()) {
                return name;
            }
        } else {
            const auto row = std::ranges::find(object_name_catalog::rows, id, &object_name_catalog::Row::definition_id);
            if (row != object_name_catalog::rows.end()) {
                auto first = localized(c.messages.mail_language.object_names, row->sign_a);
                const auto second = localized(c.messages.mail_language.object_names, row->sign_b);
                if (!first.empty() && first.back() == u'-') {
                    first.pop_back();
                } else if (!second.empty()) {
                    first += u' ';
                }
                return (first + second).substr(0, 64);
            }
        }
        for (const auto& filename : c.ports.assets.find_asset_files(c, "data/cdk registration/*.cdk")) {
            auto stream = c.ports.assets.open_asset_stream(c, "data/cdk registration/" + filename);
            CdkRegistrationRecord record;
            if (!stream || transfer_cdk_registration_record(record, *stream, false) || record.object_id != id) {
                continue;
            }
            auto name = std::filesystem::path(filename).filename().u16string();
            name.resize(std::min<std::size_t>(name.find(u'.'), name.size()));
            return name;
        }
        diagnostics.push_back("Original CDK source has no catalog or registration caption: " + std::to_string(id));
        return {};
    }

    void caption(UiControl& control, std::u16string_view value, std::uint32_t font, std::int32_t alignment = 1) {
        control.font = font;
        control.horizontal_alignment = alignment;
        control.vertical_alignment = 1;
        ui.set_text(control, value);
    }

    void close() {
        if (closing) {
            return;
        }
        closing = true;
        auto* old = std::exchange(wrapper, nullptr);
        auto* old_panel = std::exchange(panel, nullptr);
        list = nullptr;
        populated = false;
        if (ui.owns(old)) {
            ui.destroy(old);
        }
        if (host.panels.deactivate && old_panel) {
            host.panels.deactivate(old_panel);
        }
        if (std::exchange(paused, false)) {
            host.pause(false);
        }
        if (old_panel) {
            host.resume_ui_transition();
        }
        ui.set_input_root(primary);
        if (host.panels.synchronize_focus) {
            host.panels.synchronize_focus();
        } else {
            ui.set_focus(primary);
        }
        closing = false;
    }

    void cancel() {
        close();
        if (host.map_source_recreation_flag() == 1) {
            host.cancel_profile_route();
        }
    }

    void open(std::uint8_t same, std::uint8_t restore) {
        if (panel) {
            return;
        }
        if (host.panels.dismiss) {
            host.panels.dismiss();
        }
        allow_same = same;
        if (!same) {
            remember = restore;
            if (restore) {
                previous_source = host.selected_source();
            }
        }
        wrapper = &ui.create(nullptr, UiControlKind::window, 1, 1, { 0, 0, 2047, 1535 });
        wrapper->secondary_asset_hash = ui_model_asset_hash("lolight");
        const auto parsed = ui.parse_template(wrapper, std::as_bytes(std::span(source_template)));
        if (!parsed) {
            throw std::runtime_error("Original coaster source template: " + parsed.error().reason);
        }
        panel = ui.find_recursive(wrapper, 62000);
        if (!panel) {
            throw std::runtime_error("Original coaster source root is absent");
        }
        bind_ui_panel(ui, *panel, [this](auto& sender, const auto& value) {
            return event(sender, value);
        });
        ui.set_primary_callback(*wrapper, [this](UiControl& sender, const UiEvent& value) {
            if (value.type == 4 || value.type == 5) {
                cancel();
                return std::intptr_t {};
            }
            return ui.secondary(sender, value);
        });
        ui.place_modal_layer(*wrapper);
        caption(required(62003), text(62), 5);
        required(62003).primary_shadow_text = true;
        ui.set_enabled(required(62002), false);
        list = &required(62004);
        list->font = 7;
        list->row_height_font_padding = 6;
        list->list_highlight_asset_hash = ui_model_asset_hash("hilight");
        list->column_presentations.resize(3);
        constexpr std::array headings { 347u, 63u, 277u };
        for (std::size_t index = 0; index < 3; ++index) {
            auto& column = list->column_presentations[index];
            column.font = 7;
            column.format = index == 2 ? UiListCellFormat::signed_currency : UiListCellFormat::text;
            column.zero_blank = index == 2;
            column.horizontal_alignment = index == 2 ? 2 : 0;
            column.vertical_alignment = 1;
            auto& heading = *ui.find(list, static_cast<std::uint32_t>(16 + index));
            caption(heading, text(headings[index]), 8);
            heading.asset_hash = ui_model_asset_hash("!cbut");
            heading.text_constraints = { true, true };
            heading.help_index = index == 2 ? 27 : 15;
            for (std::size_t style = 0; style < 3; ++style) {
                heading.button_text_colors[style] = { 255, 239, 0, 255 };
            }
            ui.set_enabled(heading, heading.enabled);
        }
        if (host.gameplay_running()) {
            host.pause(true);
            paused = true;
        }
        ui.set_input_root(wrapper);
        ui.set_focus(panel);
        if (host.panels.activate) {
            host.panels.activate(panel);
        }
        refresh();
    }

    void refresh() {
        if (!list) {
            return;
        }
        c.construction.coaster_design_name_object_id = 0;
        std::vector<UiListRow> rows;
        auto& manager = get_or_create_theme_global_info_manager(c);
        if (manager.cdk_registration_count > manager.cdk_registrations.size()) {
            throw std::runtime_error("Invalid CDK registration extent");
        }
        std::optional<std::intptr_t> first;
        const auto selected = ui.selected_list_identity(*list);
        for (std::size_t index = 0; index < manager.cdk_registration_count; ++index) {
            const auto& record = manager.cdk_registrations[index];
            if (record.exclude_from_coaster_source_picker) {
                continue;
            }
            if (!first) {
                first = record.object_id;
            }
            rows.push_back({ record.object_id, { source_name(static_cast<std::uint16_t>(record.object_id)), theme(record.theme_category), record.source_picker_amount } });
            for (const auto& filename : c.ports.assets.find_asset_files(c, "data/my coasters/*.cos")) {
                const auto native_path = "data\\my coasters\\" + filename;
                auto stream = c.ports.assets.open_asset_stream(c, native_path);
                const auto design = stream ? read_coaster_design(*stream) : std::expected<CoasterDesign, CoasterDesignError> { std::unexpected(CoasterDesignError {}) };
                if (!design) {
                    diagnostics.push_back("Cannot read original source design: " + native_path);
                    continue;
                }
                if (!std::cmp_equal(design->definition_id, record.object_id)) {
                    continue;
                }
                const auto names = scanner_names(filename);
                UiListRow child { static_cast<std::intptr_t>(ui_model_asset_hash(native_path)), { names[0] + u" " + names[1], theme(static_cast<std::int32_t>(design->source_index)), std::bit_cast<std::int32_t>(design->build_cost) } };
                child.parent_identity = record.object_id;
                rows.push_back(std::move(child));
            }
        }
        ui.set_list_rows(*list, std::move(rows), true, false);
        ui.set_list_sort_key(*list, sort_key, false);
        const auto identity = selected ? selected : first;
        if (identity) {
            if (const auto row = ui.find_list_row(*list, *identity)) {
                ui.select_list_row(*list, static_cast<std::int32_t>(*row), false);
            }
        }
        ui.set_enabled(required(62002), list->selected_id != -1);
        populated = true;
    }

    void choose() {
        if (!list || list->selected_id < 0 || static_cast<std::size_t>(list->selected_id) >= list->rows.size()) {
            return;
        }
        const auto row = list->rows[static_cast<std::size_t>(list->selected_id)];
        const auto parent = ui.parent_list_row(*list, static_cast<std::size_t>(list->selected_id));
        const auto full_id = parent ? list->rows[*parent].identity : row.identity;
        ApplicationCoasterSourceSelection selection;
        selection.definition_id = static_cast<std::uint16_t>(full_id);
        selection.is_plain = !parent;
        selection.remember_current_source = remember;
        selection.previous_source_index = previous_source;
        if (!parent) {
            const auto& manager = get_or_create_theme_global_info_manager(c);
            const auto registrations = std::span(manager.cdk_registrations).first(manager.cdk_registration_count);
            const auto found = std::ranges::find(registrations, full_id, &CdkRegistrationRecord::object_id);
            if (found == registrations.end()) {
                diagnostics.push_back("Original CDK registration disappeared before selection");
                return;
            }
            selection.source_index = found->theme_category;
        } else {
            for (const auto& filename : c.ports.assets.find_asset_files(c, "data/my coasters/*.cos")) {
                const auto path = "data\\my coasters\\" + filename;
                if (static_cast<std::intptr_t>(ui_model_asset_hash(path)) != row.identity) {
                    continue;
                }
                auto stream = c.ports.assets.open_asset_stream(c, path);
                if (!stream) {
                    continue;
                }
                auto design = read_coaster_design(*stream);
                const auto names = scanner_names(filename);
                if (!design || design->definition_id != selection.definition_id || names[0] + u" " + names[1] != row.cells[0].text()) {
                    continue;
                }
                apply_scanner_names(*design, names);
                selection.source_index = static_cast<std::int32_t>(design->source_index);
                selection.native_design_path = path;
                selection.design = std::move(*design);
                break;
            }
            if (!selection.design) {
                //Native would reuse stale scanner/path outputs here. A removed or
                //replaced COS cannot silently select an unrelated previous design.
                diagnostics.push_back("Selected original COS changed or disappeared");
                refresh();
                return;
            }
        }
        selection.detach_profile = host.map_source_recreation_flag() == 1;
        if (allow_same && selection.source_index == host.selected_source()) {
            if (!host.begin_same_source(selection)) {
                return;
            }
            close();
        } else {
            close();
            selection.detach_profile = host.map_source_recreation_flag() == 1;
            host.request_source(std::move(selection));
        }
        c.ports.ui.clear_control_particle_group(c);
    }

    std::intptr_t event(UiControl& sender, const UiEvent& value) {
        if (value.type == 20 && &sender == panel) {
            if (!closing) {
                close();
            }
            return ui.secondary(sender, value);
        }
        const auto key = value.id > 255 ? value.id >> 8 : value.id;
        if (value.type == 5 || (value.type == 65547 && key == 27) || (value.type == 256 && (value.id == 62001 || value.id == 4294967294u))) {
            cancel();
            return 0;
        }
        if (value.type == 21 && &sender == panel && !populated) {
            refresh();
        } else if (value.type == 1025) {
            ui.set_enabled(required(62002), value.value != -1);
        } else if (value.type == 1030) {
            sort_key = static_cast<std::int32_t>(value.value);
        } else if (value.type == 1024 || (value.type == 256 && value.id == 62002)) {
            choose();
            return 0;
        }
        return ui.secondary(sender, value);
    }
};

ApplicationCoasterSourceController::ApplicationCoasterSourceController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& root, ApplicationCoasterSourceHost host, std::string language) : state_(std::make_unique<State>(c, ui, primary, root, std::move(host), std::move(language))) {}

ApplicationCoasterSourceController::~ApplicationCoasterSourceController() = default;

void ApplicationCoasterSourceController::open(std::uint8_t same, std::uint8_t remember) {
    state_->open(same, remember);
}

void ApplicationCoasterSourceController::refresh() {
    state_->refresh();
}

void ApplicationCoasterSourceController::restore_return_policy(std::uint8_t remember, std::int32_t source) {
    state_->remember = remember;
    state_->previous_source = source;
}

void ApplicationCoasterSourceController::close() {
    state_->close();
}

bool ApplicationCoasterSourceController::is_open() const {
    return state_->panel;
}

UiControl* ApplicationCoasterSourceController::root() const {
    return state_->panel;
}

const std::vector<std::string>& ApplicationCoasterSourceController::diagnostics() const {
    return state_->diagnostics;
}
} //namespace simcoaster
