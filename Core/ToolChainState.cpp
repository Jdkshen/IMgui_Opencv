#include "ToolChainState.h"

#include <algorithm>

namespace ToolChainState
{
namespace
{
    std::vector<ToolInstance> s_tools;
    int s_activeToolIndex = -1;
    bool s_yoloLiveDetect = false;
    int s_yoloLiveInstanceIndex = -1;
    float s_yoloLastTimeMs = 0.0f;
    float s_yoloLiveFrameMs = 0.0f;
}

std::vector<ToolInstance>& Tools()
{
    return s_tools;
}

const std::vector<ToolInstance>& ReadOnlyTools()
{
    return s_tools;
}

int& ActiveIndexRef()
{
    return s_activeToolIndex;
}

int ActiveIndex()
{
    return s_activeToolIndex;
}

void SetActiveIndex(int index)
{
    s_activeToolIndex = index;
}

bool YoloLiveDetect()
{
    return s_yoloLiveDetect;
}

bool& YoloLiveDetectRef()
{
    return s_yoloLiveDetect;
}

void SetYoloLiveDetect(bool enabled)
{
    s_yoloLiveDetect = enabled;
}

int& YoloLiveInstanceIndexRef()
{
    return s_yoloLiveInstanceIndex;
}

int YoloLiveInstanceIndex()
{
    return s_yoloLiveInstanceIndex;
}

void SetYoloLiveInstanceIndex(int index)
{
    s_yoloLiveInstanceIndex = index;
}

float YoloLastTimeMs()
{
    return s_yoloLastTimeMs;
}

float& YoloLastTimeMsRef()
{
    return s_yoloLastTimeMs;
}

void SetYoloLastTimeMs(float ms)
{
    s_yoloLastTimeMs = ms;
}

float& YoloLiveFrameMsRef()
{
    return s_yoloLiveFrameMs;
}

float YoloLiveFrameMs()
{
    return s_yoloLiveFrameMs;
}

void SetYoloLiveFrameMs(float ms)
{
    s_yoloLiveFrameMs = ms;
}

void MoveOriginalToolToFront()
{
    std::stable_partition(s_tools.begin(), s_tools.end(),
        [](const ToolInstance& it) { return it.type == 12; });
}
}
