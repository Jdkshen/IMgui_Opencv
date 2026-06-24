#pragma once
#include <vector>
#include <unordered_map>
#include <functional>
#include "../Core/ToolInstance.h"
#include "../Core/ToolTypes.h"

// 工具 UI 绘制函数签名（用 std::function 支持捕获 lambda）
using ToolUIFn = std::function<void(ToolInstance& it, int inst)>;

// 工具注册表（所有可用工具）
extern std::unordered_map<int, ToolUIFn> g_ToolUIMap;

// =====================================================
// 功能窗口 — 手风琴工具列表 + 全部/单步/循环执行
// =====================================================
namespace UI
{
    extern bool g_YoloLiveDetect; // YOLO 实时检测开关
    extern int  g_YoloLiveInstanceIdx; // 触发实时检测的 YOLO 实例索引
    extern float g_YoloLastTimeMs;    // 上次 YOLO 推理耗时
    extern float g_YoloLiveFrameMs;   // 实时检测每帧耗时
    void ShowToolsWindow();
    void MoveOriginalToolToFront();

    inline int FirstMovableToolIndex(const std::vector<ToolInstance>& tools)
    {
        return (!tools.empty() && tools.front().type == 12) ? 1 : 0;
    }

    inline bool MoveToolInstance(std::vector<ToolInstance>& tools,
        int from,
        int to,
        int& activeIndex,
        int& liveIndex,
        bool& liveDetect)
    {
        const int count = (int)tools.size();
        const int firstMovable = FirstMovableToolIndex(tools);
        if (from < firstMovable || from >= count || to < firstMovable || to >= count || from == to)
            return false;

        std::swap(tools[from], tools[to]);

        auto remapSwappedIndex = [from, to](int value) {
            if (value == from)
                return to;
            if (value == to)
                return from;
            return value;
        };
        activeIndex = remapSwappedIndex(activeIndex);
        liveIndex = remapSwappedIndex(liveIndex);
        return true;
    }

    inline bool RemoveToolInstance(std::vector<ToolInstance>& tools,
        int index,
        int& activeIndex,
        int& liveIndex,
        bool& liveDetect)
    {
        if (index < FirstMovableToolIndex(tools) || index >= (int)tools.size())
            return false;

        delete tools[index].toolImpl;
        tools[index].toolImpl = nullptr;
        tools.erase(tools.begin() + index);

        if (activeIndex == index)
            activeIndex = -1;
        else if (activeIndex > index)
            --activeIndex;

        if (liveIndex == index) {
            liveDetect = false;
            liveIndex = -1;
        }
        else if (liveIndex > index) {
            --liveIndex;
        }
        return true;
    }

    extern int g_ActiveToolIndex;
    extern std::vector<ToolInstance> g_ToolInstances;
}
