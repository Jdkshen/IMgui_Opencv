#pragma once

#include <vector>
#include "../UI/DockSpaceHost.h"   // ROI 结构体

struct ImDrawList;                  // 前向声明（imgui.h）

// =====================================================
// 模板匹配结果（蓝色矩形叠加显示）
// =====================================================
extern std::vector<ROI> gMatchROIs;

// =====================================================
// 调试窗口显示标志
// =====================================================
extern bool g_ShowTemplateMatch;

// =====================================================
// 模板匹配命名空间
// =====================================================
namespace TemplateMatch
{
    // 执行模板匹配（使用当前参数）
    void Run();

    // 显示调试窗口（模板预览 + 参数调节 + 执行）
    void ShowWindow();

    // 模板编辑弹窗（点击小图放大）
    void ShowTemplateEditor();

    // 在 ImDrawList 上绘制匹配结果（蓝色矩形 + 编号）
    void DrawMatches(ImDrawList* drawList);

    // 清空匹配结果
    void Clear();
}
