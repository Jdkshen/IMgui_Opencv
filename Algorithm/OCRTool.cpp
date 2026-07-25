#include "OCRTool.h"

#include "WindowsPPOCREngine.h"
#include "../Core/VisionContext.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <sstream>
#include <opencv2/imgproc.hpp>

namespace
{
cv::Rect SelectOCRRect(const VisionContext& ctx, bool useROI)
{
    if (!useROI || ctx.image.empty())
        return {};

    cv::Rect rect;
    if (ctx.HasROI())
        rect = ctx.GetActiveROIRect();
    else if (!ctx.rois.empty())
        rect = ctx.rois.front().ToCvRect();

    rect &= cv::Rect(0, 0, ctx.image.cols, ctx.image.rows);
    return rect.width > 0 && rect.height > 0 ? rect : cv::Rect();
}

cv::Rect ExpandOCRRect(const cv::Rect& rect, int padding, const cv::Size& imageSize)
{
    if (rect.empty())
        return {};

    padding = std::max(0, padding);
    cv::Rect expanded(
        rect.x - padding,
        rect.y - padding,
        rect.width + padding * 2,
        rect.height + padding * 2);
    expanded &= cv::Rect(0, 0, imageSize.width, imageSize.height);
    return expanded.width > 0 && expanded.height > 0 ? expanded : cv::Rect();
}

void AddOCRRectMeasurements(ToolResult& result, const cv::Rect& roi, const cv::Rect& inputRect, const cv::Mat& image)
{
    result.measurements.push_back({"roiX", static_cast<double>(roi.empty() ? 0 : roi.x), "px"});
    result.measurements.push_back({"roiY", static_cast<double>(roi.empty() ? 0 : roi.y), "px"});
    result.measurements.push_back({"roiWidth", static_cast<double>(roi.empty() ? image.cols : roi.width), "px"});
    result.measurements.push_back({"roiHeight", static_cast<double>(roi.empty() ? image.rows : roi.height), "px"});
    result.measurements.push_back({"ocrCropX", static_cast<double>(inputRect.empty() ? 0 : inputRect.x), "px"});
    result.measurements.push_back({"ocrCropY", static_cast<double>(inputRect.empty() ? 0 : inputRect.y), "px"});
    result.measurements.push_back({"ocrCropWidth", static_cast<double>(inputRect.empty() ? image.cols : inputRect.width), "px"});
    result.measurements.push_back({"ocrCropHeight", static_cast<double>(inputRect.empty() ? image.rows : inputRect.height), "px"});
}

WindowsPPOCRConfig BuildConfig(const OCRTool& tool)
{
    WindowsPPOCRConfig cfg;
    cfg.detParamPath = tool.detParamPath;
    cfg.detModelPath = tool.detModelPath;
    cfg.recParamPath = tool.recParamPath;
    cfg.recModelPath = tool.recModelPath;
    cfg.dictionaryPath = tool.dictionaryPath;
    cfg.inputSize = tool.inputSize;
    cfg.minConfidence = tool.minConfidence;
    cfg.maxItems = tool.maxItems;
    cfg.maxCandidates = tool.maxCandidates;
    cfg.minBoxArea = tool.minBoxArea;
    cfg.minBoxHeight = tool.minBoxHeight;
    cfg.fastMode = tool.fastMode;
    cfg.detectOnly = tool.detectOnly;
    return cfg;
}

bool SameConfig(const WindowsPPOCRConfig& a, const WindowsPPOCRConfig& b)
{
    return a.detParamPath == b.detParamPath &&
        a.detModelPath == b.detModelPath &&
        a.recParamPath == b.recParamPath &&
        a.recModelPath == b.recModelPath &&
        a.dictionaryPath == b.dictionaryPath &&
        a.inputSize == b.inputSize &&
        std::abs(a.minConfidence - b.minConfidence) < 0.0001f &&
        a.maxItems == b.maxItems &&
        a.maxCandidates == b.maxCandidates &&
        a.minBoxArea == b.minBoxArea &&
        a.minBoxHeight == b.minBoxHeight &&
        a.fastMode == b.fastMode &&
        a.detectOnly == b.detectOnly &&
        a.useGPU == b.useGPU;
}

std::string CacheKey(const WindowsPPOCRConfig& cfg)
{
    std::ostringstream out;
    out << cfg.detParamPath << '\n'
        << cfg.detModelPath << '\n'
        << cfg.recParamPath << '\n'
        << cfg.recModelPath << '\n'
        << cfg.dictionaryPath << '\n'
        << cfg.inputSize << '\n'
        << cfg.minConfidence << '\n'
        << cfg.maxItems << '\n'
        << cfg.maxCandidates << '\n'
        << cfg.minBoxArea << '\n'
        << cfg.minBoxHeight << '\n'
        << cfg.fastMode << '\n'
        << cfg.detectOnly << '\n'
        << cfg.useGPU;
    return out.str();
}
}

