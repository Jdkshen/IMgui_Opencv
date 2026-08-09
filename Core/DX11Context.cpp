#define NOMINMAX
#include "DX11Context.h"

#include "imgui/imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <iomanip>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
std::string HrMessage(const char* operation, HRESULT hr)
{
    std::ostringstream text;
    text << operation << " failed (HRESULT=0x" << std::uppercase << std::hex
         << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(hr) << ')';
    return text.str();
}
}

struct DX11Context::Impl
{
    struct TextureRecord
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> shaderView;
        cv::Mat pendingRgba;
        int width = 0;
        int height = 0;
        bool pending = false;
        bool removeRequested = false;
        bool ready = false;
    };

    HWND window = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<IDXGIAdapter3> adapter;
    DXGI_ADAPTER_DESC1 adapterDescription{};
    ComPtr<ID3D11RenderTargetView> renderTarget;
    std::unordered_map<RenderTextureHandle, TextureRecord> textures;
    RenderTextureHandle nextTexture = 1;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool imguiInitialized = false;
    bool occluded = false;
    bool softwareRenderer = false;

    bool CreateRenderTarget(std::string& error)
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            error = HrMessage("DX11 swap-chain GetBuffer", hr);
            return false;
        }
        hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr,
            renderTarget.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            error = HrMessage("DX11 CreateRenderTargetView", hr);
            return false;
        }
        return true;
    }

    bool ProcessTextureUploads(std::string& error)
    {
        for (auto it = textures.begin(); it != textures.end();)
        {
            if (it->second.removeRequested)
                it = textures.erase(it);
            else
                ++it;
        }

        for (auto& [handle, record] : textures)
        {
            (void)handle;
            if (!record.pending || record.pendingRgba.empty())
                continue;

            const cv::Mat& image = record.pendingRgba;
            if (image.type() != CV_8UC4)
            {
                error = "DX11 texture upload requires CV_8UC4 RGBA data";
                return false;
            }

            const bool sameSize = record.texture && record.width == image.cols &&
                record.height == image.rows;
            if (sameSize)
            {
                deviceContext->UpdateSubresource(record.texture.Get(), 0, nullptr,
                    image.data, static_cast<UINT>(image.step), 0);
            }
            else
            {
                D3D11_TEXTURE2D_DESC textureDesc{};
                textureDesc.Width = static_cast<UINT>(image.cols);
                textureDesc.Height = static_cast<UINT>(image.rows);
                textureDesc.MipLevels = 1;
                textureDesc.ArraySize = 1;
                textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                textureDesc.SampleDesc.Count = 1;
                textureDesc.Usage = D3D11_USAGE_DEFAULT;
                textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA initialData{};
                initialData.pSysMem = image.data;
                initialData.SysMemPitch = static_cast<UINT>(image.step);

                ComPtr<ID3D11Texture2D> texture;
                HRESULT hr = device->CreateTexture2D(&textureDesc, &initialData,
                    texture.GetAddressOf());
                if (FAILED(hr))
                {
                    error = HrMessage("DX11 CreateTexture2D", hr);
                    return false;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
                viewDesc.Format = textureDesc.Format;
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipLevels = 1;
                ComPtr<ID3D11ShaderResourceView> shaderView;
                hr = device->CreateShaderResourceView(texture.Get(), &viewDesc,
                    shaderView.GetAddressOf());
                if (FAILED(hr))
                {
                    error = HrMessage("DX11 CreateShaderResourceView", hr);
                    return false;
                }

                record.texture = std::move(texture);
                record.shaderView = std::move(shaderView);
                record.width = image.cols;
                record.height = image.rows;
            }

            record.pendingRgba.release();
            record.pending = false;
            record.ready = true;
        }
        return true;
    }
};

DX11Context::DX11Context() : impl_(std::make_unique<Impl>())
{
}

DX11Context::~DX11Context()
{
    Shutdown();
}

RenderBackendKind DX11Context::Kind() const
{
    return RenderBackendKind::DirectX11;
}

const char* DX11Context::Name() const
{
    return impl_ && impl_->softwareRenderer
        ? "DirectX 11 (WARP software)" : "DirectX 11";
}

bool DX11Context::Initialize(HWND window, std::string& error)
{
    Shutdown();
    impl_->window = window;

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    auto createDevice = [&](D3D_DRIVER_TYPE driverType)
    {
        impl_->swapChain.Reset();
        impl_->device.Reset();
        impl_->deviceContext.Reset();
        HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, driverType,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
            static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &swapChainDesc, impl_->swapChain.GetAddressOf(), impl_->device.GetAddressOf(),
            &impl_->featureLevel, impl_->deviceContext.GetAddressOf());
        if (result == E_INVALIDARG)
        {
            impl_->swapChain.Reset();
            impl_->device.Reset();
            impl_->deviceContext.Reset();
            result = D3D11CreateDeviceAndSwapChain(nullptr, driverType,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels + 1,
            static_cast<UINT>(std::size(featureLevels) - 1), D3D11_SDK_VERSION,
            &swapChainDesc, impl_->swapChain.GetAddressOf(), impl_->device.GetAddressOf(),
            &impl_->featureLevel, impl_->deviceContext.GetAddressOf());
        }
        return result;
    };

    HRESULT hr = createDevice(D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(hr))
    {
        const std::string hardwareError = HrMessage(
            "DX11 hardware D3D11CreateDeviceAndSwapChain", hr);
        hr = createDevice(D3D_DRIVER_TYPE_WARP);
        if (FAILED(hr))
        {
            error = hardwareError + "; " + HrMessage(
                "DX11 WARP D3D11CreateDeviceAndSwapChain", hr);
            Shutdown();
            return false;
        }
        impl_->softwareRenderer = true;
    }

    if (!impl_->CreateRenderTarget(error))
    {
        Shutdown();
        return false;
    }
    if (!ImGui_ImplDX11_Init(impl_->device.Get(), impl_->deviceContext.Get()))
    {
        error = "ImGui DirectX 11 renderer initialization failed";
        Shutdown();
        return false;
    }
    impl_->imguiInitialized = true;
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> baseAdapter;
    ComPtr<IDXGIAdapter1> adapter1;
    if (SUCCEEDED(impl_->device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&baseAdapter)) &&
        SUCCEEDED(baseAdapter.As(&adapter1)))
    {
        adapter1->GetDesc1(&impl_->adapterDescription);
        adapter1.As(&impl_->adapter);
    }
    impl_->occluded = false;
    error.clear();
    return true;
}

