#pragma once
#include "ui_list_columns.hpp"
#include "simcoaster/world_staff_ui.hpp"

namespace simcoaster {
namespace staff_columns {
using F = UiListCellFormat;
inline constexpr std::array<UiListColumn, 4> hire { { { ^^StaffHireUiRow::name, 218, F::text }, { ^^StaffHireUiRow::experience_meter, 219, F::meter }, { ^^StaffHireUiRow::wage, 220, F::signed_currency }, { ^^StaffHireUiRow::agency, 221, F::text } } };
inline constexpr std::array<UiListColumn, 6> list { { { ^^StaffUiRow::name, 234, F::text }, { ^^StaffUiRow::experience_meter, 698, F::meter }, { ^^StaffUiRow::wage, 235, F::signed_currency }, { ^^StaffUiRow::ability, 697, F::text }, { ^^StaffUiRow::happiness_meter, 237, F::meter }, { ^^StaffUiRow::energy_meter, 194, F::meter } } };
inline constexpr std::array<UiListColumn, 5> selector { { { ^^StaffUiRow::name, 696, F::text }, { ^^StaffUiRow::ability, 697, F::text }, { ^^StaffUiRow::experience_meter, 698, F::meter }, { ^^StaffUiRow::happiness_meter, 699, F::meter }, { ^^StaffUiRow::activity, 700, F::text } } };
} //namespace staff_columns

using StaffHireColumns = UiListColumns<staff_columns::hire>;
using StaffListColumns = UiListColumns<staff_columns::list>;
using StaffSelectorColumns = UiListColumns<staff_columns::selector>;
} //namespace simcoaster
