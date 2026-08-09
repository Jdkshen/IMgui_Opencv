#pragma once

#include "RenderBackend.h"

#include <memory>

// Adapter that exposes the preserved DX12Context implementation through the
// same contract as DX11Context.
class DX12Backend final : public IRenderBackend
{
public:
    DX12Backend();
    ~DX12Backend() override;

    DX12Backend(const DX12Backend&) = delete;
    DX12Backend& operator=(const DX12Backend&) = delete;

    RenderBackendKind Kind() const override;
    const char* Name() const override;
    bool Initialize(HWND window, std::string& error) override;
    void Shutdown() override;
    void NewFrame() override;
    bool IsOccluded() override;
    bool Resize(unsigned int width, unsigned int height, std::string& error) override;
    bool RenderAndPresent(const ImVec4& clearColor, const ImGuiIO& io,
        std::string& error) override;
    void WaitIdle() override;

    RenderTextureHandle CreateTexture() override;
    bool UploadTexture(RenderTextureHandle texture, const cv::Mat& rgba,
        std::string& error) override;
    void ReleaseTexture(RenderTextureHandle texture) override;
    ImTextureID TextureId(RenderTextureHandle texture) const override;
    bool TextureReady(RenderTextureHandle texture) const override;
    bool QueryMemoryInfo(RenderMemoryInfo& info) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
