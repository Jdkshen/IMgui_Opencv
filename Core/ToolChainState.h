#pragma once

#include <vector>

#include "ToolInstance.h"

namespace ToolChainState
{
    std::vector<ToolInstance>& Tools();
    const std::vector<ToolInstance>& ReadOnlyTools();

    int ActiveIndex();
    void SetActiveIndex(int index);

    bool YoloLiveDetect();
    void SetYoloLiveDetect(bool enabled);
    int YoloLiveInstanceIndex();
    void SetYoloLiveInstanceIndex(int index);
    float YoloLastTimeMs();
    void SetYoloLastTimeMs(float ms);
    float YoloLiveFrameMs();
    void SetYoloLiveFrameMs(float ms);

    void MoveOriginalToolToFront();
}
