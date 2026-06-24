#include "ToolExecutor.h"

#include "ResultPublisher.h"
#include "../Algorithm/BlobTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/EdgeTool.h"
#include "../Algorithm/LineDetector.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/OpenCVYoloDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/ShapeTools.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/YOLOTool.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "LegacyAppState.h"
#include "ROIState.h"
#include "ToolChainState.h"
#include "UIStateBridge.h"
#include "../Log/LogSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace
{
bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst);

const std::vector<ROI>& SelectSearchROIs(const ToolInstance& it)
{
    if (!it.searchROIs.empty())
        return it.searchROIs;
    return ROIState::ReadOnlyItems();
}

bool HasSearchROI(const ToolInstance& it)
{
    return !SelectSearchROIs(it).empty();
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

class TemplateMatchITool final : public ITool
{
public:
    bool enableRotation = false;
    int rotationStart = -45;
    int rotationEnd = 45;
    int rotationStep = 1;
    int maxResults = 5;
    float matchThreshold = 0.7f;
    int maxImageDim = 1000;
    float nmsThreshold = 0.3f;
    bool tplGray = false;
    bool tplBinary = false;
    int tplBinThresh = 128;
    bool tplEdge = false;
    int tplEdgeLow = 50;
    int tplEdgeHigh = 150;
    bool imgUseGray = false;
    bool imgEnableThreshold = false;
    int imgThreshold = 128;
    cv::Mat templateImg;
    bool useSearchROI = false;
    std::vector<ROI> searchROIs;

    const char* GetName() const override { return "模板匹配"; }
    int GetType() const override { return 1; }
    void DrawUI() override {}
    nlohmann::json Save() const override { return {{"type", 1}}; }
    void Load(const nlohmann::json&) override {}
    ToolResult Execute(VisionContext& ctx) override
    {
        ToolResult result;
        result.toolName = GetName();
        if (ctx.image.empty())
        {
            result.success = false;
            result.message = "请先加载图片";
            return result;
        }

        auto& currentImage = ImageState::CurrentRef();
        ctx.image.copyTo(currentImage);
        g_TMEnableRotation = enableRotation;
        g_TMRotationStart = rotationStart; g_TMRotationEnd = rotationEnd;
        g_TMRotationStep = rotationStep;
        g_TMMaxResults = maxResults; g_TMMatchThreshold = matchThreshold;
        g_TMMaxImageDim = maxImageDim; g_NmsThreshold = nmsThreshold;
        g_TMSearchMode = useSearchROI ? 1 : 0;
        g_TplGray = tplGray; g_TplBinary = tplBinary; g_TplBinThresh = tplBinThresh;
        g_TplEdge = tplEdge; g_TplEdgeLow = tplEdgeLow; g_TplEdgeHigh = tplEdgeHigh;

        bool didPreprocess = false;
        if (imgUseGray || imgEnableThreshold) {
            if (imgUseGray && currentImage.channels() > 1) {
                int code = (currentImage.channels() == 4) ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY;
                cv::cvtColor(currentImage, currentImage, code);
            }
            if (imgEnableThreshold) {
                if (currentImage.channels() > 1) cv::cvtColor(currentImage, currentImage, cv::COLOR_BGR2GRAY);
                cv::threshold(currentImage, currentImage, imgThreshold, 255, cv::THRESH_BINARY);
            }
            didPreprocess = true;
        }

        g_FrozenTemplate = templateImg;
        auto& globalROIs = ROIState::Items();
        const auto oldROIs = globalROIs;
        const int oldSelectedROI = ROIState::SelectedIndex();
        if (useSearchROI)
        {
            globalROIs = !searchROIs.empty() ? searchROIs : ctx.rois;
            ROIState::SetSelectedIndex(SelectROIIndex(globalROIs));
        }
        TemplateMatch::Run();
        if (useSearchROI)
        {
            globalROIs = oldROIs;
            ROIState::SetSelectedIndex(oldSelectedROI);
        }

        result.success = true;
        const int maxShown = std::min(static_cast<int>(gMatchROIs.size()), maxResults);
        for (int i = 0; i < maxShown; ++i) {
            const auto& roi = gMatchROIs[i];
            ToolResult::Region region;
            const float x1 = std::min(roi.start.x, roi.end.x);
            const float y1 = std::min(roi.start.y, roi.end.y);
            const float x2 = std::max(roi.start.x, roi.end.x);
            const float y2 = std::max(roi.start.y, roi.end.y);
            region.bbox = cv::Rect(static_cast<int>(x1), static_cast<int>(y1),
                std::max(1, static_cast<int>(x2 - x1)), std::max(1, static_cast<int>(y2 - y1)));
            region.score = (i < static_cast<int>(gMatchScores.size())) ? static_cast<float>(gMatchScores[i]) : 0.0f;
            char label[32];
            std::snprintf(label, sizeof(label), "#%d %.2f", i + 1, region.score);
            region.label = label;
            result.regions.push_back(region);
        }
        if (didPreprocess)
            result.debugImage = currentImage.clone();
        return result;
    }
};

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

        cv::Rect roi = PrimaryContextRect(ctx);
        auto objs = OpenCVYoloDetector::Detect(ctx.image, confThreshold, nmsThreshold, roi);
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
        ToolRegistry::Register(1, []() -> std::unique_ptr<ITool> { return std::make_unique<TemplateMatchITool>(); });
        ToolRegistry::RegisterName(1, "TemplateMatch");
        ToolRegistry::Register(11, []() -> std::unique_ptr<ITool> { return std::make_unique<OpenCVYoloITool>(); });
        ToolRegistry::RegisterName(11, "OpenCVYolo");
    }
};

