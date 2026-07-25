#include "ToolExecutor.h"

#include "ResultPublisher.h"
#include "ResultROIResolver.h"
#include "FixtureTransform.h"
#include "../Algorithm/BlobTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/DifferenceTool.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/EdgeTool.h"
#include "../Algorithm/GeometryDrawTool.h"
#include "../Algorithm/LineDetector.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/MeasurementTool.h"
#include "../Algorithm/TemplateMatchingTool.h"
#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/OCRTool.h"
#include "../Algorithm/QRCodeTool.h"
#include "../Algorithm/OpenCVYoloDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/ShapeTools.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/YOLOTool.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "ROIState.h"
#include "RotatedROI.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "ToolJudgement.h"
#include "InspectionHistory.h"
#include "../Log/LogSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace
{
bool IsMissingModelMessage(const std::string& message)
{
    return message.find("模型") != std::string::npos ||
        message.find("model") != std::string::npos ||
        message.find("OCR") != std::string::npos;
}

void ApplyMissingModelSkipPolicy(const ToolInstance& tool, ToolResult& result)
{
    if (!tool.skipIfModelMissing || result.success)
        return;
    if ((tool.type == 4 || tool.type == 11 || tool.type == 13) &&
        IsMissingModelMessage(result.message))
    {
        result.success = true;
        result.skipped = true;
        result.status = ToolResultStatus::Pass;
        result.statusReason.clear();
        result.message = "已跳过：" + (result.message.empty() ? std::string("模型缺失") : result.message);
    }
}

struct ToolRunTimings
{
    float prepareMs = 0.0f;
    float executeMs = 0.0f;
    float publishMs = 0.0f;
};

bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst);

double MeasurementValue(const ToolResult& result, const char* name, double fallback = 0.0)
{
    for (const auto& measurement : result.measurements)
    {
        if (measurement.name == name)
            return measurement.value;
    }
    return fallback;
}

std::vector<ROI> SelectSearchROIs(const ToolInstance& it)
{
    if (it.searchROIs.empty())
        return ROIState::ReadOnlyItems();

    std::vector<ROI> resolved = it.searchROIs;
    const auto& visibleROIs = ROIState::ReadOnlyItems();
    for (ROI& configured : resolved)
    {
        if (configured.runtimeId == 0)
            continue;
        const auto current = std::find_if(visibleROIs.begin(), visibleROIs.end(), [&](const ROI& visible)
        {
            return visible.runtimeId == configured.runtimeId;
        });
        if (current != visibleROIs.end())
            configured = *current;
    }
    return resolved;
}

bool ToolCanUseSharedInput(const ToolInstance& it)
{
    switch (it.type)
    {
    case 1:  // Template match
    case 2:  // Blob
    case 4:  // YOLO
    case 5:  // Contour
    case 6:  // Shape match
    case 7:  // Line
    case 9:  // Color analyzer
    case 11: // OpenCV YOLO
    case 13: // OCR
    case 14: // QR code
    case 15: // Measurement
    case 16: // Difference
    case 17: // Geometry drawing
        return true;
    case 10: // Multi-color finder preprocesses in-place only when gray/binary is enabled.
        return !it.mcfImgGray && !it.mcfImgBinary;
    default:
        return false;
    }
}

int SelectROIIndex(const std::vector<ROI>& rois)
{
    if (rois.empty())
        return -1;
    return ROIState::SelectIndexFor(rois);
}

cv::Rect PrimaryContextRect(const VisionContext& ctx, bool enabled = true)
{
    if (!enabled || ctx.image.empty())
        return {};

    cv::Rect r;
    if (ctx.HasROI())
        r = ctx.GetActiveROIRect();
    else if (!ctx.rois.empty())
        r = ctx.rois[0].ToCvRect();

    r &= cv::Rect(0, 0, ctx.image.cols, ctx.image.rows);
    return (r.width > 0 && r.height > 0) ? r : cv::Rect();
}

cv::Mat ApplyPipelineToMat(const cv::Mat& src, bool useGray, const PipelineState& pipe)
{
    if (src.empty())
        return {};

    cv::Mat result;
    if (useGray)
    {
        if (src.channels() == 4)
            cv::cvtColor(src, result, cv::COLOR_BGRA2GRAY);
        else if (src.channels() == 3)
            cv::cvtColor(src, result, cv::COLOR_BGR2GRAY);
        else
            result = src.clone();
    }
    else
    {
        result = src.clone();
    }

    if (pipe.enableBlur)
    {
        int k = pipe.blurSize * 2 + 1;
        if (k < 3) k = 3;
        cv::GaussianBlur(result, result, cv::Size(k, k), 0);
    }
    if (pipe.enableCanny)
    {
        cv::Mat gray;
        if (result.channels() == 4)
            cv::cvtColor(result, gray, cv::COLOR_BGRA2GRAY);
        else if (result.channels() == 3)
            cv::cvtColor(result, gray, cv::COLOR_BGR2GRAY);
        else
            gray = result;
        cv::Canny(gray, result, pipe.cannyLow, pipe.cannyHigh);
    }
    else if (pipe.enableThreshold)
    {
        cv::Mat gray;
        if (result.channels() == 4)
            cv::cvtColor(result, gray, cv::COLOR_BGRA2GRAY);
        else if (result.channels() == 3)
            cv::cvtColor(result, gray, cv::COLOR_BGR2GRAY);
        else
            gray = result;
        cv::threshold(gray, result, pipe.threshold, 255, cv::THRESH_BINARY);
    }

    return result;
}

cv::Mat ApplyPipelineToContextImage(const VisionContext& ctx, bool useGray, const PipelineState& pipe)
{
    if (ctx.image.empty())
        return {};

    cv::Mat out = ctx.image.clone();
    cv::Rect roi = PrimaryContextRect(ctx);
    cv::Mat dst = ApplyPipelineToMat(roi.empty() ? out : out(roi), useGray, pipe);
    if (dst.empty())
        return {};

    if (!roi.empty())
    {
        cv::Mat converted;
        if (!ConvertForCopyTo(dst, out.channels(), converted))
            return {};
        converted.copyTo(out(roi));
    }
    else
    {
        out = dst;
    }

    return out;
}

