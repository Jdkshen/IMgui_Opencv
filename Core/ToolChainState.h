#pragma once

#include <vector>

#include "ToolInstance.h"

// =====================================================
// ToolChainState — 工具链全局状态管理
// 管理工具实例列表、当前激活工具、YOLO 实时检测状态
// =====================================================
namespace ToolChainState
{
    // ---- 工具列表 ----
    std::vector<ToolInstance>& Tools();              // 可写引用
    const std::vector<ToolInstance>& ReadOnlyTools(); // 只读引用

    // ---- 当前激活工具 ----
    int& ActiveIndexRef();           // 可写引用
    int ActiveIndex();               // 只读
    void SetActiveIndex(int index);  // 设置激活索引

    // ---- YOLO 实时检测状态 ----
    bool& YoloLiveDetectRef();              // 实时检测开关（可写引用）
    bool YoloLiveDetect();                  // 只读
    void SetYoloLiveDetect(bool enabled);   // 设置开关
    int& YoloLiveInstanceIndexRef();        // 实时检测使用的工具实例索引
    int YoloLiveInstanceIndex();
    void SetYoloLiveInstanceIndex(int index);
    float& YoloLastTimeMsRef();             // 最近一次推理耗时（ms）
    float YoloLastTimeMs();
    void SetYoloLastTimeMs(float ms);
    float& YoloLiveFrameMsRef();            // 实时帧耗时（ms）
    float YoloLiveFrameMs();
    void SetYoloLiveFrameMs(float ms);

    // ---- 工具链操作 ----
    void MoveOriginalToolToFront();  // 将"原图"工具移到工具链最前面
}
