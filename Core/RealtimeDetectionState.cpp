#include "RealtimeDetectionState.h"

#include <utility>

namespace
{
    std::vector<DetectedObject> s_objects;
    bool s_overlayVisible = false;
    float s_overlayOffsetX = 0.0f;
    RealtimeDetectionState::Performance s_stats;
}

namespace RealtimeDetectionState
{
    const std::vector<DetectedObject>& Objects()
    {
        return s_objects;
    }

    void SetObjects(std::vector<DetectedObject> objects)
    {
        s_objects = std::move(objects);
    }

    bool IsOverlayVisible()
    {
        return s_overlayVisible;
    }

    void SetOverlayVisible(bool visible)
    {
        s_overlayVisible = visible;
    }

    float OverlayOffsetX()
    {
        return s_overlayOffsetX;
    }

    void SetOverlayOffsetX(float offset)
    {
        s_overlayOffsetX = offset;
    }

    const Performance& Stats()
    {
        return s_stats;
    }

    void SetStats(const Performance& stats)
    {
        s_stats = stats;
    }

    void Clear()
    {
        s_objects.clear();
        s_overlayVisible = false;
        s_stats = {};
    }
}
