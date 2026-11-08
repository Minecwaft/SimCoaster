#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char ride_words[]{
#embed "ui_templates/park_ride.bin"
};
constexpr unsigned char shop_words[]{
#embed "ui_templates/park_shop.bin"
};
constexpr unsigned char sideshow_words[]{
#embed "ui_templates/park_sideshow.bin"
};
constexpr unsigned char bathroom_words[]{
#embed "ui_templates/park_bathroom.bin"
};
constexpr unsigned char photo_words[]{
#embed "ui_templates/park_photo.bin"
};
constexpr unsigned char desk_words[]{
#embed "ui_templates/park_info_desk.bin"
};
constexpr unsigned char bin_words[]{
#embed "ui_templates/park_bin.bin"
};
constexpr unsigned char feature_words[]{
#embed "ui_templates/park_feature.bin"
};
constexpr unsigned char secondary_words[]{
#embed "ui_templates/park_secondary_ride.bin"
};
constexpr unsigned char mode3_words[]{
#embed "ui_templates/park_mode3.bin"
};
constexpr std::array specs{
    ObjectPanelSpec{ ParkObjectUiPanel::ride,
                     ride_words,
                     12000,
                     12036,
                     12019,
                     12037,
                     12040,
                     12002,
                     12006,
                     12003,
                     12004,
                     12005,
                     12042,
                     0,
                     168 },
    ObjectPanelSpec{ ParkObjectUiPanel::shop,
                     shop_words,
                     11000,
                     11028,
                     11014,
                     11029,
                     11033,
                     11002,
                     11006,
                     11003,
                     11004,
                     11005,
                     11037,
                     11035,
                     170 },
    ObjectPanelSpec{ ParkObjectUiPanel::sideshow,
                     sideshow_words,
                     21000,
                     21004,
                     21013,
                     21005,
                     21012,
                     21007,
                     21011,
                     21008,
                     21009,
                     21010,
                     21035,
                     21033,
                     169 },
    ObjectPanelSpec{ ParkObjectUiPanel::bathroom,
                     bathroom_words,
                     22000,
                     22026,
                     22009,
                     22027,
                     22028,
                     22002,
                     22006,
                     22003,
                     22004,
                     22005,
                     22031,
                     22029,
                     246 },
    ObjectPanelSpec{ ParkObjectUiPanel::photo_kiosk,
                     photo_words,
                     25000,
                     25025,
                     25012,
                     25026,
                     25029,
                     25002,
                     25006,
                     25003,
                     25004,
                     25005,
                     25033,
                     25031,
                     426 },
    ObjectPanelSpec{ ParkObjectUiPanel::info_desk,
                     desk_words,
                     10000,
                     10013,
                     10008,
                     10014,
                     10015,
                     10002,
                     10006,
                     10003,
                     10004,
                     10005,
                     10018,
                     10016,
                     424 },
    ObjectPanelSpec{ ParkObjectUiPanel::litter_bin,
                     bin_words,
                     23000,
                     23012,
                     23005,
                     0,
                     23013,
                     23002,
                     23004,
                     0,
                     0,
                     23003,
                     23014,
                     0,
                     423 },
    ObjectPanelSpec{ ParkObjectUiPanel::feature,
                     feature_words,
                     27000,
                     27010,
                     27005,
                     27011,
                     27012,
                     27002,
                     27004,
                     0,
                     0,
                     27003,
                     27013,
                     0,
                     245 },
    ObjectPanelSpec{ ParkObjectUiPanel::secondary_ride,
                     secondary_words,
                     21000,
                     21004,
                     21005,
                     0,
                     21010,
                     21002,
                     21003,
                     0,
                     0,
                     0,
                     0,
                     0,
                     -1 },
    ObjectPanelSpec{ ParkObjectUiPanel::mode3, mode3_words, 62200, 62203, 62205, 0, 62204, 0, 0, 0, 0, 62202, 0, 0, -1 }
};

struct Label {
    std::uint32_t control, row;
};

constexpr std::array ride_labels{ Label{ 12034, 117 }, Label{ 12030, 122 }, Label{ 12028, 123 }, Label{ 12025, 118 },
                                  Label{ 12026, 119 }, Label{ 12033, 120 }, Label{ 12027, 121 }, Label{ 12031, 124 } };
