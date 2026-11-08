#pragma once
#include "ui_assets.hpp"
#include "ui_control.hpp"
#include "ui_font.hpp"

namespace simcoaster {
//The World owner binds the installed BFST signed-currency formatter; the
//shared presenter never manufactures an English or system-font substitute.
using UiCurrencyFormatter = std::function<std::u16string(std::int32_t)>;

class UiControlPresenter {
    UiAssetLibrary& assets_;
    UiFontLibrary& fonts_;
    UiCurrencyFormatter currency_;

public:
    UiControlPresenter(UiAssetLibrary&, UiFontLibrary&, UiCurrencyFormatter = {});
    //Append only control-specific list cells/highlight and meter art. The
    //caller retains ordinary model/text attachments and child-tree traversal.
    //Native canvas2048x1536; clip uses logical coordinates. Font-derived row
    //heights also synchronize input/scroll layout through the owning runtime.
    void append(UiDrawList&, UiControl&, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height);
};
} //namespace simcoaster
