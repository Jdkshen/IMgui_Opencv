#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>

#include <opencv2/core/mat.hpp>
#include <nlohmann/json.hpp>

#include "ROI.h"
#include "GeometryPrimitive.h"
#include "BarcodeTypes.h"
#include "FixtureTransform.h"
#include "CalibrationModel.h"
#include "CalibrationFitter.h"
#include "ToolJudgement.h"
#include "ToolSettings.h"
#include "../Algorithm/ITool.h"

// =====================================================
// 工具实例（包含工具参数与可选 ITool 实现）
// =====================================================
struct ToolInstance
{
    ToolInstance() = default;
    ToolInstance(const ToolInstance& other);
    ToolInstance& operator=(const ToolInstance& other);
    ToolInstance(ToolInstance&& other) noexcept = default;
    ToolInstance& operator=(ToolInstance&& other) noexcept = default;
    ~ToolInstance() = default;

    int type = 0;                // 0-17 工具类型，12=原图
    std::uint64_t toolId = 0;    // 稳定实例身份，0 表示尚未分配
    bool enabled = true;         // 工具链执行开关，禁用时保留参数和结果位置
    std::string label;           // 用户标签，空时显示原工具名
    bool showResultLabels = true;
    bool skipIfModelMissing = false; // OCR/YOLO 模型缺失时跳过而不是阻止整条链
    std::string groupName;       // 工具链分组名称，空表示默认组
    bool collapsed = false;      // 工具卡片是否折叠
    ToolJudgementSettings judgement;
    int resultRoiMode = 0;        // 0=disabled, 1=Nth result, 2=all results
    int resultRoiSourceTool = -1;
    std::uint64_t resultRoiSourceToolId = 0;
    int resultRoiIndex = 0;
    int resultRoiMissingPolicy = 0; // 0=skip, 1=fail
    std::string resultRoiCategory;
    int resultRoiClassId = -1;
    float resultRoiMinScore = -1.0f;
    float resultRoiMinArea = -1.0f;
    int resultRoiSortMode = 0;
    bool resultRoiSortDescending = true;
    FixtureSettings fixture;
    int inputSourceMode = 2;     // 0=上一步原图, 1=上一步处理图, 2=原图工具输出
    cv::Mat templateImg;         // 该实例的模板图像数据
    bool showTemplatePreview = true;
    bool hasTemplateROI = false; // 是否保存了模板ROI
    ROI templateROI;             // 该实例专属模板ROI
    bool useSearchROI = false;   // 是否使用本工具绑定的查找区域
    std::vector<ROI> searchROIs; // 该实例专属搜索区域

    // ---- 旋转/角度参数 ----
    TemplateMatchSettings templateMatch;
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

    // ---- 已拆分的工具参数 ----
    ThresholdSettings threshold;
    BlobSettings blob;

    // ---- 图像差分参数（type==16） ----
    cv::Mat differenceReferenceImage;
    int differenceThreshold = 30;
    int differenceMinArea = 20;
    int differenceBlurSize = 0;
    int differenceMorphKernelSize = 3;
    int differenceMorphIterations = 1;
    bool differenceInvert = false;

    // ---- YOLO检测参数（type==4） ----
    YoloSettings yolo;
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
    bool cntFillContours = false;
    bool cntMatchROI = false;
    float cntMatchThresh = 0.1f;

    // ---- 形状匹配（type==6） ----
    cv::Mat shpTplImage;
    int shpBlurSize = 5, shpTplRetr = 0;
    float shpTplMinArea = 30, shpMinScore = 0.5f, shpShapeScore = 0.1f;
    int shpLineThick = 2, shpMethod = 0;
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
    bool lineUseROI = false;
    std::vector<ROI> lineSaveROIs;

    MorphologySettings morphology;
    ColorAnalysisSettings colorAnalysis;

    // ---- 多点找色（type==10） ----
    cv::Mat mcfRefImage;            // 参考图
    bool mcfShowPreview = true;
    int mcfAnchorX = 0, mcfAnchorY = 0;
    bool mcfImgGray = false, mcfImgBinary = false;
    int  mcfImgBinThresh = 128;
    bool mcfUseROI = false;
    int mcfMaxResults = 1;
    float mcfMinDist = 5.0f;
    int mcfCrossSize = 10;
    int mcfCrossThick = 2;
    int mcfRoiX = 0, mcfRoiY = 0, mcfRoiW = 0, mcfRoiH = 0; // 搜索ROI位置（配方保存）

    // ---- OCR文字识别（type==13） ----
    OCRSettings ocr;
    std::string ocrDetModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
    std::string ocrDetParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
    std::string ocrRecModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
    std::string ocrRecParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
    std::string ocrDictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
    float ocrMinConfidence = 0.30f;
    int ocrMaxItems = 8;
    int ocrInputSize = 512;
    int ocrMaxCandidates = 220;
    int ocrMinBoxArea = 0;
    int ocrMinBoxHeight = 0;
    int ocrRoiPadding = 24;
    bool ocrFastMode = true;
    bool ocrDetectOnly = false;
    bool ocrUseROI = true;

    // ---- 二维码识别（type==14） ----
    bool qrUseROI = true;
    bool qrDetectMulti = true;
    bool qrEnhance = true;
    int qrMinSize = 24;
    int qrEngine = 0; // 0=自动, 1=OpenCV, 2=ZXing-cpp
    std::uint32_t qrFormatMask = BarcodeFormatAll;
    bool qrFilterDuplicates = true;

    // ---- 工业测量（type==15） ----
    int measureMode = 0;
    std::vector<std::uint64_t> measureRuntimeROIIds; // 运行时同步 UI 绘制 ROI，不保存配方
    int measureCaliperCount = 16;
    float measureSearchLength = 30.0f;
    float measureProjectionWidth = 5.0f;
    float measureSmoothingSigma = 1.0f;
    float measureEdgeThreshold = 12.0f;
    float measureMinPairDistance = 3.0f;
    int measureEdgePolarity = 0;
    bool measureSubpixel = true;
    int measureFitMethod = 1;
    float measureFitInlierThreshold = 1.5f;
    int measureMinimumValidCalipers = 3;
    float measureMinimumConfidence = 0.0f;
    float measureMmPerPixel = 0.0f;
    float measureCalibrationPixels = 100.0f;
    float measureCalibrationMm = 10.0f;
    bool measureToleranceEnabled = false;
    float measureNominal = 0.0f;
    float measureToleranceMinus = 0.0f;
    float measureTolerancePlus = 0.0f;
    CalibrationModel measureCalibration;
    std::vector<CalibrationSample> measureCalibrationSamples;
    double measureCalibrationRmsError = 0.0;
    double measureCalibrationMaxError = 0.0;
    std::string measureCalibrationFitMessage;

    // ---- 几何绘制（type==17） ----
    int geometryDrawType = static_cast<int>(GeometryPrimitiveType::Line);
    std::vector<GeometryPrimitive> geometryItems;

    // ---- 新架构：ITool 接口指针（为空时回退旧逻辑） ----
    std::unique_ptr<ITool> toolImpl;
    ToolResult lastResult;       // 运行时缓存，不保存到配方
    bool hasLastResult = false;
    bool parametersDirty = false; // UI 参数已变更，上次执行结果可能已过期
    std::uint64_t parameterRevision = 1; // 运行时参数版本，用于拒绝后台旧结果

    nlohmann::json ToRecipeJson() const;
    void LoadRecipeJson(const nlohmann::json& json);
    void SyncSettingsFromLegacy();
    void SyncLegacyFromSettings();
    void MarkParametersChanged();
    void ClearRuntimeState();
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
