#define NOMINMAX
#include "PreviewTextureCache.h"

#include "../Core/DX12Context.h"
#include "../Log/LogSystem.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <d3dx12.h>
#include <opencv2/imgproc.hpp>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr std::size_t kMaximumPreviewTextures = 48;

    struct CacheKey
    {
        std::uint64_t toolId = 0;
        PreviewTextureKind kind = PreviewTextureKind::TemplateMatch;

        bool operator==(const CacheKey& other) const
        {
            return toolId == other.toolId && kind == other.kind;
        }
    };

    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey& key) const
        {
            return static_cast<std::size_t>(key.toolId ^
                (static_cast<std::uint64_t>(key.kind) * 0x9e3779b97f4a7c15ULL));
        }
    };

    struct CacheEntry
    {
        ComPtr<ID3D12Resource> texture;
        ComPtr<ID3D12Resource> upload;
        UINT64 uploadCapacity = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        std::uint64_t signature = 0;
        std::uint64_t pendingSignature = 0;
        std::uint64_t lastUseSerial = 0;
        cv::Mat pendingRgba;
        int width = 0;
        int height = 0;
        bool descriptorAllocated = false;
        bool pending = false;
        bool ready = false;
        bool removeRequested = false;
    };

    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> g_entries;
    std::uint64_t g_useSerial = 0;

    CacheKey MakeKey(std::uint64_t toolId, PreviewTextureKind kind)
    {
        return { toolId, kind };
    }

    cv::Mat MakeRgbaThumbnail(const cv::Mat& source, int maxDimension)
    {
        if (source.empty())
            return {};

        cv::Mat image = source;
        cv::Mat convertedDepth;
        if (image.depth() != CV_8U)
        {
            image.convertTo(convertedDepth, CV_8U);
            image = convertedDepth;
        }

        maxDimension = std::max(2, maxDimension);
        const float scale = std::min(1.0f,
            static_cast<float>(maxDimension) / static_cast<float>(std::max(image.cols, image.rows)));
        const int width = std::max(2, static_cast<int>(image.cols * scale));
        const int height = std::max(2, static_cast<int>(image.rows * scale));

        cv::Mat resized;
        cv::resize(image, resized, cv::Size(width, height), 0.0, 0.0, cv::INTER_NEAREST);

        cv::Mat rgba;
        if (resized.channels() == 1)
            cv::cvtColor(resized, rgba, cv::COLOR_GRAY2RGBA);
        else if (resized.channels() == 3)
            cv::cvtColor(resized, rgba, cv::COLOR_BGR2RGBA);
        else if (resized.channels() == 4)
            cv::cvtColor(resized, rgba, cv::COLOR_BGRA2RGBA);
        return rgba;
    }

    void ReleaseEntry(CacheEntry& entry)
    {
        entry.texture.Reset();
        entry.upload.Reset();
        if (entry.descriptorAllocated)
        {
            g_pd3dSrvDescHeapAlloc.Free(entry.cpuHandle, entry.gpuHandle);
            entry.descriptorAllocated = false;
        }
    }

    void RequestOldestEviction()
    {
        auto oldest = g_entries.end();
        std::uint64_t oldestSerial = (std::numeric_limits<std::uint64_t>::max)();
        for (auto it = g_entries.begin(); it != g_entries.end(); ++it)
        {
            if (!it->second.removeRequested && it->second.lastUseSerial < oldestSerial)
            {
                oldest = it;
                oldestSerial = it->second.lastUseSerial;
            }
        }
        if (oldest != g_entries.end())
            oldest->second.removeRequested = true;
    }

    bool CreateOrResizeTexture(ID3D12Device* device, CacheEntry& entry)
    {
        const int newWidth = entry.pendingRgba.cols;
        const int newHeight = entry.pendingRgba.rows;
        if (entry.texture && entry.width == newWidth && entry.height == newHeight)
            return true;

        entry.texture.Reset();

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = newWidth;
        desc.Height = newHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT hr = device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&entry.texture));
        if (FAILED(hr))
        {
            LogSystem::Add(LOG_ERROR, "预览纹理创建失败 hr=0x%08X", hr);
            return false;
        }

        if (!entry.descriptorAllocated)
        {
            g_pd3dSrvDescHeapAlloc.Alloc(&entry.cpuHandle, &entry.gpuHandle);
            entry.descriptorAllocated = true;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(entry.texture.Get(), &srv, entry.cpuHandle);
        entry.width = newWidth;
        entry.height = newHeight;
        entry.ready = false;
        return true;
    }

    bool EnsureUploadBuffer(ID3D12Device* device, CacheEntry& entry, UINT64 requiredSize)
    {
        if (entry.upload && entry.uploadCapacity >= requiredSize)
            return true;

        entry.upload.Reset();
        CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
        const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(requiredSize);
        const HRESULT hr = device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&entry.upload));
        if (FAILED(hr))
        {
            LogSystem::Add(LOG_ERROR, "预览上传缓冲区创建失败 hr=0x%08X", hr);
            entry.uploadCapacity = 0;
            return false;
        }
        entry.uploadCapacity = requiredSize;
        return true;
    }
}