constexpr std::array shop_labels{ Label{ 11018, 128 }, Label{ 11016, 129 }, Label{ 11024, 132 },
                                  Label{ 11020, 133 }, Label{ 11022, 131 }, Label{ 11026, 17 } };
constexpr std::array side_labels{ Label{ 21016, 136 }, Label{ 21024, 137 }, Label{ 21018, 138 },
                                  Label{ 21021, 139 }, Label{ 21019, 123 }, Label{ 21022, 140 } };
constexpr std::array photo_labels{ Label{ 25016, 128 }, Label{ 25014, 129 }, Label{ 25020, 132 },
                                   Label{ 25021, 133 }, Label{ 25018, 131 }, Label{ 25024, 17 } };
constexpr std::array bath_labels{ Label{ 22020, 149 }, Label{ 22022, 145 }, Label{ 22014, 146 }, Label{ 22017, 152 },
                                  Label{ 22012, 148 }, Label{ 22015, 153 }, Label{ 22018, 147 } };
} //namespace

const ObjectPanelSpec& object_panel_spec(ParkObjectUiPanel panel) {
    const auto found = std::ranges::find(specs, panel, &ObjectPanelSpec::panel);
    if (found == specs.end())
        throw std::invalid_argument("Unsupported original park-object settings panel");
    return *found;
}

void WorldParkObjectUiController::State::open_settings(ThingId id) {
    begin(Page::settings);
    const auto route = model.open_settings(id);
    auto& settings = model.state().settings;
    if (!settings.open) {
        close();
        return;
    }
    const auto& spec = object_panel_spec(route.panel);
    settings_input_revision.reset();
    page_target = settings.object;
    root = &parse(spec.words, spec.root, panel_callback<&State::settings_event>());
    configure_settings();
    refresh_settings();
    stage_advisor(spec.advisor);
    activate();
}

