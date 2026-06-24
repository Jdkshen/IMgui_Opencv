#pragma once

#include <vector>

#include "ToolInstance.h"

namespace ToolChainState
{
    std::vector<ToolInstance>& Tools();
    const std::vector<ToolInstance>& ReadOnlyTools();

    int& ActiveIndexRef();
    int ActiveIndex();
    void SetActiveIndex(int index);

    bool& YoloLiveDetectRef();
    bool YoloLiveDetect();
    void SetYoloLiveDetect(bool enabled);
    int& YoloLiveInstanceIndexRef();
    int YoloLiveInstanceIndex();
    void SetYoloLiveInstanceIndex(int index);
    float& YoloLastTimeMsRef();
    float YoloLastTimeMs();
    void SetYoloLastTimeMs(float ms);
    float& YoloLiveFrameMsRef();
    float YoloLiveFrameMs();
    void SetYoloLiveFrameMs(float ms);

    void MoveOriginalToolToFront();
}
