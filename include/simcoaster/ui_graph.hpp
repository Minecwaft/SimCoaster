#pragma once
#include "ui_control.hpp"
#include "ui_font.hpp"
#include "world_ui.hpp"

namespace simcoaster {
struct UiGraphSeriesStyle {
    std::uint32_t history_index {};
    RgbaPixel color { 255, 255, 255, 255 };
    bool operator==(const UiGraphSeriesStyle&) const = default;
};

//4F6C90/4F6E60 graph surface, axes and bitmap labels. Samples have already
//been resampled by the gameplay history owner (4F6820/4F6990/4F6B00).
class UiGraphPresenter {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiGraphPresenter(UiFontLibrary&);
    ~UiGraphPresenter();
    void append(UiDrawList&, const UiControl&, const StatisticsGraphUiState&, std::span<const UiGraphSeriesStyle>, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
