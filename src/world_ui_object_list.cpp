#include "simcoaster/world_fixed_point.hpp"
#include "world_ui_object_columns.hpp"
#include "world_ui_objects_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char root_words[] {
#embed "ui_templates/park_list.bin"
};
constexpr unsigned char ride_words[] {
#embed "ui_templates/park_list_rides.bin"
};
constexpr unsigned char shop_words[] {
#embed "ui_templates/park_list_shops.bin"
};
constexpr unsigned char side_words[] {
#embed "ui_templates/park_list_sideshows.bin"
};
constexpr unsigned char feature_words[] {
#embed "ui_templates/park_list_features.bin"
};
constexpr unsigned char facility_words[] {
#embed "ui_templates/park_list_facilities.bin"
};

struct ListSpec {
    ObjectUiCategory category;
    std::uint32_t button, title, root;
    std::span<const unsigned char> words;
};

constexpr std::array specs { ListSpec { ObjectUiCategory::rides, 38018, 342, 38100, ride_words }, ListSpec { ObjectUiCategory::shops, 38017, 343, 38300, shop_words }, ListSpec { ObjectUiCategory::sideshows, 38016, 344, 38200, side_words }, ListSpec { ObjectUiCategory::features, 38019, 345, 38500, feature_words }, ListSpec { ObjectUiCategory::facilities, 38020, 346, 38400, facility_words } };

const ListSpec& list_spec(ObjectUiCategory category) {
    const auto found = std::ranges::find(specs, category, &ListSpec::category);
    if (found == specs.end()) {
        throw std::logic_error("Invalid original attraction-list category");
    }
    return *found;
}

RgbaPixel profit_color(std::int32_t value) {
    return value < 0 ? RgbaPixel { 255, 0, 0, 255 } : RgbaPixel { 255, 255, 255, 255 };
}
} //namespace

void WorldParkObjectUiController::State::open_list() {
    //Native5194D0 checks eligibility before dismissing the active panel.
    model.open_list();
    if (!model.state().list.open) {
        return;
    }
    const auto category = model.state().list.category;
    begin(Page::list);
    model.open_list(category);
    root = &parse(root_words, 38000, panel_callback<&State::list_event>());
    label(38007, 123);
    label(38009, 178);
    label(38013, 497);
    rebuild_list();
    stage_advisor(153);
    activate();
}

void WorldParkObjectUiController::State::rebuild_list() {
    if (sublist && ui.owns(sublist)) {
        const auto index = static_cast<std::size_t>(*presented_category);
        list_saved_selection[index] = sublist->selected_id;
        ui.destroy(sublist);
    }
    sublist = nullptr;
    const auto& view = model.state().list;
    const auto& spec = list_spec(view.category);
    auto& owner = required(root, 38014);
    const auto parsed = ui.parse_template(&owner, std::as_bytes(spec.words));
    if (!parsed || !*parsed) {
        throw std::runtime_error("Original attraction category list could not be constructed");
    }
    sublist = *parsed;
    if (sublist->id != spec.root) {
        throw std::runtime_error("Original attraction category list root mismatch");
    }
    bind(*sublist, panel_callback<&State::list_event>());
    with_object_list_columns<std::logic_error>(
        view.category,
        [&](auto columns) {
            list(*sublist, columns.headings, columns.formats, view.sort_keys[static_cast<std::size_t>(view.category)]);
        },
        "Invalid original attraction-list category");
    presented_category = view.category;
    label(38021, spec.title, 5, 0);
    required(root, 38021).primary_shadow_text = true;
    for (const auto& category : specs) {
        ui.set_enabled(required(root, category.button), view.available[static_cast<std::size_t>(category.category)]);
    }
    ui.set_selected(required(root, 38015), static_cast<std::int32_t>(spec.button), false);
    refresh_list();
    if (!view.selected && !sublist->rows.empty()) {
        const auto saved = list_saved_selection[static_cast<std::size_t>(view.category)];
        const auto index = std::clamp(saved, 0, static_cast<std::int32_t>(sublist->rows.size() - 1));
        ui.select_list_row(*sublist, index, false, true);
        model.select_list(static_cast<ThingId>(*ui.selected_list_identity(*sublist)));
        refresh_list();
    }
}