void WorldParkObjectUiController::State::configure_settings() {
    const auto& settings = model.state().settings;
    const auto& spec = object_panel_spec(settings.panel);
    auto& graphic = required(root, spec.preview);
    graphic.help_index = 301;
    ui.set_primary_callback(graphic, [this](UiControl& control, const UiEvent& event) {
        if (event.type == 13)
            return static_cast<std::intptr_t>(control.render_depth + 10);
        if (event.type == 65542) {
            if (host.focus_object)
                host.focus_object(model.state().settings.object);
            return std::intptr_t{};
        }
        if (event.type == 20)
            previews.erase(&control);
        return ui.secondary(control, event);
    });
    auto& name = required(root, spec.name);
    //510CF0 binds this through501D50: title5, primary shadow attachment.
    name.font = 5;
    name.primary_shadow_text = true;
    name.vertical_alignment = 1;
    ui.set_primary_callback(name, [this](UiControl& control, const UiEvent& event) {
        if (event.type == 65537)
            control.color = { 239, 234, 24, 255 };
        else if (event.type == 65538)
            control.color = { 255, 255, 255, 255 };
        else if (event.type == 65542 && !event.id)
            open_name(model.state().settings.object, true);
        return ui.secondary(control, event);
    });
    if (spec.status) {
        auto& status = required(root, spec.status);
        ui.set_enabled(status, false);
        status.font = 8;
        status.horizontal_alignment = status.vertical_alignment = 1;
        status.text_constraints = { true, true };
    }
    const auto labels = [&](std::span<const Label> values) {
        for (const auto& entry : values)
            label(entry.control, entry.row);
    };
    match (settings.panel) {
        case ParkObjectUiPanel::ride => {
            labels(ride_labels);
            for (const auto id : { 12011u, 12014u }) {
                auto& gauge = required(root, id);
                gauge.meter_presentation = UiMeterPresentation{ "smallguage.tga", { 0, 0 } };
                ui.set_enabled(gauge, true);
                ui.set_primary_callback(gauge, [this](UiControl& control, const UiEvent& event) {
                    if (event.type == 12) {
                        const auto boundary = static_cast<std::int64_t>(control.value) * control.rect.width() / 1024;
                        control.help_index = ui.pointer_position().x - control.rect.left <= boundary ? 274u : 275u;
                    }
                    return ui.secondary(control, event);
                });
            }
            affixed_edit(required(root, 12015), u"$ ");
        }
        case ParkObjectUiPanel::shop => {
            labels(shop_labels);
            label(11031, 134, 6, 1);
            label(11030, 130, 6, 1);
            label(11032, 135, 6, 1);
            affixed_edit(required(root, 11013), u"$ ");
        }
        case ParkObjectUiPanel::sideshow => {
            labels(side_labels);
            label(21031, 141, 6, 1);
            label(21027, 142, 6, 1);
            label(21029, 143, 6, 1);
            affixed_edit(required(root, 21026), u"$ ");
            affixed_edit(required(root, 21028), u"$ ");
        }
        case ParkObjectUiPanel::bathroom => {
            labels(bath_labels);
            label(22008, 935, 6, 1);
            affixed_edit(required(root, 22007), u"$ ");
        }
        case ParkObjectUiPanel::photo_kiosk => {
            labels(photo_labels);
            label(25027, 134, 6, 1);
            label(25028, 135, 6, 1);
            affixed_edit(required(root, 25011), u"$ ");
        }
        case ParkObjectUiPanel::info_desk => {
            label(10009, 157);
            label(10011, 147);
        }
        case ParkObjectUiPanel::litter_bin => {
            label(23007, 154);
            label(23011, 155);
            label(23008, 156);
        }
        case ParkObjectUiPanel::feature => {
            label(27008, 167);
            label(27009, 168);
        }
        case ParkObjectUiPanel::mode3 => {
            label(62207, 64);
            label(62209, 66);
        }
        case _ => {}
    }
    if (spec.bulk) {
        const auto row = settings.panel == ParkObjectUiPanel::shop      ? 70u
                       : settings.panel == ParkObjectUiPanel::sideshow  ? 71u
                       : settings.panel == ParkObjectUiPanel::bathroom  ? 72u
                       : settings.panel == ParkObjectUiPanel::info_desk ? 73u
                                                                        : 74u;
        label(spec.bulk, row, 6, 0);
        auto& bulk = required(root, spec.bulk);
        ui.set_primary_callback(bulk, [this](UiControl& control, const UiEvent& event) {
            if (event.type == 256)
                model.state().settings.apply_to_matching = event.value == 1;
            return ui.secondary(control, event);
        });
    }
}

