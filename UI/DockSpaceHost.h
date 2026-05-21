#pragma once
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <vector>
#include "../Core/DX12Context.h"

// =====================================================
// ROI 数据结构（存储归一化图像坐标）
// =====================================================
struct ROI
{
    ImVec2 start; // ROI起始点（图像坐标）
    ImVec2 end;   // ROI结束点（图像坐标）
};

// =====================================================
// 控制点类型（8方向，类似VisionPro）
// =====================================================
enum HandleType
{
    HANDLE_NONE = 0,
    HANDLE_LT, HANDLE_RT, HANDLE_LB, HANDLE_RB,
    HANDLE_T,  HANDLE_B,  HANDLE_L,  HANDLE_R,
    HANDLE_CENTER   // 中心点（拖动移动整个ROI）
};

// =====================================================
// UI参数常量
// =====================================================
constexpr float HANDLE_SIZE = 6.0f;       // 控制点大小
constexpr float gMinROIWidth  = 5.0f;     // ROI最小宽度
constexpr float gMinROIHeight = 5.0f;     // ROI最小高度

// =====================================================
// 全局状态变量（extern 声明，定义在 DockSpaceHost.cpp）
// =====================================================
extern bool   show_demo_window;
extern bool   g_ShowLog;
extern bool   g_ShowSidebar;
extern bool   g_ShowStats;
extern bool   g_ShowOpenCV;
extern bool   g_ShowTools;
extern ImVec4 color;                     // 默认日志颜色（蓝色）

// 图像显示状态分散在 ImageViewer.h / ROIManager.h 中声明
// ROI 数据与交互状态在 ROIManager.h (namespace UI) 中声明

// =====================================================
// ROIBox 结构（预计算8个控制点位置）
// =====================================================
struct ROIBox
{
    ImVec2 lt, rt, lb, rb;  // 四角
    ImVec2 t, b, l, r;      // 四边中点
};

// =====================================================
// UI 命名空间 - 窗口函数声明
// =====================================================
namespace UI
{
    void DrawDockSpaceHost();
    void ShowLogWindow();
    void ShowSidebar();
    void ShowStatsWindow();
    void ShowToolsWindow();
}