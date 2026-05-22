#pragma once
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <vector>
#include <opencv2/core/mat.hpp>
#include "../Core/DX12Context.h"

// =====================================================
// ROI 类型枚举
// =====================================================
enum ROIType : int
{
    ROI_TYPE_GENERAL       = 0,  // 通用ROI（默认）
    ROI_TYPE_TEMPLATE      = 1,  // 模板匹配ROI
    ROI_TYPE_RECOGNITION   = 2,  // 图识别ROI
    ROI_TYPE_RESERVED3     = 3,  // 预留
    ROI_TYPE_RESERVED4     = 4,  // 预留
    ROI_TYPE_COUNT         = 5
};

// =====================================================
// ROI 数据结构（存储归一化图像坐标）
// =====================================================
struct ROI
{
    ImVec2 start; // ROI起始点（图像坐标）
    ImVec2 end;   // ROI结束点（图像坐标）
    int    type = ROI_TYPE_GENERAL; // ROI类型
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
// =====================================================
// 工具实例（可包含模板匹配的独立模板）
// =====================================================
struct ToolInstance
{
    int type = 0;                      // 工具类型: 0=边缘检测 1=模板匹配 2=Blob分析 3=阈值调试
    cv::Mat templateImg;               // 该实例的模板图像数据
    std::vector<ROI> searchROIs;       // 该实例专属搜索区域

    // ---- 旋转/角度参数 ----
    bool enableRotation = false;
    int  rotationStart  = -45;
    int  rotationEnd    = 45;
    int  rotationStep   = 1;

    // ---- 匹配参数 ----
    int   maxResults     = 5;
    float matchThreshold = 0.7f;
    int   maxImageDim    = 1000;
    float nmsThreshold   = 0.3f;
    int   searchMode     = 0;   // 0=全图, 1=ROI内

    // ---- 模板预处理 ----
    bool tplGray      = false;
    bool tplBinary    = false;
    int  tplBinThresh = 128;
    bool tplEdge      = false;
    int  tplEdgeLow   = 50;
    int  tplEdgeHigh  = 150;

    // ---- 图像预处理（模板匹配用） ----
    bool imgUseGray        = false;
    bool imgEnableThreshold = false;
    int  imgThreshold      = 128;

    // ---- 边缘检测参数（type==0） ----
    int  cannyLow  = 50;
    int  cannyHigh = 150;
    bool edgeUseGray = false;

    // ---- 阈值调试参数（type==3） ----
    bool dbgUseGray     = false;
    bool dbgEnableBlur  = false;
    int  dbgBlurSize    = 5;
    bool dbgEnableThresh = false;
    int  dbgThreshold   = 128;
    bool dbgEnableCanny = false;
    int  dbgCannyLow    = 50;
    int  dbgCannyHigh   = 150;
};

namespace UI
{
    // 绘制主停靠空间（菜单栏 + DockSpace 容器）
    void DrawDockSpaceHost();
    // 显示日志窗口（时间戳 + 颜色分级 + 清空按钮）
    void ShowLogWindow();
    // 显示侧边栏（图片加载/ROI操作/配方管理）
    void ShowSidebar();
    // 显示性能统计窗口（FPS/帧耗时/DrawCall）
    void ShowStatsWindow();
    // 显示功能窗口（手风琴工具列表 + 全部/单步执行）
    void ShowToolsWindow();

    // 功能窗口当前展开的工具索引（-1 = 全部折叠）
    extern int g_ActiveToolIndex;

    // 功能窗口中已添加的工具实例列表
    extern std::vector<ToolInstance> g_ToolInstances;
}