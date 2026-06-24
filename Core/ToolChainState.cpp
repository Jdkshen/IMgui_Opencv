#include "ToolChainState.h"

#include "UIStateBridge.h"

namespace ToolChainState
{
std::vector<ToolInstance>& Tools()
{
    return UI::g_ToolInstances;
}

const std::vector<ToolInstance>& ReadOnlyTools()
{
    return UI::g_ToolInstances;
}

int ActiveIndex()
{
    return UI::g_ActiveToolIndex;
}

void SetActiveIndex(int index)
{
    UI::g_ActiveToolIndex = index;
}

bool YoloLiveDetect()
{
    return UI::g_YoloLiveDetect;
}

void SetYoloLiveDetect(bool enabled)
{
    UI::g_YoloLiveDetect = enabled;
}

int YoloLiveInstanceIndex()
{
    return UI::g_YoloLiveInstanceIdx;
}

void SetYoloLiveInstanceIndex(int index)
{
    UI::g_YoloLiveInstanceIdx = index;
}

float YoloLastTimeMs()
{
    return UI::g_YoloLastTimeMs;
}

void SetYoloLastTimeMs(float ms)
{
    UI::g_YoloLastTimeMs = ms;
}

float YoloLiveFrameMs()
{
    return UI::g_YoloLiveFrameMs;
}

void SetYoloLiveFrameMs(float ms)
{
    UI::g_YoloLiveFrameMs = ms;
}

void MoveOriginalToolToFront()
{
    UI::MoveOriginalToolToFront();
}
}
