#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

struct VisionContext;

namespace ToolImageUtils
{
    cv::Rect PrimaryContextRect(const VisionContext& ctx, bool enabled = true);
    bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst);
    cv::Mat ToGray(const cv::Mat& src);
}
