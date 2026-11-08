#include "simcoaster/sign_font.hpp"
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace simcoaster {
namespace {
void require(bool condition) {
    if (!condition) {
        throw MeshError { MeshFailure::configuration, 0, "sign pixel buffer", {} };
    }
}

void validate(const SignPixelBuffer& b, std::uint32_t channels) {
    require(b.width && b.height && b.width <= 4096 && b.height <= 4096 && b.channels == channels && b.pixels.size() == std::size_t { b.width } * b.height * channels);
}

SignPixelBuffer buffer(std::uint32_t w, std::uint32_t h, std::uint32_t channels) {
    require(w && h && w <= 4096 && h <= 4096);
    return { w, h, channels, std::vector<std::uint8_t>(std::size_t { w } * h * channels) };
}

std::vector<double> gaussian(float sigma, bool square) {
    require(std::isfinite(sigma) && sigma > 0 && sigma <= 64);
    const auto n = 3 - static_cast<int>(static_cast<double>(sigma) * -2.5), center = (n - 1) / 2;
    std::vector<double> kernel(static_cast<std::size_t>(square ? n * n : n));
    double sum = 0;
    for (int y = 0; y < (square ? n : 1); ++y) {
        for (int x = 0; x < n; ++x) {
            const auto distance = (x - center) * (x - center) + (square ? (y - center) * (y - center) : 0);
            auto& value = kernel[static_cast<std::size_t>(y * n + x)];
            value = std::exp(-0.5 * distance / (double { sigma } * sigma)) / (double { sigma } * sigma * 6.2831853);
            sum += value;
        }
    }
    for (auto& value : kernel) {
        value /= sum;
    }
    return kernel;
}

void blur(SignPixelBuffer& b, float sigma, const SignPixelBuffer* gate = nullptr) {
    if (sigma == 0) {
        return;
    }
    validate(b, 1);
    if (gate) {
        validate(*gate, 1);
        require(gate->width == b.width && gate->height == b.height);
    }
    const auto kernel = gaussian(sigma, gate != nullptr);
    const auto n = gate ? static_cast<int>(std::sqrt(kernel.size())) : static_cast<int>(kernel.size());
    const auto center = (n - 1) / 2, w = static_cast<int>(b.width), h = static_cast<int>(b.height);
    auto source = b.pixels;
    if (gate) {
        //7B7C00's rolling row copies retain original samples, replicate
        //boundaries,
        //and gate output with the original glyph. Even kernel sizes are
        //asymmetric.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (gate->pixels[static_cast<std::size_t>(y * w + x)]) {
                    double value = 0;
                    for (int ky = 0; ky < n; ++ky) {
                        for (int kx = 0; kx < n; ++kx) {
                            value += source[static_cast<std::size_t>(std::clamp(y + ky - center, 0, h - 1) * w + std::clamp(x + kx - center, 0, w - 1))] * kernel[static_cast<std::size_t>(ky * n + kx)];
                        }
                    }
                    b.pixels[static_cast<std::size_t>(y * w + x)] = static_cast<std::uint8_t>(std::clamp(value, 0., 255.));
                }
            }
        }
        return;
    }
    //7B69F0 crops the 1D kernel at each edge without renormalizing it. Quantize
    //after EACH separable pass; it is not the clamped 2D height-mask operation.
    for (int pass = 0; pass < 2; ++pass) {
        source = b.pixels;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double value = 0;
                for (int k = 0; k < n; ++k) {
                    const auto sx = x + (pass ? 0 : k - center), sy = y + (pass ? k - center : 0);
                    if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                        value += source[static_cast<std::size_t>(sy * w + sx)] * kernel[static_cast<std::size_t>(k)];
                    }
                }
                b.pixels[static_cast<std::size_t>(y * w + x)] = static_cast<std::uint8_t>(std::clamp(value, 0., 255.));
            }
        }
    }
}

