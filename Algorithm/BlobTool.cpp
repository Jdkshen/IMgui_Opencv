#define NOMINMAX
#include "BlobTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

#include <algorithm>
#include <cmath>

nlohmann::json BlobTool::Save() const
{
    return {
        {"type", 2}, {"minArea", minArea}, {"maxArea", maxArea},
        {"thresholdMode", thresholdMode}, {"threshold", threshold},
        {"invert", invert}, {"connectivity", connectivity},
        {"minCircularity", minCircularity}, {"maxCircularity", maxCircularity},
        {"minAspectRatio", minAspectRatio}, {"maxAspectRatio", maxAspectRatio},
        {"showLabels", showLabels}
    };
}

void BlobTool::Load(const nlohmann::json &j)
{
    minArea = j.value("minArea", 100);
    maxArea = j.value("maxArea", 10000);
    thresholdMode = std::clamp(j.value("thresholdMode", 0), 0, 1);
    threshold = std::clamp(j.value("threshold", 128), 0, 255);
    invert = j.value("invert", false);
    connectivity = j.value("connectivity", 8) == 4 ? 4 : 8;
    minCircularity = std::clamp(j.value("minCircularity", 0.0f), 0.0f, 1.0f);
    maxCircularity = std::clamp(j.value("maxCircularity", 1.0f), minCircularity, 1.0f);
    minAspectRatio = (std::max)(0.0f, j.value("minAspectRatio", 0.0f));
    maxAspectRatio = (std::max)(minAspectRatio, j.value("maxAspectRatio", 100.0f));
    showLabels = j.value("showLabels", true);
}

ToolResult BlobTool::Execute(VisionContext &ctx)
{
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    cv::Mat gray = ToolImageUtils::ToGray(input);

    cv::Mat bin;
    const int thresholdType = invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
    cv::threshold(gray, bin, threshold, 255,
        thresholdMode == 0 ? thresholdType | cv::THRESH_OTSU : thresholdType);
    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(bin, labels, stats, centroids,
        connectivity == 4 ? 4 : 8, CV_32S);
    result.success = true;
    double areaSum = 0.0;
    double circularitySum = 0.0;
    double aspectRatioSum = 0.0;
    double angleSum = 0.0;
    for (int i = 1; i < count; ++i)
    {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < minArea || area > maxArea)
            continue;

        cv::Mat componentMask = labels == i;
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(componentMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty())
            continue;

        const auto contourIt = std::max_element(contours.begin(), contours.end(),
            [](const auto& left, const auto& right)
            {
                return cv::contourArea(left) < cv::contourArea(right);
            });
        const std::vector<cv::Point>& contour = *contourIt;
        const double perimeter = cv::arcLength(contour, true);
        const double contourArea = std::abs(cv::contourArea(contour));
        const float circularity = perimeter > 1e-6
            ? static_cast<float>(4.0 * CV_PI * contourArea / (perimeter * perimeter))
            : 0.0f;
        const cv::RotatedRect rotated = cv::minAreaRect(contour);
        const float longSide = (std::max)(rotated.size.width, rotated.size.height);
        const float shortSide = (std::min)(rotated.size.width, rotated.size.height);
        const float aspectRatio = shortSide > 1e-6f ? longSide / shortSide : 0.0f;
        if (circularity < minCircularity || circularity > maxCircularity ||
            aspectRatio < minAspectRatio || aspectRatio > maxAspectRatio)
            continue;

        ToolResult::Region reg;
        reg.bbox = cv::Rect(
            stats.at<int>(i, cv::CC_STAT_LEFT) + roi.x,
            stats.at<int>(i, cv::CC_STAT_TOP) + roi.y,
            stats.at<int>(i, cv::CC_STAT_WIDTH),
            stats.at<int>(i, cv::CC_STAT_HEIGHT));
        reg.center = cv::Point2f(
            static_cast<float>(centroids.at<double>(i, 0) + roi.x),
            static_cast<float>(centroids.at<double>(i, 1) + roi.y));
        reg.area = static_cast<float>(area);
        reg.score = 1.0f;
        reg.width = static_cast<float>(reg.bbox.width);
        reg.height = static_cast<float>(reg.bbox.height);
        reg.circularity = circularity;
        reg.aspectRatio = aspectRatio;
        reg.angle = rotated.angle;
        reg.contour.reserve(contour.size());
        for (const cv::Point& point : contour)
            reg.contour.emplace_back(point.x + roi.x, point.y + roi.y);
        if (showLabels)
            reg.label = "Blob " + std::to_string(area);
        result.regions.push_back(reg);

        areaSum += reg.area;
        circularitySum += reg.circularity;
        aspectRatioSum += reg.aspectRatio;
        angleSum += reg.angle;
    }
    const double validCount = static_cast<double>(result.regions.size());
    result.measurements.push_back({"blobCandidateCount", static_cast<double>((std::max)(0, count - 1)), "count"});
    result.measurements.push_back({"blobCount", static_cast<double>(result.regions.size()), ""});
    result.measurements.push_back({"blobValidCount", validCount, "count"});
    result.measurements.push_back({"blobMeanArea", validCount > 0.0 ? areaSum / validCount : 0.0, "px2"});
    result.measurements.push_back({"blobMeanCircularity", validCount > 0.0 ? circularitySum / validCount : 0.0, "ratio"});
    result.measurements.push_back({"blobMeanAspectRatio", validCount > 0.0 ? aspectRatioSum / validCount : 0.0, "ratio"});
    result.measurements.push_back({"blobMeanAngle", validCount > 0.0 ? angleSum / validCount : 0.0, "deg"});
    return result;
}
