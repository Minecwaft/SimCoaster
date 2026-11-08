#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include "simcoaster/application_postcards.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace simcoaster {
namespace {
using Microsoft::WRL::ComPtr;

struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ~Apartment() {
        if (SUCCEEDED(status)) {
            CoUninitialize();
        }
    }

    bool available() const {
        return SUCCEEDED(status) || status == RPC_E_CHANGED_MODE;
    }
};

PostcardError failed(const std::filesystem::path& path, std::string_view operation, HRESULT status) {
    return { path, std::string(operation) + " (HRESULT " + std::to_string(static_cast<std::int32_t>(status)) + ")" };
}
} //namespace

std::string postcard_to_ansi(std::u16string_view text, std::optional<std::size_t> limit) {
    if (const auto zero = text.find(char16_t {}); zero != text.npos) {
        text = text.substr(0, zero);
    }
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Postcard ANSI input exceeds the platform conversion API");
    }
    const auto* source = reinterpret_cast<const wchar_t*>(text.data());
    const auto length = WideCharToMultiByte(CP_ACP, 0, source, static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::runtime_error("Postcard ANSI conversion failed");
    }
    std::string output(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_ACP, 0, source, static_cast<int>(text.size()), output.data(), length, nullptr, nullptr) != length) {
        throw std::runtime_error("Postcard ANSI conversion failed");
    }
    if (limit && output.size() > *limit) {
        //Native %.63S is a byte-count limit, rather than a WCHAR limit.
        //Keep complete converted multibyte characters at its native boundary.
        std::size_t cursor = 0, last = 0;
        while (cursor < output.size()) {
            const auto step = IsDBCSLeadByteEx(CP_ACP, static_cast<BYTE>(output[cursor])) ? 2u : 1u;
            if (cursor + step > *limit) {
                break;
            }
            cursor += step;
            last = cursor;
        }
        output.resize(last);
    }
    return output;
}

std::u16string postcard_from_ansi(std::string_view source) {
    if (const auto zero = source.find('\0'); zero != source.npos) {
        source = source.substr(0, zero);
    }
    if (source.empty()) {
        return {};
    }
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Postcard Unicode input exceeds platform conversion API");
    }
    const auto size = MultiByteToWideChar(CP_ACP, 0, source.data(), static_cast<int>(source.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("Postcard Unicode conversion failed");
    }
    std::u16string result(static_cast<std::size_t>(size), char16_t {});
    if (MultiByteToWideChar(CP_ACP, 0, source.data(), static_cast<int>(source.size()), reinterpret_cast<wchar_t*>(result.data()), size) != size) {
        throw std::runtime_error("Postcard Unicode conversion failed");
    }
    return result;
}

std::expected<TextureImage, PostcardError> load_postcard_jpeg(const std::filesystem::path& path) {
    Apartment apartment;
    if (!apartment.available()) {
        return std::unexpected(failed(path, "Initialize postcard JPEG decoder", apartment.status));
    }
    ComPtr<IWICImagingFactory> factory;
    auto status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG decoder", status));
    }
    ComPtr<IWICBitmapDecoder> decoder;
    status = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Open postcard JPEG", status));
    }
    GUID container {};
    status = decoder->GetContainerFormat(&container);
    if (FAILED(status) || container != GUID_ContainerFormatJpeg) {
        return std::unexpected(failed(path, "Postcard file is not a JPEG", FAILED(status) ? status : E_INVALIDARG));
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    status = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Read postcard JPEG frame", status));
    }
    TextureImage image;
    status = frame->GetSize(&image.width, &image.height);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Read postcard JPEG dimensions", status));
    }
    if (!image.width || !image.height || image.width > 16384 || image.height > 16384) {
        return std::unexpected(PostcardError { path, "Invalid postcard JPEG dimensions" });
    }
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    ComPtr<IWICFormatConverter> converter;
    status = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG converter", status));
    }
    status = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Convert postcard JPEG", status));
    }
    status = converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size() * 4), reinterpret_cast<BYTE*>(image.pixels.data()));
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Decode postcard JPEG pixels", status));
    }
    for (auto& pixel : image.pixels) {
        pixel[3] = 255;
    }
    image.alpha = TextureAlpha::opaque;
    image.source_mode = TextureSourceMode::opaque;
    return image;
}

std::expected<void, PostcardError> save_postcard_jpeg(const std::filesystem::path& path, const TextureImage& image) {
    if (!image.width || !image.height || image.pixels.size() != static_cast<std::size_t>(image.width) * image.height || image.width > 16384 || image.height > 16384) {
        return std::unexpected(PostcardError { path, "Invalid postcard image dimensions" });
    }
    Apartment apartment;
    if (!apartment.available()) {
        return std::unexpected(failed(path, "Initialize postcard JPEG encoder", apartment.status));
    }
    ComPtr<IWICImagingFactory> factory;
    auto status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG encoder", status));
    }
    ComPtr<IWICStream> stream;
    status = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG stream", status));
    }
    status = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Open postcard JPEG output", status));
    }
    ComPtr<IWICBitmapEncoder> encoder;
    status = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG writer", status));
    }
    status = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Initialize postcard JPEG writer", status));
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    status = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Create postcard JPEG frame", status));
    }
    //78CB80 sets GIMEX quality50. Keep that user-visible policy with the native
    //host codec; reconstructed JPEG bitstreams need not reproduce GIMEX bytes.
    PROPBAG2 quality {};
    quality.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value {};
    value.vt = VT_R4;
    value.fltVal = .5f;
    status = properties->Write(1, &quality, &value);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Set postcard JPEG quality", status));
    }
    status = frame->Initialize(properties.Get());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Initialize postcard JPEG frame", status));
    }
    status = frame->SetSize(image.width, image.height);
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Set postcard JPEG dimensions", status));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    status = frame->SetPixelFormat(&format);
    if (FAILED(status) || format != GUID_WICPixelFormat24bppBGR) {
        return std::unexpected(failed(path, "Set postcard JPEG pixel format", FAILED(status) ? status : E_INVALIDARG));
    }
    std::vector<BYTE> pixels(static_cast<std::size_t>(image.width) * image.height * 3);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        pixels[3 * i] = image.pixels[i][2];
        pixels[3 * i + 1] = image.pixels[i][1];
        pixels[3 * i + 2] = image.pixels[i][0];
    }
    status = frame->WritePixels(image.height, image.width * 3, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Write postcard JPEG pixels", status));
    }
    status = frame->Commit();
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Commit postcard JPEG frame", status));
    }
    status = encoder->Commit();
    if (FAILED(status)) {
        return std::unexpected(failed(path, "Commit postcard JPEG", status));
    }
    return {};
}
} //namespace simcoaster
