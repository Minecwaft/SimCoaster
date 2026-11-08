#pragma once
#include "ui_control.hpp"
#include <filesystem>

namespace simcoaster {
class UiFontLibrary;
struct WorldRuntime;

//Original delayed !tooltip model and font7 caption. This owner contains only
//process UI state; the control callback clears borrowed pointers on event20.
//A null event_root represents UiRuntime's common routing root, so the popup
//remains independent of the primary/secondary World-view visibility switch.
class UiTooltipController {
    struct State;
    std::unique_ptr<State> state_;

public:
    UiTooltipController(UiRuntime&,
                        UiControl* event_root,
                        UiFontLibrary&,
                        WorldRuntime&,
                        const std::filesystem::path& game_root);
    ~UiTooltipController();
    UiTooltipController(const UiTooltipController&) = delete;
    UiTooltipController& operator=(const UiTooltipController&) = delete;
    void update(std::u16string_view, UiPoint, std::uint32_t pixel_width, std::uint32_t pixel_height);
    //Event12 may supply a copied text override or an original UIHELPTEXT row.
    //A null source clears help. Offsets outside the native291-entry table are
    //zero; the native unchecked read into the following World fields is omitted.
    void update_help(UiControl*, UiPoint, std::uint32_t pixel_width, std::uint32_t pixel_height);
    void enabled(bool);
    void offsets(UiPoint); //ToolTips[].XOffset/YOffset, consumed transposed.
    void frame_rate_below_clip(bool);
    UiControl* root() const;
};
} //namespace simcoaster
