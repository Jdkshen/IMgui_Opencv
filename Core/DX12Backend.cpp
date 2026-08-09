#define NOMINMAX
#include "DX12Backend.h"

#include "DX12Context.h"
#include "imgui/imgui_impl_dx12.h"

#include <d3dx12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <unordered_map>

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

struct DX12Backend::Impl
{
    struct TextureRecord
    {
        ComPtr<ID3D12Resource> texture;
        std::array<ComPtr<ID3D12Resource>, APP_NUM_FRAMES_IN_FLIGHT> uploads;
        std::array<UINT64, APP_NUM_FRAMES_IN_FLIGHT> uploadCapacities{};
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        cv::Mat pendingRgba;
        int width = 0;
        int height = 0;
        bool descriptorAllocated = false;
        bool pending = false;
        bool removeRequested = false;
        bool ready = false;
    };

    std::unordered_map<RenderTextureHandle, TextureRecord> textures;
    RenderTextureHandle nextTexture = 1;
    bool imguiInitialized = false;

    void ReleaseRecord(TextureRecord& record)
    {
        record.texture.Reset();
        for (ComPtr<ID3D12Resource>& upload : record.uploads)
            upload.Reset();
        record.uploadCapacities.fill(0);
        if (record.descriptorAllocated && g_pd3dSrvDescHeapAlloc.Heap)
        {
            g_pd3dSrvDescHeapAlloc.Free(record.cpuHandle, record.gpuHandle);
            record.descriptorAllocated = false;
        }
    }

    bool PrepareTextureResources(std::string& error)
    {
        bool requiresIdle = false;
        for (const auto& [handle, record] : textures)
        {
            (void)handle;
            requiresIdle |= record.removeRequested && record.texture;
            requiresIdle |= record.pending && record.texture &&
                (record.width != record.pendingRgba.cols ||
                    record.height != record.pendingRgba.rows);
        }
        if (requiresIdle)
            WaitForPendingOperations();

        for (auto it = textures.begin(); it != textures.end();)
        {
            if (it->second.removeRequested)
            {
                ReleaseRecord(it->second);
                it = textures.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto& [handle, record] : textures)
        {
            (void)handle;
            if (!record.pending || record.pendingRgba.empty())
                continue;
            if (record.pendingRgba.type() != CV_8UC4)
            {
                error = "DX12 texture upload requires CV_8UC4 RGBA data";
                return false;
            }

            const int newWidth = record.pendingRgba.cols;
            const int newHeight = record.pendingRgba.rows;
            if (!record.texture || record.width != newWidth || record.height != newHeight)
            {
                record.texture.Reset();

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = static_cast<UINT64>(newWidth);
                desc.Height = static_cast<UINT>(newHeight);
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

                const CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
                HRESULT hr = g_pd3dDevice->CreateCommittedResource(&heapDefault,
                    D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&record.texture));
                if (FAILED(hr))
                {
                    error = HrMessage("DX12 CreateCommittedResource(texture)", hr);
                    return false;
                }

                if (!record.descriptorAllocated)
                {
                    g_pd3dSrvDescHeapAlloc.Alloc(&record.cpuHandle, &record.gpuHandle);
                    record.descriptorAllocated = true;
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MipLevels = 1;
                g_pd3dDevice->CreateShaderResourceView(record.texture.Get(), &srv,
                    record.cpuHandle);
                record.width = newWidth;
                record.height = newHeight;
                record.ready = false;
            }

            const UINT uploadSlot = g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT;
            const UINT64 requiredSize = GetRequiredIntermediateSize(
                record.texture.Get(), 0, 1);
            if (!record.uploads[uploadSlot] ||
                record.uploadCapacities[uploadSlot] < requiredSize)
            {
                record.uploads[uploadSlot].Reset();
                const CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
                const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(requiredSize);
                const HRESULT hr = g_pd3dDevice->CreateCommittedResource(&heapUpload,
                    D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&record.uploads[uploadSlot]));
                if (FAILED(hr))
                {
                    record.uploadCapacities[uploadSlot] = 0;
                    error = HrMessage("DX12 CreateCommittedResource(upload)", hr);
                    return false;
                }
                record.uploadCapacities[uploadSlot] = requiredSize;
            }
        }
        return true;
    }

