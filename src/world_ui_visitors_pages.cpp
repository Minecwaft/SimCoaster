#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "world_ui_visitors_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char list_template[]{
#embed "ui_templates/app_visitor_list.bin"
};
constexpr unsigned char profile_template[]{
#embed "ui_templates/app_visitor_profile.bin"
};
constexpr unsigned char statistics_template[]{
#embed "ui_templates/app_visitor_statistics.bin"
};
constexpr unsigned char locator_template[]{
#embed "ui_templates/app_visitor_locator.bin"
};
static_assert(sizeof(list_template) == 766
              && sizeof(profile_template) == 972
              && sizeof(statistics_template) == 1326
              && sizeof(locator_template) == 366);
constexpr std::array<std::uint32_t, 6> demographic_controls{ 39003, 39004, 39006, 39005, 39007, 39008 };
constexpr std::array<std::uint32_t, 4> population_controls{ 56017, 56015, 56018, 56016 };
constexpr std::array<std::uint32_t, 4> population_captions{ 41, 42, 43, 40 };

bool escape(const UiEvent& e) {
    return e.type == 65547
        && (e.id > 255 ? e.id >> 8 : e.id) == 27
        && !(static_cast<std::uint32_t>(e.value) & 0xFF0000u);
}
} //namespace

void WorldVisitorsUiController::State::open_profile(ThingId id) {
    if (!find_ui_visitor(c, id) || id == retiring)
        return;
    begin(Page::profile);
    select(id);
    root = &parse(profile_template, 29000, panel_callback<&State::event>());
    for (const auto [control, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 7>{ { { 29012, 201 },
                                                                                               { 29010, 204 },
                                                                                               { 29014, 202 },
                                                                                               { 29016, 203 },
                                                                                               { 29009, 205 },
                                                                                               { 29018, 206 },
                                                                                               { 29008, 207 } } })
        label(control, row);
    meter(29006, "ridestatbar.tga");
    meter(29007, "happygrad.tga");
    auto& thought_control = required(root, 29021);
    caption(thought_control, {}, 6, 1);
    thought_control.text_constraints = { true, true };
    auto& preview_control = required(root, 29020);
    preview_control.help_index = 303;
    //Shared used-thing binding explicitly sends preview pointer input to
    //the enclosing controller; raw widget input elsewhere remains intrinsic.
    const auto preview_widget = preview_control.primary_callback;
    ui.set_primary_callback(preview_control, [this, preview_widget](auto& sender, const auto& input) -> std::intptr_t {
        if (input.type == 13)
            return sender.render_depth + 10;
        if (input.type == 65542) {
            if (person() && host.focus_person)
                host.focus_person(target, false);
            return 0;
        }
        return preview_widget ? preview_widget(sender, input) : ui.secondary(sender, input);
    });
    preview = std::make_unique<UiPersonPreview>(c);
    preview->bind_person(id);
    advisor = 249; //Close/Help owns249; native opener does not stage it.
    refresh_profile();
    submit_positioned_template_request(c, 0, c.resources.kids_catalog_slot, 129, 0, 0, 0);
    activate();
}

void WorldVisitorsUiController::State::refresh_profile() {
    auto* record = person();
    if (!record) {
        close();
        return;
    }
    const auto metrics = visitor_ui_metrics(c, *record);
    caption(required(root, 29023), metrics.name, 5, 0);
    required(root, 29023).primary_shadow_text = true;
    constexpr std::array<std::uint32_t, 4> ids{ 29011, 29013, 29015, 29017 };
    for (std::size_t i = 0; i < ids.size(); ++i)
        scalar(ids[i], metrics.counters[i]);
    ui.set_value(required(root, 29006), metrics.preferred_excitement_meter);
    ui.set_value(required(root, 29007), metrics.happiness_meter);
    caption(required(root, 29019), metrics.activity);
    caption(required(root, 29021), metrics.thought, 6, 1);
    required(root, 29021).help_index = static_cast<std::uint32_t>(metrics.thought_help);
    ui.set_attachment_part(required(root, 29022), static_cast<std::uint16_t>(metrics.thought_part));
    ui.set_enabled(required(root, 29003), metrics.can_context);
    ui.set_visible(required(root, 29003), metrics.can_context);
    if (preview)
        preview->bind_person(target);
}

void WorldVisitorsUiController::State::navigate(bool previous) {
    const auto next = next_ui_visitor(c, target, previous, 0, retiring);
    select(next);
    if (preview) {
        preview->clear();
        preview->bind_person(next);
    }
    refresh_profile();
}

void WorldVisitorsUiController::State::open_list() {
    begin(Page::list);
    root = &parse(list_template, 55000, panel_callback<&State::event>());
    label(55003, 365, 5);
    required(root, 55003).primary_shadow_text = true;
    auto& list = required(root, 55004);
    if (list.columns.size() != 4)
        throw std::runtime_error("Original Visitor list requires four columns");
    list.font = 7;
    list.row_height_font_padding = 6;
    list.list_highlight_asset_hash = ui_model_asset_hash("hilight");
    list.column_presentations.resize(4);
    for (auto& column : list.column_presentations) {
        column.font = 7;
        column.horizontal_alignment = 1;
        column.vertical_alignment = 1;
    }
    list.column_presentations[0].format = UiListCellFormat::signed_integer;
    list.column_presentations[1].format = UiListCellFormat::text;
    list.column_presentations[1].horizontal_alignment = 0;
    list.column_presentations[2].format = UiListCellFormat::meter;
    list.column_presentations[2].meter = { "happygrad.tga", { 0, 12 } };
    list.column_presentations[3].format = UiListCellFormat::model_state;
    list.column_presentations[3].model_asset_hash = ui_model_asset_hash("ti_hungry");
    list.column_presentations[3].model_inset = { 4, 0 };
    const auto list_widget = list.primary_callback;
    ui.set_primary_callback(list, [this, list_widget](auto& sender, const auto& input) -> std::intptr_t {
        if (input.type == 12 && sender.row_height > 0 && sender.viewport.contains(ui.pointer_position())) {
            const auto point = ui.pointer_position();
            const auto row = sender.scroll_offset + (point.y - sender.viewport.top) / sender.row_height;
            if (row >= 0
                && static_cast<std::size_t>(row) < sender.rows.size()
                && point.x > sender.columns[3].left
                && point.x < sender.columns[3].right) {
                const auto thought_index = sender.rows[static_cast<std::size_t>(row)].cells[3].number();
                help_buffer =
                    thought_index == 999 ? std::u16string{} : thought(static_cast<std::uint32_t>(thought_index + 1));
                help_query = { 4294967295u, help_buffer.c_str() };
                return reinterpret_cast<std::intptr_t>(&help_query);
            }
        }
        return list_widget ? list_widget(sender, input) : ui.secondary(sender, input);
    });
    constexpr std::array<std::uint32_t, 4> headings{ 366, 372, 371, 370 }, helps{ 40, 16, 26, 42 };
    for (std::size_t i = 0; i < 4; ++i) {
        auto& heading = required(&list, 16u + static_cast<std::uint32_t>(i));
        caption(heading, text(headings[i]), 8, 1);
        heading.asset_hash = ui_model_asset_hash("!cbut");
        heading.color = { 255, 255, 255, 255 };
        heading.text_constraints = { true, true };
        heading.help_index = helps[i];
        for (std::size_t state = 0; state < 3; ++state)
            heading.button_text_colors[state] = heading.color;
    }
    for (const auto [control, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 4>{
             { { 55006, 204 }, { 55008, 201 }, { 55010, 202 }, { 55011, 203 } } })
        label(control, row);
    ui.set_list_sort_key(list, list_sort, false);
    refresh_list();
    ui.sort_list_rows(list, false);
    refresh_list_details();
    stage(235);
    activate();
}

void WorldVisitorsUiController::State::refresh_list() {
    auto& list = required(root, 55004);
    const auto people = enumerate_ui_visitors(c, retiring);
    std::vector<std::intptr_t> remove;
    for (const auto& row : list.rows)
        if (std::ranges::find(people, static_cast<ThingId>(row.identity)) == people.end())
            remove.push_back(row.identity);
    for (const auto identity : remove) {
        ui.remove_list_row(list, identity, false);
        list_identities.erase(static_cast<ThingId>(identity));
    }
    for (const auto id : people) {
        auto* record = find_ui_visitor(c, id);
        if (!record)
            continue;
        const auto identity = &record->base.header;
        if (const auto old = list_identities.find(id); old != list_identities.end() && old->second != identity)
            ui.remove_list_row(list, id, false);
        const auto metrics = visitor_ui_metrics(c, *record);
        UiListCell number{ fixed::word(record->arrival_index) };
        if (!record->arrival_index)
            number.display_text = text(965);
        UiListCell thought_cell{ record->mobile.thought_history.last_thought == MobileThoughtCode::none
                                     ? 999
                                     : static_cast<std::int32_t>(record->mobile.thought_history.last_thought) - 1 };
        if (thought_cell.number() != 999)
            thought_cell.help_index = static_cast<std::uint32_t>(metrics.thought_help);
        std::vector<UiListCell> cells{ std::move(number),
                                       metrics.generation,
                                       metrics.happiness_meter,
                                       std::move(thought_cell) };
        if (ui.find_list_row(list, id))
            ui.update_list_row(list, id, cells, 13,
                               false); //native mask29 ignores non-existent fifth column
        else
            ui.insert_list_row(list, { id, std::move(cells), {}, {} }, {}, false);
        list_identities[id] = identity;
    }
    ui.refresh_list(list, false);
    refresh_list_details();
}

void WorldVisitorsUiController::State::refresh_list_details() {
    auto& list = required(root, 55004);
    const auto selected = ui.selected_list_identity(list);
    auto* record = selected ? find_ui_visitor(c, static_cast<ThingId>(*selected)) : nullptr;
    const auto valid = record
                    && record->base.header.id != retiring
                    && list_identities.contains(record->base.header.id)
                    && list_identities.at(record->base.header.id) == &record->base.header;
    ui.set_enabled(required(root, 55002), valid);
    ui.set_visible(required(root, 55005), valid);
    list_selected = list.selected_id;
    if (valid) {
        const auto metrics = visitor_ui_metrics(c, *record);
        constexpr std::array<std::uint32_t, 4> ids{ 55007, 55009, 55012, 55013 };
        for (std::size_t i = 0; i < ids.size(); ++i)
            scalar(ids[i], metrics.counters[i]);
    }
}

void WorldVisitorsUiController::State::open_statistics() {
    begin(Page::statistics);
    root = &parse(statistics_template, 56000, panel_callback<&State::event>());
    label(56033, 376, 5);
    required(root, 56033).primary_shadow_text = true;
    for (const auto [control, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 10>{ { { 56022, 377 },
                                                                                                { 56023, 378 },
                                                                                                { 56025, 380 },
                                                                                                { 56027, 381 },
                                                                                                { 56029, 382 },
                                                                                                { 56004, 383 },
                                                                                                { 56006, 384 },
                                                                                                { 56010, 1 },
                                                                                                { 56011, 2 },
                                                                                                { 56012, 3 } } })
        label(control, row);
    required(root, 56019).text_constraints = { true, true };
    required(root, 56029).horizontal_alignment = 1;
    meter(56003, "happy.tga");
    meter(56008, "undecided.tga");
    meter(56009, "sad.tga");
    meter(56021, "ridestatbar.tga");
    for (const auto id : { 56030u, 56031u, 56032u }) {
        auto& icon = required(root, id);
        icon.help_index = 4294967295u;
        const auto previous = icon.primary_callback;
        ui.set_primary_callback(icon, [this, previous](auto& sender, const auto& input) -> std::intptr_t {
            if (input.type == 12) {
                help_buffer = sender.value >= 0 && sender.value < 30
                                ? thought(static_cast<std::uint32_t>(sender.value + 1))
                                : std::u16string{};
                help_query = { 4294967295u, help_buffer.c_str() };
                return reinterpret_cast<std::intptr_t>(&help_query);
            }
            return previous ? previous(sender, input) : ui.secondary(sender, input);
        });
    }
    refresh_statistics();
    stage(435);
    activate();
}

void WorldVisitorsUiController::State::refresh_statistics() {
    const auto metrics = visitor_ui_statistics(c, population, retiring);
    scalar(56005, metrics.current);
    scalar(56007, metrics.lifetime);
    ui.set_value(required(root, 56003), metrics.population_meters[0]);
    ui.set_value(required(root, 56008), metrics.population_meters[1]);
    ui.set_value(required(root, 56009), metrics.population_meters[2]);
    ui.set_value(required(root, 56021), metrics.average_happiness_meter);
    constexpr std::array<std::uint32_t, 3> values{ 56024, 56026, 56028 }, icons{ 56032, 56031, 56030 };
    for (std::size_t i = 0; i < 3; ++i) {
        scalar(values[i], metrics.averages[i]);
        auto& icon = required(root, icons[i]);
        const auto code = metrics.thoughts[i];
        ui.set_visible(icon, code > 0 && code < 31);
        ui.set_value(icon, static_cast<std::int32_t>(code) - 1);
    }
    const auto index = static_cast<std::size_t>(population);
    ui.set_selected(required(root, 56014), static_cast<std::int32_t>(population_controls.at(index)), false);
    label(56019, population_captions.at(index), 8, 1);
}

void WorldVisitorsUiController::State::open_locator(ThingId id) {
    if (root && page == Page::locator) {
        select(id);
    } else {
        begin(Page::locator);
        root = &parse(locator_template, 39000, panel_callback<&State::event>());
        caption(*root, {}, 6, 1);
        root->text_constraints = { true, true };
        select(id);
    }
    locator_filter = person() ? visitor_ui_demographic(c, *person()) : 0;
    if (person())
        c.view.flags |= 4096u;
    refresh_locator();
    activate();
}

void WorldVisitorsUiController::State::refresh_locator() {
    const auto available = visitor_ui_demographics(c, retiring);
    for (std::size_t i = 0; i < available.size(); ++i)
        ui.set_enabled(required(root, demographic_controls[i]), available[i]);
    auto* record = person();
    if (!record) {
        select(0);
        locator_filter = 0;
        ui.set_selected(required(root, 39002), -1, false);
        caption(*root, {}, 6, 1);
        c.view.flags &= ~4096u;
        return;
    }
    ui.set_selected(required(root, 39002),
                    static_cast<std::int32_t>(demographic_controls.at(static_cast<std::size_t>(locator_filter - 1))),
                    false);
    const auto name = visitor_ui_name(c, *record);
    const std::array strings{ BfstWideStringArgument{ 0, name } };
    caption(*root, text(47, {}, strings), 6, 1);
    //Native596410's passive availability refresh never resumes follow after
    //manual camera input clears it. Only opening/selecting a target does so.
}

void WorldVisitorsUiController::State::locator_select(std::int32_t filter, bool previous) {
    if (filter != locator_filter)
        select(0);
    locator_filter = filter;
    select(next_ui_visitor(c, target, previous, filter, retiring));
    if (person())
        c.view.flags |= 4096u;
    refresh_locator();
}

std::intptr_t WorldVisitorsUiController::State::event(UiControl& sender, const UiEvent& input) {
    if (closing)
        return ui.secondary(sender, input);
    if (input.type == 5
        || escape(input)
        || (input.type == 256
            && (input.id == static_cast<std::uint32_t>(-2)
                || input.id == 55001
                || input.id == 29001
                || input.id == 56001
                || input.id == 39001))) {
        close();
        return 1;
    }
    if (input.type == 20 && &sender == root) {
        close();
        return 1;
    }
    if (input.type == 256 && (input.id == 55014 || input.id == 29024 || input.id == 56034)) {
        help();
        return 1;
    }
    if (page == Page::profile) {
        if ((input.type == 256 && input.id == 29002) || (input.type == 65546 && input.id == 9728)) {
            navigate(true);
            return 1;
        }
        if ((input.type == 256 && input.id == 29004) || (input.type == 65546 && input.id == 10240)) {
            navigate(false);
            return 1;
        }
        if (input.type == 256 && input.id == 29003) {
            open_context(target, ui.pointer_position());
            return 1;
        }
    } else if (page == Page::list) {
        auto& list = required(root, 55004);
        if (input.type == 1025) {
            refresh_list_details();
            return 1;
        }
        if (input.type == 1030) {
            list_sort = static_cast<std::int32_t>(input.value);
            return 1;
        }
        if (input.type == 1029 && list_selected != -1) {
            const auto upper = static_cast<std::int32_t>(input.value);
            const auto lower = static_cast<std::int32_t>(input.id);
            if (upper > lower)
                ui.select_list_row(list, std::clamp(list_selected, lower, upper - 1), true, false);
            return 1;
        }
        if ((input.type == 256 && input.id == 55002) || input.type == 1024 || input.type == 1026) {
            const auto index = input.type == 256 ? list.selected_id : static_cast<std::int32_t>(input.value);
            if (index >= 0 && static_cast<std::size_t>(index) < list.rows.size()) {
                const auto id = static_cast<ThingId>(list.rows[static_cast<std::size_t>(index)].identity);
                auto* record = find_ui_visitor(c, id);
                if (record && list_identities.contains(id) && list_identities.at(id) == &record->base.header) {
                    const bool focus = input.type == 1026;
                    close();
                    if (focus) {
                        if (host.focus_person)
                            host.focus_person(id, false);
                    } else
                        open_profile(id);
                }
            }
            return 1;
        }
    } else if (page == Page::statistics && input.type == 257 && input.value == 1) {
        const auto found = std::ranges::find(population_controls, input.id);
        if (found != population_controls.end()) {
            population = static_cast<VisitorPopulation>(found - population_controls.begin());
            refresh_statistics();
            return 1;
        }
    } else if (page == Page::locator) {
        if ((input.type == 257 && input.value == 1) || input.type == 258) {
            const auto found = std::ranges::find(demographic_controls, input.id);
            if (found != demographic_controls.end()) {
                locator_select(static_cast<std::int32_t>(found - demographic_controls.begin()) + 1, input.type == 258);
                return 1;
            }
        }
        if (input.type == 69638 && person()) {
            const auto id = target;
            close();
            open_profile(id);
            return 1;
        }
        if ((input.type == 65546 || input.type == 65547) && host.camera_key && host.camera_key(input))
            return 1;
    }
    return ui.secondary(sender, input);
}

void WorldVisitorsUiController::State::append(UiDrawList& draw,
                                              UiControl& control,
                                              UiRect clip,
                                              std::uint32_t width,
                                              std::uint32_t height) {
    pixel_width = width;
    pixel_height = height;
    if (page != Page::profile || control.id != 29020 || !preview || !person())
        return;
    auto rect = control.attachment_rect;
    const auto generation = person()->generation;
    const auto offset =
        generation == PersonGeneration::parent
            ? 35
            : (generation == PersonGeneration::child || generation == PersonGeneration::grandparent ? 70 : 0);
    rect.top += offset;
    rect.bottom += offset;
    (void)preview->append(draw,
                          { { rect.left, rect.top, rect.right, rect.bottom },
                            { static_cast<float>(clip.left),
                              static_cast<float>(clip.top),
                              static_cast<float>(clip.right),
                              static_cast<float>(clip.bottom) },
                            static_cast<std::int16_t>(control.render_depth + 2),
                            12.f });
}
} //namespace simcoaster
