#include "simcoaster/standard_balance.hpp"
#include "simcoaster/sam_integer.hpp"
#include "simcoaster/sprite_geometry.hpp"
#include "simcoaster/terrain_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/world_visitor_ui.hpp"
#include "world_ui_park_map_internal.hpp"
#include "simcoaster/world_advisor.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char map_template[] {
#embed "ui_templates/app_park_map.bin"
};
static_assert(sizeof(map_template) == 1694);
constexpr std::array<std::uint32_t, 7> heat_rows { 473, 39, 474, 475, 476, 477, 472 };
constexpr std::array<std::uint32_t, 7> staff_rows { 478, 479, 480, 481, 482, 483, 472 };
constexpr std::array<std::uint32_t, 4> overlay_rows { 484, 485, 486, 487 };
constexpr std::array<std::uint32_t, 7> staff_filters { 2, 4, 3, 6, 5, 1, 0 };
constexpr std::array<std::uint32_t, 4> overlay_masks { 128, 256, 512, 1024 };

TextureImage load_map_image(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::format("Missing original Park Map art: {}", path.native_encoded_string()));
    }
    const auto length = file.tellg();
    if (length < 18 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid Park Map image size");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Unable to read original Park Map image");
    }
    auto image = decode_tga_image(bytes);
    if (!image) {
        throw std::runtime_error(std::format("Original Park Map art: {}", describe_texture_error(image.error())));
    }
    return std::move(*image);
}

std::uint32_t index32(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("Park Map draw index overflow");
    }
    return static_cast<std::uint32_t>(value);
}

void quad(UiDrawList& draw, TextureImagePtr image, std::array<float, 4> bounds, RgbaPixel color, float projected_depth, MeshMaterial material = {}) {
    const auto first = index32(draw.vertices.size());
    draw.vertices.push_back({ { bounds[0], bounds[1], projected_depth }, { 0, 0 }, color });
    draw.vertices.push_back({ { bounds[2], bounds[1], projected_depth }, { 1, 0 }, color });
    draw.vertices.push_back({ { bounds[2], bounds[3], projected_depth }, { 1, 1 }, color });
    draw.vertices.push_back({ { bounds[0], bounds[3], projected_depth }, { 0, 1 }, color });
    UiDrawCommand command;
    command.texture = std::move(image);
    command.first_index = index32(draw.indices.size());
    command.index_count = 6;
    command.material = material;
    command.depth_buffered = true;
    for (const auto offset : { 0u, 1u, 2u, 0u, 2u, 3u }) {
        draw.indices.push_back(first + offset);
    }
    draw.commands.push_back(std::move(command));
}
} //namespace

WorldParkMapUiController::State::State(WorldRuntime& context, UiRuntime& runtime, const std::filesystem::path& path, WorldParkMapUiHost callbacks) : c(context), ui(runtime), game_root(path), host(std::move(callbacks)) {
    if (!host.map_mode || !host.focus_world || !host.inspect) {
        throw std::invalid_argument("Park Map requires its World mode, camera and inspection host");
    }
}

WorldParkMapUiController::State::~State() {
    close();
}

UiControl& WorldParkMapUiController::State::required(std::uint32_t id) {
    if (auto* control = ui.find_recursive(root, id)) {
        return *control;
    }
    throw std::runtime_error(std::format("Original Park Map template missing control {}", id));
}

std::u16string WorldParkMapUiController::State::text(std::uint32_t row) const {
    auto value = format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, row, 16383);
    if (!value) {
        throw std::runtime_error(std::format("Park Map caption: {}", value.error().reason));
    }
    return std::move(*value);
}

void WorldParkMapUiController::State::caption(std::uint32_t id, std::u16string_view value, std::uint32_t font) {
    auto& control = required(id);
    control.font = font;
    control.horizontal_alignment = 1;
    control.vertical_alignment = 1;
    control.color = { 255, 255, 255, 255 };
    ui.set_text(control, value);
}

