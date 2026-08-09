#pragma once

#include "RenderBackend.h"

#include <memory>

// Independent DirectX 11 implementation of the common renderer contract.
// No UI or vision module depends on D3D11 types; they remain private to the PIMPL.
class DX11Context final : public IRenderBackend
{
public:
    DX11Context();
    ~DX11Context() override;

    DX11Context(const DX11Context&) = delete;
    DX11Context& operator=(const DX11Context&) = delete;

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
