#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <cstdlib>
#include "simcoaster/mesh_renderer.hpp"
#include "simcoaster/ui_draw.hpp"
#include "simcoaster/weather_runtime.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
using Microsoft::WRL::ComPtr;

void check(HRESULT result, const char* action) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string { action } + " failed (HRESULT " + std::to_string(result) + ")");
    }
}

constexpr unsigned char shader_source[] = {
#embed "mesh_shader.hlsl"
};

ComPtr<ID3DBlob> shader(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> bytecode, errors;
    const auto result = D3DCompile(shader_source, sizeof(shader_source), "mesh_shader.hlsl", nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors) {
        throw std::runtime_error(std::string { static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize() });
    }
    check(result, "Compile mesh shader");
    return bytecode;
}

struct alignas(16) Constants {
    MeshMatrix matrix {}, view {};
    std::array<float, 4> material { -1, 0, 0, 1 }, effects {}, diffuse_mask { 1, 1, 1, 1 };
    std::array<float, 4> fog_parameters {}, fog_color {};
    std::array<float, 4> texture_uv { 1, 1, 0, 0 };
};

static_assert(sizeof(Constants) == 224 && sizeof(SceneVertex) == 48 && sizeof(RgbaPixel) == 4);

void atmosphere_constants(Constants& state, const WorldAtmosphere* atmosphere, const MeshMaterial& material, WorldFogEligibility eligibility) {
    //Every draw writes these values, including excluded/disabled draws. UI
    //and later standalone screen effects cannot inherit a World fog pass.
    state.fog_parameters = {};
    state.fog_color = {};
    if (!atmosphere) {
        return;
    }
    const auto flags = world_fog_material_flags(*atmosphere, material.flags, eligibility);
    if (!world_fog_material_enabled(*atmosphere, flags)) {
        return;
    }
    const auto color = world_atmosphere_fog_color(*atmosphere);
    state.fog_parameters = { atmosphere->range.start, atmosphere->range.end, atmosphere->projection.scale, atmosphere->projection.bias };
    state.fog_color = { color[0], color[1], color[2], 1.f };
}

D3D11_BLEND blend_factor(MeshBlendFactor value, bool alpha = false) {
    return match (value) -> D3D11_BLEND {
        case MeshBlendFactor::zero                      => D3D11_BLEND_ZERO;
        case MeshBlendFactor::one                       => D3D11_BLEND_ONE;
        case MeshBlendFactor::source_color              => alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
        case MeshBlendFactor::inverse_source_color      => alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
        case MeshBlendFactor::source_alpha              => D3D11_BLEND_SRC_ALPHA;
        case MeshBlendFactor::inverse_source_alpha      => D3D11_BLEND_INV_SRC_ALPHA;
        case MeshBlendFactor::destination_alpha         => D3D11_BLEND_DEST_ALPHA;
        case MeshBlendFactor::inverse_destination_alpha => D3D11_BLEND_INV_DEST_ALPHA;
        case MeshBlendFactor::destination_color         => alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
        case MeshBlendFactor::inverse_destination_color => alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
        case MeshBlendFactor::source_alpha_saturate     => alpha ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA_SAT;
        case _                                          => throw std::runtime_error("Invalid native blend factor");
    };
}

#include "renderer_profile.inc"
} //namespace

struct MeshRenderer::Impl {
    RendererProfiler profiler;
    std::uint32_t width {}, height {};
    bool software {};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11Texture2D> target, depth;
    ComPtr<ID3D11RenderTargetView> color_view;
    ComPtr<ID3D11DepthStencilView> depth_view;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> effect_shader;
    ComPtr<ID3D11InputLayout> effect_layout;
    ComPtr<ID3D11VertexShader> ui_vertex_shader;
    ComPtr<ID3D11PixelShader> ui_pixel_shader;
    ComPtr<ID3D11InputLayout> ui_layout;
    ComPtr<ID3D11RasterizerState> ui_rasterizer;
    TextureImagePtr ui_white;

    struct GeometryStream {
        ComPtr<ID3D11Buffer> vertices, indices;
        std::size_t vertex_capacity {}, index_capacity {};
    };

    GeometryStream world_stream, effect_stream, ui_stream;
    ComPtr<ID3D11Buffer> constants;
    //All setters below own the context's relevant state, including transitions
    //between World, weather, sprites, UI and standalone screen effects.
    Constants previous_constants {};
    bool constants_valid {};
    std::array<ID3D11ShaderResourceView*, 2> bound_resources {};
    std::array<ID3D11SamplerState*, 2> bound_samplers {};
    ID3D11BlendState* bound_blend {};
    bool blend_valid {};
    ID3D11DepthStencilState* bound_depth {};
    ID3D11RasterizerState* bound_rasterizer {};
    D3D11_RECT bound_clip {};
    bool clip_valid {};

    void set_constants(const Constants& value) {
        if (constants_valid && std::memcmp(&previous_constants, &value, sizeof(value)) == 0) {
            return;
        }
        profile_constants();
        context->UpdateSubresource(constants.Get(), 0, nullptr, &value, 0, 0);
        previous_constants = value;
        constants_valid = true;
    }