void WorldParkMapUiController::State::bind(UiControl& control) {
    const auto widget = control.primary_callback;
    const auto id = control.id;
    ui.set_primary_callback(control, [this, widget, id](UiControl& sender, const UiEvent& input) {
        if (id == 2432 || id == 2433 || (input.type >= 256 && input.type <= 2055)) {
            return event(sender, input);
        }
        //Map coordinates are observed before a concrete button's own capture,
        //repeat and check-group callback. Those constructor callbacks stay owned.
        if (input.type == 65537 || input.type == 65539) {
            pointer({ static_cast<std::int32_t>(input.id >> 16), static_cast<std::int32_t>(input.id & 65535u) });
        }
        return widget ? widget(sender, input) : ui.secondary(sender, input);
    });
    for (auto* child : control.children) {
        bind(*child);
    }
}

void WorldParkMapUiController::State::policy() {
    const auto& p = ensure_world_standard_ui_balance(c, game_root).user_interface;
    const auto rgba = [](std::int32_t r, std::int32_t g, std::int32_t b, std::int32_t a = 255) {
        return RgbaPixel { static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a) };
    };
    heat_colors = { rgba(p.empty_red, p.empty_green, p.empty_blue, p.empty_alpha), rgba(p.minimum_red, p.minimum_green, p.minimum_blue, p.minimum_alpha), rgba(p.middle_red, p.middle_green, p.middle_blue, p.middle_alpha), rgba(p.maximum_red, p.maximum_green, p.maximum_blue, p.maximum_alpha) };
    menu_colors = { rgba(p.red, p.green, p.blue), rgba(p.active_red, p.active_green, p.active_blue), rgba(p.down_red, p.down_green, p.down_blue) };
}

void WorldParkMapUiController::State::labels() {
    for (std::size_t n = 0; n < heat_rows.size(); ++n) {
        auto& control = required(2448 + static_cast<std::uint32_t>(n));
        caption(control.id, text(heat_rows[n]));
        control.button_text_colors[0] = menu_colors[0];
        control.button_text_colors[1] = menu_colors[1];
        control.button_text_colors[2] = menu_colors[2];
        ui.set_value(control, control.value);
    }
    for (std::size_t n = 0; n < staff_rows.size(); ++n) {
        auto& control = required(2457 + static_cast<std::uint32_t>(n));
        caption(control.id, text(staff_rows[n]));
        control.button_text_colors[0] = menu_colors[0];
        control.button_text_colors[1] = menu_colors[1];
        control.button_text_colors[2] = menu_colors[0];
        ui.set_value(control, control.value);
    }
    for (std::size_t n = 0; n < overlay_rows.size(); ++n) {
        auto& control = required(2466 + static_cast<std::uint32_t>(n));
        caption(control.id, text(overlay_rows[n]));
        control.button_text_colors[0] = menu_colors[0];
        control.button_text_colors[1] = menu_colors[1];
        control.button_text_colors[2] = menu_colors[0];
        ui.set_value(control, control.value);
    }
    for (const auto id : { 2439u, 2440u, 2441u }) {
        auto& control = required(id);
        control.button_text_colors[0] = { 255, 255, 255, 255 };
        control.button_text_colors[1] = menu_colors[1];
        control.button_text_colors[2] = { 255, 255, 255, 255 };
    }
    caption(2440, text(heat_rows.at(static_cast<std::size_t>(heat_choice))));
    caption(2441, text(staff_rows.at(static_cast<std::size_t>(staff_choice))));
    caption(2439, text(overlay_rows.at(static_cast<std::size_t>(overlay_choice))));
    caption(2437, text(488), 8);
    caption(2438, text(489), 8);
    caption(2436, text(490), 8);
    for (const auto id : { 2436u, 2437u, 2438u }) {
        required(id).text_constraints = { true, true };
    }
    ui.set_selected(required(2447), 2448 + heat_choice);
    ui.set_selected(required(2456), 2457 + staff_choice);
    ui.set_selected(required(2465), 2466 + overlay_choice);
}

