#pragma once
#include "../Algorithm/YOLODetector.h"
#include "../Core/VisionContext.h"

#include <utility>

// =====================================================
// ResultPublisher — 统一结果清理
// =====================================================
inline void ClearAllResults()
{
    gContext.ClearUnifiedResults();
    extern std::vector<struct DetectedObject> g_YoloOverlays;
    extern bool g_YoloShowOverlay;
    g_YoloOverlays.clear();
    g_YoloShowOverlay = false;
}

inline void PublishUnifiedResult(ToolResult result)
{
    ClearAllResults();
    gContext.unifiedResults.push_back(std::move(result));
}
