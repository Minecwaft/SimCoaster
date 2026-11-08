#pragma once
#include "simcoaster/application_postcards.hpp"
#include "simcoaster/ui_font.hpp"

namespace simcoaster {
struct UiControl;
struct UiRect;

class PostcardImageComposer {
    std::array<TextureImage, 6> art_;
    TextureImage scene_, composite_;
    bool hires_{}, american_{};
    UiFontLibrary& fonts_;

public:
    PostcardImageComposer(const std::filesystem::path&,
                          std::string_view language,
                          UiFontLibrary&,
                          const TextureImage& capture,
                          bool hires);
    void compose(std::u16string_view message, bool draw_text = true);

    const TextureImage& image() const {
        return composite_;
    }
};

void append_postcard_surface(UiDrawList&, TextureImagePtr, const UiControl&, const UiRect&);
} //namespace simcoaster