void WorldParkMapUiController::State::open() {
    if (root || (c.ui.command_menu_builder && c.ui.command_menu_builder->visible)) {
        return;
    }
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid) {
        throw std::runtime_error("Park Map requires its original terrain dimensions");
    }
    const auto& grid = *c.construction.active_terrain_model_data->terrain_grid;
    width = grid.width_cells;
    height = grid.height_cells;
    if (!width || !height || width > 128 || height > 128) {
        throw std::runtime_error("Park Map terrain dimensions exceed its original cell grid");
    }
    policy();
    auto level_path = std::filesystem::path(build_selected_resource_levels_path(c));
    if (level_path.is_relative()) {
        level_path = game_root / level_path;
    }
    background = load_map_image(level_path / "2dmap.tga");
    constexpr std::array<std::string_view, 21> names { "vsprite", "rsprite", "ssprite", "lsprite", "esprite", "gsprite", "ksprite", "msprite", "nsprite", "map_facility", "map_feature", "map_ride", "map_shop", "map_sshow", "map_stars1", "map_stars2", "map_stars3", "map_stars4", "map_stars5", "map_upgradel1", "map_upgradel2" };
    for (std::size_t n = 0; n < names.size(); ++n) {
        auto image = load_map_image(game_root / "Data/2dmap" / (std::string(names[n]) + ".tga"));
        image.source_mode = TextureSourceMode::alpha_low;
        textures[n] = std::make_shared<TextureImage>(std::move(image));
    }
    if (host.panels.dismiss) {
        host.panels.dismiss();
    }
    host.map_mode(true);
    stop_active_advisor_response(c, AdvisorStopTargetAction::parameter_request);
    try {
        const auto parsed = ui.parse_template(nullptr, std::as_bytes(std::span(map_template)));
        if (!parsed || !*parsed) {
            throw std::runtime_error("Cannot construct original Park Map template");
        }
        root = *parsed;
        if (root->id != 2432) {
            throw std::runtime_error("Wrong original Park Map root");
        }
        bind(*root);
        center = { static_cast<float>(fixed::truncate_integer(c.view.camera.focus_position[0] * .1f)), static_cast<float>(fixed::truncate_integer(c.view.camera.focus_position[2] * .1f)) };
        center[0] = std::clamp(center[0], 0.f, static_cast<float>(width - 1));
        center[1] = std::clamp(center[1], 0.f, static_cast<float>(height - 1));
        for (const auto id : { 2442u, 2443u, 2444u, 2445u }) {
            auto& arrow = required(id);
            arrow.repeat_delay = std::chrono::milliseconds(0);
            arrow.repeat_interval = std::chrono::milliseconds(10);
        }
        for (const auto id : { 2446u, 2455u, 2464u }) {
            ui.set_visible(required(id), false);
        }
        labels();
        hover.reset();
        hover_cell = 0;
        pan_active = false;
        keys.clear();
        retired.clear();
        build_cache();
        build_markers();
        render();
        if (host.tooltip_offsets) {
            host.tooltip_offsets(UiPoint { -1, 10 });
        }
        if (host.panels.activate) {
            host.panels.activate(root);
        }
        ui.set_input_root(root);
        ui.set_focus(&required(2433));
    } catch (...) {
        if (root) {
            close();
        } else {
            host.map_mode(false);
        }
        throw;
    }
}

void WorldParkMapUiController::State::close() {
    if (closing || !root) {
        return;
    }
    closing = true;
    auto* old = std::exchange(root, nullptr);
    popup = nullptr;
    ui.set_input_root(nullptr);
    if (ui.owns(old) && !old->destroying) {
        ui.destroy(old);
    }
    markers.clear();
    cells.fill({});
    heat.fill(0);
    weights.fill(0);
    textures.fill({});
    raster.reset();
    background = {};
    hover.reset();
    hover_cell = 0;
    pan_active = false;
    keys.clear();
    if (host.tooltip) {
        host.tooltip({});
    }
    if (host.tooltip_offsets) {
        host.tooltip_offsets(std::nullopt);
    }
    host.map_mode(false);
    if (host.panels.deactivate) {
        host.panels.deactivate(old);
    }
    if (host.panels.synchronize_focus) {
        host.panels.synchronize_focus();
    }
    closing = false;
}

