#pragma once
#include "simcoaster/ui_control.hpp"
#include <meta>
#include <ranges>
#include <stdexcept>

namespace simcoaster {
struct UiListColumn {
    std::meta::info member;
    std::uint32_t heading;
    UiListCellFormat format;
    std::meta::info visible {};
    bool color_by_sign {};
};

consteval bool valid_ui_list_columns(std::span<const UiListColumn> columns) {
    using namespace std::meta;
    if (columns.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const auto& column = columns[i];
        if (column.member == info {} || !is_nonstatic_data_member(column.member) || is_bit_field(column.member)) {
            return false;
        }
        if (parent_of(column.member) != parent_of(columns.front().member)) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (column.member == columns[j].member) {
                return false;
            }
        }
        const auto type = dealias(type_of(column.member));
        const bool compatible = match (column.format) -> bool {
            case UiListCellFormat::text                                                                                                            => type == dealias(^^std::u16string) && !column.color_by_sign;
            case UiListCellFormat::signed_integer or UiListCellFormat::signed_currency or UiListCellFormat::meter or UiListCellFormat::model_state => type == ^^decltype(std::int32_t {});
            case _                                                                                                                                 => false;
        };
        if (!compatible) {
            return false;
        }
        if (column.visible != info {} && (!is_nonstatic_data_member(column.visible) || parent_of(column.visible) != parent_of(column.member) || dealias(type_of(column.visible)) != ^^bool)) {
            return false;
        }
    }
    return true;
}

template <auto Columns>
struct UiListColumns {
    static_assert(valid_ui_list_columns(Columns), "Incompatible or duplicate UI column metadata");
    static constexpr auto size = Columns.size();
    static constexpr auto headings = std::define_static_array(Columns | std::views::transform(&UiListColumn::heading));
    static constexpr auto formats = std::define_static_array(Columns | std::views::transform(&UiListColumn::format));

    template <class Row>
    static std::vector<UiListCell> cells(const Row& row) {
        std::vector<UiListCell> result;
        result.reserve(size);
        template for (constexpr auto column : Columns) {
            auto& cell = result.emplace_back(row.[:column.member:]);
            if constexpr (column.visible != std::meta::info {}) {
                cell.visible = row.[:column.visible:];
            }
            if constexpr (column.color_by_sign) {
                using Color = std::array<std::uint8_t, 4>;
                cell.color = row.[:column.member:] < 0 ? Color { 255, 0, 0, 255 } : Color { 255, 255, 255, 255 };
            }
        }
        return result;
    }

    template <class Row>
    static bool precedes(const Row& a, const Row& b, std::size_t column, bool descending, const char* error) {
        template for (constexpr auto index : std::views::iota(std::size_t {}, size)) {
            if (column == index) {
                const auto& first = a.[:Columns[index].member:];
                const auto& second = b.[:Columns[index].member:];
                return descending ? first > second : first < second;
            }
        }
        throw std::out_of_range(error);
    }
};
} //namespace simcoaster
