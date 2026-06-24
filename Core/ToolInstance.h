#pragma once

#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "ROI.h"
#include "../Algorithm/ITool.h"

// =====================================================
// 工具实例（包含工具参数与可选 ITool 实现）
// =====================================================
struct ToolInstance
{
    int type = 0;                // 0=边缘检测 1=模板匹配 2=Blob分析 3=阈值调试 4=YOLO 5=轮廓 6=形状 7=直线 12=原图
    std::string label;           // 用户标签，空时显示原工具名
    int inputSourceMode = 2;     // 0=上一步原图, 1=上一步处理图, 2=原图工具输出
    cv::Mat templateImg;         // 该实例的模板图像数据
    bool hasTemplateROI = false; // 是否保存了模板ROI
    ROI templateROI;             // 该实例专属模板ROI
    bool useSearchROI = false;   // 是否使用本工具绑定的查找区域
    std::vector<ROI> searchROIs; // 该实例专属搜索区域

    // ---- 旋转/角度参数 ----
    bool enableRotation = false;
    int rotationStart = -45;
    int rotationEnd = 45;
    int rotationStep = 1;

    // ---- 匹配参数 ----
    int maxResults = 5;
    float matchThreshold = 0.7f;
    int maxImageDim = 1000;
    float nmsThreshold = 0.3f;
    int searchMode = 0; // 0=全图, 1=ROI内

    // ---- 模板预处理 ----
    bool tplGray = false;
    bool tplBinary = false;
    int tplBinThresh = 128;
    bool tplEdge = false;
    int tplEdgeLow = 50;
    int tplEdgeHigh = 150;

    // ---- 图像预处理（模板匹配用） ----
    bool imgUseGray = false;
    bool imgEnableThreshold = false;
    int imgThreshold = 128;

    // ---- 边缘检测参数（type==0） ----
    int cannyLow = 50;
    int cannyHigh = 150;
    bool edgeUseGray = false;

    // ---- 阈值调试参数（type==3） ----
    bool dbgUseGray = false;
    bool dbgEnableBlur = false;
    int dbgBlurSize = 5;
    bool dbgEnableThresh = false;
    int dbgThreshold = 128;
    bool dbgEnableCanny = false;
    int dbgCannyLow = 50;
    int dbgCannyHigh = 150;

    // ---- Blob分析参数（type==2） ----
    int blobMinArea = 100;
    int blobMaxArea = 10000;

    // ---- YOLO检测参数（type==4） ----
    std::string yoloModelPath;      // ONNX 模型路径
    std::string yoloClassesPath;    // 类别文件路径
    float yoloConfThreshold = 0.5f; // 置信度阈值
    float yoloNmsThreshold = 0.4f;  // NMS 阈值
    bool yoloUseROI = false;
    bool yoloUseGPU = false;        // 使用 DirectML GPU 加速

    // ---- 轮廓分析（type==5） ----
    bool cntUseGray = true;
    int cntBlurSize = 5, cntThreshMode = 0, cntThreshValue = 128, cntAdaptBlock = 11;
    bool cntInvert = false;
    int cntRetrMode = 0, cntApproxMethod = 1;
    float cntMinArea = 100;
    int cntMaxContours = 500;
    bool cntFilterConvex = false;
    float cntApproxEps = 0.02f;
    int cntLineThick = 2;
    bool cntShowLabels = true, cntFillContours = false;
    bool cntMatchROI = false;
    float cntMatchThresh = 0.1f;

    // ---- 形状匹配（type==6） ----
    cv::Mat shpTplImage;
    int shpBlurSize = 5, shpTplRetr = 0;
    float shpTplMinArea = 30, shpMinScore = 0.5f, shpShapeScore = 0.1f;
    int shpLineThick = 2, shpMethod = 0;
    bool shpShowLabels = true;
    int shpMaxResults = 1;
    bool shpTplGray = false, shpTplBinary = false;
    int shpTplBinThresh = 128;
    bool shpTplBlur = false;
    int shpTplBlurK = 5;
    bool shpTplInvert = false;

    // ---- 直线检测（type==7） ----
    int lineCannyLow = 50, lineCannyHigh = 150;
    float lineMinLength = 100, lineMaxGap = 20, lineMinAngle = 0, lineMaxAngle = 180;
    int lineThickness = 2, lineMaxLines = 1;
    bool lineShowLabels = true, lineUseROI = false;
    std::vector<ROI> lineSaveROIs;

    // ---- 形态学工具（type==8） ----
    int morphOpType = 0, morphKernelSize = 3, morphKernelShape = 0, morphIterations = 1;
    bool morphUseGray = false;

    // ---- 颜色分析（type==9） ----
    int colorSpace = 0, colorHistBins = 32;
    bool colorShowHist = true, colorUseROI = false;
    int colorHistHeight = 100;

    // ---- 多点找色（type==10） ----
    cv::Mat mcfRefImage;            // 参考图
    int mcfAnchorX = 0, mcfAnchorY = 0;
    bool mcfImgGray = false, mcfImgBinary = false;
    int  mcfImgBinThresh = 128;
    bool mcfUseROI = false;
    int mcfMaxResults = 1;
    float mcfMinDist = 5.0f;
    int mcfCrossSize = 10;
    int mcfCrossThick = 2;
    int mcfRoiX = 0, mcfRoiY = 0, mcfRoiW = 0, mcfRoiH = 0; // 搜索ROI位置（配方保存）

    // ---- 新架构：ITool 接口指针（为空时回退旧逻辑） ----
    ITool* toolImpl = nullptr;
};

inline std::string ToolInstanceTitle(const char* baseName, const std::string& label)
{
    const std::string base = baseName ? std::string(baseName) : std::string();
    if (label.empty() || label == base)
        return base;
    return base + " · " + label;
}

inline std::string ToolInstanceLogName(const char* baseName, const std::string& label)
{
    const std::string base = baseName ? std::string(baseName) : std::string();
    if (label.empty() || label == base)
        return base;
    return base + "[" + label + "]";
}
