#pragma once

#include "ROI.h"
#include "../Algorithm/ToolResult.h"

#include <opencv2/core/mat.hpp>

namespace RotatedROI
{
struct Transform
{
    cv::Mat sourceToCrop;
    cv::Mat cropToSource;
    cv::Size cropSize;

    bool IsValid() const;
};

bool Extract(const cv::Mat& source, const ROI& roi, cv::Mat& crop, Transform& transform);
cv::Point2f MapPoint(const cv::Point2f& point, const cv::Mat& affine);
ROI RestoreROI(const ROI& roi, const Transform& transform);
void RestoreResult(ToolResult& result, const Transform& transform);
bool RestoreDebugImage(const cv::Mat& crop, const cv::Mat& source,
                       const Transform& transform, cv::Mat& restored);
}
