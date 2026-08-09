#include "QRCodeTool.h"

#include "ToolImageUtils.h"
#include "../Core/BarcodeBenchmark.h"
#include "../Core/VisionContext.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <unordered_set>

namespace
{
struct DecodeAttempt
{
    BarcodeBenchmarkResult result;
    float imageScale = 1.0f;
};

cv::Rect ToImageRect(const cv::Rect2f& box, float imageScale, cv::Point offset, cv::Size imageSize)
{
    if (box.width <= 0.0f || box.height <= 0.0f || imageScale <= 0.0f)
        return {};

    const float invScale = 1.0f / imageScale;
    const int left = static_cast<int>(std::floor(box.x * invScale)) + offset.x;
    const int top = static_cast<int>(std::floor(box.y * invScale)) + offset.y;
    const int right = static_cast<int>(std::ceil((box.x + box.width) * invScale)) + offset.x;
    const int bottom = static_cast<int>(std::ceil((box.y + box.height) * invScale)) + offset.y;
    return cv::Rect(left, top, (std::max)(0, right - left), (std::max)(0, bottom - top)) &
        cv::Rect(0, 0, imageSize.width, imageSize.height);
}

std::vector<cv::Point> ToImageContour(
    const BarcodeBenchmarkItem& item,
    float imageScale,
    cv::Point offset,
    cv::Size imageSize)
{
    std::vector<cv::Point> contour;
    if (!item.hasQuad || imageScale <= 0.0f)
        return contour;

    contour.reserve(item.quad.size());
    const float invScale = 1.0f / imageScale;
    for (const cv::Point2f& point : item.quad)
    {
        const int x = std::clamp(
            static_cast<int>(std::lround(point.x * invScale)) + offset.x,
            0, (std::max)(0, imageSize.width - 1));
        const int y = std::clamp(
            static_cast<int>(std::lround(point.y * invScale)) + offset.y,
            0, (std::max)(0, imageSize.height - 1));
        contour.emplace_back(x, y);
    }
    return contour;
}

DecodeAttempt RunOnce(
    const cv::Mat& image,
    BarcodeBackendId backend,
    const BarcodeBackendOptions& options,
    float imageScale = 1.0f)
{
    DecodeAttempt attempt;
    attempt.result = BarcodeBenchmark::RunBackend(image, backend, options);
    attempt.imageScale = imageScale;
    return attempt;
}

DecodeAttempt RunWithEnhancement(
    const cv::Mat& input,
    BarcodeBackendId backend,
    const BarcodeBackendOptions& options,
    bool enhance)
{
    DecodeAttempt attempt = RunOnce(input, backend, options);
    if (!attempt.result.items.empty() || !enhance || input.empty())
        return attempt;

    cv::Mat gray = ToolImageUtils::ToGray(input);
    if (gray.empty())
        return attempt;

    cv::Mat contrast;
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.5, cv::Size(8, 8));
    clahe->apply(gray, contrast);
    DecodeAttempt enhanced = RunOnce(contrast, backend, options);
    if (!enhanced.result.items.empty())
        return enhanced;

    cv::Mat binary;
    cv::adaptiveThreshold(
        contrast, binary, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY, 31, 5);
    enhanced = RunOnce(binary, backend, options);
    if (!enhanced.result.items.empty())
        return enhanced;

    const int maxDimension = (std::max)(input.cols, input.rows);
    if (maxDimension > 0 && maxDimension < 1800)
    {
        cv::Mat enlarged;
        cv::resize(contrast, enlarged, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
        enhanced = RunOnce(enlarged, backend, options, 2.0f);
        if (!enhanced.result.items.empty())
            return enhanced;
    }

    return attempt;
}

DecodeAttempt RunSelectedBackend(
    const cv::Mat& input,
    int engine,
    const BarcodeBackendOptions& options,
    bool enhance)
{
    if (engine == 1)
        return RunWithEnhancement(input, BarcodeBackendId::OpenCV, options, enhance);
    if (engine == 2)
        return RunWithEnhancement(input, BarcodeBackendId::ZXingCpp, options, enhance);

    DecodeAttempt zxing = RunWithEnhancement(input, BarcodeBackendId::ZXingCpp, options, enhance);
    if (!zxing.result.items.empty())
        return zxing;
    return RunWithEnhancement(input, BarcodeBackendId::OpenCV, options, enhance);
}
}