namespace PreviewTextureCache
{
    std::uint64_t CombineSignature(std::uint64_t seed, std::uint64_t value)
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    std::uint64_t ImageSignature(const cv::Mat& image)
    {
        std::uint64_t signature = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(image.data));
        signature = CombineSignature(signature, static_cast<std::uint64_t>(image.rows));
        signature = CombineSignature(signature, static_cast<std::uint64_t>(image.cols));
        signature = CombineSignature(signature, static_cast<std::uint64_t>(image.type()));
        signature = CombineSignature(signature, static_cast<std::uint64_t>(image.step));
        return signature;
    }

    bool NeedsUpdate(std::uint64_t toolId, PreviewTextureKind kind, std::uint64_t signature)
    {
        const auto it = g_entries.find(MakeKey(toolId, kind));
        if (it == g_entries.end())
            return true;
        const CacheEntry& entry = it->second;
        return entry.removeRequested ||
            (entry.pending ? entry.pendingSignature != signature : entry.signature != signature);
    }

    void Queue(std::uint64_t toolId, PreviewTextureKind kind, std::uint64_t signature,
        const cv::Mat& image, int maxDimension)
    {
        if (toolId == 0 || image.empty())
            return;

        const CacheKey key = MakeKey(toolId, kind);
        auto it = g_entries.find(key);
        if (it == g_entries.end())
        {
            if (g_entries.size() >= kMaximumPreviewTextures)
                RequestOldestEviction();
            it = g_entries.emplace(key, CacheEntry{}).first;
        }

        CacheEntry& entry = it->second;
        entry.pendingRgba = MakeRgbaThumbnail(image, maxDimension);
        if (entry.pendingRgba.empty())
            return;
        entry.pendingSignature = signature;
        entry.pending = true;
        entry.removeRequested = false;
        entry.lastUseSerial = ++g_useSerial;
    }

    PreviewTextureView Get(std::uint64_t toolId, PreviewTextureKind kind)
    {
        const auto it = g_entries.find(MakeKey(toolId, kind));
        if (it == g_entries.end())
            return {};

        CacheEntry& entry = it->second;
        entry.lastUseSerial = ++g_useSerial;
        PreviewTextureView view;
        view.gpuHandle = entry.gpuHandle;
        view.width = entry.pending ? entry.pendingRgba.cols : entry.width;
        view.height = entry.pending ? entry.pendingRgba.rows : entry.height;
        view.ready = entry.ready && entry.descriptorAllocated;
        return view;
    }

    void ForgetTool(std::uint64_t toolId)
    {
        for (auto& pair : g_entries)
        {
            if (pair.first.toolId == toolId)
                pair.second.removeRequested = true;
        }
    }

    void Prune(const std::vector<std::uint64_t>& activeToolIds)
    {
        const std::unordered_set<std::uint64_t> active(activeToolIds.begin(), activeToolIds.end());
        for (auto& pair : g_entries)
        {
            if (pair.first.kind != PreviewTextureKind::RunResult &&
                active.find(pair.first.toolId) == active.end())
                pair.second.removeRequested = true;
        }
    }

    void UploadPending(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
    {
        if (!device || !commandList)
            return;

        bool requiresGpuIdle = false;
        for (const auto& pair : g_entries)
        {
            const CacheEntry& entry = pair.second;
            requiresGpuIdle |= entry.removeRequested;
            requiresGpuIdle |= entry.pending && entry.texture &&
                (entry.width != entry.pendingRgba.cols || entry.height != entry.pendingRgba.rows);
        }
        if (requiresGpuIdle)
            WaitForPendingOperations();

        for (auto it = g_entries.begin(); it != g_entries.end();)
        {
            if (it->second.removeRequested)
            {
                ReleaseEntry(it->second);
                it = g_entries.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto& pair : g_entries)
        {
            CacheEntry& entry = pair.second;
            if (!entry.pending || entry.pendingRgba.empty())
                continue;

            const bool sameSize = entry.texture && entry.width == entry.pendingRgba.cols &&
                entry.height == entry.pendingRgba.rows;
            if (!CreateOrResizeTexture(device, entry))
                continue;

            if (sameSize && entry.ready)
            {
                const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
                    entry.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                commandList->ResourceBarrier(1, &toCopy);
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(entry.texture.Get(), 0, 1);
            if (!EnsureUploadBuffer(device, entry, uploadSize))
                continue;

            D3D12_SUBRESOURCE_DATA subresource{};
            subresource.pData = entry.pendingRgba.data;
            subresource.RowPitch = static_cast<LONG_PTR>(entry.pendingRgba.step);
            subresource.SlicePitch = static_cast<LONG_PTR>(entry.pendingRgba.step * entry.pendingRgba.rows);
            UpdateSubresources(commandList, entry.texture.Get(), entry.upload.Get(), 0, 0, 1, &subresource);

            const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
                entry.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, &toShader);

            entry.signature = entry.pendingSignature;
            entry.pending = false;
            entry.ready = true;
            entry.pendingRgba.release();
        }
    }

    void Shutdown()
    {
        for (auto& pair : g_entries)
            ReleaseEntry(pair.second);
        g_entries.clear();
        g_useSerial = 0;
    }
}
