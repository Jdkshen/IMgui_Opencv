#define NOMINMAX
#include "RotatedROI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
bool IsFiniteAffine(const cv::Mat& matrix)
{
    if (matrix.rows != 2 || matrix.cols != 3 || matrix.type() != CV_64F)
        return false;
    for (int row = 0; row < matrix.rows; ++row)
        for (int column = 0; column < matrix.cols; ++column)
            if (!std::isfinite(matrix.at<double>(row, column)))
                return false;
    const double determinant = matrix.at<double>(0, 0) * matrix.at<double>(1, 1) -
                               matrix.at<double>(0, 1) * matrix.at<double>(1, 0);
    return std::isfinite(determinant) && std::abs(determinant) > 1e-12;
}

cv::Point MapIntegerPoint(const cv::Point& point, const cv::Mat& affine)
{
    const cv::Point2f mapped = RotatedROI::MapPoint(
        cv::Point2f(static_cast<float>(point.x), static_cast<float>(point.y)), affine);
    return cv::Point(cvRound(mapped.x), cvRound(mapped.y));
}

cv::Rect MapRect(const cv::Rect& rect, const cv::Mat& affine)
{
    if (rect.width <= 0 || rect.height <= 0)
        return {};
    const std::array<cv::Point2f, 4> points = {
        RotatedROI::MapPoint(cv::Point2f(static_cast<float>(rect.x),
                                        static_cast<float>(rect.y)), affine),
        RotatedROI::MapPoint(cv::Point2f(static_cast<float>(rect.x + rect.width),
                                        static_cast<float>(rect.y)), affine),
        RotatedROI::MapPoint(cv::Point2f(static_cast<float>(rect.x + rect.width),
                                        static_cast<float>(rect.y + rect.height)), affine),
        RotatedROI::MapPoint(cv::Point2f(static_cast<float>(rect.x),
                                        static_cast<float>(rect.y + rect.height)), affine)};
    float minX = points[0].x;
    float maxX = points[0].x;
    float minY = points[0].y;
    float maxY = points[0].y;
    for (const cv::Point2f& point : points)
    {
        minX = (std::min)(minX, point.x);
        maxX = (std::max)(maxX, point.x);
        minY = (std::min)(minY, point.y);
        maxY = (std::max)(maxY, point.y);
    }
    const int left = static_cast<int>(std::floor(minX));
    const int top = static_cast<int>(std::floor(minY));
    const int right = static_cast<int>(std::ceil(maxX));
    const int bottom = static_cast<int>(std::ceil(maxY));
    return cv::Rect(left, top, (std::max)(0, right - left),
                    (std::max)(0, bottom - top));
}

float AffineRotationDegrees(const cv::Mat& affine)
{
    return static_cast<float>(std::atan2(affine.at<double>(1, 0),
                                         affine.at<double>(0, 0)) * 180.0 / CV_PI);
}

float NormalizeAngle(float angle)
{
    while (angle > 180.0f) angle -= 360.0f;
    while (angle <= -180.0f) angle += 360.0f;
    return angle;
}

bool ConvertChannels(const cv::Mat& source, int channels, cv::Mat& converted)
{
    if (source.empty())
        return false;
    if (source.channels() == channels)
    {
        converted = source;
        return true;
    }
    if (source.channels() == 1 && channels == 3)
        cv::cvtColor(source, converted, cv::COLOR_GRAY2BGR);
    else if (source.channels() == 1 && channels == 4)
        cv::cvtColor(source, converted, cv::COLOR_GRAY2BGRA);
    else if (source.channels() == 3 && channels == 1)
        cv::cvtColor(source, converted, cv::COLOR_BGR2GRAY);
    else if (source.channels() == 3 && channels == 4)
        cv::cvtColor(source, converted, cv::COLOR_BGR2BGRA);
    else if (source.channels() == 4 && channels == 1)
        cv::cvtColor(source, converted, cv::COLOR_BGRA2GRAY);
    else if (source.channels() == 4 && channels == 3)
        cv::cvtColor(source, converted, cv::COLOR_BGRA2BGR);
    else
        return false;
    return true;
}
}