std::string PrefixDisplayLabel(const std::string& toolLabel, const std::string& itemLabel)
{
    if (toolLabel.empty())
        return itemLabel;
    if (itemLabel.empty())
        return toolLabel;
    if (itemLabel.rfind(toolLabel, 0) == 0)
        return itemLabel;
    return toolLabel + " " + itemLabel;
}

void ApplyToolLabelToOverlayItems(ToolResult& result, const std::string& toolLabel)
{
    if (toolLabel.empty())
        return;

    for (auto& region : result.regions)
        region.label = PrefixDisplayLabel(toolLabel, region.label);

    for (auto& detection : result.detections)
        detection.label = PrefixDisplayLabel(toolLabel, detection.label);

    for (auto& text : result.texts)
        text.text = PrefixDisplayLabel(toolLabel, text.text);
}

int FindToolInstanceIndex(const ToolInstance& it)
{
    const auto& tools = ToolChainState::ReadOnlyTools();
    for (int i = 0; i < static_cast<int>(tools.size()); ++i)
    {
        if (&tools[i] == &it)
            return i;
    }
    return -1;
}

int ResolveToolSourceIndex(const std::vector<ToolInstance>& tools, int legacyIndex, std::uint64_t stableId)
{
    if (stableId != 0)
    {
        for (int i = 0; i < static_cast<int>(tools.size()); ++i)
        {
            if (tools[i].toolId == stableId)
                return i;
        }
    }
    return legacyIndex >= 0 && legacyIndex < static_cast<int>(tools.size()) ? legacyIndex : -1;
}

bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst)
{
    if (src.empty())
        return false;
    if (src.channels() == targetChannels)
    {
        dst = src;
        return true;
    }
    if (src.channels() == 1 && targetChannels == 3)
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
    else if (src.channels() == 1 && targetChannels == 4)
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGRA);
    else if (src.channels() == 3 && targetChannels == 1)
        cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);
    else if (src.channels() == 3 && targetChannels == 4)
        cv::cvtColor(src, dst, cv::COLOR_BGR2BGRA);
    else if (src.channels() == 4 && targetChannels == 1)
        cv::cvtColor(src, dst, cv::COLOR_BGRA2GRAY);
    else if (src.channels() == 4 && targetChannels == 3)
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
    else
        return false;
    return true;
}

class OpenCVYoloITool final : public ITool
{
public:
    std::string modelPath;
    std::string classesPath;
    float confThreshold = 0.5f;
    float nmsThreshold = 0.4f;

    const char* GetName() const override { return "YOLO OpenCV 5.0"; }
    int GetType() const override { return 11; }
    void DrawUI() override {}
    nlohmann::json Save() const override
    {
        return {{"type", 11}, {"modelPath", modelPath}, {"classesPath", classesPath},
            {"confThreshold", confThreshold}, {"nmsThreshold", nmsThreshold}};
    }
    void Load(const nlohmann::json& j) override
    {
        modelPath = j.value("modelPath", "");
        classesPath = j.value("classesPath", "");
        confThreshold = j.value("confThreshold", 0.5f);
        nmsThreshold = j.value("nmsThreshold", 0.4f);
    }
    ToolResult Execute(VisionContext& ctx) override
    {
        ToolResult result;
        result.toolName = GetName();
        if (ctx.IsCancellationRequested()) {
            result.success = false;
            result.message = "执行已取消";
            return result;
        }
        if (modelPath.empty()) {
            result.success = false;
            result.message = "请先选择 ONNX 模型";
            return result;
        }
        if (ctx.image.empty()) {
            result.success = false;
            result.message = "请先加载图片";
            return result;
        }
        if (!OpenCVYoloDetector::LoadModel(modelPath, classesPath)) {
            result.success = false;
            result.message = "模型加载失败";
            return result;
        }

        if (ctx.IsCancellationRequested()) {
            result.success = false;
            result.message = "执行已取消";
            return result;
        }

        cv::Rect roi = PrimaryContextRect(ctx);
        OpenCVYoloDetector::Timing backendTiming;
        auto objs = OpenCVYoloDetector::Detect(ctx.image, confThreshold, nmsThreshold,
            roi, &backendTiming);
        result.timing.backendPreprocessMs = backendTiming.preprocessMs;
        result.timing.backendInferenceMs = backendTiming.inferenceMs;
        result.timing.backendPostprocessMs = backendTiming.postprocessMs;
        if (ctx.IsCancellationRequested()) {
            result.success = false;
            result.message = "执行已取消";
            return result;
        }
        result.success = true;
        for (const auto& o : objs) {
            ToolResult::Detection d;
            d.box = o.box;
            d.label = o.className;
            d.score = o.confidence;
            d.classId = o.classId;
            result.detections.push_back(d);
        }
        return result;
    }
};

struct LegacyIToolRegister
{
    LegacyIToolRegister()
    {
        ToolRegistry::Register(11, []() -> std::unique_ptr<ITool> { return std::make_unique<OpenCVYoloITool>(); });
        ToolRegistry::RegisterName(11, "OpenCVYolo");
    }
};

static LegacyIToolRegister s_LegacyIToolRegister;

