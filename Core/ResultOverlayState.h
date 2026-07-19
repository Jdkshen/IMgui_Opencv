#pragma once

#include "DetectionTypes.h"
#include "../Algorithm/ToolResult.h"

#include <opencv2/core/types.hpp>

#include <string>
#include <vector>

namespace ResultOverlayState
{
    struct Settings
    {
        bool showLabels = true;
        bool avoidLabelOverlap = true;
        int maxVisibleLabels = 30;
    };

    struct FixtureOverlay
    {
        cv::Point2f referenceOrigin;
        float referenceAngleDegrees = 0.0f;
        cv::Point2f currentOrigin;
        float currentAngleDegrees = 0.0f;
        bool showLabel = true;
    };

    Settings& MutableSettings();
    const Settings& ReadOnlySettings();

    const std::vector<ToolResult>& Results();
    const std::vector<DetectedObject>& RealtimeObjects();
    bool IsRealtimeOverlayVisible();
    float RealtimeOverlayOffsetX();
    std::vector<FixtureOverlay> FixtureOverlays();
    void ClearResults();

    bool ShouldDrawResultLabels(const ToolResult& result);
    int MaxVisibleLabels();
    bool ShouldDrawRegionLabel(const ToolResult& result, const std::string& label);
    std::string BuildLabel(const ToolResult& result, const std::string& itemLabel);
}
