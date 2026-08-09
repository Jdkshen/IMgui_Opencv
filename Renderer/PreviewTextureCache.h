#pragma once

#include "../Core/RenderBackend.h"

#include <cstdint>
#include <vector>

#include <opencv2/core/mat.hpp>

enum class PreviewTextureKind : std::uint8_t
{
    TemplateMatch,
    ShapeTemplate,
    MultiColorReference,
    RunResult
};

struct PreviewTextureView
{
    ImTextureID textureId = ImTextureID_Invalid;
    int width = 0;
    int height = 0;
    bool ready = false;
};

namespace PreviewTextureCache
{
    std::uint64_t ImageSignature(const cv::Mat& image);
    std::uint64_t CombineSignature(std::uint64_t seed, std::uint64_t value);

    bool NeedsUpdate(std::uint64_t toolId, PreviewTextureKind kind,
        std::uint64_t signature);
    void Queue(std::uint64_t toolId, PreviewTextureKind kind,
        std::uint64_t signature, const cv::Mat& image, int maxDimension);
    PreviewTextureView Get(std::uint64_t toolId, PreviewTextureKind kind);

    void ForgetTool(std::uint64_t toolId);
    void Prune(const std::vector<std::uint64_t>& activeToolIds);
    void UploadPending();
    void Shutdown();
}
