#include "application_postcard_image.hpp"
#include "simcoaster/ui_control.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
TextureImage load_art(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Missing original postcard decoration: " + path.native_encoded_string());
    }
    const auto length = file.tellg();
    if (length < 18) {
        throw std::runtime_error("Invalid postcard decoration header");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read original postcard decoration");
    }
    auto image = decode_tga_image(bytes);
    if (!image) {
        throw std::runtime_error("Original postcard decoration: " + describe_texture_error(image.error()));
    }
    return std::move(*image);
}

TextureImage capture(const TextureImage& source, std::uint32_t width, std::uint32_t height) {
    if (source.width < width || source.height < height || source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) {
        throw std::runtime_error("Postcard capture requires the " "actual640x480-or-larger rendered surface");
    }
    TextureImage result { width, height, {}, TextureAlpha::opaque, TextureSourceMode::opaque };
    result.pixels.resize(static_cast<std::size_t>(width) * height);
    //78B250: integer16.16 edge differences, horizontal byte averages followed
    //by vertical byte averages. No interpolation of invented scene pixels.
    const auto xstep = (std::uint64_t { source.width } << 16) / width;
    const auto ystep = (std::uint64_t { source.height } << 16) / height;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto top = static_cast<std::uint32_t>((y * ystep) >> 16);
        const auto bottom = static_cast<std::uint32_t>(((y + 1) * ystep) >> 16);
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto left = static_cast<std::uint32_t>((x * xstep) >> 16);
            const auto right = static_cast<std::uint32_t>(((x + 1) * xstep) >> 16);
            if (bottom == top || right == left || bottom > source.height || right > source.width) {
                throw std::runtime_error("Invalid original postcard resampling interval");
            }
            std::array<std::uint32_t, 3> vertical {};
            for (auto row = top; row < bottom; ++row) {
                std::array<std::uint32_t, 3> horizontal {};
                for (auto column = left; column < right; ++column) {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        horizontal[channel] += source.pixels[static_cast<std::size_t>(row) * source.width + column][channel];
                    }
                }
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    vertical[channel] += static_cast<std::uint16_t>(horizontal[channel]) / (right - left);
                }
            }
            auto& pixel = result.pixels[static_cast<std::size_t>(y) * width + x];
            for (std::size_t channel = 0; channel < 3; ++channel) {
                pixel[channel] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(vertical[channel]) / (bottom - top));
            }
            pixel[3] = 255;
        }
    }
    return result;
}

void overlay(TextureImage& destination, const TextureImage& source, std::uint32_t x, std::uint32_t y) {
    if (x > destination.width || y > destination.height || source.width > destination.width - x || source.height > destination.height - y) {
        throw std::runtime_error("Original postcard decoration exceeds its composition bounds");
    }
    for (std::uint32_t row = 0; row < source.height; ++row) {
        for (std::uint32_t column = 0; column < source.width; ++column) {
            const auto& from = source.pixels[static_cast<std::size_t>(row) * source.width + column];
            auto& to = destination.pixels[static_cast<std::size_t>(y + row) * destination.width + x + column];
            //78BAC0: exact8-bit source alpha /256 blend, including negative deltas.
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto delta = static_cast<std::int32_t>(from[channel]) - to[channel];
                const auto product = static_cast<std::uint16_t>(delta * from[3]);
                to[channel] = static_cast<std::uint8_t>(to[channel] + (product >> 8));
            }
            to[3] = 255;
        }
    }
}

void draw_text(TextureImage& destination, UiFontLibrary& fonts, std::u16string_view text, bool hires) {
    const auto selected = fonts.slot(12, 640);
    if (!selected) {
        throw std::runtime_error("Original postcard BF4 font: " + selected.error().reason);
    }
    UiTextStyle style;
    style.color = { 0, 0, 204, 255 };
    style.vertical = UiTextVertical::middle;
    style.wrap = true;
    const std::array<float, 4> bounds = hires ? std::array<float, 4> { 644, 188, 1272, 396 } : std::array<float, 4> { 322, 94, 636, 198 };
    const auto clip = bounds;
    UiDrawList draw;
    const auto layout = fonts.append(draw, **selected, text, bounds, style, clip);
    if (!layout) {
        throw std::runtime_error("Postcard BF4 message layout: " + layout.error().reason);
    }
    for (const auto& command : draw.commands) {
        if (!command.texture) {
            throw std::logic_error("Postcard glyph lost its BF4 atlas");
        }
        const auto& atlas = *command.texture;
        for (std::size_t index = command.first_index; index < static_cast<std::size_t>(command.first_index) + command.index_count; index += 6) {
            const auto& a = draw.vertices.at(draw.indices.at(index));
            const auto& b = draw.vertices.at(draw.indices.at(index + 1));
            const auto& d = draw.vertices.at(draw.indices.at(index + 5));
            const auto left = std::max(static_cast<int>(bounds[0]), static_cast<int>(std::ceil(a.position[0]))), top = std::max(static_cast<int>(bounds[1]), static_cast<int>(std::ceil(a.position[1])));
            const auto right = std::min(static_cast<int>(bounds[2]), static_cast<int>(std::ceil(b.position[0])));
            const auto bottom = std::min(static_cast<int>(bounds[3]), static_cast<int>(std::ceil(d.position[1])));
            for (auto y = top; y < bottom; ++y) {
                for (auto x = left; x < right; ++x) {
                    const auto u = a.uv[0] + (static_cast<float>(x) + .5f - a.position[0]) / (b.position[0] - a.position[0]) * (b.uv[0] - a.uv[0]);
                    const auto v = a.uv[1] + (static_cast<float>(y) + .5f - a.position[1]) / (d.position[1] - a.position[1]) * (d.uv[1] - a.uv[1]);
                    const auto sx = std::clamp(static_cast<std::int32_t>(u * static_cast<float>(atlas.width)), 0, static_cast<std::int32_t>(atlas.width) - 1);
                    const auto sy = std::clamp(static_cast<std::int32_t>(v * static_cast<float>(atlas.height)), 0, static_cast<std::int32_t>(atlas.height) - 1);
                    const auto alpha = static_cast<std::uint32_t>(atlas.pixels[static_cast<std::size_t>(sy) * atlas.width + static_cast<std::size_t>(sx)][3]);
                    auto& pixel = destination.pixels[static_cast<std::size_t>(y) * destination.width + static_cast<std::size_t>(x)];
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        pixel[channel] = static_cast<std::uint8_t>((alpha * style.color[channel] + (255u - alpha) * pixel[channel]) / 255u);
                    }
                }
            }
        }
    }
}
} //namespace