    void set_resources(const std::array<ID3D11ShaderResourceView*, 2>& value) {
        if (bound_resources == value) {
            return;
        }
        profile_binding();
        context->PSSetShaderResources(0, 2, value.data());
        bound_resources = value;
    }

    void set_sampler(unsigned slot, ID3D11SamplerState* value) {
        if (bound_samplers.at(slot) == value) {
            return;
        }
        profile_binding();
        context->PSSetSamplers(slot, 1, &value);
        bound_samplers[slot] = value;
    }

    void set_blend(ID3D11BlendState* value) {
        if (blend_valid && bound_blend == value) {
            return;
        }
        profile_binding();
        context->OMSetBlendState(value, nullptr, std::numeric_limits<UINT>::max());
        bound_blend = value;
        blend_valid = true;
    }

    void set_depth(ID3D11DepthStencilState* value) {
        if (bound_depth == value) {
            return;
        }
        profile_binding();
        context->OMSetDepthStencilState(value, 0);
        bound_depth = value;
    }

    void set_rasterizer(ID3D11RasterizerState* value) {
        if (bound_rasterizer == value) {
            return;
        }
        profile_binding();
        context->RSSetState(value);
        bound_rasterizer = value;
    }

    void set_clip(const D3D11_RECT& value) {
        if (clip_valid && std::memcmp(&bound_clip, &value, sizeof(value)) == 0) {
            return;
        }
        profile_binding();
        context->RSSetScissorRects(1, &value);
        bound_clip = value;
        clip_valid = true;
    }

    std::array<ComPtr<ID3D11SamplerState>, 8> samplers;
    ComPtr<ID3D11RasterizerState> rasterizer, mesh_rasterizer;
    std::unordered_map<std::uint32_t, ComPtr<ID3D11BlendState>> blends;
    std::array<ComPtr<ID3D11DepthStencilState>, 4> depths;

    struct GpuTexture {
        std::weak_ptr<const TextureImage> owner;
        ComPtr<ID3D11ShaderResourceView> view;
    };

    std::unordered_map<const TextureImage*, GpuTexture> textures;

    ~Impl() {
        release_presentation();
    }

    void release_presentation() noexcept {
        if (context) {
            context->ClearState();
        }
        //DXGI keeps a flip chain associated with its HWND until the back-buffer
        //views, buffer and chain are released and deferred destruction is flushed.
        color_view.Reset();
        depth_view.Reset();
        target.Reset();
        depth.Reset();
        swap.Reset();
        if (context) {
            context->Flush();
        }
    }

    Impl(std::uint32_t w, std::uint32_t h, void* window, bool force_software, std::optional<std::uint32_t> adapter_index, std::optional<ApplicationGraphicsProfile> profile) : software(force_software) {
        try {
            initialize(w, h, window, adapter_index, profile);
        } catch (...) {
            //A constructor failure skips ~Impl, including when a later shader
            //or target allocation fails after the new swap chain was created.
            release_presentation();
            throw;
        }
    }