void WorldParkMapUiController::State::close_to_center(ThingId inspect) {
    const auto generation = generations[inspect];
    const auto* identity = inspect && inspect < c.entities.size() ? c.entities[inspect].payload : nullptr;
    const auto x = std::clamp(fixed::truncate_integer(center[0]), 0, static_cast<std::int32_t>(width) - 1);
    const auto y = std::clamp(fixed::truncate_integer(center[1]), 0, static_cast<std::int32_t>(height) - 1);
    host.focus_world(static_cast<float>(x) * 10.f, static_cast<float>(y) * 10.f);
    close();
    if (!identity || inspect >= c.entities.size() || c.entities[inspect].payload != identity || identity->retirement_pending || generations[inspect] != generation) {
        return;
    }
    const auto& base = *reinterpret_cast<const MapObjectBase*>(identity);
    const auto cell = static_cast<std::size_t>(base.y >> 8) * 128 + (base.x >> 8);
    if (cell < map_cell_count && !(c.simulation.map_cell_records[cell].base.common.flags & 64u)) {
        host.inspect(inspect);
    }
}

void WorldParkMapUiController::State::close_popup() {
    if (!popup) {
        return;
    }
    const auto id = popup->id;
    if (ui.owns(popup)) {
        ui.set_visible(*popup, false);
    }
    popup = nullptr;
    const auto button = id == 2446 ? 2440u : id == 2455 ? 2441u : 2439u;
    ui.set_value(required(button), 0);
}

void WorldParkMapUiController::State::toggle_popup(std::uint32_t button, bool selected) {
    close_popup();
    if (!selected) {
        return;
    }
    popup = &required(button == 2440 ? 2446u : button == 2441 ? 2455u : 2464u);
    ui.set_visible(*popup, true);
    ui.set_value(required(button), 1);
}

void WorldParkMapUiController::State::pan(float dx, float dy) {
    center[0] = std::clamp(center[0] + dx, 0.f, static_cast<float>(width - 1));
    center[1] = std::clamp(center[1] + dy, 0.f, static_cast<float>(height - 1));
    pointer(cursor);
}

void WorldParkMapUiController::State::pointer(UiPoint point) {
    cursor = point;
    if (!root || !pixel_width || !pixel_height) {
        return;
    }
    const auto previous = hover;
    hover.reset();
    float closest = 100000000.f;
    for (std::size_t n = 0; n < markers.size(); ++n) {
        auto& marker = markers[n];
        update_marker(marker);
        if (!marker.visible) {
            continue;
        }
        const auto projected = project(marker.position);
        const auto dx = std::abs(projected[0] - static_cast<float>(point.x)) * static_cast<float>(pixel_width) / 2048.f;
        const auto dy = std::abs(projected[1] - static_cast<float>(point.y)) * static_cast<float>(pixel_height) / 1536.f;
        if (dx < 5 && dy < 5 && std::hypot(dx, dy) < closest) {
            closest = std::hypot(dx, dy);
            hover = n;
        }
    }
    if (previous != hover && ((previous && markers[*previous].source_kind == 2) || (hover && markers[*hover].source_kind == 2))) {
        dirty = true;
    }
    const auto position = unproject(point);
    const auto x = fixed::truncate_integer(position[0]), y = fixed::truncate_integer(position[1]);
    hover_cell = x >= 0 && y >= 0 && x < 128 && y < 128 ? cells[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)].object : 0;
    std::u16string help;
    const auto* record = hover ? markers[*hover].identity : nullptr;
    if (record && record->kind == ThingKind::person) {
        help = visitor_ui_name(c, *reinterpret_cast<const PersonRuntimeView*>(record));
    } else if (record && markers[*hover].source_kind == 2) {
        const auto& staff = *reinterpret_cast<const StaffRuntimeRecord*>(record);
        help = { staff.name.begin(), std::ranges::find(staff.name, char16_t {}) };
    } else if (hover_cell && hover_cell < c.entities.size()) {
        const auto* object = c.entities[hover_cell].payload;
        if (object && object->kind == ThingKind::park_object && !object->retirement_pending && !retired.contains(hover_cell)) {
            const auto& park_object = *reinterpret_cast<const ParkObjectRuntimeView*>(object);
            if (passes(park_object)) {
                help = park_object_ui_display_name(park_object);
            }
        }
    }
    if (host.tooltip) {
        host.tooltip(help);
    }
}

