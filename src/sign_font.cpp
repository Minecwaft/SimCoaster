#include "simcoaster/sign_font.hpp"
#include <cmath>

namespace simcoaster {
namespace {
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t cursor{};

    [[noreturn]] void fail(MeshFailure reason) const {
        throw MeshError{ reason, cursor, "sign configuration", {} };
    }

    std::span<const std::byte> take(std::size_t count) {
        if (count > bytes.size() - cursor)
            fail(MeshFailure::bounds);
        const auto result = bytes.subspan(cursor, count);
        cursor += count;
        return result;
    }

    template <class T>
    T get() {
        constexpr auto n = save::value_width<T>;
        const auto source = take(n);
        T result{};
        save::detail::decode<T>(std::span<const std::byte, n>{ source.data(), n }, result);
        return result;
    }

    SignPixelBuffer image() {
        SignPixelBuffer result{ get<std::uint32_t>(), get<std::uint32_t>(), get<std::uint32_t>(), {} };
        if (result.channels != 0 && result.channels != 1 && result.channels != 2 && result.channels != 4)
            fail(MeshFailure::metadata);
        if (result.width > 4096 || result.height > 4096)
            fail(MeshFailure::bounds);
        const auto data = take(std::size_t{ result.width } * result.height * result.channels);
        result.pixels.reserve(data.size());
        for (auto b : data)
            result.pixels.push_back(std::to_integer<std::uint8_t>(b));
        return result;
    }
};
} //namespace

std::expected<SignFontConfiguration, MeshError> parse_sign_font_configuration(std::span<const std::byte> bytes) {
    try {
        Reader r{ bytes };
        SignFontConfiguration result;
        result.version = r.get<std::uint32_t>();
        if (result.version < 100)
            r.fail(MeshFailure::version);
        result.primary_on_top = r.get<std::uint32_t>();
        const auto background = r.get<std::uint8_t>();
        result.background_requested = background != 0;
        for (auto& placement : result.effect_placement) {
            placement = r.get<SignEffectPlacement>();
            if (std::to_underlying(placement) > 2)
                r.fail(MeshFailure::metadata);
        }
        for (auto& font : result.fonts) {
            font.exported_settings = r.get<std::array<std::byte, 64>>();
            const auto resource = r.get<std::array<char, 260>>();
            const auto end = std::ranges::find(resource, '\0');
            if (end == resource.end())
                r.fail(MeshFailure::unterminated_string);
            font.resource.assign(resource.begin(), end);
            font.advance_percent = r.get<std::int32_t>();
            font.baseline_y = r.get<std::int32_t>();
            font.logical = r.get<SignLogicalFont>();
            font.lighting = r.get<SignSurfaceLighting>();
            if (font.advance_percent < 0
                || font.advance_percent > 10000
                || font.lighting.edge_steps < 0
                || font.lighting.edge_steps > 256
                || !std::isfinite(font.lighting.blur_sigma)
                || font.lighting.blur_sigma < 0
                || font.lighting.blur_sigma > 64)
                r.fail(MeshFailure::configuration);
        }
        for (std::size_t i = 0; i < 2; ++i)
            if (result.effect_placement[i] != SignEffectPlacement::disabled) {
                result.effects[i] = r.get<SignMaskEffect>();
                const auto& e = result.effects[i];
                if (e.radius < -4096
                    || e.radius > 4096
                    || !std::isfinite(e.blur_sigma)
                    || e.blur_sigma < 0
                    || e.blur_sigma > 64)
                    r.fail(MeshFailure::configuration);
            }
        for (auto& font : result.fonts) {
            font.surface = r.image();
            if (!font.surface.pixels.empty() && font.surface.channels != 4)
                r.fail(MeshFailure::metadata);
        }
        if (background) {
            result.exported_background_data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(r.cursor), bytes.end());
        } else if (r.cursor != bytes.size())
            r.fail(MeshFailure::metadata);
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::array<std::u16string, 2> split_sign_caption(std::u16string_view input) {
    input = input.substr(0, input.find(char16_t{}));
    const auto middle = input.size() / 2;
    for (std::size_t probe = 0; probe < input.size(); ++probe) {
        const auto delta = (probe + 1) / 2;
        const auto index = (probe & 1u) ? middle - delta : middle + delta;
        if (index < input.size() && input[index] == u' ')
            return { std::u16string{ input.substr(0, index) }, std::u16string{ input.substr(index + 1) } };
    }
    return { std::u16string{ input }, {} };
}
} //namespace simcoaster