void SyncIToolParams(ToolInstance& it, const VisionContext& context)
{
    it.SyncSettingsFromLegacy();
    const int t = it.type;
    if (t == 0) {
        if (auto* et = dynamic_cast<EdgeTool*>(it.toolImpl.get())) {
            et->cannyLow = it.cannyLow;
            et->cannyHigh = it.cannyHigh;
            et->useGray = it.edgeUseGray;
        }
    } else if (t == 1) {
        if (auto* tt = dynamic_cast<TemplateMatchingTool*>(it.toolImpl.get())) {
            tt->enableRotation = it.templateMatch.enableRotation;
            tt->rotationStart = it.templateMatch.rotationStart;
            tt->rotationEnd = it.templateMatch.rotationEnd;
            tt->rotationStep = it.templateMatch.rotationStep;
            tt->maxResults = it.templateMatch.maxResults;
            tt->matchThreshold = it.templateMatch.matchThreshold;
            tt->maxImageDim = it.templateMatch.maxImageDim;
            tt->nmsThreshold = it.templateMatch.nmsThreshold;
            tt->tplGray = it.templateMatch.templateGray;
            tt->tplBinary = it.templateMatch.templateBinary;
            tt->tplBinThresh = it.templateMatch.templateBinaryThreshold;
            tt->tplEdge = it.templateMatch.templateEdge;
            tt->tplEdgeLow = it.templateMatch.templateEdgeLow;
            tt->tplEdgeHigh = it.templateMatch.templateEdgeHigh;
            tt->imgUseGray = it.templateMatch.imageGray;
            tt->imgEnableThreshold = it.templateMatch.imageThresholdEnabled;
            tt->imgThreshold = it.templateMatch.imageThreshold;
            tt->templateImg = it.templateImg;
            tt->useSearchROI = !context.rois.empty();
            tt->searchROIs = context.rois;
        }
    } else if (t == 2) {
        if (auto* bt = dynamic_cast<BlobTool*>(it.toolImpl.get())) {
            bt->minArea = it.blob.minArea;
            bt->maxArea = it.blob.maxArea;
            bt->thresholdMode = it.blob.thresholdMode;
            bt->threshold = it.blob.threshold;
            bt->invert = it.blob.invert;
            bt->connectivity = it.blob.connectivity;
            bt->minCircularity = it.blob.minCircularity;
            bt->maxCircularity = it.blob.maxCircularity;
            bt->minAspectRatio = it.blob.minAspectRatio;
            bt->maxAspectRatio = it.blob.maxAspectRatio;
            bt->showLabels = it.showResultLabels;
        }
    } else if (t == 16) {
        if (auto* dt = dynamic_cast<DifferenceTool*>(it.toolImpl.get())) {
            dt->referenceImage = it.differenceReferenceImage;
            dt->threshold = it.differenceThreshold;
            dt->minArea = it.differenceMinArea;
            dt->blurSize = it.differenceBlurSize;
            dt->morphKernelSize = it.differenceMorphKernelSize;
            dt->morphIterations = it.differenceMorphIterations;
            dt->invert = it.differenceInvert;
            dt->showLabels = it.showResultLabels;
        }
    } else if (t == 3) {
        if (auto* tt = dynamic_cast<ThresholdITool*>(it.toolImpl.get())) {
            tt->useGray = it.threshold.useGray;
            tt->enableBlur = it.threshold.enableBlur;
            tt->blurSize = it.threshold.blurSize;
            tt->enableThreshold = it.threshold.enableThreshold;
            tt->threshold = it.threshold.threshold;
            tt->enableCanny = it.threshold.enableCanny;
            tt->cannyLow = it.threshold.cannyLow;
            tt->cannyHigh = it.threshold.cannyHigh;
        }
    } else if (t == 4) {
        if (auto* yt = dynamic_cast<YOLOTool*>(it.toolImpl.get())) {
            yt->modelPath = it.yolo.modelPath;
            yt->classesPath = it.yolo.classesPath;
            yt->confThreshold = it.yolo.confidenceThreshold;
            yt->nmsThreshold = it.yolo.nmsThreshold;
            yt->useROI = !context.rois.empty();
            yt->useGPU = it.yolo.useGPU;
        }
    } else if (t == 5) {
        if (auto* ct = dynamic_cast<ContourTool*>(it.toolImpl.get())) {
            ct->useGray = it.cntUseGray; ct->blurSize = it.cntBlurSize;
            ct->threshMode = it.cntThreshMode; ct->threshValue = it.cntThreshValue;
            ct->adaptBlock = it.cntAdaptBlock; ct->invert = it.cntInvert;
            ct->retrMode = it.cntRetrMode; ct->approxMethod = it.cntApproxMethod;
            ct->minArea = it.cntMinArea; ct->maxContours = it.cntMaxContours;
            ct->filterConvex = it.cntFilterConvex; ct->approxEps = it.cntApproxEps;
            ct->lineThick = it.cntLineThick; ct->showLabels = it.showResultLabels;
            ct->fillContours = it.cntFillContours;
            ct->matchROI = it.cntMatchROI; ct->matchThresh = it.cntMatchThresh;
        }
    } else if (t == 6) {
        if (auto* st = dynamic_cast<ShapeTool*>(it.toolImpl.get())) {
            st->tplImage = it.shpTplImage; st->blurSize = it.shpBlurSize;
            st->tplRetr = it.shpTplRetr; st->tplMinArea = it.shpTplMinArea;
            st->minScore = it.shpMinScore; st->shapeScore = it.shpShapeScore;
            st->lineThick = it.shpLineThick; st->method = it.shpMethod;
            st->showLabels = it.showResultLabels; st->maxResults = it.shpMaxResults;
            st->tplGray = it.shpTplGray; st->tplBinary = it.shpTplBinary;
            st->tplBinThresh = it.shpTplBinThresh;
            st->tplBlur = it.shpTplBlur; st->tplBlurK = it.shpTplBlurK;
            st->tplInvert = it.shpTplInvert;
        }
    } else if (t == 7) {
        if (auto* lt = dynamic_cast<LineTool*>(it.toolImpl.get())) {
            lt->cannyLow = it.lineCannyLow; lt->cannyHigh = it.lineCannyHigh;
            lt->minLength = it.lineMinLength; lt->maxGap = it.lineMaxGap;
            lt->minAngle = it.lineMinAngle; lt->maxAngle = it.lineMaxAngle;
            lt->thickness = it.lineThickness; lt->maxLines = it.lineMaxLines;
            lt->showLabels = it.showResultLabels; lt->useROI = !context.rois.empty();
        }
    } else if (t == 8) {
        if (auto* mt = dynamic_cast<MorphologyITool*>(it.toolImpl.get())) {
            mt->params.opType = it.morphology.operation;
            mt->params.kernelSize = it.morphology.kernelSize;
            mt->params.kernelShape = it.morphology.kernelShape;
            mt->params.iterations = it.morphology.iterations;
            mt->params.useGray = it.morphology.useGray;
        }
    } else if (t == 9) {
        if (auto* ct = dynamic_cast<ColorAnalyzerITool*>(it.toolImpl.get())) {
            ct->params.colorSpace = it.colorAnalysis.colorSpace;
            ct->params.histBins = it.colorAnalysis.histogramBins;
            ct->params.showHist = it.colorAnalysis.showHistogram;
            ct->params.useROI = it.colorAnalysis.useROI || !context.rois.empty();
            ct->params.histHeight = it.colorAnalysis.histogramHeight;
        }
    } else if (t == 10) {
        if (auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get())) {
            mf->refImage = it.mcfRefImage;
            mf->refAnchorX = it.mcfAnchorX;
            mf->refAnchorY = it.mcfAnchorY;
            mf->imgUseGray = it.mcfImgGray;
            mf->imgUseBinary = it.mcfImgBinary;
            mf->imgBinThresh = it.mcfImgBinThresh;
            mf->useROI = !context.rois.empty();
            mf->maxResults = it.mcfMaxResults;
            mf->minDist = it.mcfMinDist;
            mf->crossSize = it.mcfCrossSize;
            mf->crossThick = it.mcfCrossThick;
        }
    } else if (t == 11) {
        if (auto* yt = dynamic_cast<OpenCVYoloITool*>(it.toolImpl.get())) {
            yt->modelPath = it.yolo.modelPath;
            yt->classesPath = it.yolo.classesPath;
            yt->confThreshold = it.yolo.confidenceThreshold;
            yt->nmsThreshold = it.yolo.nmsThreshold;
        }
    } else if (t == 13) {
        if (auto* ot = dynamic_cast<OCRTool*>(it.toolImpl.get())) {
            ot->detModelPath = it.ocr.detectionModelPath;
            ot->detParamPath = it.ocr.detectionParamPath;
            ot->recModelPath = it.ocr.recognitionModelPath;
            ot->recParamPath = it.ocr.recognitionParamPath;
            ot->dictionaryPath = it.ocr.dictionaryPath;
            ot->minConfidence = it.ocr.minimumConfidence;
            ot->maxItems = it.ocr.maximumItems;
            ot->inputSize = it.ocr.inputSize;
            ot->maxCandidates = it.ocr.maximumCandidates;
            ot->minBoxArea = it.ocr.minimumBoxArea;
            ot->minBoxHeight = it.ocr.minimumBoxHeight;
            ot->roiPadding = it.ocr.roiPadding;
            ot->fastMode = it.ocr.fastMode;
            ot->detectOnly = it.ocr.detectOnly;
            ot->useROI = it.ocr.useROI || !context.rois.empty();
        }
    } else if (t == 14) {
        if (auto* qt = dynamic_cast<QRCodeTool*>(it.toolImpl.get())) {
            qt->useROI = it.qrUseROI || !context.rois.empty();
            qt->detectMulti = it.qrDetectMulti;
            qt->enhance = it.qrEnhance;
            qt->minSize = it.qrMinSize;
            qt->showText = it.showResultLabels;
            qt->engine = it.qrEngine;
            qt->formatMask = it.qrFormatMask;
            qt->filterDuplicates = it.qrFilterDuplicates;
        }
    } else if (t == 15) {
        if (auto* mt = dynamic_cast<MeasurementTool*>(it.toolImpl.get())) {
            mt->mode = it.measureMode;
            mt->caliperCount = it.measureCaliperCount;
            mt->caliper.searchLength = it.measureSearchLength;
            mt->caliper.projectionWidth = it.measureProjectionWidth;
            mt->caliper.smoothingSigma = it.measureSmoothingSigma;
            mt->caliper.edgeThreshold = it.measureEdgeThreshold;
            mt->caliper.minPairDistance = it.measureMinPairDistance;
            mt->caliper.polarity = static_cast<CaliperOperators::EdgePolarity>(
                std::clamp(it.measureEdgePolarity, 0, 2));
            mt->caliper.subpixel = it.measureSubpixel;
            mt->fitMethod = static_cast<CaliperOperators::FitMethod>(
                std::clamp(it.measureFitMethod, 0, 1));
            mt->fitInlierThreshold = it.measureFitInlierThreshold;
            mt->minimumValidCalipers = it.measureMinimumValidCalipers;
            mt->minimumConfidence = it.measureMinimumConfidence;
            mt->mmPerPixel = it.measureMmPerPixel;
            mt->calibration = it.measureCalibration;
            mt->toleranceEnabled = it.measureToleranceEnabled;
            mt->nominal = it.measureNominal;
            mt->toleranceMinus = it.measureToleranceMinus;
            mt->tolerancePlus = it.measureTolerancePlus;
        }
    } else if (t == 17) {
        if (auto* geometry = dynamic_cast<GeometryDrawTool*>(it.toolImpl.get()))
            geometry->primitives = it.geometryItems;
    }
}