std::intptr_t WorldParkMapUiController::State::event(UiControl& sender, const UiEvent& input) {
    if (!root) {
        return ui.secondary(sender, input);
    }
    if (input.type == 20 && &sender == root) {
        close();
        return 0;
    }
    if (input.type == 65537 || input.type == 65539) {
        pointer({ static_cast<std::int32_t>(input.id >> 16), static_cast<std::int32_t>(input.id & 65535u) });
    } else if (input.type == 65541 && input.id == 1) {
        pan_active = true;
    } else if (input.type == 65540 && input.id == 1) {
        pan_active = false;
    } else if (input.type == 65542 && input.id == 0 && &sender == root) {
        pointer(cursor);
        const auto target = hover && markers[*hover].identity ? markers[*hover].thing : hover_cell;
        center = unproject(cursor);
        close_to_center(target);
        return 0;
    } else if (input.type == 256) {
        if (input.id == 2434) {
            close_to_center();
            return 0;
        }
        if (input.id == 2439 || input.id == 2440 || input.id == 2441) {
            toggle_popup(input.id, input.value == 1);
        } else if (input.id == 2470 || input.id == 2471) {
            zoom = std::clamp(zoom + (input.id == 2470 ? .5f : -.5f), 1.f, 6.f);
            pointer(cursor);
        }
    } else if (input.type == 257 && input.value == 1) {
        if (input.id >= 2448 && input.id <= 2454) {
            heat_choice = static_cast<std::int32_t>(input.id - 2448);
        } else if (input.id >= 2457 && input.id <= 2463) {
            staff_choice = static_cast<std::int32_t>(input.id - 2457);
            primary_filter = staff_filters[static_cast<std::size_t>(staff_choice)];
        } else if (input.id >= 2466 && input.id <= 2469) {
            overlay_choice = static_cast<std::int32_t>(input.id - 2466);
            mask = overlay_masks[static_cast<std::size_t>(overlay_choice)];
        } else {
            return ui.secondary(sender, input);
        }
        close_popup();
        labels();
        dirty = true;
        pointer(cursor);
    } else if (input.type == 65544 || input.type == 65545) {
        if (input.id == 2442) {
            pan(0, 1);
        } else if (input.id == 2443) {
            pan(1, 0);
        } else if (input.id == 2444) {
            pan(0, -1);
        } else if (input.id == 2445) {
            pan(-1, 0);
        }
    } else if (input.type == 65546 || input.type == 65547) {
        const auto key = input.id > 255 ? input.id >> 8 : input.id;
        if (input.type == 65547) {
            keys.erase(key);
            if (key == 27 && !(input.value & 16711680)) {
                close_to_center();
                return 0;
            }
        } else {
            keys.insert(key);
            if (key == 37) {
                pan(-1, 0);
            } else if (key == 39) {
                pan(1, 0);
            } else if (key == 38) {
                pan(0, 1);
            } else if (key == 40) {
                pan(0, -1);
            } else if (key == 107 || key == 109) {
                zoom = std::clamp(zoom + (key == 107 ? .5f : -.5f), 1.f, 6.f);
                pointer(cursor);
            }
        }
    }
    return ui.secondary(sender, input);
}

