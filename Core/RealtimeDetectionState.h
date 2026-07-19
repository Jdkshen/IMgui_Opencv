#pragma once

#include "DetectionTypes.h"

#include <vector>

namespace RealtimeDetectionState
{
    struct Performance
    {
        float preprocessMs = 0.0f;
        float inferenceMs = 0.0f;
        float postprocessMs = 0.0f;
        float totalMs = 0.0f;
    };

    const std::vector<DetectedObject>& Objects();
    void SetObjects(std::vector<DetectedObject> objects);
    bool IsOverlayVisible();
    void SetOverlayVisible(bool visible);
    float OverlayOffsetX();
    void SetOverlayOffsetX(float offset);
    const Performance& Stats();
    void SetStats(const Performance& stats);
    void Clear();
}
