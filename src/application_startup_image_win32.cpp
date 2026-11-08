#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "application_startup_image.hpp"
#include <format>
#include <stdexcept>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace simcoaster {
namespace {
using Microsoft::WRL::ComPtr;

void check(HRESULT result, std::string_view operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::format("{} (HRESULT {})", operation, static_cast<std::int32_t>(result)));
    }
}

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Apartment() {
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            check(result, "Initialize startup PNG decoder");
        }
    }

    ~Apartment() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};
} //namespace

TextureImage load_startup_png(const std::filesystem::path& path) {
    Apartment apartment;
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())), "Create startup PNG factory");
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()), "Open original startup PNG");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()), "Read original startup PNG frame");
    TextureImage image;
    check(frame->GetSize(&image.width, &image.height), "Read startup PNG dimensions");
    if (!image.width || !image.height || image.width > 8192 || image.height > 8192) {
        throw std::runtime_error("Invalid original startup PNG dimensions");
    }
    image.pixels.resize(std::size_t(image.width) * image.height);
    image.alpha = TextureAlpha::opaque;
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(converter.GetAddressOf()), "Create startup PNG converter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert original startup PNG");
    check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size() * 4), reinterpret_cast<BYTE*>(image.pixels.data())), "Read original startup PNG pixels");
    //Native startup surfaces copy RGB pixels, not alpha-composited overlays.
    for (auto& pixel : image.pixels) {
        pixel[3] = 255;
    }
    return image;
}
} //namespace simcoaster