static LegacyIToolRegister s_LegacyIToolRegister;

void SyncIToolParams(ToolInstance& it)
{
    const int t = it.type;
    if (t == 0) {
        if (auto* et = dynamic_cast<EdgeTool*>(it.toolImpl)) {
            et->cannyLow = it.cannyLow;
            et->cannyHigh = it.cannyHigh;
            et->useGray = it.edgeUseGray;
        }
    } else if (t == 1) {
        if (auto* tt = dynamic_cast<TemplateMatchITool*>(it.toolImpl)) {
            tt->enableRotation = it.enableRotation;
            tt->rotationStart = it.rotationStart;
            tt->rotationEnd = it.rotationEnd;
            tt->rotationStep = it.rotationStep;
            tt->maxResults = it.maxResults;
            tt->matchThreshold = it.matchThreshold;
            tt->maxImageDim = it.maxImageDim;
            tt->nmsThreshold = it.nmsThreshold;
            tt->tplGray = it.tplGray;
            tt->tplBinary = it.tplBinary;
            tt->tplBinThresh = it.tplBinThresh;
            tt->tplEdge = it.tplEdge;
            tt->tplEdgeLow = it.tplEdgeLow;
            tt->tplEdgeHigh = it.tplEdgeHigh;
            tt->imgUseGray = it.imgUseGray;
            tt->imgEnableThreshold = it.imgEnableThreshold;
            tt->imgThreshold = it.imgThreshold;
            tt->templateImg = it.templateImg;
            tt->useSearchROI = HasSearchROI(it);
            tt->searchROIs = SelectSearchROIs(it);
        }
    } else if (t == 2) {
        if (auto* bt = dynamic_cast<BlobTool*>(it.toolImpl)) {
            bt->minArea = it.blobMinArea;
            bt->maxArea = it.blobMaxArea;
        }
    } else if (t == 3) {
        if (auto* tt = dynamic_cast<ThresholdITool*>(it.toolImpl)) {
            tt->useGray = it.dbgUseGray;
            tt->enableBlur = it.dbgEnableBlur;
            tt->blurSize = it.dbgBlurSize;
            tt->enableThreshold = it.dbgEnableThresh;
            tt->threshold = it.dbgThreshold;
            tt->enableCanny = it.dbgEnableCanny;
            tt->cannyLow = it.dbgCannyLow;
            tt->cannyHigh = it.dbgCannyHigh;
        }
    } else if (t == 4) {
        if (auto* yt = dynamic_cast<YOLOTool*>(it.toolImpl)) {
            yt->modelPath = it.yoloModelPath;
            yt->classesPath = it.yoloClassesPath;
            yt->confThreshold = it.yoloConfThreshold;
            yt->nmsThreshold = it.yoloNmsThreshold;
            yt->useROI = HasSearchROI(it);
            yt->useGPU = it.yoloUseGPU;
        }
    } else if (t == 5) {
        if (auto* ct = dynamic_cast<ContourTool*>(it.toolImpl)) {
            ct->useGray = it.cntUseGray; ct->blurSize = it.cntBlurSize;
            ct->threshMode = it.cntThreshMode; ct->threshValue = it.cntThreshValue;
            ct->adaptBlock = it.cntAdaptBlock; ct->invert = it.cntInvert;
            ct->retrMode = it.cntRetrMode; ct->approxMethod = it.cntApproxMethod;
            ct->minArea = it.cntMinArea; ct->maxContours = it.cntMaxContours;
            ct->filterConvex = it.cntFilterConvex; ct->approxEps = it.cntApproxEps;
            ct->lineThick = it.cntLineThick; ct->showLabels = it.cntShowLabels;
            ct->fillContours = it.cntFillContours;
            ct->matchROI = it.cntMatchROI; ct->matchThresh = it.cntMatchThresh;
        }
    } else if (t == 6) {
        if (auto* st = dynamic_cast<ShapeTool*>(it.toolImpl)) {
            st->tplImage = it.shpTplImage; st->blurSize = it.shpBlurSize;
            st->tplRetr = it.shpTplRetr; st->tplMinArea = it.shpTplMinArea;
            st->minScore = it.shpMinScore; st->shapeScore = it.shpShapeScore;
            st->lineThick = it.shpLineThick; st->method = it.shpMethod;
            st->showLabels = it.shpShowLabels; st->maxResults = it.shpMaxResults;
            st->tplGray = it.shpTplGray; st->tplBinary = it.shpTplBinary;
            st->tplBinThresh = it.shpTplBinThresh;
            st->tplBlur = it.shpTplBlur; st->tplBlurK = it.shpTplBlurK;
            st->tplInvert = it.shpTplInvert;
        }
    } else if (t == 7) {
        if (auto* lt = dynamic_cast<LineTool*>(it.toolImpl)) {
            lt->cannyLow = it.lineCannyLow; lt->cannyHigh = it.lineCannyHigh;
            lt->minLength = it.lineMinLength; lt->maxGap = it.lineMaxGap;
            lt->minAngle = it.lineMinAngle; lt->maxAngle = it.lineMaxAngle;
            lt->thickness = it.lineThickness; lt->maxLines = it.lineMaxLines;
            lt->showLabels = it.lineShowLabels; lt->useROI = HasSearchROI(it);
        }
    } else if (t == 8) {
        if (auto* mt = dynamic_cast<MorphologyITool*>(it.toolImpl)) {
            mt->params.opType = it.morphOpType;
            mt->params.kernelSize = it.morphKernelSize;
            mt->params.kernelShape = it.morphKernelShape;
            mt->params.iterations = it.morphIterations;
            mt->params.useGray = it.morphUseGray;
        }
    } else if (t == 9) {
        if (auto* ct = dynamic_cast<ColorAnalyzerITool*>(it.toolImpl)) {
            ct->params.colorSpace = it.colorSpace;
            ct->params.histBins = it.colorHistBins;
            ct->params.showHist = it.colorShowHist;
            ct->params.useROI = it.colorUseROI || HasSearchROI(it);
            ct->params.histHeight = it.colorHistHeight;
        }
    } else if (t == 10) {
        if (auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl)) {
            mf->refImage = it.mcfRefImage;
            mf->refAnchorX = it.mcfAnchorX;
            mf->refAnchorY = it.mcfAnchorY;
            mf->imgUseGray = it.mcfImgGray;
            mf->imgUseBinary = it.mcfImgBinary;
            mf->imgBinThresh = it.mcfImgBinThresh;
            mf->useROI = HasSearchROI(it);
            mf->maxResults = it.mcfMaxResults;
            mf->minDist = it.mcfMinDist;
            mf->crossSize = it.mcfCrossSize;
            mf->crossThick = it.mcfCrossThick;
        }
    } else if (t == 11) {
        if (auto* yt = dynamic_cast<OpenCVYoloITool*>(it.toolImpl)) {
            yt->modelPath = it.yoloModelPath;
            yt->classesPath = it.yoloClassesPath;
            yt->confThreshold = it.yoloConfThreshold;
            yt->nmsThreshold = it.yoloNmsThreshold;
        }
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
        g_McfLastTimeMs = ms;
        g_McfLastCount = static_cast<int>(result.regions.size());
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu matches, %.3f ms", result.toolName.c_str(), result.regions.size(), ms);
    } else if (type == 11) {
        LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
            "%s: %zu detections, total %.3f ms | pre %.3f infer %.3f post %.3f",
            result.toolName.c_str(),
            result.detections.size(),
            OpenCVYoloDetector::g_OpenCVYoloTotalMs,
            OpenCVYoloDetector::g_OpenCVYoloPreMs,
            OpenCVYoloDetector::g_OpenCVYoloInfMs,
            OpenCVYoloDetector::g_OpenCVYoloPostMs);
    }

    PublishUnifiedResult(std::move(result));
}
}