    bool RecordTextureCopies(std::string& error)
    {
        for (auto& [handle, record] : textures)
        {
            (void)handle;
            if (!record.pending || record.pendingRgba.empty())
                continue;

            if (record.ready)
            {
                const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
                    record.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                g_pd3dCommandList->ResourceBarrier(1, &toCopy);
            }

            D3D12_SUBRESOURCE_DATA subresource{};
            subresource.pData = record.pendingRgba.data;
            subresource.RowPitch = static_cast<LONG_PTR>(record.pendingRgba.step);
            subresource.SlicePitch = static_cast<LONG_PTR>(
                record.pendingRgba.step * record.pendingRgba.rows);
            const UINT uploadSlot = g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT;
            UpdateSubresources(g_pd3dCommandList, record.texture.Get(),
                record.uploads[uploadSlot].Get(), 0, 0, 1, &subresource);

            const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
                record.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            g_pd3dCommandList->ResourceBarrier(1, &toShader);

            record.pendingRgba.release();
            record.pending = false;
            record.ready = true;
        }
        error.clear();
        return true;
    }
};

DX12Backend::DX12Backend() : impl_(std::make_unique<Impl>())
{
}

DX12Backend::~DX12Backend()
{
    Shutdown();
}

RenderBackendKind DX12Backend::Kind() const
{
    return RenderBackendKind::DirectX12;
}

const char* DX12Backend::Name() const
{
    return "DirectX 12";
}

bool DX12Backend::Initialize(HWND window, std::string& error)
{
    Shutdown();

    ComPtr<ID3D12Device> capabilityProbe;
    const HRESULT probeResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&capabilityProbe));
    if (FAILED(probeResult))
    {
        error = HrMessage("D3D12 device capability check", probeResult);
        return false;
    }

    if (!CreateDeviceD3D(window))
    {
        error = "DX12 device/swap-chain/render-resource initialization failed "
            "after the device capability check";
        CleanupDeviceD3D();
        return false;
    }

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = g_pd3dDevice;
    initInfo.CommandQueue = g_pd3dCommandQueue;
    initInfo.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = g_pd3dSrvDescHeap;
    initInfo.SrvDescriptorAllocFn = [](
        ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
    {
        g_pd3dSrvDescHeapAlloc.Alloc(cpu, gpu);
    };
    initInfo.SrvDescriptorFreeFn = [](
        ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        g_pd3dSrvDescHeapAlloc.Free(cpu, gpu);
    };

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        error = "ImGui DirectX 12 renderer initialization failed";
        CleanupDeviceD3D();
        return false;
    }
    impl_->imguiInitialized = true;

    // Preserve the legacy handles for any DX12-only diagnostic code that still
    // includes DX12Context directly. UI and business code use GraphicsBackend.
    gSrvCpuHandle = g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    gSrvGpuHandle = g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    error.clear();
    return true;
}

void DX12Backend::Shutdown()
{
    if (!impl_)
        return;
    if (g_pd3dCommandQueue && g_fence && g_fenceEvent)
        WaitForPendingOperations();
    for (auto& [handle, record] : impl_->textures)
    {
        (void)handle;
        impl_->ReleaseRecord(record);
    }
    impl_->textures.clear();
    if (impl_->imguiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        impl_->imguiInitialized = false;
    }
    if (g_pd3dDevice || g_pSwapChain || g_pd3dCommandQueue || g_pd3dSrvDescHeap)
        CleanupDeviceD3D();
    impl_->nextTexture = 1;
}

void DX12Backend::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
}

bool DX12Backend::IsOccluded()
{
    if (!g_SwapChainOccluded || !g_pSwapChain)
        return false;
    g_SwapChainOccluded = g_pSwapChain->Present(0, DXGI_PRESENT_TEST) ==
        DXGI_STATUS_OCCLUDED;
    return g_SwapChainOccluded;
}

bool DX12Backend::Resize(unsigned int width, unsigned int height, std::string& error)
{
    if (!g_pSwapChain || width == 0 || height == 0)
        return true;
    CleanupRenderTarget();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    g_pSwapChain->GetDesc1(&desc);
    const HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height,
        desc.Format, desc.Flags);
    if (FAILED(hr))
    {
        error = HrMessage("DX12 ResizeBuffers", hr);
        return false;
    }
    CreateRenderTarget();
    error.clear();
    return true;
}

