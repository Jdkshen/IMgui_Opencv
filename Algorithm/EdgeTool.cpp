#define NOMINMAX
#include "EdgeTool.h"
#include "ThresholdTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

namespace
{
cv::Mat ApplyEdgeToMat(const cv::Mat& src, bool useGray, int cannyLow, int cannyHigh)
{
    if (src.empty())
        return {};

    cv::Mat input;
    if (useGray)
    {
        if (src.channels() == 4)
            cv::cvtColor(src, input, cv::COLOR_BGRA2GRAY);
        else if (src.channels() == 3)
            cv::cvtColor(src, input, cv::COLOR_BGR2GRAY);
        else
            input = src.clone();
    }
    else
    {
        input = src.clone();
    }

    cv::Mat gray = ToolImageUtils::ToGray(input);

    cv::Mat edges;
    cv::Canny(gray, edges, cannyLow, cannyHigh);
    return edges;
}

cv::Mat ApplyEdgeToContextImage(const VisionContext& ctx, bool useGray, int cannyLow, int cannyHigh)
{
    if (ctx.image.empty())
        return {};

    cv::Mat out = ctx.image.clone();
    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    const cv::Mat domainMask = ToolImageUtils::PrimaryContextMask(ctx);
    cv::Mat dst = ApplyEdgeToMat(roi.empty() ? out : out(roi), useGray, cannyLow, cannyHigh);
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

nlohmann::json EdgeTool::Save() const
{
    return {{"type", 0}, {"cannyLow", cannyLow}, {"cannyHigh", cannyHigh}, {"useGray", useGray}};
}

void EdgeTool::Load(const nlohmann::json& j)
{
    cannyLow = j.value("cannyLow", 50);
    cannyHigh = j.value("cannyHigh", 150);
    useGray = j.value("useGray", false);
}

ToolResult EdgeTool::Execute(VisionContext& ctx)
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
    r.debugImage = ApplyEdgeToContextImage(ctx, useGray, cannyLow, cannyHigh);
    r.success = !r.debugImage.empty();
    if (!r.success)
        r.message = "边缘处理失败";
    return r;
}
