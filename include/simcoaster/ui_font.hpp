#pragma once
#include "ui_draw.hpp"
#include <expected>
#include <filesystem>
#include <map>

namespace simcoaster {
struct UiFontError {
    std::size_t offset {};
    std::string resource, reason;
};

struct UiBitmapGlyph {
    std::uint32_t character_code {};
    std::uint16_t width {}, height {}, advance {};
    std::int8_t bearing_x {}, bearing_y {};
    TextureImagePtr image;
    std::array<float, 4> uv {};
};

struct UiBitmapFont {
    std::string name;
    std::uint8_t line_advance {};
    std::vector<UiBitmapGlyph> glyphs;
};

std::expected<UiBitmapFont, UiFontError> parse_ui_bitmap_font(std::span<const std::byte>, std::string name = {});
enum class UiTextHorizontal : std::uint8_t {
    left,
    center,
    right
};
enum class UiTextVertical : std::uint8_t {
    top,
    middle,
    bottom
};

struct UiTextStyle {
    RgbaPixel color { 255, 255, 255, 255 };
    UiTextHorizontal horizontal {};
    UiTextVertical vertical {};
    //Logical UI units per original bitmap pixel, independently on each axis.
    std::array<float, 2> scale { 1, 1 };
    //Native preprojection surface Z; absent inherits the owning control's
    //UiNativeDepthScope. Diagnostic text without either remains depthless.
    std::optional<float> depth;
    bool wrap = true;
};

struct UiTextExtent {
    std::array<float, 4> bounds {};
    std::size_t lines {};
    //Character pen positions plus a terminal position, for edit/caret clients.
    std::vector<std::array<float, 2>> positions;
};

class UiFontLibrary {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit UiFontLibrary(const std::filesystem::path& game_root, std::string language = "American");
    ~UiFontLibrary();
    UiFontLibrary(const UiFontLibrary&) = delete;
    UiFontLibrary& operator=(const UiFontLibrary&) = delete;
    std::expected<std::shared_ptr<const UiBitmapFont>, UiFontError> load(std::string_view name);
    //Native five resolution groups by fourteen UIFontSlot ordinals.
    std::expected<std::shared_ptr<const UiBitmapFont>, UiFontError> slot(std::uint32_t font_ordinal, std::uint32_t pixel_width);
    std::expected<UiTextExtent, UiFontError> measure(const UiBitmapFont&, std::u16string_view, const std::array<float, 4>& bounds, const UiTextStyle& = {}) const;
    std::expected<UiTextExtent, UiFontError> append(UiDrawList&, const UiBitmapFont&, std::u16string_view, const std::array<float, 4>& bounds, const UiTextStyle& = {}, const std::array<float, 4>& clip = { 0, 0, 2048, 1536 }) const;
};
} //namespace simcoaster
