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
extern bool g_PendingMatch;    // 待执行匹配标志

// =====================================================
// 模板匹配参数（extern，供配方系统读写）
// =====================================================
extern int    g_TMSearchMode;
extern int    g_TMMaxResults;
extern int    g_TMMaxImageDim;
extern float  g_TMMatchThreshold;
extern bool   g_TMEnableRotation;
extern int    g_TMRotationStart;
extern int    g_TMRotationEnd;
extern int    g_TMRotationStep;

// 模板预处理参数（extern，供配方系统和其他窗口读写）
extern bool   g_TplGray;
extern bool   g_TplBinary;
extern int    g_TplBinThresh;
extern bool   g_TplEdge;
extern int    g_TplEdgeLow;
extern int    g_TplEdgeHigh;

// NMS 阈值 + 性能统计
extern float  g_NmsThreshold;
extern float  g_TMLastMatchTime;
extern double g_TMLastBestScore;

// 模板预览显示状态
extern bool   g_ShowPreview;

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

    // 保存/加载模板图像到文件
    bool SaveTemplate(const char* filepath);
    bool LoadTemplate(const char* filepath);
}