void DX11Context::Shutdown()
{
    if (!impl_)
        return;
    impl_->textures.clear();
    if (impl_->imguiInitialized)
    {
        ImGui_ImplDX11_Shutdown();
        impl_->imguiInitialized = false;
    }
    impl_->renderTarget.Reset();
    impl_->swapChain.Reset();
    impl_->deviceContext.Reset();
    impl_->device.Reset();
    impl_->adapter.Reset();
    impl_->adapterDescription = {};
    impl_->window = nullptr;
    impl_->nextTexture = 1;
    impl_->occluded = false;
    impl_->softwareRenderer = false;
}

void DX11Context::NewFrame()
{
    ImGui_ImplDX11_NewFrame();
}

bool DX11Context::IsOccluded()
{
    if (!impl_->occluded || !impl_->swapChain)
        return false;
    impl_->occluded = impl_->swapChain->Present(0, DXGI_PRESENT_TEST) ==
        DXGI_STATUS_OCCLUDED;
    return impl_->occluded;
}

bool DX11Context::Resize(unsigned int width, unsigned int height, std::string& error)
{
    if (!impl_->swapChain || width == 0 || height == 0)
        return true;
    impl_->renderTarget.Reset();
    const HRESULT hr = impl_->swapChain->ResizeBuffers(0, width, height,
        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        error = HrMessage("DX11 ResizeBuffers", hr);
        return false;
    }
    return impl_->CreateRenderTarget(error);
}

bool DX11Context::RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io,
    std::string& error)
{
    if (!impl_->ProcessTextureUploads(error))
        return false;

    const float clearColorWithAlpha[4] = {
        clearColor.x * clearColor.w,
        clearColor.y * clearColor.w,
        clearColor.z * clearColor.w,
        clearColor.w
    };
    ID3D11RenderTargetView* renderTarget = impl_->renderTarget.Get();
    impl_->deviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);
    impl_->deviceContext->ClearRenderTargetView(renderTarget, clearColorWithAlpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    const HRESULT hr = impl_->swapChain->Present(1, 0);
    impl_->occluded = hr == DXGI_STATUS_OCCLUDED;
    if (FAILED(hr))
    {
        const HRESULT removedReason = impl_->device->GetDeviceRemovedReason();
        error = HrMessage("DX11 Present", hr);
        if (FAILED(removedReason))
            error += "; " + HrMessage("device removed reason", removedReason);
        return false;
    }
    error.clear();
    return true;
}

void DX11Context::WaitIdle()
{
    if (impl_->deviceContext)
        impl_->deviceContext->Flush();
}

RenderTextureHandle DX11Context::CreateTexture()
{
    const RenderTextureHandle handle = impl_->nextTexture++;
    impl_->textures.try_emplace(handle);
    return handle;
}

bool DX11Context::UploadTexture(RenderTextureHandle texture, const cv::Mat& rgba,
    std::string& error)
{
    const auto it = impl_->textures.find(texture);
    if (it == impl_->textures.end())
    {
        error = "DX11 texture handle is invalid";
        return false;
    }
    if (rgba.empty() || rgba.type() != CV_8UC4)
    {
        error = "DX11 texture upload requires non-empty CV_8UC4 RGBA data";
        return false;
    }
    it->second.pendingRgba = rgba.clone();
    it->second.pending = true;
    it->second.removeRequested = false;
    error.clear();
    return true;
}

void DX11Context::ReleaseTexture(RenderTextureHandle texture)
{
    const auto it = impl_->textures.find(texture);
    if (it != impl_->textures.end())
        it->second.removeRequested = true;
}

ImTextureID DX11Context::TextureId(RenderTextureHandle texture) const
{
    const auto it = impl_->textures.find(texture);
    if (it == impl_->textures.end() || !it->second.ready || !it->second.shaderView)
        return ImTextureID_Invalid;
    return static_cast<ImTextureID>(
        reinterpret_cast<std::uintptr_t>(it->second.shaderView.Get()));
}

bool DX11Context::TextureReady(RenderTextureHandle texture) const
{
    return TextureId(texture) != ImTextureID_Invalid;
}

bool DX11Context::QueryMemoryInfo(RenderMemoryInfo& info) const
{
    info = {};
    if (!impl_ || !impl_->adapter)
        return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (FAILED(impl_->adapter->QueryVideoMemoryInfo(0,
        DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
        return false;
    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, impl_->adapterDescription.Description, -1,
        name, static_cast<int>(std::size(name)), nullptr, nullptr);
    info.available = true;
    info.adapterName = name;
    info.dedicatedVideoMemory = impl_->adapterDescription.DedicatedVideoMemory;
    info.currentUsage = memory.CurrentUsage;
    info.budget = memory.Budget;
    return true;
}
