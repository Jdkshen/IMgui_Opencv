#pragma once

#include "ROI.h"
#include "../Algorithm/ToolResult.h"

#include <vector>
#include <cstdint>

struct FixturePose
{
    bool valid = false;
    cv::Point2f origin;
    float angleDegrees = 0.0f;
};

struct FixtureSettings
{
    bool enabled = false;
    int sourceToolIndex = -1;
    std::uint64_t sourceToolId = 0;
    int resultIndex = 0;
    cv::Point2f referenceOrigin;
    float referenceAngleDegrees = 0.0f;
    bool failOnMissing = true;
};

namespace FixtureTransform
{
    bool TryExtractPose(const ToolResult& result, int resultIndex, FixturePose& pose);
    cv::Point2f TransformPoint(cv::Point2f point, const FixturePose& reference, const FixturePose& current);
    ROI TransformROI(const ROI& roi, const FixturePose& reference, const FixturePose& current);
    std::vector<ROI> TransformROIs(
        const std::vector<ROI>& rois,
        const FixturePose& reference,
        const FixturePose& current);
}