void WorldParkMapUiController::State::advance() {
    if (!root) {
        return;
    }
    const auto pressed = [&](std::uint32_t id, std::uint32_t key) {
        return required(id).visual_flags == 2 || keys.contains(key);
    };
    ui.set_attachment_part(required(2435), pressed(2445, 37) ? 1 : pressed(2443, 39) ? 2 : pressed(2442, 38) ? 3 : pressed(2444, 40) ? 4 : 0);
    ui.set_enabled(required(2470), zoom < 6);
    ui.set_enabled(required(2471), zoom > 1);
    for (const auto [id, key] : { std::pair { 2470u, 107u }, std::pair { 2471u, 109u } }) {
        auto& button = required(id);
        if (button.enabled && button.visual_flags != 2) {
            ui.set_value(button, keys.contains(key) ? 1 : 0);
        }
    }
    if (rmb_scroll) {
        if (pan_active) {
            pan(static_cast<float>(cursor.x - 1024) * .0024414062f, -static_cast<float>(cursor.y - 768) * .0032552083f);
        }
    } else {
        const auto x = std::clamp(static_cast<float>(cursor.x) / 2048.f, 0.f, 1.f);
        const auto y = std::clamp(static_cast<float>(cursor.y) / 1536.f, 0.f, 1.f);
        const auto dx = x < .01f ? -(.01f - x) * 250 : x > .99f ? (x - .99f) * 250 : 0;
        const auto dy = y < .01f ? (.01f - y) * 250 : y > .99f ? -(y - .99f) * 250 : 0;
        if (dx || dy) {
            pan(dx, dy);
        }
    }
}

void WorldParkMapUiController::State::append(UiDrawList& draw, std::uint32_t px, std::uint32_t py) {
    if (!root || !root->visible()) {
        return;
    }
    pixel_width = std::max(px, 1u);
    pixel_height = std::max(py, 1u);
    if (dirty) {
        render();
    }
    //Native10273's registry excludes the World and clears to black. This
    //compositor clear replaces color only; the shared UI depth was already
    //cleared by its renderer, so it must not stamp a map/control depth layer.
    MeshMaterial clear_material;
    clear_material.flags |= 0x8000u | 0x800u;
    quad(draw, {}, { 0, 0, 2048, 1536 }, { 0, 0, 0, 255 }, 1.f, clear_material);
    const auto bottom_left = project({ -15.f * static_cast<float>(width) / 480.f, -42.f * static_cast<float>(height) / 425.f });
    const auto top_right = project({ (static_cast<float>(background.width - 1) - 15.f) * static_cast<float>(width) / 480.f, (static_cast<float>(background.height - 1) - 42.f) * static_cast<float>(height) / 425.f });
    MeshMaterial background_material;
    //6DE530 sets source Z1.0;6DEC30 uses mode2 and opaque4694 plus point
    //magnification after7C2080 clears the render-state gate. The shared
    //projection6E16C0 writes Z*0.1, retaining depth test and depth writes.
    background_material.flags = 4694u | 131072u;
    quad(draw, raster, { bottom_left[0], top_right[1], top_right[0], bottom_left[1] }, { 255, 255, 255, 255 }, .1f, background_material);
    SpriteGeometrySettings settings;
    settings.coordinate_scale = static_cast<float>(pixel_height) / static_cast<float>(pixel_width);
    settings.direct3d_state = true;
    for (auto& marker : markers) {
        update_marker(marker);
        if (!marker.visible) {
            continue;
        }
        const auto& image = textures.at(marker.texture);
        if (!image) {
            throw std::logic_error("Park Map marker lost its owned texture");
        }
        if (image->width > 32767 || image->height > 32767) {
            throw std::runtime_error("Park Map marker dimensions exceed native sprite bounds");
        }
        EncodedSpriteFrame frame { static_cast<std::uint16_t>(image->width), static_cast<std::uint16_t>(image->height), static_cast<std::uint16_t>(image->width), static_cast<std::uint16_t>(image->height), static_cast<std::int16_t>(-static_cast<std::int32_t>(image->width) / 2), static_cast<std::int16_t>(-static_cast<std::int32_t>(image->height) / 2), static_cast<float>(image->width) / static_cast<float>(image->height), {} };
        const auto position = project(marker.position);
        ResourceSpriteCommand command;
        command.flags = 0x58000u | 0x30000u | 0x10000000u;
        command.position = { position[0] / 1024.f - 1.f, 1.f - position[1] / 768.f, marker.source_kind >= 0 && marker.source_kind <= 2 ? .99000001f : .99989998f };
        command.scale = static_cast<float>(image->width) * marker.scale * zoom * .5f * .0625f;
        command.color = (static_cast<std::uint32_t>(marker.color[3]) << 24) | (static_cast<std::uint32_t>(marker.color[0]) << 16) | (static_cast<std::uint32_t>(marker.color[1]) << 8) | marker.color[2];
        const SpriteTextureState texture { 512, 0, 0, 0, 1, 1 };
        const auto sprite = build_sprite_quad(command, frame, &texture, settings);
        if (!sprite) {
            throw std::runtime_error("Original Park Map sprite expansion failed");
        }
        const auto first = index32(draw.vertices.size());
        const std::array<std::array<float, 2>, 4> uv { { { sprite->uv[0], 1 - sprite->uv[1] }, { sprite->uv[2], 1 - sprite->uv[1] }, { sprite->uv[2], 1 - sprite->uv[3] }, { sprite->uv[0], 1 - sprite->uv[3] } } };
        for (std::size_t n = 0; n < 4; ++n) {
            const auto& v = sprite->vertices[n];
            //7C9560's two marker layers retain the explicit-sprite-.00125 bias
            //applied by6FC4F0, then use the same mode2 Z scale as the map raster.
            draw.vertices.push_back({ { (v[0] + 1) * 1024, (v[1] + 1) * 768, v[2] * .1f }, uv[n], marker.color });
        }
        UiDrawCommand output;
        output.texture = image;
        output.first_index = index32(draw.indices.size());
        output.index_count = 6;
        output.material.flags = sprite->render_state;
        output.depth_buffered = true;
        if (sprite->packet_flags & 2u) {
            for (const auto i : { 0u, 2u, 1u, 0u, 3u, 2u }) {
                draw.indices.push_back(first + i);
            }
        } else {
            for (const auto i : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                draw.indices.push_back(first + i);
            }
        }
        draw.commands.push_back(std::move(output));
    }
}