void morphology(SignPixelBuffer& b, int radius, bool maximum) {
    if (!radius) {
        return;
    }
    validate(b, 1);
    require(radius > 0 && radius <= 4096);
    const auto w = static_cast<int>(b.width), h = static_cast<int>(b.height);
    for (int pass = 0; pass < 2; ++pass) {
        const auto source = b.pixels;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                auto value = maximum ? std::uint8_t {} : std::uint8_t { 255 };
                const auto position = pass ? y : x, extent = pass ? h : w;
                if (!maximum && (position < radius || position > extent - radius - 1)) {
                    b.pixels[static_cast<std::size_t>(y * w + x)] = 0;
                    continue;
                }
                for (int at = std::max(0, position - radius); at <= std::min(extent - 1, position + radius); ++at) {
                    const auto sample = source[static_cast<std::size_t>(pass ? at * w + x : y * w + at)];
                    value = maximum ? std::max(value, sample) : std::min(value, sample);
                }
                b.pixels[static_cast<std::size_t>(y * w + x)] = value;
            }
        }
    }
}

std::uint8_t blend_byte(std::uint8_t old, std::uint8_t target, unsigned coverage) {
    //Native signed differences are multiplied then narrowed to WORD, shifted
    //by8 and added modulo256. Do not replace the denominator with255.
    return static_cast<std::uint8_t>(old + (static_cast<std::uint16_t>(coverage * (int { target } - old)) >> 8));
}

void composite(const SignPixelBuffer& source, SignPixelBuffer& destination) {
    if (source.pixels.empty()) {
        return;
    }
    require(source.width <= destination.width && source.height <= destination.height && source.channels == 4);
    for (std::size_t y = 0; y < source.height; ++y) {
        for (std::size_t x = 0; x < source.width; ++x) {
            const auto s = (y * source.width + x) * 4, d = (y * destination.width + x) * 4;
            const auto a = source.pixels[s];
            destination.pixels[d] = std::max(destination.pixels[d], a);
            for (std::size_t c = 1; c < 4; ++c) {
                destination.pixels[d + c] = blend_byte(destination.pixels[d + c], source.pixels[s + c], a);
            }
        }
    }
}

void colorize(const SignPixelBuffer& mask, SignPixelBuffer& destination, const SignMaskEffect& effect) {
    for (std::size_t i = 0; i < mask.pixels.size(); ++i) {
        const unsigned a = (unsigned { effect.color[3] } * mask.pixels[i]) >> 8;
        destination.pixels[i * 4] = std::max(destination.pixels[i * 4], static_cast<std::uint8_t>(a));
        for (std::size_t c = 0; c < 3; ++c) {
            destination.pixels[i * 4 + c + 1] = blend_byte(destination.pixels[i * 4 + c + 1], static_cast<std::uint8_t>((unsigned { effect.color[3] } * effect.color[c]) >> 8), a);
        }
    }
}

SignPixelBuffer resample_surface(const SignPixelBuffer& source, std::uint32_t height) {
    validate(source, 4);
    require(height > 0 && height <= 4096);
    auto result = buffer(source.width, height, 4);
    //7B2170 partitions source rows with a16.16 accumulator biased by half a
    //source pixel, averages each partition, and truncates each channel. The
    //installed surfaces keep their width; no unnecessary horizontal pass.
    const auto step = ((std::uint64_t { source.height } << 16) + 0x8000u) / height;
    std::size_t start = 0;
    for (std::size_t y = 0; y < height; ++y) {
        auto end = static_cast<std::size_t>(((y + 1) * step) >> 16);
        if (end <= start) {
            //The old routine divided by zero on enlargement. Define the source
            //sample explicitly for that case without an unsafe native buffer read.
            const auto row = std::min(start, std::size_t { source.height } - 1);
            std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(row * source.width * 4), source.width * 4, result.pixels.begin() + static_cast<std::ptrdiff_t>(y * source.width * 4));
        } else {
            end = std::min(end, std::size_t { source.height });
            require(start < end);
            for (std::size_t x = 0; x < source.width * 4; ++x) {
                unsigned sum = 0;
                for (auto row = start; row < end; ++row) {
                    sum += source.pixels[row * source.width * 4 + x];
                }
                result.pixels[y * source.width * 4 + x] = static_cast<std::uint8_t>((sum & 65535u) / (end - start));
            }
        }
        start = end;
    }
    return result;
}

using Float3 = std::array<float, 3>;

Float3 normalized(double x, double y, double z) {
    const auto length = std::sqrt(x * x + y * y + z * z);
    require(std::isfinite(length) && length > 0);
    return { static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length) };
}