bool DX12Backend::RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io,
    std::string& error)
{
    FrameContext* frameContext = WaitForNextFrameContext();
    const UINT backBufferIndex = g_pSwapChain->GetCurrentBackBufferIndex();
    // Waiting for the selected frame context also makes its matching upload
    // buffer slot safe to reuse.
    if (!impl_->PrepareTextureResources(error))
        return false;

    HRESULT hr = frameContext->CommandAllocator->Reset();
    if (FAILED(hr))
    {
        error = HrMessage("DX12 command allocator Reset", hr);
        return false;
    }
    hr = g_pd3dCommandList->Reset(frameContext->CommandAllocator, nullptr);
    if (FAILED(hr))
    {
        error = HrMessage("DX12 command list Reset", hr);
        return false;
    }

    if (!impl_->RecordTextureCopies(error))
        return false;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIndex];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_pd3dCommandList->ResourceBarrier(1, &barrier);

    const float clearColorWithAlpha[4] = {
        clearColor.x * clearColor.w,
        clearColor.y * clearColor.w,
        clearColor.z * clearColor.w,
        clearColor.w
    };
    g_pd3dCommandList->ClearRenderTargetView(
        g_mainRenderTargetDescriptor[backBufferIndex], clearColorWithAlpha, 0, nullptr);
    g_pd3dCommandList->OMSetRenderTargets(1,
        &g_mainRenderTargetDescriptor[backBufferIndex], FALSE, nullptr);
    g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_pd3dCommandList->ResourceBarrier(1, &barrier);
    hr = g_pd3dCommandList->Close();
    if (FAILED(hr))
    {
        error = HrMessage("DX12 command list Close", hr);
        return false;
    }

    ID3D12CommandList* commandLists[] = {g_pd3dCommandList};
    g_pd3dCommandQueue->ExecuteCommandLists(1, commandLists);

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    hr = g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
    if (FAILED(hr))
    {
        error = HrMessage("DX12 queue Signal", hr);
        return false;
    }
    frameContext->FenceValue = g_fenceLastSignaledValue;

    hr = g_pSwapChain->Present(1, 0);
    g_SwapChainOccluded = hr == DXGI_STATUS_OCCLUDED;
    ++g_frameIndex;
    if (FAILED(hr))
    {
        error = HrMessage("DX12 Present", hr);
        if (g_pd3dDevice)
        {
            const HRESULT removedReason = g_pd3dDevice->GetDeviceRemovedReason();
            if (FAILED(removedReason))
                error += "; " + HrMessage("device removed reason", removedReason);
        }
        return false;
    }
    error.clear();
    return true;
}

void DX12Backend::WaitIdle()
{
    WaitForPendingOperations();
}

RenderTextureHandle DX12Backend::CreateTexture()
{
    const RenderTextureHandle handle = impl_->nextTexture++;
    impl_->textures.try_emplace(handle);
    return handle;
}

bool DX12Backend::UploadTexture(RenderTextureHandle texture, const cv::Mat& rgba,
    std::string& error)
{
    const auto it = impl_->textures.find(texture);
    if (it == impl_->textures.end())
    {
        error = "DX12 texture handle is invalid";
        return false;
    }
    if (rgba.empty() || rgba.type() != CV_8UC4)
    {
        error = "DX12 texture upload requires non-empty CV_8UC4 RGBA data";
        return false;
    }
    it->second.pendingRgba = rgba.clone();
    it->second.pending = true;
    it->second.removeRequested = false;
    error.clear();
    return true;
}

void DX12Backend::ReleaseTexture(RenderTextureHandle texture)
{
    const auto it = impl_->textures.find(texture);
    if (it != impl_->textures.end())
        it->second.removeRequested = true;
}

ImTextureID DX12Backend::TextureId(RenderTextureHandle texture) const
{
    const auto it = impl_->textures.find(texture);
    if (it == impl_->textures.end() || !it->second.ready ||
        !it->second.descriptorAllocated)
        return ImTextureID_Invalid;
    return static_cast<ImTextureID>(it->second.gpuHandle.ptr);
}

bool DX12Backend::TextureReady(RenderTextureHandle texture) const
{
    return TextureId(texture) != ImTextureID_Invalid;
}

bool DX12Backend::QueryMemoryInfo(RenderMemoryInfo& info) const
{
    info = {};
    if (!g_pd3dDevice)
        return false;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;
    ComPtr<IDXGIAdapter1> adapter1;
    if (FAILED(factory->EnumAdapterByLuid(g_pd3dDevice->GetAdapterLuid(),
        IID_PPV_ARGS(&adapter1))))
        return false;
    ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(adapter1.As(&adapter3)))
        return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (FAILED(adapter3->QueryVideoMemoryInfo(0,
        DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
        return false;
    DXGI_ADAPTER_DESC1 description{};
    adapter1->GetDesc1(&description);
    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name,
        static_cast<int>(std::size(name)), nullptr, nullptr);
    info.available = true;
    info.adapterName = name;
    info.dedicatedVideoMemory = description.DedicatedVideoMemory;
    info.currentUsage = memory.CurrentUsage;
    info.budget = memory.Budget;
    return true;
}
