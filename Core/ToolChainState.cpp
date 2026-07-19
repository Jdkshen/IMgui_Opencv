#include "ToolChainState.h"

#include <algorithm>

// =====================================================
// 内部状态（模块私有）
// =====================================================
namespace ToolChainState
{
namespace
{
    std::vector<ToolInstance> s_tools;       // 工具实例列表
    int s_activeToolIndex = -1;              // 当前激活的工具索引（-1 = 无）
    bool s_yoloLiveDetect = false;           // YOLO 实时检测开关
    int s_yoloLiveInstanceIndex = -1;        // 实时检测使用的工具实例索引
    float s_yoloLastTimeMs = 0.0f;           // 最近一次推理耗时
    float s_yoloLiveFrameMs = 0.0f;          // 实时帧耗时
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
    if (s_tools.size() < 2)
        return;

    std::vector<int> order;
    order.reserve(s_tools.size());
    for (int i = 0; i < static_cast<int>(s_tools.size()); ++i)
        if (s_tools[i].type == 12)
            order.push_back(i);
    for (int i = 0; i < static_cast<int>(s_tools.size()); ++i)
        if (s_tools[i].type != 12)
            order.push_back(i);

    bool changed = false;
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        changed |= order[i] != i;
    if (!changed)
        return;

    std::vector<int> oldToNew(s_tools.size(), -1);
    std::vector<ToolInstance> reordered;
    reordered.reserve(s_tools.size());
    for (int newIndex = 0; newIndex < static_cast<int>(order.size()); ++newIndex)
    {
        oldToNew[order[newIndex]] = newIndex;
        reordered.push_back(std::move(s_tools[order[newIndex]]));
    }

    for (ToolInstance& tool : reordered)
    {
        if (tool.resultRoiSourceTool >= 0 &&
            tool.resultRoiSourceTool < static_cast<int>(oldToNew.size()))
        {
            tool.resultRoiSourceTool = oldToNew[tool.resultRoiSourceTool];
        }
        if (tool.fixture.sourceToolIndex >= 0 &&
            tool.fixture.sourceToolIndex < static_cast<int>(oldToNew.size()))
        {
            tool.fixture.sourceToolIndex = oldToNew[tool.fixture.sourceToolIndex];
        }
    }
    if (s_activeToolIndex >= 0 && s_activeToolIndex < static_cast<int>(oldToNew.size()))
        s_activeToolIndex = oldToNew[s_activeToolIndex];
    if (s_yoloLiveInstanceIndex >= 0 && s_yoloLiveInstanceIndex < static_cast<int>(oldToNew.size()))
        s_yoloLiveInstanceIndex = oldToNew[s_yoloLiveInstanceIndex];
    s_tools = std::move(reordered);
}
}
