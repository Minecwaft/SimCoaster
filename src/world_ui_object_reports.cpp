#include "simcoaster/world_fixed_point.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>

namespace simcoaster {
namespace {
constexpr unsigned char statistics_words[] {
#embed "ui_templates/park_statistics.bin"
};
constexpr unsigned char opinion_words[] {
#embed "ui_templates/park_opinions.bin"
};

struct OpinionRow {
    std::uint32_t label, percentage, meter, text;
};

constexpr std::array satisfaction_rows { OpinionRow { 15036, 15037, 15029, 433 }, OpinionRow { 15030, 15031, 15025, 434 }, OpinionRow { 15032, 15033, 15026, 435 }, OpinionRow { 15034, 15035, 15027, 436 }, OpinionRow { 15038, 15039, 15028, 437 } };
constexpr std::array price_rows { OpinionRow { 15007, 15008, 15004, 440 }, OpinionRow { 15011, 15012, 15006, 439 }, OpinionRow { 15009, 15010, 15005, 438 } };
constexpr std::array queue_rows { OpinionRow { 15018, 15019, 15016, 0 }, OpinionRow { 15020, 15021, 15017, 0 } };

struct Series {
    std::uint32_t control, bit, part, text;
};

constexpr std::array series { Series { 5005, 2, 1, 170 }, Series { 5006, 1, 2, 171 }, Series { 5003, 4, 3, 172 }, Series { 5007, 8, 4, 173 } };
constexpr std::array styles { UiGraphSeriesStyle { 0, { 255, 255, 0, 255 } }, UiGraphSeriesStyle { 2, { 255, 0, 255, 255 } }, UiGraphSeriesStyle { 4, { 255, 0, 0, 255 } }, UiGraphSeriesStyle { 5, { 0, 0xBD, 0xEC, 255 } } };
} //namespace

void WorldParkObjectUiController::State::open_statistics(ThingId id) {
    begin(Page::statistics);
    model.open_statistics(id);
    if (!model.state().statistics.open) {
        close();
        return;
    }
    page_target = id;
    root = &parse(statistics_words, 5000, panel_callback<&State::statistics_event>());
    for (const auto [control, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 3> { { { 5010, 872 }, { 5011, 873 }, { 5009, 874 } } }) {
        label(control, row, 11, 1);
        required(root, control).color = { 0, 0, 0, 255 };
    }
    for (const auto& entry : series) {
        label(entry.control, entry.text, 11, 1);
        required(root, entry.control).color = { 0, 0, 0, 255 };
    }
    for (const auto [control, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 6> { { { 5022, 170 }, { 5018, 174 }, { 5014, 175 }, { 5016, 176 }, { 5020, 177 }, { 5024, 178 } } }) {
        label(control, row, 6, 0);
    }
    graph_control = &required(root, 5012);
    refresh_statistics();
    stage_advisor(427);
    activate();
}

void WorldParkObjectUiController::State::refresh_statistics() {
    if (!root || !model.state().statistics.open) {
        return;
    }
    const auto& view = model.state().statistics;
    const std::array name { BfstWideStringArgument { 4, view.name } };
    caption(required(root, 5026), text(45, {}, name), 5, 0);
    required(root, 5026).primary_shadow_text = true;
    scalar(5023, view.takings, UiScalarTextKind::signed_currency);
    scalar(5019, view.running_cost, UiScalarTextKind::signed_currency);
    scalar(5015, view.goods_cost, UiScalarTextKind::signed_currency);
    scalar(5017, view.goods_cost, UiScalarTextKind::signed_currency);
    scalar(5021, view.profit, UiScalarTextKind::signed_currency);
    scalar(5025, view.total_profit, UiScalarTextKind::signed_currency);
    //526450 refreshes both profit colors, including recovery from a loss.
    required(root, 5021).color = view.profit < 0 ? RgbaPixel { 255, 0, 0, 255 } : RgbaPixel { 255, 255, 255, 255 };
    required(root, 5025).color = view.total_profit < 0 ? RgbaPixel { 255, 0, 0, 255 } : RgbaPixel { 255, 255, 255, 255 };
    for (const auto id : { 5014u, 5015u }) {
        ui.set_visible(required(root, id), view.show_goods_cost);
    }
    for (const auto id : { 5016u, 5017u }) {
        ui.set_visible(required(root, id), view.show_prizes_cost);
    }
    for (const auto& entry : series) {
        auto& button = required(root, entry.control);
        const bool selected = (view.graph.selected_series_mask & entry.bit) != 0;
        ui.set_value(button, selected ? 1 : 0);
        ui.set_attachment_part(required(&button, 5004), static_cast<std::uint16_t>(selected ? entry.part : 0));
    }
    ui.set_selected(required(root, 5008), view.graph.range == StatisticsGraphRange::one_year ? 5010 : view.graph.range == StatisticsGraphRange::three_years ? 5011 : 5009, false);
}

std::intptr_t WorldParkObjectUiController::State::statistics_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 256 || event.type == 257) {
        std::optional<StatisticsGraphRange> range;
        if (event.id == 5010) {
            range = StatisticsGraphRange::one_year;
        } else if (event.id == 5011) {
            range = StatisticsGraphRange::three_years;
        } else if (event.id == 5009) {
            range = StatisticsGraphRange::twelve_years;
        }
        if (range) {
            model.set_statistics_range(*range);
            refresh_page();
            return 1;
        }
    }
    if (event.type == 256) {
        if (const auto found = std::ranges::find(series, event.id, &Series::control); found != series.end()) {
            model.toggle_statistics_series(found->bit);
            refresh_page();
            return 1;
        }
        if (event.id == 5027) {
            help_advisor(427);
            return 1;
        }
        if (event.id == 5001 || event.id == 4294967294u) {
            close();
            return 1;
        }
    }
    return common_event(sender, event);
}

void WorldParkObjectUiController::State::open_opinions(ThingId id) {
    begin(Page::opinions);
    model.open_opinions(id);
    if (!model.state().opinions.open) {
        close();
        return;
    }
    page_target = id;
    root = &parse(opinion_words, 15000, panel_callback<&State::opinions_event>());
    label(15040, 448, 8);
    label(15013, 447, 8);
    label(15022, 446, 8);
    label(15049, 449, 8);
    required(root, 15050).color = { 0, 0, 0, 255 };
    refresh_opinions();
    stage_advisor(429);
    activate();
}

void WorldParkObjectUiController::State::refresh_opinions() {
    if (!root || !model.state().opinions.open) {
        return;
    }
    const auto& view = model.state().opinions;
    const std::array name { BfstWideStringArgument { 0, view.name } };
    caption(required(root, 15042), text(429, {}, name), 5, 0);
    required(root, 15042).primary_shadow_text = true;
    const std::array days { BfstSignedIntegerArgument { 19, view.days } };
    caption(required(root, 15050), text(view.days_caption_row, days), 8);
    const auto update = [&](const OpinionRow& row, std::int32_t value, std::uint32_t caption_row) {
        label(row.label, caption_row);
        scalar(row.percentage, value, UiScalarTextKind::percentage);
        //579140 shifts before unsigned division; the original bar clamps later.
        meter(row.meter, fixed::word((static_cast<std::uint32_t>(value) << 10) / 100u), 10);
    };
    for (std::size_t i = 0; i < satisfaction_rows.size(); ++i) {
        update(satisfaction_rows[i], view.satisfaction[i], satisfaction_rows[i].text);
    }
    for (std::size_t i = 0; i < price_rows.size(); ++i) {
        update(price_rows[i], view.price[i], price_rows[i].text);
    }
    update(queue_rows[0], view.queue[0], view.queue_caption_rows[0]);
    for (const auto id : { 15020u, 15021u, 15017u }) {
        ui.set_visible(required(root, id), view.show_second_queue);
    }
    if (view.show_second_queue) {
        update(queue_rows[1], view.queue[1], view.queue_caption_rows[1]);
    }
    label(15047, 450);
    scalar(15045, view.users);
    label(15048, 451);
    scalar(15046, view.walkaways);
}

std::intptr_t WorldParkObjectUiController::State::opinions_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 256) {
        if (event.id == 15041) {
            model.reset_opinions();
            refresh_page();
            return 1;
        }
        if (event.id == 15051) {
            help_advisor(429);
            return 1;
        }
        if (event.id == 15001 || event.id == 4294967294u) {
            close();
            return 1;
        }
    }
    return common_event(sender, event);
}

void WorldParkObjectUiController::State::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    pixel_width = width;
    pixel_height = height;
    if (const auto found = previews.find(&control); found != previews.end()) {
        found->second->append(draw, control, clip, width, height);
    }
    if (page == Page::statistics && &control == graph_control) {
        if (!graph_presenter) {
            graph_presenter = std::make_unique<UiGraphPresenter>(fonts());
        }
        graph_presenter->append(draw, control, model.state().statistics.graph, styles, clip, width, height);
    }
}
} //namespace simcoaster