nlohmann::json QRCodeTool::Save() const
{
    return {
        {"type", GetType()},
        {"useROI", useROI},
        {"detectMulti", detectMulti},
        {"enhance", enhance},
        {"minSize", minSize},
        {"showText", showText},
        {"engine", engine},
        {"formatMask", formatMask},
        {"filterDuplicates", filterDuplicates},
    };
}

void QRCodeTool::Load(const nlohmann::json& j)
{
    useROI = j.value("useROI", true);
    detectMulti = j.value("detectMulti", true);
    enhance = j.value("enhance", true);
    minSize = j.value("minSize", 24);
    showText = j.value("showText", true);
    engine = std::clamp(j.value("engine", 0), 0, 2);
    formatMask = j.value("formatMask", static_cast<std::uint32_t>(BarcodeFormatAll));
    filterDuplicates = j.value("filterDuplicates", true);
}

ToolResult QRCodeTool::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, useROI, result.message))
    {
        result.success = false;
        return result;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx, useROI);

    const cv::Mat inputView = roi.empty() ? ctx.image : ctx.image(roi);
    cv::Mat input = inputView;
    const cv::Mat domainMask = ToolImageUtils::PrimaryContextMask(ctx, useROI);
    if (!domainMask.empty())
    {
        input = inputView.clone();
        ToolImageUtils::ApplyDomainMask(input, domainMask);
    }
    const cv::Point offset = roi.empty() ? cv::Point() : roi.tl();
    BarcodeBackendOptions options;
    options.tryHarder = enhance;
    options.maxSymbols = detectMulti ? 32 : 1;
    options.formatMask = formatMask;

    DecodeAttempt attempt = RunSelectedBackend(
        input, std::clamp(engine, 0, 2), options, enhance);

    const int minimumSize = (std::max)(1, minSize);
    std::unordered_set<std::string> decodedKeys;
    for (const BarcodeBenchmarkItem& item : attempt.result.items)
    {
        cv::Rect box = ToImageRect(item.bbox, attempt.imageScale, offset, ctx.image.size());
        if (box.width < minimumSize || box.height < minimumSize)
            continue;
        const cv::Point2f localCenter(
            box.x - offset.x + box.width * 0.5f,
            box.y - offset.y + box.height * 0.5f);
        if (!ToolImageUtils::PointInDomain(domainMask, localCenter))
            continue;
        const std::string duplicateKey = item.format + "\n" + item.text;
        if (filterDuplicates && !decodedKeys.insert(duplicateKey).second)
            continue;

        ToolResult::TextItem text;
        text.text = item.text;
        text.box = box;
        text.confidence = 1.0f;
        result.texts.push_back(std::move(text));

        ToolResult::Region region;
        region.contour = ToImageContour(item, attempt.imageScale, offset, ctx.image.size());
        region.bbox = box;
        region.area = static_cast<float>(box.area());
        region.score = 1.0f;
        region.label = showText ? (item.format + ": " + item.text) : "";
        result.regions.push_back(std::move(region));

        if (!detectMulti)
            break;
    }

    result.success = !result.texts.empty();
    result.message = attempt.result.backendName;
    if (!result.success)
    {
        if (!attempt.result.message.empty())
            result.message += ": " + attempt.result.message;
        else
            result.message += ": 未识别到二维码";
    }
    result.measurements.push_back({"barcodeCount", static_cast<double>(result.texts.size()), ""});
    result.measurements.push_back({"backendMs", attempt.result.elapsedMs, "ms"});
    result.measurements.push_back({"roiWidth", static_cast<double>(input.cols), "px"});
    result.measurements.push_back({"roiHeight", static_cast<double>(input.rows), "px"});
    return result;
}