ToolResult OCRTool::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = GetName();

    if (ctx.IsCancellationRequested())
    {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }

    if (ctx.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }

    const cv::Rect roi = SelectOCRRect(ctx, useROI);
    const cv::Rect inputRect = roi.empty() ? cv::Rect() : ExpandOCRRect(roi, roiPadding, ctx.image.size());
    const cv::Mat input = inputRect.empty() ? ctx.image : ctx.image(inputRect);
    if (input.empty())
    {
        result.success = false;
        result.message = "OCR ROI 无效";
        return result;
    }

    if (detModelPath.empty() || detParamPath.empty() || recModelPath.empty() || recParamPath.empty() || dictionaryPath.empty())
    {
        result.success = false;
        result.message = "请先配置 OCR 检测/识别模型 param/bin 和字典";
        AddOCRRectMeasurements(result, roi, inputRect, ctx.image);
        return result;
    }

    const WindowsPPOCRConfig cfg = BuildConfig(*this);
    const std::string configKey = CacheKey(cfg);
    if (cacheValid &&
        cacheImageVersion == ctx.imageVersion &&
        cacheImageData == ctx.image.data &&
        cacheImageRows == ctx.image.rows &&
        cacheImageCols == ctx.image.cols &&
        cacheImageType == ctx.image.type() &&
        cacheRoi == inputRect &&
        cacheConfigKey == configKey)
    {
        ToolResult cached = cacheResult;
        cached.message = cached.message.empty() ? "OCR缓存" : cached.message + "(缓存)";
        return cached;
    }

    static std::mutex engineMutex;
    static WindowsPPOCREngine engine;
    static WindowsPPOCRConfig loadedConfig;
    static bool hasLoadedConfig = false;

    std::lock_guard<std::mutex> lock(engineMutex);
    if (ctx.IsCancellationRequested())
    {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }
    std::string error;
    if (!engine.IsReady() || !hasLoadedConfig || !SameConfig(loadedConfig, cfg))
    {
        if (!engine.Load(cfg, &error))
        {
            result.success = false;
            result.message = error.empty() ? "NCNN OCR engine load failed" : error;
            AddOCRRectMeasurements(result, roi, inputRect, ctx.image);
            return result;
        }
        loadedConfig = cfg;
        hasLoadedConfig = true;
    }

    std::vector<PPOCRTextResult> texts;
    if (!engine.Recognize(input, texts, &error, ctx.stopToken))
    {
        result.success = false;
        result.message = error.empty() ? "NCNN OCR recognize failed" : error;
        AddOCRRectMeasurements(result, roi, inputRect, ctx.image);
        return result;
    }
    const WindowsPPOCRStats stats = engine.LastStats();
    if (ctx.IsCancellationRequested())
    {
        result.success = false;
        result.message = "执行已取消";
        return result;
    }

    const cv::Point offset = inputRect.empty() ? cv::Point(0, 0) : inputRect.tl();
    for (const PPOCRTextResult& text : texts)
    {
        if (ctx.IsCancellationRequested())
        {
            result.success = false;
            result.message = "执行已取消";
            return result;
        }
        ToolResult::TextItem item;
        item.text = text.text;
        item.box = text.box + offset;
        item.confidence = text.confidence;
        result.texts.push_back(item);
    }

    result.success = true;
    result.message = detectOnly ? "OCR检测完成" : (result.texts.empty() ? "未识别到文字" : "OCR完成");
    result.measurements.push_back({"textCount", static_cast<double>(result.texts.size()), "items"});
    result.measurements.push_back({"ocrInputWidth", static_cast<double>(stats.inputWidth), "px"});
    result.measurements.push_back({"ocrInputHeight", static_cast<double>(stats.inputHeight), "px"});
    result.measurements.push_back({"ocrResizedWidth", static_cast<double>(stats.resizedWidth), "px"});
    result.measurements.push_back({"ocrResizedHeight", static_cast<double>(stats.resizedHeight), "px"});
    result.measurements.push_back({"ocrContours", static_cast<double>(stats.contours), "items"});
    result.measurements.push_back({"ocrCandidates", static_cast<double>(stats.candidates), "items"});
    result.measurements.push_back({"ocrRecognizedCandidates", static_cast<double>(stats.recognizedCandidates), "items"});
    result.measurements.push_back({"ocrWorkers", static_cast<double>(stats.workers), "threads"});
    result.measurements.push_back({"ocrPreprocessMs", stats.preprocessMs, "ms"});
    result.measurements.push_back({"ocrDetectMs", stats.detectMs, "ms"});
    result.measurements.push_back({"ocrPostprocessMs", stats.postprocessMs, "ms"});
    result.measurements.push_back({"ocrRecognizeMs", stats.recognizeMs, "ms"});
    result.measurements.push_back({"ocrRoiPadding", static_cast<double>(roi.empty() ? 0 : roiPadding), "px"});
    AddOCRRectMeasurements(result, roi, inputRect, ctx.image);

    cacheValid = true;
    cacheImageVersion = ctx.imageVersion;
    cacheImageData = ctx.image.data;
    cacheImageRows = ctx.image.rows;
    cacheImageCols = ctx.image.cols;
    cacheImageType = ctx.image.type();
    cacheRoi = inputRect;
    cacheConfigKey = configKey;
    cacheResult = result;
    return result;
}