namespace RotatedROI
{
bool Transform::IsValid() const
{
    return cropSize.width > 0 && cropSize.height > 0 &&
           IsFiniteAffine(sourceToCrop) && IsFiniteAffine(cropToSource) &&
           !domainMask.empty() && domainMask.type() == CV_8UC1 &&
           domainMask.size() == cropSize;
}

bool Extract(const cv::Mat& source, const ROI& roi, cv::Mat& crop, Transform& transform)
{
    crop.release();
    transform = {};
    const bool isAreaROI = roi.type == ROI_TYPE_RECT ||
                           roi.type == ROI_TYPE_CIRCLE ||
                           roi.type == ROI_TYPE_POLYGON;
    if (source.empty() || !isAreaROI || !std::isfinite(roi.angle) || roi.IsEmpty())
    {
        return false;
    }

    if (roi.type == ROI_TYPE_RECT && std::abs(roi.angle) >= 0.0001f)
    {
        if (roi.Width() < 1.0f || roi.Height() < 1.0f)
            return false;
        const int width = (std::max)(1, cvRound(roi.Width()));
        const int height = (std::max)(1, cvRound(roi.Height()));
        const auto corners = roi.Corners();
        const cv::Point2f sourcePoints[3] = {
            {corners[0].x, corners[0].y},
            {corners[1].x, corners[1].y},
            {corners[3].x, corners[3].y}};
        const cv::Point2f cropPoints[3] = {
            {0.0f, 0.0f}, {static_cast<float>(width), 0.0f},
            {0.0f, static_cast<float>(height)}};
        transform.sourceToCrop = cv::getAffineTransform(sourcePoints, cropPoints);
        transform.cropSize = cv::Size(width, height);
    }
    else
    {
        cv::Rect bounds = roi.ToCvRect() & cv::Rect(0, 0, source.cols, source.rows);
        if (bounds.width <= 0 || bounds.height <= 0)
            return false;
        transform.sourceToCrop = cv::Mat::zeros(2, 3, CV_64F);
        transform.sourceToCrop.at<double>(0, 0) = 1.0;
        transform.sourceToCrop.at<double>(1, 1) = 1.0;
        transform.sourceToCrop.at<double>(0, 2) = -static_cast<double>(bounds.x);
        transform.sourceToCrop.at<double>(1, 2) = -static_cast<double>(bounds.y);
        transform.cropSize = bounds.size();
    }
    cv::invertAffineTransform(transform.sourceToCrop, transform.cropToSource);

    transform.domainMask = cv::Mat(transform.cropSize, CV_8UC1, cv::Scalar(0));
    if (roi.type == ROI_TYPE_RECT)
    {
        transform.domainMask.setTo(255);
    }
    else if (roi.type == ROI_TYPE_CIRCLE)
    {
        const cv::Point2f center = MapPoint({roi.start.x, roi.start.y}, transform.sourceToCrop);
        cv::circle(transform.domainMask,
                   cv::Point(cvRound(center.x), cvRound(center.y)),
                   (std::max)(1, cvRound(roi.CircleRadius())), cv::Scalar(255), cv::FILLED,
                   cv::LINE_8);
    }
    else
    {
        std::vector<cv::Point> polygon;
        polygon.reserve(roi.points.size());
        for (const ImVec2& point : roi.points)
        {
            const cv::Point2f mapped = MapPoint({point.x, point.y}, transform.sourceToCrop);
            polygon.emplace_back(cvRound(mapped.x), cvRound(mapped.y));
        }
        if (polygon.size() < 3)
            return false;
        cv::fillPoly(transform.domainMask, std::vector<std::vector<cv::Point>>{polygon},
                     cv::Scalar(255), cv::LINE_8);
    }

    // HALCON reduce_domain uses the intersection with the image's existing domain.
    // This matters for a rotated rectangle that extends beyond the source image.
    cv::Mat sourceDomain(source.size(), CV_8UC1, cv::Scalar(255));
    cv::Mat sourceDomainInCrop;
    cv::warpAffine(sourceDomain, sourceDomainInCrop, transform.sourceToCrop,
                   transform.cropSize, cv::INTER_NEAREST, cv::BORDER_CONSTANT,
                   cv::Scalar(0));
    cv::bitwise_and(transform.domainMask, sourceDomainInCrop, transform.domainMask);

    if (!transform.IsValid() || cv::countNonZero(transform.domainMask) == 0)
    {
        transform = {};
        return false;
    }

    cv::warpAffine(source, crop, transform.sourceToCrop, transform.cropSize,
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    return !crop.empty();
}

cv::Mat BuildDomainMask(cv::Size imageSize, const std::vector<ROI>& rois)
{
    if (imageSize.width <= 0 || imageSize.height <= 0)
        return {};
    cv::Mat mask(imageSize, CV_8UC1, cv::Scalar(0));
    for (const ROI& roi : rois)
    {
        if (roi.type == ROI_TYPE_RECT && !roi.IsEmpty())
        {
            const auto corners = roi.Corners();
            std::vector<cv::Point> polygon;
            polygon.reserve(corners.size());
            for (const ImVec2& point : corners)
                polygon.emplace_back(cvRound(point.x), cvRound(point.y));
            cv::fillConvexPoly(mask, polygon, cv::Scalar(255), cv::LINE_8);
        }
        else if (roi.type == ROI_TYPE_CIRCLE && roi.CircleRadius() > 0.0f)
        {
            cv::circle(mask, cv::Point(cvRound(roi.start.x), cvRound(roi.start.y)),
                       cvRound(roi.CircleRadius()), cv::Scalar(255), cv::FILLED,
                       cv::LINE_8);
        }
        else if (roi.type == ROI_TYPE_POLYGON && roi.points.size() >= 3)
        {
            std::vector<cv::Point> polygon;
            polygon.reserve(roi.points.size());
            for (const ImVec2& point : roi.points)
                polygon.emplace_back(cvRound(point.x), cvRound(point.y));
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{polygon},
                         cv::Scalar(255), cv::LINE_8);
        }
    }
    return cv::countNonZero(mask) > 0 ? mask : cv::Mat();
}

cv::Point2f MapPoint(const cv::Point2f& point, const cv::Mat& affine)
{
    return cv::Point2f(
        static_cast<float>(affine.at<double>(0, 0) * point.x +
                           affine.at<double>(0, 1) * point.y +
                           affine.at<double>(0, 2)),
        static_cast<float>(affine.at<double>(1, 0) * point.x +
                           affine.at<double>(1, 1) * point.y +
                           affine.at<double>(1, 2)));
}

ROI ToCropROI(const ROI& roi, const Transform& transform)
{
    if (!transform.IsValid())
        return roi;

    ROI local = roi;
    if (roi.type == ROI_TYPE_RECT)
    {
        local.start = ImVec2(0.0f, 0.0f);
        local.end = ImVec2(static_cast<float>(transform.cropSize.width),
                           static_cast<float>(transform.cropSize.height));
        local.angle = 0.0f;
        return local;
    }

    const cv::Point2f start = MapPoint({roi.start.x, roi.start.y}, transform.sourceToCrop);
    const cv::Point2f end = MapPoint({roi.end.x, roi.end.y}, transform.sourceToCrop);
    local.start = ImVec2(start.x, start.y);
    local.end = ImVec2(end.x, end.y);
    for (ImVec2& point : local.points)
    {
        const cv::Point2f mapped = MapPoint({point.x, point.y}, transform.sourceToCrop);
        point = ImVec2(mapped.x, mapped.y);
    }
    return local;
}

ROI RestoreROI(const ROI& roi, const Transform& transform)
{
    if (!transform.IsValid())
        return roi;

    ROI restored = roi;
    const cv::Point2f start = MapPoint({roi.start.x, roi.start.y}, transform.cropToSource);
    const cv::Point2f end = MapPoint({roi.end.x, roi.end.y}, transform.cropToSource);
    if (roi.type == ROI_TYPE_RECT)
    {
        const cv::Point2f center = MapPoint(
            {roi.Center().x, roi.Center().y}, transform.cropToSource);
        const double scaleX = std::hypot(transform.cropToSource.at<double>(0, 0),
                                         transform.cropToSource.at<double>(1, 0));
        const double scaleY = std::hypot(transform.cropToSource.at<double>(0, 1),
                                         transform.cropToSource.at<double>(1, 1));
        const float halfWidth = static_cast<float>(roi.Width() * scaleX * 0.5);
        const float halfHeight = static_cast<float>(roi.Height() * scaleY * 0.5);
        restored.start = ImVec2(center.x - halfWidth, center.y - halfHeight);
        restored.end = ImVec2(center.x + halfWidth, center.y + halfHeight);
        restored.angle = NormalizeAngle(roi.angle + AffineRotationDegrees(transform.cropToSource));
    }
    else
    {
        restored.start = ImVec2(start.x, start.y);
        restored.end = ImVec2(end.x, end.y);
        for (ImVec2& point : restored.points)
        {
            const cv::Point2f mapped = MapPoint({point.x, point.y}, transform.cropToSource);
            point = ImVec2(mapped.x, mapped.y);
        }
    }
    return restored;
}

void RestoreResult(ToolResult& result, const Transform& transform)
{
    if (!transform.IsValid())
        return;

    const cv::Mat& affine = transform.cropToSource;
    const float rotation = AffineRotationDegrees(affine);
    const double areaScale = std::abs(
        affine.at<double>(0, 0) * affine.at<double>(1, 1) -
        affine.at<double>(0, 1) * affine.at<double>(1, 0));

    for (ToolResult::Region& region : result.regions)
    {
        const cv::Rect originalBox = region.bbox;
        for (cv::Point& point : region.contour)
            point = MapIntegerPoint(point, affine);
        region.bbox = !region.contour.empty()
            ? cv::boundingRect(region.contour)
            : MapRect(originalBox, affine);
        if (region.center == cv::Point2f() && originalBox.area() > 0)
        {
            region.center = MapPoint(
                cv::Point2f(originalBox.x + originalBox.width * 0.5f,
                            originalBox.y + originalBox.height * 0.5f), affine);
        }
        else
        {
            region.center = MapPoint(region.center, affine);
        }
        region.area = static_cast<float>(region.area * areaScale);
        region.angle = NormalizeAngle(region.angle + rotation);
    }

    for (ToolResult::Detection& detection : result.detections)
        detection.box = MapRect(detection.box, affine);

    for (ToolResult::Line& line : result.lines)
    {
        line.p1 = MapIntegerPoint(line.p1, affine);
        line.p2 = MapIntegerPoint(line.p2, affine);
        const float dx = static_cast<float>(line.p2.x - line.p1.x);
        const float dy = static_cast<float>(line.p2.y - line.p1.y);
        line.length = std::hypot(dx, dy);
        line.angle = NormalizeAngle(static_cast<float>(std::atan2(dy, dx) * 180.0 / CV_PI));
    }

    for (ToolResult::TextItem& text : result.texts)
        text.box = MapRect(text.box, affine);
}

bool RestoreDebugImage(const cv::Mat& crop, const cv::Mat& source,
                       const Transform& transform, cv::Mat& restored)
{
    restored.release();
    if (crop.empty() || source.empty() || !transform.IsValid() ||
        crop.size() != transform.cropSize)
        return false;

    cv::Mat converted;
    if (!ConvertChannels(crop, source.channels(), converted))
        return false;
    cv::Mat warped(source.size(), source.type(), cv::Scalar::all(0));
    cv::warpAffine(converted, warped, transform.cropToSource, source.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::Mat mask(source.size(), CV_8UC1, cv::Scalar(0));
    cv::warpAffine(transform.domainMask, mask, transform.cropToSource, source.size(),
                   cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
    restored = source.clone();
    warped.copyTo(restored, mask);
    return true;
}
}
