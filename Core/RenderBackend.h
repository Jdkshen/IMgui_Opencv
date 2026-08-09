#pragma once

#define NOMINMAX
#include <windows.h>

#include "imgui/imgui.h"

#include <opencv2/core/mat.hpp>

#include <cstdint>
#include <string>

enum class RenderBackendKind : std::uint8_t
{
    None,
    DirectX12,
    DirectX11
};

using RenderTextureHandle = std::uint64_t;
constexpr RenderTextureHandle kInvalidRenderTexture = 0;

struct RenderMemoryInfo
{
    bool available = false;
    std::string adapterName;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t currentUsage = 0;
    std::uint64_t budget = 0;
};

constexpr RenderBackendKind SelectRenderBackend(bool dx12Available, bool dx11Available)
{
    if (dx12Available)
        return RenderBackendKind::DirectX12;
    if (dx11Available)
        return RenderBackendKind::DirectX11;
    return RenderBackendKind::None;
}

class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual const char* Name() const = 0;

    virtual bool Initialize(HWND window, std::string& error) = 0;
    virtual void Shutdown() = 0;
    virtual void NewFrame() = 0;
    virtual bool IsOccluded() = 0;
    virtual bool Resize(unsigned int width, unsigned int height, std::string& error) = 0;
    virtual bool RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io,
        std::string& error) = 0;
    virtual void WaitIdle() = 0;

    // UploadTexture queues a CPU RGBA image. The backend commits queued uploads at
    // the beginning of RenderAndPresent so DX11 and DX12 share one call contract.
    virtual RenderTextureHandle CreateTexture() = 0;
    virtual bool UploadTexture(RenderTextureHandle texture, const cv::Mat& rgba,
        std::string& error) = 0;
    virtual void ReleaseTexture(RenderTextureHandle texture) = 0;
    virtual ImTextureID TextureId(RenderTextureHandle texture) const = 0;
    virtual bool TextureReady(RenderTextureHandle texture) const = 0;
    virtual bool QueryMemoryInfo(RenderMemoryInfo& info) const
    {
        info = {};
        return false;
    }
};
