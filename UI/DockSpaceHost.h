#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <opencv2/core/mat.hpp>
#include "imgui/imgui.h"
#include "../Core/DX12Context.h"
#include "../Core/ROI.h"

// =====================================================
// 全局状态变量（extern 声明，定义在 DockSpaceHost.cpp）
// =====================================================
extern bool show_demo_window;
extern bool g_ShowLog;
extern bool g_ShowSidebar;
extern bool g_ShowStats;
extern bool g_ShowOpenCV;
extern bool g_ShowTools;
extern bool g_ShowHardware;

// 图像显示状态分散在 ImageViewer.h / ROIManager.h 中声明
// ROI 数据与交互状态在 ROIManager.h (namespace UI) 中声明

// =====================================================
// UI 命名空间 - 窗口函数声明
// =====================================================
// =====================================================
// UI 命名空间 - 窗口函数声明
// =====================================================
namespace UI
{
    // 绘制主停靠空间（菜单栏 + DockSpace 容器）
    void DrawDockSpaceHost();
    bool SaveCurrentRecipe();
    void MarkCurrentRecipeDirty();
    void UpdateCurrentRecipeAutoSave();
    bool IsCurrentRecipeDirty();
    const char* CurrentRecipeName();
    std::string CurrentRecipePath();
}
