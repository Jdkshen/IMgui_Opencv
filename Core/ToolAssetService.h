#pragma once

#include <cstdint>
#include <string>

#include <opencv2/core/types.hpp>

#include "ToolInstance.h"

enum class ToolAssetKind
{
    TemplateMatch,
    ShapeTemplate,
    MultiColorReference,
    DifferenceReference
};

struct ToolAssetCaptureResult
{
    bool success = false;
    std::string message;
    cv::Rect bounds;
};

namespace ToolAssetService
{
    int BeginROICapture(ToolInstance& tool, ToolAssetKind kind);
    int ActiveROIIndex(std::uint64_t toolId, ToolAssetKind kind);
    bool IsROICaptureActive(std::uint64_t toolId, ToolAssetKind kind);
    ToolAssetCaptureResult ConfirmROICapture(ToolInstance& tool, ToolAssetKind kind);
    void CancelROICapture(std::uint64_t toolId, ToolAssetKind kind);

    ToolAssetCaptureResult CaptureCurrentImage(ToolInstance& tool, ToolAssetKind kind);
    void ClearAsset(ToolInstance& tool, ToolAssetKind kind);

    void ForgetTool(std::uint64_t toolId);
    void ClearSessions();
}