void WorldParkObjectUiController::State::refresh_list() {
    if (!root || !model.state().list.open) {
        return;
    }
    const auto& view = model.state().list;
    if (presented_category != view.category) {
        rebuild_list();
        return;
    }
    if (!sublist) {
        return;
    }
    std::vector<UiListRow> data;
    for (const auto& row : view.rows) {
        auto cells = with_object_list_columns<std::logic_error>(
            view.category,
            [&](auto columns) {
                return columns.cells(row);
            },
            "Invalid attraction-list category");
        UiListRow item { row.object, std::move(cells) };
        if (row.parent_object) {
            item.parent_identity = *row.parent_object;
        }
        item.color = row.color;
        data.push_back(std::move(item));
    }
    //Native periodic metric refresh neither resorts existing rows nor centers
    //their selected item. Preserve the widget's identity/order/scroll state.
    project_rows<true>(*sublist, std::move(data));
    if (view.selected) {
        const auto index = ui.find_list_row(*sublist, view.selected);
        ui.select_list_row(*sublist, index ? static_cast<std::int32_t>(*index) : -1, false, false);
    }
    ui.set_enabled(required(root, 38002), ui.selected_list_identity(*sublist).has_value());
    ui.set_visible(required(root, 38003), view.details_visible);
    if (!view.details_visible) {
        return;
    }
    scalar(38008, view.monthly_running_cost, UiScalarTextKind::signed_currency);
    scalar(38010, view.profit_to_date, UiScalarTextKind::signed_currency);
    const auto selected = std::ranges::find(view.rows, view.selected, &ParkObjectListUiRow::object);
    required(root, 38008).color = profit_color(selected == view.rows.end() ? 0 : selected->profit);
    required(root, 38010).color = profit_color(view.profit_to_date);
    const bool ride = view.category == ObjectUiCategory::rides;
    ui.set_visible(required(root, 38012), ride);
    ui.set_visible(required(root, 38013), ride);
    if (ride) {
        const std::array argument { BfstSignedIntegerArgument { 3, fixed::word(view.upgrade_level) } };
        caption(required(root, 38012), view.upgrade_level ? text(38, argument) : text(37));
    }
    constexpr std::array ids { 38004u, 38005u, 38006u, 38011u };
    constexpr std::array low { 270u, 272u, 269u, 271u }, high { 266u, 268u, 265u, 267u };
    const bool metrics = view.category != ObjectUiCategory::features && view.category != ObjectUiCategory::facilities;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto& image = required(root, ids[i]);
        const auto value = view.definition_modifiers[i];
        ui.set_visible(image, metrics && value != 0 && value != 100);
        ui.set_attachment_part(image, value > 100 ? 0 : 1);
        image.help_text = text(value > 100 ? high[i] : low[i]);
    }
}

std::intptr_t WorldParkObjectUiController::State::list_event(UiControl& sender, const UiEvent& event) {
    if (closing || refreshing || !root) {
        return ui.secondary(sender, event);
    }
    if (event.type == 257 && event.value == 1) {
        const auto found = std::ranges::find(specs, event.id, &ListSpec::button);
        if (found != specs.end()) {
            model.open_list(found->category);
            rebuild_list();
            return 1;
        }
    }
    if (event.type == 1030 && sublist) {
        model.sort_list(ui.list_sort_key(*sublist));
        return 1;
    }
    if (event.type == 1025 && sublist) {
        model.select_list(static_cast<ThingId>(ui.selected_list_identity(*sublist).value_or(0)));
        refresh_page();
        return 1;
    }
    if ((event.type == 1024 || event.type == 1026 || (event.type == 256 && event.id == 38002)) && sublist) {
        const auto target = static_cast<ThingId>(ui.selected_list_identity(*sublist).value_or(0));
        if (target) {
            close();
            if (event.type == 1026) {
                if (host.focus_object) {
                    host.focus_object(target);
                }
            } else {
                open_object(target);
            }
        }
        return 1;
    }
    if (event.type == 256 && event.id == 38022) {
        help_advisor(153);
        return 1;
    }
    if (event.type == 256 && (event.id == 38001 || event.id == 4294967294u)) {
        close();
        return 1;
    }
    return common_event(sender, event);
}
} //namespace simcoaster