float dot(const Float3& a, const Float3& b) {
    return static_cast<float>(double { a[0] } * b[0] + double { a[1] } * b[1] + double { a[2] } * b[2]);
}
} //namespace

SignPixelBuffer transform_sign_mask(const SignPixelBuffer& source, const SignMaskEffect& effect) {
    validate(source, 1);
    require(effect.radius >= -4096 && effect.radius <= 4096);
    auto result = buffer(source.width, source.height, 1);
    for (std::int64_t y = 0; y < source.height; ++y) {
        for (std::int64_t x = 0; x < source.width; ++x) {
            const auto dx = x + effect.offset_x, dy = y + effect.offset_y;
            if (dx >= 0 && dy >= 0 && dx < source.width && dy < source.height) {
                result.pixels[static_cast<std::size_t>(dy * source.width + dx)] = source.pixels[static_cast<std::size_t>(y * source.width + x)];
            }
        }
    }
    if (effect.radius) {
        morphology(result, std::abs(effect.radius), effect.radius > 0);
    }
    blur(result, effect.blur_sigma);
    return result;
}

SignPixelBuffer build_sign_height_profile(const SignPixelBuffer& mask, std::uint32_t steps) {
    validate(mask, 1);
    require(steps <= 256);
    auto result = buffer(mask.width, mask.height, 1);
    auto work = mask.pixels;
    for (auto& pixel : work) {
        pixel = pixel ? 255 : 0;
    }
    const auto w = static_cast<int>(mask.width), h = static_cast<int>(mask.height);
    for (std::uint32_t step = 0; step < steps; ++step) {
        const double t = 1 - static_cast<double>(step) / steps;
        const auto strength = static_cast<std::uint8_t>(static_cast<std::int64_t>((2 * t * t * t - 3 * t * t) * 256));
        auto next = work;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto index = static_cast<std::size_t>(y * w + x);
                auto value = work[index];
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        //The original erosion specifically omits right/bottom neighbors in
                        //the last TWO columns/rows, rather than one. Retain that edge
                        //rule.
                        if ((dx < 0 && x == 0) || (dx > 0 && x >= w - 2) || (dy < 0 && y == 0) || (dy > 0 && y >= h - 2)) {
                            continue;
                        }
                        value = std::min(value, work[static_cast<std::size_t>((y + dy) * w + x + dx)]);
                    }
                }
                next[index] = value;
                if (value != work[index]) {
                    result.pixels[index] = strength;
                }
            }
        }
        work = std::move(next);
    }
    for (std::size_t i = 0; i < work.size(); ++i) {
        result.pixels[i] |= work[i];
    }
    return result;
}

SignPixelBuffer shade_sign_surface(const SignTextMask& mask, const SignFontDefinition& font) {
    validate(mask.image, 1);
    auto result = buffer(mask.image.width, mask.image.height, 4);
    const auto ink_height = mask.ink_bottom - mask.ink_top;
    if (ink_height <= 0 || font.surface.pixels.empty()) {
        return result;
    }
    auto profile = build_sign_height_profile(mask.image, static_cast<std::uint32_t>(font.lighting.edge_steps));
    if (font.lighting.blur_sigma > 0) {
        blur(profile, font.lighting.blur_sigma, &mask.image);
    }
    const auto surface = resample_surface(font.surface, static_cast<std::uint32_t>(ink_height));
    const auto& p = font.lighting;
    const double elevation = p.elevation_degrees * -0.017453292, azimuth = p.azimuth_degrees * 0.017453292;
    const auto light = normalized(std::cos(elevation) * std::cos(azimuth), -std::cos(elevation) * std::sin(azimuth), -std::sin(elevation));
    const auto half = normalized(light[0] * 0.5, light[1] * 0.5, (double { light[2] } + 1) * 0.5);
    const auto w = std::size_t { mask.image.width };
    for (std::size_t y = 0; y < mask.image.height; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const auto index = y * w + x;
            const auto coverage = mask.image.pixels[index];
            if (!coverage) {
                continue;
            }
            const auto center = profile.pixels[index];
            //Native's positive-neighbor reads rely on the empty glyph border. An
            //explicit clamped sample retains defined behavior for imported edge ink.
            const double dx = (int { profile.pixels[std::min(index + 1, profile.pixels.size() - 1)] } - center) * double { p.height_scale } * 0.0039215689;
            const double dy = (int { profile.pixels[std::min(index + w, profile.pixels.size() - 1)] } - center) * double { p.height_scale } * 0.0039215689;
            const auto normal = normalized(dx, dy, 1);
            const auto diffuse = dot(normal, light), specular = dot(normal, half);
            const auto row = static_cast<std::size_t>((static_cast<std::int64_t>(y) - mask.ink_top) % ink_height);
            require(row < surface.height);
            const auto sample = (row * surface.width + x % surface.width) * 4;
            result.pixels[index * 4] = coverage;
            for (std::size_t c = 0; c < 3; ++c) {
                const float source = static_cast<float>(surface.pixels[sample + c + 1] * 0.0039215689);
                float color = source * p.ambient;
                if (diffuse > 0) {
                    color += static_cast<float>(double { diffuse } * p.diffuse * source);
                }
                if (specular > 0) {
                    color += static_cast<float>(std::pow(specular, p.exponent) * p.specular);
                }
                color *= 255;
                require(std::isfinite(color));
                result.pixels[index * 4 + c + 1] = static_cast<std::uint8_t>(std::clamp(color, 0.f, 255.f));
            }
        }
    }
    return result;
}