namespace ToolExecutor
{

bool RunViaITool(ToolInstance& it, VisionContext& ctx)
{
    if (ctx.image.empty()) {
        LogSystem::Add(LOG_WARN, "Please load an image first");
        return false;
    }

    const int t = it.type;
    if (!it.toolImpl) {
        it.toolImpl = ITool::Create(t).release();
    }
    if (!it.toolImpl) {
        return false;
    }

    SyncIToolParams(it);
    if (t == 6) {
        ctx.frozenTemplate = g_FrozenTemplate;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto result = it.toolImpl->Execute(ctx);
    auto t1 = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    const std::string baseName = result.toolName.empty() ? it.toolImpl->GetName() : result.toolName;
    result.toolName = ToolInstanceLogName(baseName.c_str(), it.label);
    ApplyToolLabelToOverlayItems(result, it.label);

    const bool dirty = !result.debugImage.empty();
    if (dirty) {
        ImageState::SetDebugImage(result.debugImage);
        gThresholdMat = ImageState::Current().clone();
        gTimeTotal = ms;
        result.debugImage.release();
    }

    PublishResult(t, std::move(result), ms);
    return dirty;
}

bool RunViaITool(ToolInstance& it)
{
    gContext.image = ImageState::Current().clone();
    gContext.originalImage = !ImageState::Original().empty() ? ImageState::Original().clone() : ImageState::Current().clone();
    gContext.width = ImageState::Width();
    gContext.height = ImageState::Height();
    gContext.imageVersion = ImageState::Version();
    gContext.frame.original = gContext.originalImage.clone();
    gContext.rois = SelectSearchROIs(it);
    gContext.selectedROI = SelectROIIndex(gContext.rois);

    if (it.type == 10 && !gContext.rois.empty()) {
        const int idx = SelectROIIndex(gContext.rois);
        const cv::Rect r = gContext.rois[idx].ToCvRect();
        it.mcfRoiX = r.x; it.mcfRoiY = r.y; it.mcfRoiW = r.width; it.mcfRoiH = r.height;
    }

    return RunViaITool(it, gContext);
}

bool Execute(int type, ToolInstance& it)
{
    switch (type) {
    case 0: case 1: case 2: case 3:
    case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11:
        return RunViaITool(it);
    default: return false;
    }
}

} // namespace ToolExecutor