WorldParkMapUiController::WorldParkMapUiController(WorldRuntime& c, UiRuntime& ui, const std::filesystem::path& path, WorldParkMapUiHost host) : state_(std::make_unique<State>(c, ui, path, std::move(host))) {}

WorldParkMapUiController::~WorldParkMapUiController() = default;

bool WorldParkMapUiController::command(WorldUiCommand command) {
    if (command != WorldUiCommand::park_map) {
        return false;
    }
    open();
    return true;
}

void WorldParkMapUiController::open() {
    state_->open();
}

void WorldParkMapUiController::close() {
    state_->close();
}

bool WorldParkMapUiController::is_open() const {
    return state_->root != nullptr;
}

UiControl* WorldParkMapUiController::root() const {
    return state_->root;
}

void WorldParkMapUiController::preferences(const UiPreferences& preferences) {
    state_->rmb_scroll = preferences.rmb_scroll_on != 0;
}

void WorldParkMapUiController::advance() {
    state_->advance();
}

void WorldParkMapUiController::append(UiDrawList& draw, std::uint32_t w, std::uint32_t h) {
    state_->append(draw, w, h);
}

void WorldParkMapUiController::handle_event(const WorldEvent& event) {
    auto& s = *state_;
    if (const auto* retired = std::get_if<RetirementEvent>(&event.value)) {
        ++s.generations[retired->retired_id];
        if (!s.root) {
            return;
        }
        s.retired.insert(retired->retired_id);
        for (auto& marker : s.markers) {
            if (marker.thing == retired->retired_id) {
                marker.visible = false;
                marker.identity = nullptr;
            }
        }
        s.build_cache();
        s.hover.reset();
        s.hover_cell = 0;
        s.dirty = true;
        if (s.host.tooltip) {
            s.host.tooltip({});
        }
    } else if (s.root && (event.type() == 34 || event.type() == 6)) {
        const auto id = event.type() == 34 ? std::get<PersonCreatedEvent>(event.value).person_id : std::get<ParkObjectCreatedEvent>(event.value).park_object_id;
        s.retired.erase(id);
        s.build_cache();
        s.build_markers();
        s.hover.reset();
        s.hover_cell = 0;
        s.dirty = true;
    }
}
} //namespace simcoaster
