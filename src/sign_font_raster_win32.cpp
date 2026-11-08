#include "simcoaster/sign_font.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <format>
#include <cstring>
#include <map>
#include <usp10.h>
#include <utility>
#include <windows.h>

namespace simcoaster {
namespace {
[[noreturn]] void fail(std::string operation) {
    throw MeshError { MeshFailure::configuration, GetLastError(), "sign font: " + operation, {} };
}

void check(bool result, std::string operation) {
    if (!result) {
        fail(std::move(operation));
    }
}

struct PrivateFont {
    HANDLE handle {};

    ~PrivateFont() {
        if (handle) {
            RemoveFontMemResourceEx(handle);
        }
    }
};

struct Analysis {
    SCRIPT_STRING_ANALYSIS value {};

    ~Analysis() {
        if (value) {
            ScriptStringFree(&value);
        }
    }

    Analysis() = default;
    Analysis(const Analysis&) = delete;
    Analysis& operator=(const Analysis&) = delete;

    void reset() {
        if (value) {
            ScriptStringFree(&value);
        }
        value = nullptr;
    }
};

struct RasterDevice {
    HDC dc { CreateCompatibleDC(nullptr) };
    HBITMAP bitmap {};
    HGDIOBJ previous_bitmap {}, previous_font {};
    HFONT font {};

    ~RasterDevice() {
        if (dc && previous_font) {
            SelectObject(dc, previous_font);
        }
        if (font) {
            DeleteObject(font);
        }
        if (dc && previous_bitmap) {
            SelectObject(dc, previous_bitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }

    void select_font(const LOGFONTA& source) {
        if (previous_font) {
            SelectObject(dc, previous_font);
        }
        if (font) {
            DeleteObject(font);
        }
        font = CreateFontIndirectA(&source);
        check(font != nullptr, "CreateFontIndirectA");
        previous_font = SelectObject(dc, font);
        check(previous_font && previous_font != HGDI_ERROR, "SelectObject(font)");
    }
};

std::string normalized(std::string text) {
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}
} //namespace

struct SignFontLibrary::State {
    std::filesystem::path source;
    std::optional<DwfbArchive> archive;
    std::map<std::string, std::unique_ptr<PrivateFont>> fonts;

    explicit State(std::filesystem::path path) : source(std::move(path)) {}

