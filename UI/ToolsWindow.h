#pragma once
#include <vector>
#include <opencv2/core/mat.hpp>
#include "DockSpaceHost.h"   // ROI 结构体

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
    bool imgUseGray         = false;
    bool imgEnableThreshold = false;
    int  imgThreshold       = 128;

    // ---- 边缘检测参数（type==0） ----
    int  cannyLow   = 50;
    int  cannyHigh  = 150;
    bool edgeUseGray = false;

    // ---- 阈值调试参数（type==3） ----
    bool dbgUseGray      = false;
    bool dbgEnableBlur   = false;
    int  dbgBlurSize     = 5;
    bool dbgEnableThresh = false;
    int  dbgThreshold    = 128;
    bool dbgEnableCanny  = false;
    int  dbgCannyLow     = 50;
    int  dbgCannyHigh    = 150;

    // ---- YOLO检测参数（type==4） ----
    std::string yoloModelPath;       // ONNX 模型路径
    std::string yoloClassesPath;     // 类别文件路径
    float yoloConfThreshold = 0.5f;  // 置信度阈值
    float yoloNmsThreshold  = 0.4f;  // NMS 阈值
    bool  yoloUseROI        = false; // 是否限定 ROI 区域
};

// =====================================================
// 功能窗口 — 手风琴工具列表 + 全部/单步/循环执行
// =====================================================
namespace UI
{
    extern bool g_YoloLiveDetect;    // YOLO 实时检测开关
    void ShowToolsWindow();

    extern int g_ActiveToolIndex;
    extern std::vector<ToolInstance> g_ToolInstances;
}
