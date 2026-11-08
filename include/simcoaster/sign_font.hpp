#pragma once
#include "texture_image.hpp"

namespace simcoaster {
//The serialized LOGFONTA is data, independent of the host handle/pointer ABI.
struct SignLogicalFont {
    [[=save::Field{ "height", 0 }]] std::int32_t height{};
    [[=save::Field{ "width", 1 }]] std::int32_t width{};
    [[=save::Field{ "escapement", 2 }]] std::int32_t escapement{};
    [[=save::Field{ "orientation", 3 }]] std::int32_t orientation{};
    [[=save::Field{ "weight", 4 }]] std::int32_t weight{ 400 };
    [[=save::Field{ "style_and_charset", 5 }]] std::array<std::uint8_t, 8> style{ 0, 0, 0, 1, 4, 1, 4, 0 };
    [[=save::Field{ "face", 6 }]] std::array<char, 32> face{ 'A', 'r', 'i', 'a', 'l', ' ', 'B', 'l', 'a', 'c', 'k' };
};

struct SignSurfaceLighting {
    [[=save::Field{ "edge_steps", 0 }]] std::int32_t edge_steps{ 3 };
    [[=save::Field{ "height_scale", 1 }]] float height_scale{ 10 };
    [[=save::Field{ "blur_sigma", 2 }]] float blur_sigma{ 1 };
    [[=save::Field{ "ambient", 3 }]] float ambient{};
    [[=save::Field{ "diffuse", 4 }]] float diffuse{};
    [[=save::Field{ "specular", 5 }]] float specular{ 1 };
    [[=save::Field{ "exponent", 6 }]] float exponent{};
    [[=save::Field{ "elevation_degrees", 7 }]] float elevation_degrees{};
    [[=save::Field{ "azimuth_degrees", 8 }]] float azimuth_degrees{};
    [[=save::Field{ "stored_ink_height", 9 }]] std::int32_t stored_ink_height{};
    [[=save::Field{ "stored_ink_top", 10 }]] std::int32_t stored_ink_top{};
};
enum class SignEffectPlacement : std::uint32_t {
    disabled = 0,
    before_text = 1,
    behind_all_text = 2
};

struct SignMaskEffect {
    [[=save::Field{ "rgb_opacity", 0 }]] std::array<std::uint8_t, 4> color{};
    [[=save::Field{ "morphology_radius", 1 }]] std::int32_t radius{};
    [[=save::Field{ "blur_sigma", 2 }]] float blur_sigma{};
    [[=save::Field{ "offset_x", 3 }]] std::int32_t offset_x{};
    [[=save::Field{ "offset_y", 4 }]] std::int32_t offset_y{};
};

struct SignPixelBuffer {
    std::uint32_t width{}, height{}, channels{};
    //Native compositing order: A,R,G,B. A one-channel buffer is a gray mask.
    std::vector<std::uint8_t> pixels;
};

struct SignFontDefinition {
    std::array<std::byte, 64> exported_settings{};
    std::string resource;
    std::int32_t advance_percent{ 100 }, baseline_y{};
    SignLogicalFont logical;
    SignSurfaceLighting lighting;
    SignPixelBuffer surface;
};

struct SignFontConfiguration {
    std::uint32_t version{}, primary_on_top{};
    bool background_requested{};
    std::array<SignEffectPlacement, 2> effect_placement{};
    std::array<SignFontDefinition, 2> fonts;
    std::array<SignMaskEffect, 2> effects;
    std::optional<SignPixelBuffer> background;
    //Native7B16F0 requests the whole stream size after earlier reads, so its
    //embedded-image load fails and7B8FF0 ignores that failure. Retain exported
    //data for tooling without assigning it an invented SHPI interpretation.
    std::vector<std::byte> exported_background_data;
};

struct SignTextMask {
    SignPixelBuffer image;
    std::int32_t ink_top{}, ink_bottom{};
};

struct SignTexturePair {
    std::array<TextureImagePtr, 2> halves;
};

static_assert(save::record_width<SignLogicalFont> == 60);
static_assert(save::record_width<SignSurfaceLighting> == 44);
static_assert(save::record_width<SignMaskEffect> == 20);
std::expected<SignFontConfiguration, MeshError> parse_sign_font_configuration(std::span<const std::byte>);
//Original two-line auto split searches outward from the midpoint, right first.
std::array<std::u16string, 2> split_sign_caption(std::u16string_view);
//Pure native mask/compositing stages, shared by the renderer and native
//oracle.
SignPixelBuffer transform_sign_mask(const SignPixelBuffer&, const SignMaskEffect&);
SignPixelBuffer build_sign_height_profile(const SignPixelBuffer&, std::uint32_t steps);
SignPixelBuffer shade_sign_surface(const SignTextMask&, const SignFontDefinition&);
SignPixelBuffer compose_sign_canvas(const SignFontConfiguration&, const std::array<SignTextMask, 2>&);
SignTexturePair split_sign_canvas(const SignPixelBuffer&, std::uint32_t width = 128, std::uint32_t height = 128);

class SignFontLibrary {
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit SignFontLibrary(std::filesystem::path font_archive);
    ~SignFontLibrary();
    SignFontLibrary(const SignFontLibrary&) = delete;
    SignFontLibrary& operator=(const SignFontLibrary&) = delete;
    std::expected<SignTextMask, MeshError>
    rasterize(const SignFontDefinition&, std::u16string_view, std::uint32_t width = 256, std::uint32_t height = 256);
    std::expected<SignTexturePair, MeshError>
    render(const SignFontConfiguration&, std::u16string_view first, std::u16string_view second);
};
} //namespace simcoaster