    void bind(std::string_view name) {
        if (name.empty()) {
            return;
        }
        const auto key = normalized(std::string { name });
        if (fonts.contains(key)) {
            return;
        }
        if (!archive) {
            auto loaded = load_dwfb_archive(source);
            if (!loaded) {
                throw MeshError { MeshFailure::archive, loaded.error().offset, source.native_encoded_string(), loaded.error() };
            }
            archive = std::move(*loaded);
        }
        const auto index = archive->find_entry(key);
        if (!index) {
            throw MeshError { MeshFailure::missing, 0, source.native_encoded_string() + "/" + key, {} };
        }
        auto data = archive->read_entry(*index);
        if (!data) {
            throw MeshError { MeshFailure::archive, data.error().offset, key, data.error() };
        }
        if (data->size() > std::numeric_limits<DWORD>::max()) {
            fail("font resource size");
        }
        auto font = std::make_unique<PrivateFont>();
        DWORD count {};
        font->handle = AddFontMemResourceEx(data->data(), static_cast<DWORD>(data->size()), nullptr, &count);
        check(font->handle && count, "AddFontMemResourceEx " + key);
        fonts.emplace(key, std::move(font));
    }
};

SignFontLibrary::SignFontLibrary(std::filesystem::path source) : state_(std::make_unique<State>(std::move(source))) {}

SignFontLibrary::~SignFontLibrary() = default;

std::expected<SignTextMask, MeshError> SignFontLibrary::rasterize(const SignFontDefinition& definition, std::u16string_view input, std::uint32_t width, std::uint32_t height) {
    try {
        check(width && height && width <= 2048 && height <= 2048, "raster dimensions");
        SignTextMask result { { width, height, 1, std::vector<std::uint8_t>(std::size_t { width } * height) }, 0, 0 };
        input = input.substr(0, input.find(char16_t {}));
        if (input.empty()) {
            return result;
        }
        check(input.size() <= 4096, "caption length");
        state_->bind(definition.resource);
        static_assert(sizeof(wchar_t) == sizeof(char16_t));
        //An owned wide string preserves the Unicode units without aliasing a
        //char16_t object as wchar_t, including paired/supplementary characters.
        const std::wstring text { input.begin(), input.end() };

        struct BitmapInfo {
            BITMAPINFOHEADER header;
            std::array<RGBQUAD, 256> colors;
        };

        BitmapInfo info {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = static_cast<LONG>(width * 2);
        info.header.biHeight = -static_cast<LONG>(height * 2);
        info.header.biPlanes = 1;
        info.header.biBitCount = 8;
        info.header.biCompression = BI_RGB;
        info.header.biClrUsed = 256;
        info.header.biClrImportant = 256;
        for (std::size_t i = 0; i < info.colors.size(); ++i) {
            info.colors[i] = { static_cast<BYTE>(i), static_cast<BYTE>(i), static_cast<BYTE>(i), 0 };
        }
        RasterDevice device;
        check(device.dc != nullptr, "CreateCompatibleDC");
        void* bits {};
        device.bitmap = CreateDIBSection(device.dc, reinterpret_cast<const BITMAPINFO*>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
        check(device.bitmap && bits, "CreateDIBSection");
        device.previous_bitmap = SelectObject(device.dc, device.bitmap);
        check(device.previous_bitmap && device.previous_bitmap != HGDI_ERROR, "SelectObject(bitmap)");
        const auto stride = (std::size_t { width } * 2 + 3) & ~std::size_t { 3 };
        std::memset(bits, 0, stride * height * 2);
        SetMapMode(device.dc, MM_TEXT);
        LOGFONTA logical {};
        static_assert(sizeof(logical) == sizeof(definition.logical));
        std::memcpy(&logical, &definition.logical, sizeof(logical));
        logical.lfFaceName[LF_FACESIZE - 1] = 0;
        std::vector<int> advances(text.size());
        Analysis analysis;
        const auto analyze = [&](const int* dx) {
            SCRIPT_CONTROL control {};
            SCRIPT_STATE state {};
            check(SUCCEEDED(ScriptStringAnalyse(device.dc, text.data(), static_cast<int>(text.size()), 0, -1, SSA_GLYPHS, 0, &control, &state, dx, nullptr, nullptr, &analysis.value)), "ScriptStringAnalyse");
        };
        const auto measure = [&] {
            analysis.reset();
            device.select_font(logical);
            analyze(nullptr);
            check(SUCCEEDED(ScriptStringGetLogicalWidths(analysis.value, advances.data())), "ScriptStringGetLogicalWidths");
            for (auto& advance : advances) {
                advance = static_cast<int>(static_cast<std::int64_t>(advance) * definition.advance_percent / 100);
            }
            analysis.reset();
            analyze(advances.data());
            const auto* size = ScriptString_pSize(analysis.value);
            check(size != nullptr, "ScriptString_pSize");
            return size->cx;
        };
        auto measured = measure();
        const auto extent = static_cast<int>(width * 2);
        if (measured >= extent) {
            int low = 1, high = logical.lfWidth ? logical.lfWidth : -logical.lfHeight;
            check(high > 0 && high <= 65536, "font fitting bounds");
            int candidate = ((high - 1) / 2) + 1;
            for (unsigned pass = 0;; ++pass) {
                check(pass < 32, "font fitting convergence");
                logical.lfWidth = candidate;
                measured = measure();
                if (high - low <= 2 && measured < extent) {
                    break;
                }
                if (measured < extent) {
                    low = candidate;
                } else {
                    high = candidate;
                }
                if (high <= 1) {
                    break;
                } //Native loops forever when even width1 cannot fit.
                candidate = low + (high - low) / 2;
            }
        }
        SetBkColor(device.dc, RGB(0, 0, 0));
        SetTextColor(device.dc, RGB(255, 255, 255));
        const RECT clip { 0, 0, static_cast<LONG>(width * 2), static_cast<LONG>(height * 2) };
        const auto written = ScriptStringOut(analysis.value, static_cast<int>(width) - measured / 2, definition.baseline_y, ETO_CLIPPED, &clip, 0, 0, FALSE);
        check(SUCCEEDED(written), std::format("ScriptStringOut HRESULT {}", static_cast<std::uint32_t>(written)));
        GdiFlush();
        const auto* source = static_cast<const std::uint8_t*>(bits);
        result.ink_top = -1;
        result.ink_bottom = -1;
        for (std::size_t y = 0; y < height; ++y) {
            bool ink = false;
            for (std::size_t x = 0; x < width; ++x) {
                const auto at = y * 2 * stride + x * 2;
                const auto value = static_cast<std::uint8_t>((unsigned { source[at] } + source[at + 1] + source[at + stride] + source[at + stride + 1]) >> 2);
                result.image.pixels[y * width + x] = value;
                if (x + 1 < width && value) {
                    ink = true; //7B7520 excludes ink in only the final column.
                }
            }
            if (ink) {
                if (result.ink_top < 0) {
                    result.ink_top = static_cast<std::int32_t>(y);
                } else {
                    result.ink_bottom = static_cast<std::int32_t>(y + 1);
                }
            }
        }
        return result;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<SignTexturePair, MeshError> SignFontLibrary::render(const SignFontConfiguration& configuration, std::u16string_view first, std::u16string_view second) {
    try {
        if (!configuration.background && configuration.fonts[0].surface.pixels.empty() && configuration.fonts[1].surface.pixels.empty() && std::ranges::all_of(configuration.effect_placement, [](auto mode) {
                return mode == SignEffectPlacement::disabled;
            })) {
            return split_sign_canvas({ 256, 256, 4, std::vector<std::uint8_t>(256 * 256 * 4) });
        }
        std::array<SignTextMask, 2> masks;
        for (std::size_t i = 0; i < 2; ++i) {
            const auto rendered = rasterize(configuration.fonts[i], i ? second : first);
            if (!rendered) {
                return std::unexpected(rendered.error());
            }
            masks[i] = std::move(*rendered);
        }
        return split_sign_canvas(compose_sign_canvas(configuration, masks));
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