void WorldParkObjectUiController::State::refresh_settings() {
    if (!root || !model.state().settings.open)
        return;
    const auto& s = model.state().settings;
    const auto& m = s.metrics;
    const auto& spec = object_panel_spec(s.panel);
    const bool load_inputs = settings_input_revision != s.input_revision;
    settings_input_revision = s.input_revision;
    page_target = s.object;
    caption(required(root, spec.name), s.name, 5);
    if (spec.previous)
        ui.set_visible(required(root, spec.previous), s.can_navigate);
    if (spec.next)
        ui.set_visible(required(root, spec.next), s.can_navigate);
    if (spec.toggle_open) {
        auto& button = required(root, spec.toggle_open);
        ui.set_enabled(button, s.can_toggle_open);
        ui.set_value(button, s.closed ? 1 : 0);
        button.help_index = s.closed ? 11u : 12u;
    }
    if (spec.status) {
        auto& status = required(root, spec.status);
        const auto presentation = park_object_ui_status_presentation(s.status);
        status.color = presentation.color;
        ui.set_text(status, text(presentation.text_row));
        const auto* record = c.entities.at(s.object).payload;
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(record);
        const bool chill =
            (std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::chills_you_out)) != 0;
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, s.definition);
        const bool feature = definition && definition->which_ui_type == ObjectUiCategory::features;
        ui.set_visible(status,
                       s.status != ParkObjectUiStatus::open
                           && !(chill && s.status == ParkObjectUiStatus::park_closed)
                           && !(feature && s.status != ParkObjectUiStatus::dead));
    }
    const auto setting = [&](std::uint32_t id, const ParkObjectUiSettingRange& range, std::int32_t value) {
        auto& control = required(root, id);
        ui.set_visible(control, range.visible);
        ui.set_enabled(control, range.enabled);
        //57F960/57E280 update staged statistics without rewriting the widget.
        //Numeric setters really queue2048 (85CDCB), so replaying earlier edit
        //notifications back through these setters creates a stale-value loop.
        if (load_inputs) {
            //57F44B/57F4D5/57F57D pass native(end=min,start=max). The modern
            //range API takes(start,end): retain the horizontal green+ direction.
            if (control.kind == UiControlKind::scroll)
                ui.set_range(control, range.maximum, range.minimum);
            else
                ui.set_range(control, range.minimum, range.maximum);
            if (control.kind == UiControlKind::numeric_stepper)
                control.step_size = range.step;
            ui.set_value(control, value);
        }
    };
    const auto ratio = [&](std::uint32_t id) {
        const std::array arguments{ BfstSignedIntegerArgument{ 1, m.users },
                                    BfstSignedIntegerArgument{ 2, fixed::add(m.users, m.walkaways) } };
        caption(required(root, id), text(968, arguments));
    };
    const auto cash = [&](std::uint32_t id, std::int32_t value) {
        scalar(id, value, UiScalarTextKind::signed_currency);
    };
    std::array<std::uint32_t, 4> modifiers{};
    match (s.panel) {
        case ParkObjectUiPanel::ride => {
            setting(12013, s.speed, s.staged.speed);
            setting(12010, s.capacity, s.staged.capacity);
            setting(12012, s.duration, s.staged.duration);
            setting(12015, s.price, s.staged.price);
            ui.set_value(required(root, 12014), s.speed_redline_meter);
            ui.set_value(required(root, 12011), s.capacity_redline_meter);
            const std::array speed{ BfstSignedIntegerArgument{ 32, s.staged.speed } };
            caption(required(root, 12039), text(932, speed), 6, 1);
            const std::array capacity{ BfstSignedIntegerArgument{ 34, s.staged.capacity } };
            caption(required(root, 12016), text(s.capacity_caption_row, capacity), 6, 1);
            const std::array duration{ BfstSignedIntegerArgument{ 35, s.staged.duration } };
            const auto duration_row = s.duration_unit == ParkObjectDurationUnit::seconds
                                        ? (s.staged.duration == 1 ? 937u : 938u)
                                    : s.duration_unit == ParkObjectDurationUnit::laps   ? 939u
                                    : s.duration_unit == ParkObjectDurationUnit::cycles ? 940u
                                                                                        : 941u;
            ui.set_visible(required(root, 12017), s.duration_unit != ParkObjectDurationUnit::none);
            if (s.duration_unit != ParkObjectDurationUnit::none)
                caption(required(root, 12017), text(duration_row, duration), 6, 1);
            label(12018, 935, 6, 1);
            scalar(12035, m.users);
            cash(12029, m.monthly_running_cost);
            meter(12020, m.excitement_meter);
            meter(12021, m.condition_meter);
            meter(12024, m.repair_meter);
            meter(12022, m.life_meter);
            meter(12023, m.satisfaction_meter);
            cash(12032, fixed::word(m.scrap_value));
            modifiers = { 12007, 12008, 12009, 12041 };
        }
        case ParkObjectUiPanel::shop => {
            setting(11012, s.quality, s.staged.quality);
            setting(11011, s.portion, s.staged.portion);
            setting(11010, s.ingredient, s.staged.ingredient);
            setting(11013, s.price, s.staged.price);
            ui.set_visible(required(root, 11030), s.portion.visible);
            ui.set_visible(required(root, 11027), s.ingredient.visible);
            caption(required(root, 11027), s.ingredient_caption, 6, 1);
            ratio(11019);
            cash(11017, m.goods_cost);
            meter(11015, m.satisfaction_meter);
            cash(11021, fixed::word(m.scrap_value));
            cash(11023, m.monthly_running_cost);
            meter(11025, m.value_meter);
            modifiers = { 11007, 11008, 11009, 11034 };
        }
        case ParkObjectUiPanel::sideshow => {
            setting(21030, s.chance, s.staged.chance);
            setting(21026, s.goods_cost, s.staged.goods_cost);
            setting(21028, s.price, s.staged.price);
            ui.set_visible(required(root, 21027), s.goods_cost.visible);
            ui.set_visible(required(root, 21031), s.chance.visible);
            label(21029, s.chance.visible ? 143u : 144u, 6, 1);
            ratio(21017);
            scalar(21025, m.winners);
            meter(21014, m.excitement_meter);
            meter(21015, m.satisfaction_meter);
            cash(21020, m.monthly_running_cost);
            cash(21023, fixed::word(m.scrap_value));
            modifiers = { 21001, 21002, 21003, 21032 };
        }
        case ParkObjectUiPanel::photo_kiosk => {
            setting(25010, s.quality, s.staged.quality);
            setting(25011, s.price, s.staged.price);
            const std::array name{ BfstWideStringArgument{ 4, s.related_ride_name } };
            auto& related_ride = required(root, 25029);
            caption(related_ride, text(44, {}, name), 5);
            related_ride.primary_shadow_text = true; //56D77C delegates to501D50.
            ratio(25017);
            cash(25015, m.goods_cost);
            meter(25013, m.satisfaction_meter);
            cash(25022, fixed::word(m.scrap_value));
            cash(25019, m.monthly_running_cost);
            meter(25023, m.value_meter);
            modifiers = { 25007, 25008, 25009, 25030 };
        }
        case ParkObjectUiPanel::bathroom => {
            setting(22007, s.price, s.staged.price);
            caption(required(root, 22021), m.gender == 1 ? text(150) : m.gender == 2 ? text(151) : u"-");
            ratio(22023);
            meter(22010, m.repair_meter);
            meter(22011, m.satisfaction_meter);
            scalar(22013, m.capacity);
            cash(22016, m.monthly_running_cost);
            cash(22019, fixed::word(m.scrap_value));
        }
        case ParkObjectUiPanel::info_desk => {
            caption(required(root, 10010),
                    s.promoted_attraction_name.empty() ? text(284) : s.promoted_attraction_name,
                    8);
            cash(10012, fixed::word(m.scrap_value));
        }
        case ParkObjectUiPanel::litter_bin => {
            scalar(23006, m.users);
            meter(23010, m.contents_meter, 0);
            cash(23009, fixed::word(m.scrap_value));
        }
        case ParkObjectUiPanel::feature => {
            scalar(27007, fixed::word(m.owned_count));
            cash(27006, fixed::word(m.scrap_value));
        }
        case ParkObjectUiPanel::secondary_ride => {
            scalar(21008, fixed::word(m.owned_count));
            cash(21006, fixed::word(m.scrap_value));
        }
        case ParkObjectUiPanel::mode3 => {
            if (s.build_cost_valid)
                cash(62208, s.build_cost);
            else {
                required(root, 62208).scalar_text.reset();
                label(62208, 65);
            }
            meter(62206, m.excitement_meter);
        }
        case _ => {}
    }
    constexpr std::array low_rows{ 270u, 272u, 269u, 271u };
    constexpr std::array high_rows{ 266u, 268u, 265u, 267u };
    for (std::size_t i = 0; i < modifiers.size(); ++i) {
        if (!modifiers[i])
            continue;
        auto& image = required(root, modifiers[i]);
        const auto value = s.definition_modifiers[i];
        ui.set_visible(image, value != 0 && value != 100);
        ui.set_attachment_part(image, value > 100 ? 0 : 1);
        image.help_text = text(value > 100 ? high_rows[i] : low_rows[i]);
    }
    std::uint32_t animation = 5;
    if (const auto* record = c.entities.at(s.object).payload) {
        const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(record);
        if (s.panel == ParkObjectUiPanel::feature
            && (std::to_underlying(object.capabilities)
                & std::to_underlying(ParkObjectCapabilities::requires_watering)))
            animation = 12;
    }
    preview(required(root, spec.preview), s.object, animation);
}