nlohmann::json OCRTool::Save() const
{
    return {
        {"type", GetType()},
        {"detModelPath", detModelPath},
        {"detParamPath", detParamPath},
        {"recModelPath", recModelPath},
        {"recParamPath", recParamPath},
        {"dictionaryPath", dictionaryPath},
        {"minConfidence", minConfidence},
        {"maxItems", maxItems},
        {"inputSize", inputSize},
        {"maxCandidates", maxCandidates},
        {"minBoxArea", minBoxArea},
        {"minBoxHeight", minBoxHeight},
        {"roiPadding", roiPadding},
        {"fastMode", fastMode},
        {"detectOnly", detectOnly},
        {"useROI", useROI}
    };
}

void OCRTool::Load(const nlohmann::json& j)
{
    cacheValid = false;
    cacheResult = ToolResult{};
    detModelPath = j.value("detModelPath", "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin");
    detParamPath = j.value("detParamPath", "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param");
    recModelPath = j.value("recModelPath", "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin");
    recParamPath = j.value("recParamPath", "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param");
    dictionaryPath = j.value("dictionaryPath", "models\\ppocrv6\\ppocr_keys_v6_tiny.txt");
    minConfidence = j.value("minConfidence", 0.30f);
    maxItems = std::clamp(j.value("maxItems", 8), 1, 1000);
    inputSize = std::max(32, j.value("inputSize", 512));
    maxCandidates = std::clamp(j.value("maxCandidates", 220), 1, 2000);
    minBoxArea = std::max(0, j.value("minBoxArea", 0));
    minBoxHeight = std::max(0, j.value("minBoxHeight", 0));
    roiPadding = std::clamp(j.value("roiPadding", 24), 0, 256);
    fastMode = j.value("fastMode", true);
    detectOnly = j.value("detectOnly", false);
    useROI = j.value("useROI", true);
}
