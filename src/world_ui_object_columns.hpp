#pragma once
#include "ui_list_columns.hpp"
#include "simcoaster/world_object_ui_runtime.hpp"

namespace simcoaster {
namespace object_columns {
using F = UiListCellFormat;
using Row = ParkObjectListUiRow;
inline constexpr std::array<UiListColumn, 6> rides {
    { { ^^Row::name, 347, F::text }, { ^^Row::users, 348, F::signed_integer }, { ^^Row::excitement_meter, 349, F::meter }, { ^^Row::satisfaction_meter, 355, F::meter }, { ^^Row::repair_meter, 350, F::meter }, { ^^Row::profit, 172, F::signed_currency, {}, true } }
};
inline constexpr std::array<UiListColumn, 5> shops { { { ^^Row::name, 351, F::text }, { ^^Row::users, 352, F::signed_integer }, { ^^Row::satisfaction_meter, 355, F::meter }, { ^^Row::profit, 172, F::signed_currency, {}, true }, { ^^Row::value_meter, 18, F::meter } } };
inline constexpr std::array<UiListColumn, 6> sideshows {
    { { ^^Row::name, 356, F::text }, { ^^Row::users, 357, F::signed_integer }, { ^^Row::excitement_meter, 358, F::meter }, { ^^Row::satisfaction_meter, 361, F::meter }, { ^^Row::winners, 137, F::signed_integer }, { ^^Row::profit, 172, F::signed_currency, {}, true } }
};
inline constexpr std::array<UiListColumn, 2> features { { { ^^Row::name, 362, F::text }, { ^^Row::owned_count, 363, F::signed_integer } } };
inline constexpr std::array<UiListColumn, 6> facilities { { { ^^Row::name, 162, F::text },
                                                            { ^^Row::users, 145, F::signed_integer, ^^Row::has_usage },
                                                            { ^^Row::satisfaction_meter, 152, F::meter, ^^Row::has_satisfaction },
                                                            { ^^Row::repair_meter, 146, F::meter, ^^Row::has_cleanliness },
                                                            { ^^Row::profit, 172, F::signed_currency, ^^Row::has_finances, true },
                                                            { ^^Row::value_meter, 18, F::meter, ^^Row::has_value } } };
inline constexpr std::array bindings { std::pair { ObjectUiCategory::rides, ^^UiListColumns<rides> },
                                       std::pair { ObjectUiCategory::shops, ^^UiListColumns<shops> },
                                       std::pair { ObjectUiCategory::sideshows, ^^UiListColumns<sideshows> },
                                       std::pair { ObjectUiCategory::features, ^^UiListColumns<features> },
                                       std::pair { ObjectUiCategory::facilities, ^^UiListColumns<facilities> } };
} //namespace object_columns

template <class Error, class F>
decltype(auto) with_object_list_columns(ObjectUiCategory category, F&& visit, const char* error) {
    template for (constexpr auto binding : object_columns::bindings) if (category == binding.first) return visit(typename[:binding.second:] {});
    throw Error(error);
}
} //namespace simcoaster
