#define NOMINMAX
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

#include <opencv2/imgproc.hpp>

namespace ToolImageUtils
{
cv::Rect PrimaryContextRect(const VisionContext& ctx, bool enabled)
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

cv::Mat ToGray(const cv::Mat& src)
{
    if (src.empty())
        return {};
    cv::Mat gray;
    if (src.channels() == 4)
        cv::cvtColor(src, gray, cv::COLOR_BGRA2GRAY);
    else if (src.channels() == 3)
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else
        gray = src;
    return gray;
}
}