PostcardImageComposer::PostcardImageComposer(const std::filesystem::path& path, std::string_view language, UiFontLibrary& fonts, const TextureImage& source, bool hires)
    : hires_(hires),
      american_(language.size() == 8
                && std::equal(language.begin(),
                              language.end(),
                              std::string_view("American").begin(),
                              [](char a, char b) {
                                  return (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b);
                              })),
      fonts_(fonts) {
    const std::array<std::string_view, 6> names { "hplogo", "tpwlogo", "stamp", "frank", "legal", "hpcameratext" };
    for (std::size_t n = 0; n < names.size(); ++n) {
        art_[n] = load_art(path / "Data/postcard" / (std::string(names[n]) + (hires ? "HiRes.tga" : ".tga")));
    }
    scene_ = capture(source, hires ? 640u : 320u, hires ? 480u : 240u);
    composite_.width = hires ? 1280u : 640u;
    composite_.height = hires ? 480u : 240u;
    composite_.pixels.resize(static_cast<std::size_t>(composite_.width) * composite_.height);
    composite_.alpha = TextureAlpha::opaque;
    composite_.source_mode = TextureSourceMode::opaque;
    compose({}, false);
}

void PostcardImageComposer::compose(std::u16string_view message, bool include_text) {
    std::ranges::fill(composite_.pixels, RgbaPixel { 251, 248, 198, 255 });
    overlay(composite_, scene_, 0, 0);
    const auto factor = hires_ ? 2u : 1u;
    overlay(composite_, art_[2], 544 * factor, 0);
    overlay(composite_, art_[3], 477 * factor, 6 * factor);
    overlay(composite_, art_[4], 321 * factor, 218 * factor);
    if (!american_) {
        overlay(composite_, art_[5], 321 * factor, 201 * factor);
    }
    overlay(composite_, art_[1], 0, 0);
    if (!american_) {
        overlay(composite_, art_[0], 268 * factor, 201 * factor);
    }
    if (include_text) {
        draw_text(composite_, fonts_, message, hires_);
    }
}

void append_postcard_surface(UiDrawList& draw, TextureImagePtr image, const UiControl& control, const UiRect& clip) {
    if (!image || !control.visible()) {
        return;
    }
    if (draw.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 4 || draw.indices.size() > std::numeric_limits<std::uint32_t>::max() - 6) {
        throw std::overflow_error("Postcard draw list overflow");
    }
    const auto& bounds = control.attachment_rect;
    const auto first = static_cast<std::uint32_t>(draw.vertices.size());
    const auto depth = 1.f - static_cast<float>(control.render_depth + 1) / 40.f;
    const auto l = static_cast<float>(bounds.left), t = static_cast<float>(bounds.top), r = static_cast<float>(bounds.right), b = static_cast<float>(bounds.bottom);
    draw.vertices.insert(draw.vertices.end(), { { { l, t, depth }, { 0, 0 } }, { { r, t, depth }, { 1, 0 } }, { { r, b, depth }, { 1, 1 } }, { { l, b, depth }, { 0, 1 } } });
    UiDrawCommand command;
    command.texture = std::move(image);
    command.first_index = static_cast<std::uint32_t>(draw.indices.size());
    command.index_count = 6;
    command.clip = { std::max(l, static_cast<float>(clip.left)), std::max(t, static_cast<float>(clip.top)), std::min(r, static_cast<float>(clip.right)), std::min(b, static_cast<float>(clip.bottom)) };
    command.material.flags |= 0x20000u | 0x40000u;
    for (const auto i : { 0u, 1u, 2u, 0u, 2u, 3u }) {
        draw.indices.push_back(first + i);
    }
    draw.commands.push_back(std::move(command));
}
} //namespace simcoaster