std::intptr_t WorldParkObjectUiController::State::settings_event(UiControl& control, const UiEvent& event) {
    if (closing || !model.state().settings.open)
        return common_event(control, event);
    auto& s = model.state().settings;
    const auto& spec = object_panel_spec(s.panel);
    if (event.type == 2048 && !refreshing) {
        std::int32_t* target{};
        const auto id = event.id;
        match (s.panel) {
            case ParkObjectUiPanel::ride => {
                if (id == 12013)
                    target = &s.staged.speed;
                else if (id == 12010)
                    target = &s.staged.capacity;
                else if (id == 12012)
                    target = &s.staged.duration;
                else if (id == 12015)
                    target = &s.staged.price;
            }
            case ParkObjectUiPanel::shop => {
                if (id == 11012)
                    target = &s.staged.quality;
                else if (id == 11011)
                    target = &s.staged.portion;
                else if (id == 11010)
                    target = &s.staged.ingredient;
                else if (id == 11013)
                    target = &s.staged.price;
            }
            case ParkObjectUiPanel::sideshow => {
                if (id == 21030)
                    target = &s.staged.chance;
                else if (id == 21026)
                    target = &s.staged.goods_cost;
                else if (id == 21028)
                    target = &s.staged.price;
            }
            case ParkObjectUiPanel::photo_kiosk => {
                if (id == 25010)
                    target = &s.staged.quality;
                else if (id == 25011)
                    target = &s.staged.price;
            }
            case ParkObjectUiPanel::bathroom => {
                if (id == 22007)
                    target = &s.staged.price;
            }
            case _ => {}
        }
        if (target) {
            *target = static_cast<std::int32_t>(event.value);
            model.refresh_settings();
            refresh_page();
            return 1;
        }
    }
    const auto key = event.id > 255 ? event.id >> 8 : event.id;
    if (event.type == 65546 && (key == 38 || key == 40)) {
        model.navigate_settings(key == 38);
        refresh_page();
        return 1;
    }
    if (event.type == 256) {
        if (event.id == spec.previous || event.id == spec.next) {
            if (event.id) {
                model.navigate_settings(event.id == spec.previous);
                refresh_page();
                return 1;
            }
        }
        if (spec.apply && event.id == spec.apply) {
            model.apply_settings();
            close();
            return 1;
        }
        if (spec.toggle_open && event.id == spec.toggle_open) {
            model.set_closed(s.object, !s.closed);
            model.refresh_settings();
            refresh_page();
            return 1;
        }
        if (spec.context && event.id == spec.context) {
            open_context(s.object, ui.pointer_position(), true);
            return 1;
        }
        if (spec.help && event.id == spec.help) {
            help_advisor(spec.advisor);
            return 1;
        }
        if (s.panel == ParkObjectUiPanel::info_desk && event.id == 10007) {
            open_promotion(s.object, true);
            return 1;
        }
        if (s.panel == ParkObjectUiPanel::secondary_ride && event.id == 21011)
            return 1; //Native596010 explicitly retains this inactive action.
        if (s.panel == ParkObjectUiPanel::secondary_ride && event.id == 21012) {
            const auto id = s.object;
            close();
            if (host.relocate_object)
                (void)host.relocate_object(id);
            return 1;
        }
        if (s.panel == ParkObjectUiPanel::secondary_ride && event.id == 21001) {
            const auto id = s.object;
            const auto* identity = c.entities.at(id).payload;
            const auto generation = reinterpret_cast<const ParkObjectRuntimeView*>(identity)->creation_file_time;
            //50FBC0 closes the owning panel even while its confirmation is pending.
            close();
            const auto execute = [this, id, identity, generation] {
                if (id >= c.entities.size()
                    || c.entities[id].payload != identity
                    || identity->retirement_pending
                    || reinterpret_cast<const ParkObjectRuntimeView*>(identity)->creation_file_time != generation)
                    return;
                if (host.request)
                    host.request(WorldDeleteParkObjectAction{ .id = id,
                                                              .identity = identity,
                                                              .identity_generation = generation });
            };
            if (confirm_deletions) {
                message(913, execute);
                confirmation_target = id;
            } else
                execute();
            return 1;
        }
        if ((s.panel != ParkObjectUiPanel::sideshow
             && s.panel != ParkObjectUiPanel::secondary_ride
             && event.id == spec.root + 1)
            || (s.panel == ParkObjectUiPanel::sideshow && event.id == 21006)
            || event.id == 4294967294u
            || (s.panel == ParkObjectUiPanel::secondary_ride && event.id == 4294967295u)) {
            close();
            return 1;
        }
    }
    return common_event(control, event);
}
} //namespace simcoaster