SignPixelBuffer compose_sign_canvas(const SignFontConfiguration& configuration, const std::array<SignTextMask, 2>& masks) {
    validate(masks[0].image, 1);
    validate(masks[1].image, 1);
    require(masks[0].image.width == masks[1].image.width && masks[0].image.height == masks[1].image.height);
    auto canvas = buffer(masks[0].image.width, masks[0].image.height, 4);
    if (configuration.background) {
        composite(*configuration.background, canvas);
    }
    std::array<SignPixelBuffer, 2> effects, shaded;
    for (std::size_t i = 0; i < 2; ++i) {
        effects[i] = configuration.effect_placement[i] == SignEffectPlacement::disabled ? buffer(canvas.width, canvas.height, 1) : transform_sign_mask(masks[i].image, configuration.effects[i]);
        shaded[i] = shade_sign_surface(masks[i], configuration.fonts[i]);
    }
    const bool first_behind = configuration.effect_placement[0] == SignEffectPlacement::behind_all_text;
    const bool second_behind = configuration.effect_placement[1] == SignEffectPlacement::behind_all_text;
    if (first_behind) {
        if (second_behind) {
            for (std::size_t i = 0; i < effects[0].pixels.size(); ++i) {
                effects[0].pixels[i] = std::max(effects[0].pixels[i], effects[1].pixels[i]);
            }
        }
        colorize(effects[0], canvas, configuration.effects[0]);
    } else if (second_behind) {
        colorize(effects[1], canvas, configuration.effects[1]);
    }
    for (const auto i : configuration.primary_on_top ? std::array<std::size_t, 2> { 1, 0 } : std::array<std::size_t, 2> { 0, 1 }) {
        if (configuration.effect_placement[i] == SignEffectPlacement::before_text) {
            colorize(effects[i], canvas, configuration.effects[i]);
        }
        composite(shaded[i], canvas);
    }
    return canvas;
}

SignTexturePair split_sign_canvas(const SignPixelBuffer& canvas, std::uint32_t width, std::uint32_t height) {
    validate(canvas, 4);
    require(width && height && canvas.width >= 2 * width && canvas.height >= height);
    SignTexturePair result;
    for (std::size_t half = 0; half < 2; ++half) {
        auto image = std::make_shared<TextureImage>();
        image->width = width;
        image->height = height;
        image->alpha = TextureAlpha::translucent;
        image->source_mode = TextureSourceMode::alpha_low;
        image->pixels.reserve(std::size_t { width } * height);
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                //7B9F00 writes the top-down canvas into descending texture rows.
                //Generated signs share the mesh texture convention of TGA/SHPI.
                const auto index = ((height - 1 - y) * canvas.width + x + half * width) * 4;
                image->pixels.push_back({ canvas.pixels[index + 1], canvas.pixels[index + 2], canvas.pixels[index + 3], canvas.pixels[index] });
            }
        }
        result.halves[half] = std::move(image);
    }
    return result;
}
} //namespace simcoaster
