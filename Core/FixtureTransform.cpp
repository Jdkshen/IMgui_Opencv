#include "FixtureTransform.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

cv::Point2f ToPoint(const ImVec2& point)
{
    return {point.x, point.y};
}

ImVec2 ToImVec(cv::Point2f point)
{
    return {point.x, point.y};
}
}

namespace FixtureTransform
{
bool TryExtractPose(const ToolResult& result, int resultIndex, FixturePose& pose)
{
    pose = {};
    if (resultIndex < 0)
        return false;

    if (resultIndex < static_cast<int>(result.regions.size()))
    {
        const auto& region = result.regions[resultIndex];
        pose.origin = {
            region.bbox.x + region.bbox.width * 0.5f,
            region.bbox.y + region.bbox.height * 0.5f,
        };
        pose.angleDegrees = region.angle;
        pose.valid = region.bbox.width > 0 && region.bbox.height > 0;
        return pose.valid;
    }

    if (resultIndex < static_cast<int>(result.detections.size()))
    {
        const auto& detection = result.detections[resultIndex];
        pose.origin = {
            detection.box.x + detection.box.width * 0.5f,
            detection.box.y + detection.box.height * 0.5f,
        };
        pose.valid = detection.box.width > 0 && detection.box.height > 0;
        return pose.valid;
    }

    if (resultIndex < static_cast<int>(result.lines.size()))
    {
        const auto& line = result.lines[resultIndex];
        pose.origin = cv::Point2f(
            (line.p1.x + line.p2.x) * 0.5f,
            (line.p1.y + line.p2.y) * 0.5f);
        pose.angleDegrees = line.angle;
        pose.valid = line.p1 != line.p2;
        return pose.valid;
    }
    return false;
}

cv::Point2f TransformPoint(cv::Point2f point, const FixturePose& reference, const FixturePose& current)
{
    if (!reference.valid || !current.valid)
        return point;
    const float radians = (current.angleDegrees - reference.angleDegrees) * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const cv::Point2f local = point - reference.origin;
    return current.origin + cv::Point2f(
        local.x * cosine - local.y * sine,
        local.x * sine + local.y * cosine);
}

ROI TransformROI(const ROI& roi, const FixturePose& reference, const FixturePose& current)
{
    ROI transformed = roi;
    if (!reference.valid || !current.valid)
        return transformed;

    if (roi.type == ROI_TYPE_POINT || roi.type == ROI_TYPE_LINE)
    {
        transformed.start = ToImVec(TransformPoint(ToPoint(roi.start), reference, current));
        transformed.end = ToImVec(TransformPoint(ToPoint(roi.end), reference, current));
        return transformed;
    }

    if (roi.type == ROI_TYPE_CIRCLE)
    {
        const cv::Point2f center = TransformPoint(ToPoint(roi.start), reference, current);
        const float radius = roi.CircleRadius();
        transformed.start = ToImVec(center);
        transformed.end = ToImVec(center + cv::Point2f(radius, 0.0f));
        return transformed;
    }

    std::vector<cv::Point2f> points;
    if (roi.type == ROI_TYPE_POLYGON)
    {
        points.reserve(roi.points.size());
        for (const ImVec2& point : roi.points)
            points.push_back(TransformPoint(ToPoint(point), reference, current));
    }
    else
    {
        const float left = (std::min)(roi.start.x, roi.end.x);
        const float top = (std::min)(roi.start.y, roi.end.y);
        const float right = (std::max)(roi.start.x, roi.end.x);
        const float bottom = (std::max)(roi.start.y, roi.end.y);
        points = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        for (cv::Point2f& point : points)
            point = TransformPoint(point, reference, current);
    }

    transformed.type = ROI_TYPE_POLYGON;
    transformed.points.clear();
    if (points.empty())
        return transformed;
    float minX = points.front().x;
    float minY = points.front().y;
    float maxX = points.front().x;
    float maxY = points.front().y;
    for (const cv::Point2f& point : points)
    {
        transformed.points.push_back(ToImVec(point));
        minX = (std::min)(minX, point.x);
        minY = (std::min)(minY, point.y);
        maxX = (std::max)(maxX, point.x);
        maxY = (std::max)(maxY, point.y);
    }
    transformed.start = {minX, minY};
    transformed.end = {maxX, maxY};
    return transformed;
}

std::vector<ROI> TransformROIs(
    const std::vector<ROI>& rois,
    const FixturePose& reference,
    const FixturePose& current)
{
    std::vector<ROI> transformed;
    transformed.reserve(rois.size());
    for (const ROI& roi : rois)
        transformed.push_back(TransformROI(roi, reference, current));
    return transformed;
}
}