    void initialize(std::uint32_t w, std::uint32_t h, void* window, std::optional<std::uint32_t> adapter_index, const std::optional<ApplicationGraphicsProfile>& profile) {
        ComPtr<IDXGIAdapter1> selected;
        if (adapter_index && !software) {
            ComPtr<IDXGIFactory1> factory;
            check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "Enumerate graphics devices");
            std::uint32_t hardware_index = 0;
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const auto status = factory->EnumAdapters1(index, candidate.GetAddressOf());
                if (status == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                check(status, "Enumerate graphics adapter");
                DXGI_ADAPTER_DESC1 description {};
                check(candidate->GetDesc1(&description), "Read graphics device");
                if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    continue;
                }
                if (hardware_index++ == *adapter_index) {
                    selected = std::move(candidate);
                    break;
                }
            }
            if (!selected) {
                throw std::runtime_error("Selected graphics device is unavailable");
            }
        }
        constexpr std::array levels { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL level {};
        const auto* serial_warp = std::getenv("SIMCOASTER_TEST_SERIAL_WARP");
        const UINT software_flags = serial_warp && std::string_view { serial_warp } == "1" ? static_cast<UINT>(D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS) : 0u;
        auto result = D3D11CreateDevice(selected.Get(), selected ? D3D_DRIVER_TYPE_UNKNOWN : software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, software ? software_flags : 0u, levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, device.GetAddressOf(), &level, context.GetAddressOf());
        if (FAILED(result) && selected) {
            check(result, "Create selected graphics device");
        }
        if (FAILED(result) && !software) {
            device.Reset();
            context.Reset();
            software = true;
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, software_flags, levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, device.GetAddressOf(), &level, context.GetAddressOf());
        }
        check(result, "Create Direct3D11 device");
        if (window) {
            ComPtr<IDXGIDevice> dxgi;
            check(device.As(&dxgi), "Query DXGI device");
            ComPtr<IDXGIAdapter> adapter;
            check(dxgi->GetAdapter(adapter.GetAddressOf()), "Get adapter");
            ComPtr<IDXGIFactory2> factory;
            check(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), "Get DXGI factory");
            DXGI_SWAP_CHAIN_DESC1 desc {};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (profile && profile->triple_buffer) {
                desc.BufferCount = 3;
            }
            check(factory->CreateSwapChainForHwnd(device.Get(), static_cast<HWND>(window), &desc, nullptr, nullptr, swap.GetAddressOf()), "Create flip swap chain");
            check(factory->MakeWindowAssociation(static_cast<HWND>(window), DXGI_MWA_NO_ALT_ENTER), "Set window association");
        }
        const auto vs = shader("vertex_main", "vs_5_0"), ps = shader("pixel_main", "ps_5_0");
        check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_shader.GetAddressOf()), "Create vertex shader");
        check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel_shader.GetAddressOf()), "Create pixel shader");
        const std::array inputs { D3D11_INPUT_ELEMENT_DESC { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(SceneVertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                  D3D11_INPUT_ELEMENT_DESC { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(SceneVertex, normal)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                  D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(SceneVertex, uv)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                  D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(SceneVertex, reflection_position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                  D3D11_INPUT_ELEMENT_DESC { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(SceneVertex, diffuse)), D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        check(device->CreateInputLayout(inputs.data(), static_cast<UINT>(inputs.size()), vs->GetBufferPointer(), vs->GetBufferSize(), layout.GetAddressOf()), "Create input layout");
        const auto fx = shader("effect_main", "vs_5_0");
        check(device->CreateVertexShader(fx->GetBufferPointer(), fx->GetBufferSize(), nullptr, effect_shader.GetAddressOf()), "Create weather vertex shader");
        static_assert(sizeof(EffectVertex) == 32);
        const std::array effect_inputs { D3D11_INPUT_ELEMENT_DESC { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(EffectVertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                         D3D11_INPUT_ELEMENT_DESC { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(EffectVertex, diffuse)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                         D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(EffectVertex, uv)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                         D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(EffectVertex, fog_camera_depth)), D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        check(device->CreateInputLayout(effect_inputs.data(), static_cast<UINT>(effect_inputs.size()), fx->GetBufferPointer(), fx->GetBufferSize(), effect_layout.GetAddressOf()), "Create weather input layout");
        const auto ui_vs = shader("ui_vertex_main", "vs_5_0"), ui_ps = shader("ui_pixel_main", "ps_5_0");
        check(device->CreateVertexShader(ui_vs->GetBufferPointer(), ui_vs->GetBufferSize(), nullptr, ui_vertex_shader.GetAddressOf()), "Create UI vertex shader");
        check(device->CreatePixelShader(ui_ps->GetBufferPointer(), ui_ps->GetBufferSize(), nullptr, ui_pixel_shader.GetAddressOf()), "Create UI pixel shader");
        static_assert(sizeof(UiDrawVertex) == 32);
        const std::array ui_inputs { D3D11_INPUT_ELEMENT_DESC { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(UiDrawVertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                     D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(UiDrawVertex, uv)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                     D3D11_INPUT_ELEMENT_DESC { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(UiDrawVertex, color)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                     D3D11_INPUT_ELEMENT_DESC { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(UiDrawVertex, reflection_uv)), D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        check(device->CreateInputLayout(ui_inputs.data(), static_cast<UINT>(ui_inputs.size()), ui_vs->GetBufferPointer(), ui_vs->GetBufferSize(), ui_layout.GetAddressOf()), "Create UI input layout");
        auto white = std::make_shared<TextureImage>();
        white->width = white->height = 1;
        white->pixels.push_back({ 255, 255, 255, 255 });
        ui_white = std::move(white);
        D3D11_BUFFER_DESC constant_desc {};
        constant_desc.ByteWidth = sizeof(Constants);
        constant_desc.Usage = D3D11_USAGE_DEFAULT;
        constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&constant_desc, nullptr, constants.GetAddressOf()), "Create constants");
        for (std::size_t key = 0; key < samplers.size(); ++key) {
            D3D11_SAMPLER_DESC sample {};
            const bool wrap = (key & 1u) != 0, point_mag = (key & 2u) != 0;
            sample.Filter = wrap ? (point_mag ? D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR) : (point_mag ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR);
            sample.AddressU = sample.AddressV = sample.AddressW = wrap ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
            sample.MaxAnisotropy = 1;
            sample.MaxLOD = key & 4u ? D3D11_FLOAT32_MAX : 0;
            sample.ComparisonFunc = D3D11_COMPARISON_NEVER;
            if (profile) {
                //6C3860/6C49D0: global filtering chooses only the mip filter.
                //Material flags retain their own MIN/MAG and address overrides.
                const bool linear_mip = profile->texture_filtering >= 2;
                sample.Filter = wrap ? (point_mag ? (linear_mip ? D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT) : (linear_mip ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT)) : (point_mag ? (linear_mip ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT) : (linear_mip ? D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR : D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT));
                if (!profile->mipmap) {
                    sample.MaxLOD = 0;
                }
            }
            check(device->CreateSamplerState(&sample, samplers[key].GetAddressOf()), "Create native profile sampler");
        }
        D3D11_RASTERIZER_DESC raster {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, rasterizer.GetAddressOf()), "Create rasterizer");
        raster.ScissorEnable = TRUE;
        check(device->CreateRasterizerState(&raster, ui_rasterizer.GetAddressOf()), "Create UI scissor rasterizer");
        //Game.exe6EE050 rejects generic mesh back faces through6EF310 before
        //submitting to its CULL_NONE device. Let the modern rasterizer perform
        //that same rejection; authored reverse faces remain available below.
        raster.ScissorEnable = FALSE;
        raster.CullMode = D3D11_CULL_BACK;
        raster.FrontCounterClockwise = FALSE;
        check(device->CreateRasterizerState(&raster, mesh_rasterizer.GetAddressOf()), "Create native mesh face rasterizer");
        for (std::size_t key = 0; key < depths.size(); ++key) {
            D3D11_DEPTH_STENCIL_DESC desc {};
            desc.DepthEnable = (key & 1u) != 0;
            desc.DepthWriteMask = key & 2u ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            check(device->CreateDepthStencilState(&desc, depths[key].GetAddressOf()), "Create material depth state");
        }
        resize(w, h);
    }

    void resize(std::uint32_t w, std::uint32_t h) {
        if (!w || !h || w > 16384 || h > 16384) {
            throw std::runtime_error("Invalid render dimensions");
        }
        context->OMSetRenderTargets(0, nullptr, nullptr);
        color_view.Reset();
        depth_view.Reset();
        target.Reset();
        depth.Reset();
        if (swap) {
            if (width) {
                check(swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "Resize swap chain");
            }
            check(swap->GetBuffer(0, IID_PPV_ARGS(target.GetAddressOf())), "Get swap-chain buffer");
        } else {
            D3D11_TEXTURE2D_DESC desc {};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            check(device->CreateTexture2D(&desc, nullptr, target.GetAddressOf()), "Create render target");
        }
        check(device->CreateRenderTargetView(target.Get(), nullptr, color_view.GetAddressOf()), "Create target view");
        D3D11_TEXTURE2D_DESC desc {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&desc, nullptr, depth.GetAddressOf()), "Create depth buffer");
        check(device->CreateDepthStencilView(depth.Get(), nullptr, depth_view.GetAddressOf()), "Create depth view");
        width = w;
        height = h;
    }

    template <class T>
    void upload(ComPtr<ID3D11Buffer>& buffer, std::size_t& capacity, std::span<const T> data, UINT binding) {
        const auto bytes = data.size_bytes();
        if (active_frame_profile) {
            ++active_frame_profile->counters.geometry_maps;
            active_frame_profile->counters.geometry_bytes += bytes;
        }
        if (bytes > std::numeric_limits<UINT>::max()) {
            throw std::runtime_error("GPU geometry buffer exceeds device limits");
        }
        if (capacity < bytes) {
            buffer.Reset();
            D3D11_BUFFER_DESC desc {};
            desc.ByteWidth = static_cast<UINT>(std::min<std::size_t>(std::numeric_limits<UINT>::max(), std::max(bytes, capacity + capacity / 2)));
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = binding;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()), "Create geometry buffer");
            capacity = desc.ByteWidth;
        }
        D3D11_MAPPED_SUBRESOURCE mapped {};
        check(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map geometry buffer");
        std::memcpy(mapped.pData, data.data(), bytes);
        context->Unmap(buffer.Get(), 0);
    }

    ID3D11ShaderResourceView* texture(const TextureImagePtr& image) {
        if (const auto found = textures.find(image.get()); found != textures.end() && !found->second.owner.expired()) {
            return found->second.view.Get();
        }
        if (image->pixels.size() != static_cast<std::size_t>(image->width) * image->height) {
            throw std::runtime_error("Invalid texture pixels");
        }
        if (active_frame_profile) {
            ++active_frame_profile->counters.texture_uploads;
            active_frame_profile->counters.texture_bytes += image->pixels.size() * sizeof(RgbaPixel);
            ++active_frame_profile->counters.mip_generations;
        }
        D3D11_TEXTURE2D_DESC desc {};
        desc.Width = image->width;
        desc.Height = image->height;
        desc.ArraySize = 1;
        desc.MipLevels = 0;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> resource;
        check(device->CreateTexture2D(&desc, nullptr, resource.GetAddressOf()), "Create GPU texture");
        context->UpdateSubresource(resource.Get(), 0, nullptr, image->pixels.data(), image->width * 4, 0);
        GpuTexture result;
        result.owner = image;
        check(device->CreateShaderResourceView(resource.Get(), nullptr, result.view.GetAddressOf()), "Create texture view");
        context->GenerateMips(result.view.Get());
        const auto pointer = result.view.Get();
        textures[image.get()] = std::move(result);
        return pointer;
    }

    ID3D11BlendState* blend(const MeshMaterial& material) {
        const auto key = material.blended() ? material.flags & 0x1FFu : 0u;
        if (const auto found = blends.find(key); found != blends.end()) {
            return found->second.Get();
        }
        D3D11_BLEND_DESC desc {};
        auto& state = desc.RenderTarget[0];
        state.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (material.blended()) {
            auto source = material.source_blend(), destination = material.destination_blend();
            if (source == MeshBlendFactor::both_source_alpha) {
                source = MeshBlendFactor::source_alpha;
                destination = MeshBlendFactor::inverse_source_alpha;
            } else if (source == MeshBlendFactor::both_inverse_source_alpha) {
                source = MeshBlendFactor::inverse_source_alpha;
                destination = MeshBlendFactor::source_alpha;
            }
            state.BlendEnable = TRUE;
            state.SrcBlend = blend_factor(source);
            state.DestBlend = blend_factor(destination);
            state.BlendOp = D3D11_BLEND_OP_ADD;
            state.SrcBlendAlpha = blend_factor(source, true);
            state.DestBlendAlpha = blend_factor(destination, true);
            state.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        ComPtr<ID3D11BlendState> result;
        check(device->CreateBlendState(&desc, result.GetAddressOf()), "Create native material blend");
        const auto pointer = result.Get();
        blends.emplace(key, std::move(result));
        return pointer;
    }

    void draw_effects(const EffectScene& geometry, const WorldAtmosphere* frame_atmosphere = nullptr) {
        if (geometry.indices.empty()) {
            return;
        }
        set_rasterizer(rasterizer.Get());
        const auto* atmosphere = geometry.atmosphere ? &*geometry.atmosphere : frame_atmosphere;
        auto& stream = effect_stream;
        upload(stream.vertices, stream.vertex_capacity, std::span<const EffectVertex> { geometry.vertices }, D3D11_BIND_VERTEX_BUFFER);
        upload(stream.indices, stream.index_capacity, std::span<const std::uint32_t> { geometry.indices }, D3D11_BIND_INDEX_BUFFER);
        const UINT stride = sizeof(EffectVertex), offset = 0;
        context->IASetVertexBuffers(0, 1, stream.vertices.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(stream.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetInputLayout(effect_layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(effect_shader.Get(), nullptr, 0);
        for (std::size_t at = 0; at < geometry.draws.size(); ++at) {
            auto draw = geometry.draws[at];
            if (draw.first_index > geometry.indices.size() || draw.index_count > geometry.indices.size() - draw.first_index || draw.index_count % 3u) {
                throw std::runtime_error("Invalid effect draw range");
            }
            //Only adjacent, contiguous ranges with exactly the same material and
            //source may merge. Primitive order and all cross-pass ordering stay put.
            while (at + 1 < geometry.draws.size()) {
                const auto& next = geometry.draws[at + 1];
                if (static_cast<std::uint64_t>(draw.first_index) + draw.index_count != next.first_index
                    || draw.texture != next.texture
                    || draw.material.flags != next.material.flags
                    || draw.material.generated_uv != next.material.generated_uv
                    || draw.material.secondary_texture != next.material.secondary_texture
                    || draw.material.texture_uv != next.material.texture_uv
                    || draw.fog_eligibility != next.fog_eligibility
                    || next.index_count % 3u
                    || static_cast<std::uint64_t>(draw.index_count) + next.index_count > std::numeric_limits<UINT>::max()) {
                    break;
                }
                draw.index_count += next.index_count;
                ++at;
                if (active_frame_profile) {
                    ++active_frame_profile->counters.draws_coalesced;
                }
            }
            if (draw.first_index > geometry.indices.size() || draw.index_count > geometry.indices.size() - draw.first_index || draw.index_count % 3u) {
                throw std::runtime_error("Invalid effect draw range");
            }
            const auto& material = draw.material;
            Constants state;
            state.texture_uv = material.texture_uv;
            state.material = { material.alpha_reference(), 0, 0, material.flags & 0x10000u ? 2.0f : 1.0f };
            atmosphere_constants(state, atmosphere, material, draw.fog_eligibility);
            if (state.fog_color[3] > 0.f) {
                const auto end = static_cast<std::size_t>(draw.first_index) + draw.index_count;
                for (std::size_t i = draw.first_index; i < end; ++i) {
                    const auto index = geometry.indices[i];
                    if (index >= geometry.vertices.size() || !std::isfinite(geometry.vertices[index].fog_camera_depth)) {
                        throw std::runtime_error("World effect fog requires camera-space vertex depth");
                    }
                }
            }
            set_constants(state);
            const std::array resources { texture(draw.texture), static_cast<ID3D11ShaderResourceView*>(nullptr) };
            set_resources(resources);
            const auto key = (material.wrap_linear_min() ? 1u : 0u) | (material.point_mag() ? 2u : 0u) | (material.mipmaps() ? 4u : 0u);
            set_sampler(0, samplers[key].Get());
            set_blend(blend(material));
            set_depth(depths[(material.depth_test() ? 1u : 0u) | (material.depth_write() ? 2u : 0u)].Get());
            profile_draw(), context->DrawIndexed(draw.index_count, draw.first_index, 0);
        }
    }

    void draw_weather(const MeshScene& scene, const SceneCamera& camera, bool background) {
        if (scene.weather) {
            draw_effects(build_weather_scene(*scene.weather, camera, static_cast<float>(width) / static_cast<float>(height), background), scene.atmosphere ? &*scene.atmosphere : nullptr);
        }
    }
};

MeshRenderer::MeshRenderer(std::uint32_t w, std::uint32_t h, void* window, bool software, std::optional<std::uint32_t> adapter, std::optional<ApplicationGraphicsProfile> profile) : impl_(std::make_unique<Impl>(w, h, window, software, adapter, std::move(profile))) {}

std::vector<std::u16string> MeshRenderer::hardware_adapters() {
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "Enumerate graphics devices");
    std::vector<std::u16string> names;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const auto status = factory->EnumAdapters1(index, adapter.GetAddressOf());
        if (status == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        check(status, "Enumerate graphics adapter");
        DXGI_ADAPTER_DESC1 description {};
        check(adapter->GetDesc1(&description), "Read graphics device");
        if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            std::u16string name;
            for (const auto ch : description.Description) {
                if (!ch) {
                    break;
                }
                name.push_back(static_cast<char16_t>(ch));
            }
            names.push_back(std::move(name));
        }
    }
    return names;
}

MeshRenderer::~MeshRenderer() = default;

void MeshRenderer::resize(std::uint32_t w, std::uint32_t h) {
    impl_->resize(w, h);
}

bool MeshRenderer::software_adapter() const {
    return impl_->software;
}

std::string MeshRenderer::adapter_name() const {
    ComPtr<IDXGIDevice> device;
    check(impl_->device.As(&device), "Get DXGI device");
    ComPtr<IDXGIAdapter> adapter;
    check(device->GetAdapter(adapter.GetAddressOf()), "Get adapter");
    DXGI_ADAPTER_DESC desc {};
    check(adapter->GetDesc(&desc), "Read adapter description");
    const auto count = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(std::max(count, 1)), 0);
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, text.data(), count, nullptr, nullptr);
    text.pop_back();
    return text;
}

void MeshRenderer::draw(const MeshScene& scene, const SceneCamera& camera) {
    std::erase_if(impl_->textures, [](const auto& item) {
        return item.second.owner.expired();
    });
    FrameTimer timer { FrameStage::world_submit };
    auto& r = *impl_;
    r.profiler.begin(r.device.Get(), r.context.Get());
    const auto* atmosphere = scene.atmosphere ? &*scene.atmosphere : nullptr;
    const auto background = atmosphere ? world_atmosphere_clear_color(*atmosphere) : std::array<float, 4> { 0.08f, 0.11f, 0.16f, 1 };
    r.context->ClearRenderTargetView(r.color_view.Get(), background.data());
    r.context->ClearDepthStencilView(r.depth_view.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    r.context->OMSetRenderTargets(1, r.color_view.GetAddressOf(), r.depth_view.Get());
    r.context->PSSetShader(r.pixel_shader.Get(), nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.set_rasterizer(r.rasterizer.Get());
    const D3D11_VIEWPORT weather_viewport { 0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1 };
    r.context->RSSetViewports(1, &weather_viewport);
    r.profiler.begin_pass(r.context.Get(), GpuPass::background);
    r.draw_weather(scene, camera, true);
    r.profiler.end_pass(r.context.Get(), GpuPass::background);
    r.profiler.begin_pass(r.context.Get(), GpuPass::world);
    if (scene.indices.empty()) {
        r.profiler.end_pass(r.context.Get(), GpuPass::world);
        r.profiler.begin_pass(r.context.Get(), GpuPass::sprites);
        r.draw_effects(scene.sprites, atmosphere);
        r.profiler.end_pass(r.context.Get(), GpuPass::sprites);
        r.profiler.begin_pass(r.context.Get(), GpuPass::foreground);
        r.draw_weather(scene, camera, false);
        r.profiler.end_pass(r.context.Get(), GpuPass::foreground);
        return;
    }
    r.upload(r.world_stream.vertices, r.world_stream.vertex_capacity, std::span<const SceneVertex> { scene.vertices }, D3D11_BIND_VERTEX_BUFFER);
    r.upload(r.world_stream.indices, r.world_stream.index_capacity, std::span<const std::uint32_t> { scene.indices }, D3D11_BIND_INDEX_BUFFER);
    const UINT stride = sizeof(SceneVertex), offset = 0;
    r.context->IASetVertexBuffers(0, 1, r.world_stream.vertices.GetAddressOf(), &stride, &offset);
    r.context->IASetIndexBuffer(r.world_stream.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    r.context->IASetInputLayout(r.layout.Get());
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.vertex_shader.Get(), nullptr, 0);
    r.context->PSSetShader(r.pixel_shader.Get(), nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.set_rasterizer(r.rasterizer.Get());
    const D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1 };
    r.context->RSSetViewports(1, &viewport);
    const auto order = scene_draw_order(scene, camera.eye());
    Constants constants;
    constants.matrix = camera.view_projection(static_cast<float>(r.width) / static_cast<float>(r.height));
    constants.view = camera.view();
    for (const auto index : order) {
        const auto& draw = scene.draws[index];
        const auto& material = draw.material;
        constants.texture_uv = material.texture_uv;
        r.set_rasterizer(draw.cull_back_faces ? r.mesh_rasterizer.Get() : r.rasterizer.Get());
        constants.material = { material.alpha_reference(), material.generated_uv ? 1.0f : 0.0f, draw.secondary_texture ? 1.0f : 0.0f, material.flags & 0x10000u ? 2.0f : 1.0f };
        constants.effects = { draw.unlit ? 1.0f : 0.0f, material.flags & 0x80000000u ? 1.0f : 0.0f, draw.secondary_texture ? 1.0f : 0.0f, 0 };
        for (std::size_t channel = 0; channel < 4; ++channel) {
            constants.diffuse_mask[channel] = draw.diffuse_mask[channel] / 255.0f;
        }
        atmosphere_constants(constants, atmosphere, material, draw.fog_eligibility);
        r.set_constants(constants);
        const std::array resources { r.texture(draw.texture), draw.secondary_texture ? r.texture(draw.secondary_texture) : nullptr };
        r.set_resources(resources);
        const auto sampler_key = (material.wrap_linear_min() ? 1u : 0u) | (material.point_mag() ? 2u : 0u) | (material.mipmaps() ? 4u : 0u);
        const std::array samplers { r.samplers[sampler_key].Get(), r.samplers[5].Get() };
        r.set_sampler(0, samplers[0]);
        r.set_sampler(1, samplers[1]);
        r.set_blend(r.blend(material));
        r.set_depth(r.depths[(material.depth_test() ? 1u : 0u) | (material.depth_write() ? 2u : 0u)].Get());
        profile_draw(), r.context->DrawIndexed(draw.index_count, draw.first_index, 0);
    }
    r.profiler.end_pass(r.context.Get(), GpuPass::world);
    r.profiler.begin_pass(r.context.Get(), GpuPass::sprites);
    r.draw_effects(scene.sprites, atmosphere);
    r.profiler.end_pass(r.context.Get(), GpuPass::sprites);
    r.profiler.begin_pass(r.context.Get(), GpuPass::foreground);
    r.draw_weather(scene, camera, false);
    r.profiler.end_pass(r.context.Get(), GpuPass::foreground);
}

void MeshRenderer::draw_effects(const EffectScene& effects) {
    FrameTimer timer { FrameStage::screen_particles };
    if (effects.indices.empty()) {
        return;
    }
    auto& r = *impl_;
    r.context->OMSetRenderTargets(1, r.color_view.GetAddressOf(), r.depth_view.Get());
    r.context->PSSetShader(r.pixel_shader.Get(), nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.set_rasterizer(r.rasterizer.Get());
    const D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1 };
    r.context->RSSetViewports(1, &viewport);
    r.profiler.begin_pass(r.context.Get(), GpuPass::screen);
    r.draw_effects(effects);
    r.profiler.end_pass(r.context.Get(), GpuPass::screen);
}

void MeshRenderer::draw_ui(const UiDrawList& draws) {
    FrameTimer timer { FrameStage::ui_submit };
    if (draws.indices.empty()) {
        return;
    }
    if (!std::isfinite(draws.canvas_width) || !std::isfinite(draws.canvas_height) || draws.canvas_width <= 0 || draws.canvas_height <= 0 || !std::ranges::all_of(draws.indices, [&](auto index) {
            return index < draws.vertices.size();
        })) {
        throw std::runtime_error("Invalid UI canvas or geometry");
    }
    auto& r = *impl_;
    r.profiler.begin_pass(r.context.Get(), GpuPass::ui);
    r.upload(r.ui_stream.vertices, r.ui_stream.vertex_capacity, std::span<const UiDrawVertex> { draws.vertices }, D3D11_BIND_VERTEX_BUFFER);
    r.upload(r.ui_stream.indices, r.ui_stream.index_capacity, std::span<const std::uint32_t> { draws.indices }, D3D11_BIND_INDEX_BUFFER);
    const UINT stride = sizeof(UiDrawVertex), offset = 0;
    r.context->IASetVertexBuffers(0, 1, r.ui_stream.vertices.GetAddressOf(), &stride, &offset);
    r.context->IASetIndexBuffer(r.ui_stream.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    r.context->IASetInputLayout(r.ui_layout.Get());
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.ui_vertex_shader.Get(), nullptr, 0);
    r.context->PSSetShader(r.ui_pixel_shader.Get(), nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->OMSetRenderTargets(1, r.color_view.GetAddressOf(), r.depth_view.Get());
    r.set_depth(r.depths[0].Get());
    r.set_rasterizer(r.ui_rasterizer.Get());
    const D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1 };
    r.context->RSSetViewports(1, &viewport);
    Constants constants {};
    const auto pixel_x = static_cast<float>(r.width) / draws.canvas_width;
    const auto pixel_y = static_cast<float>(r.height) / draws.canvas_height;
    if (!std::isfinite(pixel_x) || !std::isfinite(pixel_y)) {
        throw std::runtime_error("Invalid UI canvas scale");
    }
    for (const auto layer : { UiDrawLayer::advisor, UiDrawLayer::controls }) {
        if (layer == UiDrawLayer::advisor && std::ranges::none_of(draws.commands, [](const auto& draw) {
                return draw.layer == UiDrawLayer::advisor;
            })) {
            continue;
        }
        //Preserve depth within the animated Advisor, then discard it before
        //controls render. Command emission order cannot change layer ownership.
        r.context->ClearDepthStencilView(r.depth_view.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        for (const auto& draw : draws.commands) {
            if (draw.layer != layer) {
                continue;
            }
            if (draw.first_index > draws.indices.size() || draw.index_count > draws.indices.size() - draw.first_index || draw.index_count % 3 || !std::ranges::all_of(draw.clip, [](float v) {
                    return std::isfinite(v);
                })) {
                throw std::runtime_error("Invalid UI draw command");
            }
            const auto edge = [](float v, std::uint32_t limit, bool upper) {
                return static_cast<LONG>(std::clamp(upper ? std::ceil(v) : std::floor(v), 0.f, static_cast<float>(limit)));
            };
            const D3D11_RECT clip { edge(draw.clip[0] * pixel_x, r.width, false), edge(draw.clip[1] * pixel_y, r.height, false), edge(draw.clip[2] * pixel_x, r.width, true), edge(draw.clip[3] * pixel_y, r.height, true) };
            if (!draw.index_count || clip.left >= clip.right || clip.top >= clip.bottom) {
                continue;
            }
            r.set_clip(clip);
            auto material = draw.material;
            constants.texture_uv = material.texture_uv;
            //Native UI attachment construction forces point magnification at640px.
            if (r.width == 640 && draw.native_model_filter) {
                material.flags |= 0x20000u;
            }
            constants.material = { draws.canvas_width, draws.canvas_height, material.alpha_reference(), material.flags & 0x10000u ? 2.f : 1.f };
            constants.effects = { draw.depth_buffered ? 1.f : 0.f, material.flags & 0x80000000u ? 1.f : 0.f, draw.secondary_texture ? 1.f : 0.f, material.generated_uv ? 1.f : 0.f };
            r.set_constants(constants);
            const std::array resources { r.texture(draw.texture ? draw.texture : r.ui_white), draw.secondary_texture ? r.texture(draw.secondary_texture) : nullptr };
            r.set_resources(resources);
            const auto sampler = (material.wrap_linear_min() ? 1u : 0u) | (material.point_mag() ? 2u : 0u) | (material.mipmaps() ? 4u : 0u);
            r.set_sampler(0, r.samplers[sampler].Get());
            r.set_sampler(1, r.samplers[1].Get());
            const auto depth = draw.depth_buffered ? (material.depth_test() ? 1u : 0u) | (material.depth_write() ? 2u : 0u) : 0u;
            r.set_depth(r.depths[depth].Get());
            r.set_blend(r.blend(material));
            profile_draw(), r.context->DrawIndexed(draw.index_count, draw.first_index, 0);
        }
    }
    r.profiler.end_pass(r.context.Get(), GpuPass::ui);
}

void MeshRenderer::finish_profile_frame() {
    impl_->profiler.finish(impl_->context.Get());
}

std::vector<GpuFrameProfile> MeshRenderer::take_profile_samples(bool drain) {
    return impl_->profiler.take(impl_->context.Get(), drain);
}

void MeshRenderer::present() {
    FrameTimer timer { FrameStage::present };
    finish_profile_frame();
    if (impl_->swap) {
        check(impl_->swap->Present(1, 0), "Present frame");
    }
}

TextureImage MeshRenderer::capture() {
    auto& r = *impl_;
    D3D11_TEXTURE2D_DESC desc {};
    r.target->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(r.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()), "Create capture buffer");
    r.context->CopyResource(staging.Get(), r.target.Get());
    D3D11_MAPPED_SUBRESOURCE map {};
    check(r.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Read rendered frame");
    TextureImage image;
    image.width = r.width;
    image.height = r.height;
    image.pixels.resize(static_cast<std::size_t>(r.width) * r.height);
    for (std::size_t y = 0; y < r.height; ++y) {
        std::memcpy(image.pixels.data() + y * r.width, static_cast<const std::byte*>(map.pData) + y * map.RowPitch, static_cast<std::size_t>(r.width) * 4);
    }
    r.context->Unmap(staging.Get(), 0);
    return image;
}

void write_texture_png(const TextureImage& image, const std::filesystem::path& path) {
    struct Apartment {
        HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        Apartment() {
            if (status != RPC_E_CHANGED_MODE) {
                check(status, "Initialize image COM");
            }
        }

        ~Apartment() {
            if (SUCCEEDED(status)) {
                CoUninitialize();
            }
        }
    } apartment;

    if (image.pixels.size() != static_cast<std::size_t>(image.width) * image.height || image.pixels.size() > std::numeric_limits<UINT>::max() / 4) {
        throw std::runtime_error("Invalid PNG dimensions");
    }
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())), "Create PNG factory");
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(stream.GetAddressOf()), "Create PNG stream");
    check(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE), "Open PNG file");
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()), "Create PNG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr), "Create PNG frame");
    check(frame->Initialize(nullptr), "Initialize PNG frame");
    check(frame->SetSize(image.width, image.height), "Set PNG size");
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "Set PNG format");
    if (format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("PNG encoder changed pixel format");
    }
    auto pixels = image.pixels;
    for (auto& pixel : pixels) {
        std::swap(pixel[0], pixel[2]);
    }
    check(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())), "Write PNG pixels");
    check(frame->Commit(), "Commit PNG frame");
    check(encoder->Commit(), "Commit PNG image");
}
} //namespace simcoaster