void PublishResult(int type, ToolResult result, float ms)
{
    if (type == 0) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %.3f ms", result.toolName.c_str(), ms);
    } else if (type == 1) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu regions, %.3f ms", result.toolName.c_str(), result.regions.size(), ms);
    } else if (type == 3) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %.3f ms", result.toolName.c_str(), ms);
    } else if (type == 4) {
        ToolChainState::SetYoloLastTimeMs(ms);
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s(%s): %zu detections, %.3f ms",
            result.toolName.c_str(), YOLODetector::GetBackendName(), result.detections.size(), ms);
    } else if (type == 2) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu regions, %.3f ms", result.toolName.c_str(), result.regions.size(), ms);
    } else if (type == 5) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu regions", result.toolName.c_str(), result.regions.size());
    } else if (type == 6) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu regions", result.toolName.c_str(), result.regions.size());
    } else if (type == 7) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu", result.toolName.c_str(), result.lines.size());
    } else if (type == 10) {
        ToolChainState::SetMcfLastTimeMs(ms);
        ToolChainState::SetMcfLastCount(static_cast<int>(result.regions.size()));
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu matches, %.3f ms", result.toolName.c_str(), result.regions.size(), ms);
    } else if (type == 11) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu detections, total %.3f ms | pre %.3f infer %.3f post %.3f",
            result.toolName.c_str(),
            result.detections.size(),
            result.timing.executeMs,
            result.timing.backendPreprocessMs,
            result.timing.backendInferenceMs,
            result.timing.backendPostprocessMs);
    } else if (type == 13) {
        LogSystem::Add(result.success ? LOG_INFO : LOG_WARN, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu texts, %.3f ms%s%s",
            result.toolName.c_str(),
            result.texts.size(),
            ms,
            result.message.empty() ? "" : " | ",
            result.message.c_str());
        if (MeasurementValue(result, "ocrCandidates", -1.0) >= 0.0)
        {
            LogSystem::Add(LOG_INFO, ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
                "OCR stats: det %.3f ms | rec %.3f ms | post %.3f ms | candidates %.0f/%0.f | workers %.0f | resized %.0fx%.0f | crop %.0fx%.0f",
                MeasurementValue(result, "ocrDetectMs"),
                MeasurementValue(result, "ocrRecognizeMs"),
                MeasurementValue(result, "ocrPostprocessMs"),
                MeasurementValue(result, "ocrCandidates"),
                MeasurementValue(result, "ocrRecognizedCandidates"),
                MeasurementValue(result, "ocrWorkers"),
                MeasurementValue(result, "ocrResizedWidth"),
                MeasurementValue(result, "ocrResizedHeight"),
                MeasurementValue(result, "ocrCropWidth"),
                MeasurementValue(result, "ocrCropHeight"));
        }
    } else if (type == 14) {
        LogSystem::Add(result.success ? LOG_INFO : LOG_WARN, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu barcodes, %.3f ms%s%s",
            result.toolName.c_str(),
            result.texts.size(),
            ms,
            result.message.empty() ? "" : " | ",
            result.message.c_str());
    } else if (type == 15) {
        LogSystem::Add(result.status == ToolResultStatus::Pass ? LOG_INFO : LOG_WARN,
            ImVec4(0, 1, 0.5f, 1), "%s: %s, %.3f ms",
            result.toolName.c_str(), result.message.c_str(), ms);
    }

    if (result.status != ToolResultStatus::Pass)
    {
        LogSystem::Add(
            result.status == ToolResultStatus::Error ? LOG_ERROR : LOG_WARN,
            ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
            "%s: %s%s%s",
            result.toolName.c_str(),
            ToolResultStatusName(result.status),
            result.statusReason.empty() ? "" : " | ",
            result.statusReason.c_str());
    }

    PublishUnifiedResult(std::move(result));
}

