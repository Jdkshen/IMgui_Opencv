#pragma once

#include "RenderBackend.h"

#include <string>

namespace GraphicsBackend
{
    // Default startup policy is DX12 first, then automatic DX11 fallback.
    // IMGUI_OPENCV_RENDER_BACKEND=dx11 is a diagnostic override for smoke tests.
    bool Initialize(HWND window);
    void Shutdown();
    bool IsInitialized();
    RenderBackendKind Kind();
    const char* Name();

    void NewFrame();
    bool IsOccluded();
    bool Resize(unsigned int width, unsigned int height);
    bool RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io);
    void WaitIdle();

    RenderTextureHandle CreateTexture();
    bool UploadTexture(RenderTextureHandle texture, const cv::Mat& rgba);
    void ReleaseTexture(RenderTextureHandle texture);
    ImTextureID TextureId(RenderTextureHandle texture);
    bool TextureReady(RenderTextureHandle texture);

    bool UploadMainTexture(const cv::Mat& rgba);
    void ReleaseMainTexture();
    ImTextureID MainTextureId();
    bool HasMainTexture();
    RenderMemoryInfo MemoryInfo();
    double ProcessGpuUtilization();

    const std::string& DX12FailureReason();
    const std::string& LastError();
}
