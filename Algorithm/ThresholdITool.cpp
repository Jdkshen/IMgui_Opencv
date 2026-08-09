#define NOMINMAX
#include "ThresholdTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

namespace
{
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
        if (k < 3)
            k = 3;
        cv::GaussianBlur(result, result, cv::Size(k, k), 0);
    }
    if (pipe.enableCanny)
    {
        cv::Mat gray = ToolImageUtils::ToGray(result);
        cv::Canny(gray, result, pipe.cannyLow, pipe.cannyHigh);
    }
    else if (pipe.enableThreshold)
    {
        cv::Mat gray = ToolImageUtils::ToGray(result);
        cv::threshold(gray, result, pipe.threshold, 255, cv::THRESH_BINARY);
    }

    return result;
}

cv::Mat ApplyPipelineToContextImage(const VisionContext& ctx, bool useGray, const PipelineState& pipe)
{
    if (ctx.image.empty())
        return {};

    cv::Mat out = ctx.image.clone();
    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    const cv::Mat domainMask = ToolImageUtils::PrimaryContextMask(ctx);
    cv::Mat dst = ApplyPipelineToMat(roi.empty() ? out : out(roi), useGray, pipe);
    if (dst.empty())
        return {};
    ToolImageUtils::ApplyDomainMask(dst, domainMask);

    if (!roi.empty())
    {
        cv::Mat converted;
        if (!ToolImageUtils::ConvertForCopyTo(dst, out.channels(), converted))
            return {};
        if (domainMask.empty())
            converted.copyTo(out(roi));
        else
            converted.copyTo(out(roi), domainMask);
    }
    else
    {
        if (domainMask.empty())
            out = dst;
        else
        {
            cv::Mat converted;
            if (!ToolImageUtils::ConvertForCopyTo(dst, out.channels(), converted))
                return {};
            converted.copyTo(out, domainMask);
        }
    }

    return out;
}
}

nlohmann::json ThresholdITool::Save() const
{
    return {{"type", 3}, {"useGray", useGray}, {"enableBlur", enableBlur}, {"blurSize", blurSize},
        {"enableThreshold", enableThreshold}, {"threshold", threshold}, {"enableCanny", enableCanny},
        {"cannyLow", cannyLow}, {"cannyHigh", cannyHigh}};
}

void ThresholdITool::Load(const nlohmann::json& j)
{
    useGray = j.value("useGray", false);
    enableBlur = j.value("enableBlur", false);
    blurSize = j.value("blurSize", 5);
    enableThreshold = j.value("enableThreshold", false);
    threshold = j.value("threshold", 128);
    enableCanny = j.value("enableCanny", false);
    cannyLow = j.value("cannyLow", 50);
    cannyHigh = j.value("cannyHigh", 150);
}

ToolResult ThresholdITool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = GetName();
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, true, r.message))
    {
        r.success = false;
        return r;
    }
    PipelineState pipe;
    pipe.enableBlur = enableBlur;
    pipe.blurSize = blurSize;
    pipe.enableThreshold = enableThreshold;
    pipe.threshold = threshold;
    pipe.enableCanny = enableCanny;
    pipe.cannyLow = cannyLow;
    pipe.cannyHigh = cannyHigh;
    r.debugImage = ApplyPipelineToContextImage(ctx, useGray, pipe);
    r.success = !r.debugImage.empty();
    if (!r.success)
        r.message = "图像预处理失败";
    return r;
}