void LogToolTimings(const ToolInstance& it, const ToolRunTimings& timings)
{
    const float total = timings.prepareMs + timings.executeMs + timings.publishMs;
    if (total < 2.0f)
        return;

    LogSystem::Add(LOG_INFO, ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
        "%s timing: prepare %.3f ms | execute %.3f ms | publish %.3f ms | total %.3f ms",
        ToolInstanceLogName(ToolRegistry::GetName(it.type), it.label).c_str(),
        timings.prepareMs, timings.executeMs, timings.publishMs, total);
}
}

namespace ToolExecutor
{

bool PrepareDetached(const ToolInstance& source, const cv::Mat& input,
    int sourceToolIndex, ToolInstance& toolSnapshot, VisionContext& context)
{
    if (input.empty())
        return false;

    toolSnapshot = source;
    context = VisionContext{};
    const ImmutableImageFrame frame = ImageState::AcquireImmutableFrame();
    if (frame.current && input.data == frame.current->data)
        context.immutableImageOwner = frame.current;
    else if (frame.original && input.data == frame.original->data)
        context.immutableImageOwner = frame.original;
    else
        context.immutableImageOwner = std::make_shared<const cv::Mat>(input);
    context.image = *context.immutableImageOwner;

    context.immutableOriginalOwner = frame.original
        ? frame.original : context.immutableImageOwner;
    context.originalImage = *context.immutableOriginalOwner;
    context.frame.original = context.originalImage;
    context.width = context.image.cols;
    context.height = context.image.rows;
    context.imageVersion = frame.version;
    context.rois = SelectSearchROIs(source);

    const auto& tools = ToolChainState::ReadOnlyTools();
    if (source.resultRoiMode != static_cast<int>(ResultROIMode::Disabled))
    {
        const int upstreamIndex = ResolveToolSourceIndex(
            tools, source.resultRoiSourceTool, source.resultRoiSourceToolId);
        if (upstreamIndex < 0 || upstreamIndex == sourceToolIndex ||
            !tools[upstreamIndex].hasLastResult)
        {
            return false;
        }

        ResultROIRequest request;
        request.mode = static_cast<ResultROIMode>(std::clamp(source.resultRoiMode, 0, 2));
        request.resultIndex = (std::max)(0, source.resultRoiIndex);
        request.missingPolicy = static_cast<MissingResultPolicy>(
            std::clamp(source.resultRoiMissingPolicy, 0, 1));
        request.category = source.resultRoiCategory;
        request.classId = source.resultRoiClassId;
        request.minScore = source.resultRoiMinScore;
        request.minArea = source.resultRoiMinArea;
        request.sortMode = source.resultRoiSortMode;
        request.sortDescending = source.resultRoiSortDescending;
        ResultROIResolution resolution = ResultROIResolver::Resolve(
            tools[upstreamIndex].lastResult, request, context.image.size());
        if (!resolution.available)
            return false;
        context.rois = std::move(resolution.rois);
    }

    if (source.fixture.enabled)
    {
        const int upstreamIndex = ResolveToolSourceIndex(
            tools, source.fixture.sourceToolIndex, source.fixture.sourceToolId);
        FixturePose currentPose;
        if (upstreamIndex < 0 || upstreamIndex == sourceToolIndex ||
            !tools[upstreamIndex].hasLastResult ||
            !FixtureTransform::TryExtractPose(tools[upstreamIndex].lastResult,
                (std::max)(0, source.fixture.resultIndex), currentPose))
        {
            return false;
        }

        FixturePose referencePose;
        referencePose.valid = true;
        referencePose.origin = source.fixture.referenceOrigin;
        referencePose.angleDegrees = source.fixture.referenceAngleDegrees;
        context.rois = FixtureTransform::TransformROIs(
            context.rois, referencePose, currentPose);
    }

    context.selectedROI = SelectROIIndex(context.rois);
    context.frozenTemplate = TemplateState::FrozenTemplate().empty()
        ? cv::Mat() : TemplateState::FrozenTemplate().clone();
    return true;
}

bool PrepareDetachedSnapshot(const ToolInstance& source,
    const cv::Mat& input, const cv::Mat& original, int sourceToolIndex,
    const std::vector<ToolInstance>& taskTools,
    const std::vector<int>& taskToolIndices,
    const std::vector<ROI>& visibleROIs, int selectedROI,
    const cv::Mat& frozenTemplate, ToolInstance& toolSnapshot,
    VisionContext& context)
{
    if (input.empty() || taskTools.size() != taskToolIndices.size())
        return false;

    toolSnapshot = source;
    context = VisionContext{};
    context.immutableImageOwner = std::make_shared<const cv::Mat>(input);
    context.image = *context.immutableImageOwner;
    context.immutableOriginalOwner = std::make_shared<const cv::Mat>(
        original.empty() ? input : original);
    context.originalImage = *context.immutableOriginalOwner;
    context.frame.original = context.originalImage;
    context.width = context.image.cols;
    context.height = context.image.rows;
    context.imageVersion = 0;

    if (source.searchROIs.empty())
    {
        context.rois = visibleROIs;
    }
    else
    {
        context.rois = source.searchROIs;
        for (ROI& configured : context.rois)
        {
            if (configured.runtimeId == 0)
                continue;
            const auto current = std::find_if(visibleROIs.begin(), visibleROIs.end(),
                [&configured](const ROI& visible)
                {
                    return visible.runtimeId == configured.runtimeId;
                });
            if (current != visibleROIs.end())
                configured = *current;
        }
    }

    auto resolveTaskSource = [&](int legacyIndex, std::uint64_t stableId) -> int
    {
        for (int index = 0; index < static_cast<int>(taskTools.size()); ++index)
        {
            if ((stableId != 0 && taskTools[index].toolId == stableId) ||
                (stableId == 0 && taskToolIndices[index] == legacyIndex))
            {
                return index;
            }
        }
        return -1;
    };

    if (source.resultRoiMode != static_cast<int>(ResultROIMode::Disabled))
    {
        const int upstreamIndex = resolveTaskSource(
            source.resultRoiSourceTool, source.resultRoiSourceToolId);
        if (upstreamIndex < 0 || taskToolIndices[upstreamIndex] == sourceToolIndex ||
            !taskTools[upstreamIndex].hasLastResult)
        {
            return false;
        }

        ResultROIRequest request;
        request.mode = static_cast<ResultROIMode>(std::clamp(source.resultRoiMode, 0, 2));
        request.resultIndex = (std::max)(0, source.resultRoiIndex);
        request.missingPolicy = static_cast<MissingResultPolicy>(
            std::clamp(source.resultRoiMissingPolicy, 0, 1));
        request.category = source.resultRoiCategory;
        request.classId = source.resultRoiClassId;
        request.minScore = source.resultRoiMinScore;
        request.minArea = source.resultRoiMinArea;
        request.sortMode = source.resultRoiSortMode;
        request.sortDescending = source.resultRoiSortDescending;
        ResultROIResolution resolution = ResultROIResolver::Resolve(
            taskTools[upstreamIndex].lastResult, request, context.image.size());
        if (!resolution.available)
            return false;
        context.rois = std::move(resolution.rois);
    }

    if (source.fixture.enabled)
    {
        const int upstreamIndex = resolveTaskSource(
            source.fixture.sourceToolIndex, source.fixture.sourceToolId);
        FixturePose currentPose;
        if (upstreamIndex < 0 || taskToolIndices[upstreamIndex] == sourceToolIndex ||
            !taskTools[upstreamIndex].hasLastResult ||
            !FixtureTransform::TryExtractPose(taskTools[upstreamIndex].lastResult,
                (std::max)(0, source.fixture.resultIndex), currentPose))
        {
            return false;
        }
        FixturePose referencePose;
        referencePose.valid = true;
        referencePose.origin = source.fixture.referenceOrigin;
        referencePose.angleDegrees = source.fixture.referenceAngleDegrees;
        context.rois = FixtureTransform::TransformROIs(
            context.rois, referencePose, currentPose);
    }

    context.selectedROI = context.rois.empty() ? -1 :
        (selectedROI >= 0 && selectedROI < static_cast<int>(context.rois.size())
            ? selectedROI : 0);
    context.frozenTemplate = frozenTemplate;
    return true;
}

bool ExecuteDetached(ToolInstance& it, VisionContext& ctx, int sourceToolIndex,
    ToolExecutionOutput& output)
{
    output = ToolExecutionOutput{};
    if (ctx.image.empty() || ctx.IsCancellationRequested())
        return false;

    const int t = it.type;
    if (!it.toolImpl)
        it.toolImpl = ITool::Create(t);
    if (!it.toolImpl)
        return false;

    auto tPrepare0 = std::chrono::steady_clock::now();
    VisionContext rotatedContext;
    VisionContext* executionContext = &ctx;
    RotatedROI::Transform rotatedTransform;
    if (it.type != 17 && ctx.rois.size() == 1 && ctx.rois[0].type == ROI_TYPE_RECT &&
        std::abs(ctx.rois[0].angle) >= 0.0001f)
    {
        cv::Mat rotatedImage;
        if (RotatedROI::Extract(ctx.image, ctx.rois[0], rotatedImage, rotatedTransform))
        {
            rotatedContext = ctx;
            rotatedContext.image = std::move(rotatedImage);
            if (!ctx.originalImage.empty())
            {
                RotatedROI::Transform originalTransform;
                cv::Mat rotatedOriginal;
                if (RotatedROI::Extract(ctx.originalImage, ctx.rois[0],
                                        rotatedOriginal, originalTransform))
                {
                    rotatedContext.originalImage = std::move(rotatedOriginal);
                }
                else
                {
                    rotatedContext.originalImage = rotatedContext.image;
                }
            }
            ROI localROI;
            localROI.type = ROI_TYPE_RECT;
            localROI.start = ImVec2(0.0f, 0.0f);
            localROI.end = ImVec2(static_cast<float>(rotatedContext.image.cols),
                                  static_cast<float>(rotatedContext.image.rows));
            rotatedContext.rois.assign(1, localROI);
            rotatedContext.selectedROI = 0;
            rotatedContext.width = rotatedContext.image.cols;
            rotatedContext.height = rotatedContext.image.rows;
            rotatedContext.frame.original = rotatedContext.originalImage;
            executionContext = &rotatedContext;
        }
    }

    SyncIToolParams(it, *executionContext);
    if (executionContext->IsCancellationRequested())
        return false;
    if (executionContext == &rotatedContext && it.type == 16)
    {
        if (auto* difference = dynamic_cast<DifferenceTool*>(it.toolImpl.get()))
        {
            RotatedROI::Transform referenceTransform;
            cv::Mat rotatedReference;
            if (RotatedROI::Extract(it.differenceReferenceImage, ctx.rois[0],
                                    rotatedReference, referenceTransform))
            {
                difference->referenceImage = std::move(rotatedReference);
            }
        }
    }
    auto tPrepare1 = std::chrono::steady_clock::now();
    output.prepareMs = std::chrono::duration<float, std::milli>(tPrepare1 - tPrepare0).count();

    auto t0 = std::chrono::steady_clock::now();
    auto result = it.toolImpl->Execute(*executionContext);
    if (executionContext->IsCancellationRequested())
        return false;
    if (executionContext == &rotatedContext)
    {
        RotatedROI::RestoreResult(result, rotatedTransform);
        if (!result.debugImage.empty())
        {
            cv::Mat restoredDebugImage;
            if (RotatedROI::RestoreDebugImage(result.debugImage, ctx.image,
                                              rotatedTransform, restoredDebugImage))
            {
                result.debugImage = std::move(restoredDebugImage);
            }
        }
    }
    ApplyMissingModelSkipPolicy(it, result);
    auto t1 = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    output.executeMs = ms;
    result.timing.prepareMs = output.prepareMs;
    result.timing.executeMs = output.executeMs;
    result.timing.wallMs = output.prepareMs + output.executeMs;
    const std::string baseName = result.toolName.empty() ? it.toolImpl->GetName() : result.toolName;
    result.toolName = ToolInstanceLogName(baseName.c_str(), it.label);
    result.sourceToolIndex = sourceToolIndex;
    result.sourceToolId = it.toolId;
    const std::string overlayLabel = (it.label == baseName) ? std::string() : it.label;
    ApplyToolLabelToOverlayItems(result, overlayLabel);
    ToolJudgement::Evaluate(result, it.judgement);
    output.result = std::move(result);
    output.completed = true;
    return true;
}

bool PublishDetached(ToolInstance& it, ToolExecutionOutput&& output)
{
    if (!output.completed)
        return false;

    auto tPublish0 = std::chrono::steady_clock::now();
    const bool dirty = !output.result.debugImage.empty();
    if (dirty)
    {
        ImageState::SetDebugImage(output.result.debugImage);
        output.result.debugImage.release();
    }

    const auto tPublishReady = std::chrono::steady_clock::now();
    output.publishMs = std::chrono::duration<float, std::milli>(
        tPublishReady - tPublish0).count();
    output.result.timing.publishMs = output.publishMs;
    output.result.timing.wallMs = output.prepareMs + output.executeMs + output.publishMs;

    if (!output.cacheHit)
    {
        InspectionHistory::AddResult(
            output.result.sourceToolId, output.result.toolName, output.result);
    }

    it.lastResult = output.result;
    it.hasLastResult = true;
    it.parametersDirty = false;

    if (output.cacheHit)
    {
        LogSystem::Add(LOG_INFO, ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
            "%s: 缓存命中", output.result.toolName.c_str());
        PublishUnifiedResult(std::move(output.result));
    }
    else
    {
        PublishResult(it.type, std::move(output.result), output.executeMs);
    }
    ToolRunTimings timings;
    timings.prepareMs = output.prepareMs;
    timings.executeMs = output.executeMs;
    timings.publishMs = output.publishMs;
    LogToolTimings(it, timings);
    return dirty;
}

bool RunViaIToolInternal(ToolInstance& it, VisionContext& ctx, ToolRunTimings* timings = nullptr)
{
    if (ctx.image.empty())
    {
        LogSystem::Add(LOG_WARN, "Please load an image first");
        return false;
    }

    if (it.type == 6 && ctx.frozenTemplate.empty())
        ctx.frozenTemplate = TemplateState::FrozenTemplate();

    ToolExecutionOutput output;
    if (!ExecuteDetached(it, ctx, FindToolInstanceIndex(it), output))
        return false;

    if (timings)
    {
        timings->prepareMs += output.prepareMs;
        timings->executeMs += output.executeMs;
    }
    const bool dirty = PublishDetached(it, std::move(output));
    return dirty;
}

bool RunViaITool(ToolInstance& it)
{
    ToolRunTimings timings;
    auto tPrepare0 = std::chrono::steady_clock::now();
    const bool canUseSharedImage = ToolCanUseSharedInput(it);
    if (canUseSharedImage)
    {
        gContext.image = ImageState::Current();
        gContext.originalImage = !ImageState::Original().empty() ? ImageState::Original() : ImageState::Current();
    }
    else
    {
        gContext.image = ImageState::Current().clone();
        gContext.originalImage = !ImageState::Original().empty() ? ImageState::Original().clone() : ImageState::Current().clone();
    }
    gContext.width = ImageState::Width();
    gContext.height = ImageState::Height();
    gContext.imageVersion = ImageState::Version();
    gContext.frame.original = canUseSharedImage ? gContext.originalImage : gContext.originalImage.clone();
    bool useResolvedResultROI = false;
    std::vector<ROI> resolvedResultROIs;
    if (it.resultRoiMode != static_cast<int>(ResultROIMode::Disabled))
    {
        ResultROIResolution resolution;
        const auto& tools = ToolChainState::ReadOnlyTools();
        const int currentIndex = FindToolInstanceIndex(it);
        const int resultRoiSourceIndex = ResolveToolSourceIndex(
            tools, it.resultRoiSourceTool, it.resultRoiSourceToolId);
        if (resultRoiSourceIndex < 0 || resultRoiSourceIndex == currentIndex)
        {
            resolution.reason = "结果 ROI 的上游工具无效";
        }
        else if (!tools[resultRoiSourceIndex].hasLastResult)
        {
            resolution.reason = "上游工具尚未产生结果";
        }
        else
        {
            ResultROIRequest request;
            request.mode = static_cast<ResultROIMode>(std::clamp(it.resultRoiMode, 0, 2));
            request.resultIndex = (std::max)(0, it.resultRoiIndex);
            request.missingPolicy = static_cast<MissingResultPolicy>(std::clamp(it.resultRoiMissingPolicy, 0, 1));
            request.category = it.resultRoiCategory;
            request.classId = it.resultRoiClassId;
            request.minScore = it.resultRoiMinScore;
            request.minArea = it.resultRoiMinArea;
            request.sortMode = it.resultRoiSortMode;
            request.sortDescending = it.resultRoiSortDescending;
            resolution = ResultROIResolver::Resolve(
                tools[resultRoiSourceIndex].lastResult,
                request,
                gContext.image.size());
        }

        if (!resolution.available)
        {
            ToolResult result;
            result.toolName = ToolInstanceLogName(ToolRegistry::GetName(it.type), it.label);
            result.sourceToolIndex = currentIndex;
            result.sourceToolId = it.toolId;
            result.success = true;
            result.skipped = it.resultRoiMissingPolicy == static_cast<int>(MissingResultPolicy::Skip);
            result.status = result.skipped ? ToolResultStatus::Pass : ToolResultStatus::Fail;
            result.message = result.skipped ? "已跳过: " + resolution.reason : resolution.reason;
            result.statusReason = result.skipped ? std::string() : resolution.reason;
            it.lastResult = result;
            it.hasLastResult = true;
            PublishResult(it.type, std::move(result), 0.0f);
            return false;
        }

        resolvedResultROIs = std::move(resolution.rois);
        useResolvedResultROI = true;
    }

    std::vector<ROI> contextROIs = useResolvedResultROI
        ? std::move(resolvedResultROIs)
        : SelectSearchROIs(it);
    if (it.fixture.enabled)
    {
        const auto& tools = ToolChainState::ReadOnlyTools();
        const int currentIndex = FindToolInstanceIndex(it);
        const int fixtureSourceIndex = ResolveToolSourceIndex(
            tools, it.fixture.sourceToolIndex, it.fixture.sourceToolId);
        FixturePose currentPose;
        std::string fixtureError;
        if (fixtureSourceIndex < 0 || fixtureSourceIndex == currentIndex)
        {
            fixtureError = "Fixture 上游工具无效";
        }
        else if (!tools[fixtureSourceIndex].hasLastResult)
        {
            fixtureError = "Fixture 上游工具尚未产生定位结果";
        }
        else if (!FixtureTransform::TryExtractPose(
            tools[fixtureSourceIndex].lastResult,
            (std::max)(0, it.fixture.resultIndex),
            currentPose))
        {
            fixtureError = "Fixture 无法从上游结果提取位置和角度";
        }

        if (!fixtureError.empty())
        {
            ToolResult result;
            result.toolName = ToolInstanceLogName(ToolRegistry::GetName(it.type), it.label);
            result.sourceToolIndex = currentIndex;
            result.sourceToolId = it.toolId;
            result.success = true;
            result.skipped = !it.fixture.failOnMissing;
            result.status = result.skipped ? ToolResultStatus::Pass : ToolResultStatus::Fail;
            result.message = result.skipped ? "已跳过: " + fixtureError : fixtureError;
            result.statusReason = result.skipped ? std::string() : fixtureError;
            it.lastResult = result;
            it.hasLastResult = true;
            PublishResult(it.type, std::move(result), 0.0f);
            return false;
        }

        FixturePose referencePose;
        referencePose.valid = true;
        referencePose.origin = it.fixture.referenceOrigin;
        referencePose.angleDegrees = it.fixture.referenceAngleDegrees;
        contextROIs = FixtureTransform::TransformROIs(contextROIs, referencePose, currentPose);
    }

    gContext.rois = std::move(contextROIs);
    gContext.selectedROI = SelectROIIndex(gContext.rois);

    if (it.type == 10 && !gContext.rois.empty()) {
        const int idx = SelectROIIndex(gContext.rois);
        const cv::Rect r = gContext.rois[idx].ToCvRect();
        it.mcfRoiX = r.x; it.mcfRoiY = r.y; it.mcfRoiW = r.width; it.mcfRoiH = r.height;
    }

    auto tPrepare1 = std::chrono::steady_clock::now();
    timings.prepareMs += std::chrono::duration<float, std::milli>(tPrepare1 - tPrepare0).count();

    const bool dirty = RunViaIToolInternal(it, gContext, &timings);
    return dirty;
}

bool RunViaITool(ToolInstance& it, VisionContext& ctx)
{
    ToolRunTimings timings;
    const bool dirty = RunViaIToolInternal(it, ctx, &timings);
    return dirty;
}

bool Execute(int type, ToolInstance& it)
{
    switch (type) {
    case 0: case 1: case 2: case 3:
    case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11:
    case 13: case 14: case 15: case 16: case 17:
        return RunViaITool(it);
    default: return false;
    }
}

} // namespace ToolExecutor
