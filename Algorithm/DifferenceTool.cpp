#include "DifferenceTool.h"

#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

#include <algorithm>
#include <cmath>

nlohmann::json DifferenceTool::Save() const
{
    return {
        {"type", 16}, {"threshold", threshold}, {"minArea", minArea},
        {"blurSize", blurSize}, {"morphKernelSize", morphKernelSize},
        {"morphIterations", morphIterations}, {"invert", invert},
        {"showLabels", showLabels}
    };
}

void DifferenceTool::Load(const nlohmann::json& json)
{
    threshold = std::clamp(json.value("threshold", 30), 0, 255);
    minArea = (std::max)(1, json.value("minArea", 20));
    blurSize = std::clamp(json.value("blurSize", 0), 0, 31);
    morphKernelSize = std::clamp(json.value("morphKernelSize", 3), 1, 31);
    if ((morphKernelSize % 2) == 0)
        ++morphKernelSize;
    morphIterations = std::clamp(json.value("morphIterations", 1), 1, 10);
    invert = json.value("invert", false);
    showLabels = json.value("showLabels", true);
}

ToolResult DifferenceTool::Execute(VisionContext& context)
{
    ToolResult result;
    result.toolName = GetName();
    if (context.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }
    if (referenceImage.empty())
    {
        result.success = false;
        result.message = "请先抓取参考图";
        return result;
    }
    if (referenceImage.size() != context.image.size())
    {
        result.success = false;
        result.message = "参考图与当前图尺寸不一致";
        return result;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(context);
    const cv::Mat current = roi.empty() ? context.image : context.image(roi);
    const cv::Mat reference = roi.empty() ? referenceImage : referenceImage(roi);
    cv::Mat currentGray = ToolImageUtils::ToGray(current);
    cv::Mat referenceGray = ToolImageUtils::ToGray(reference);
    cv::Mat difference;
    cv::absdiff(currentGray, referenceGray, difference);
    if (blurSize > 0)
    {
        int kernel = blurSize | 1;
        cv::GaussianBlur(difference, difference, cv::Size(kernel, kernel), 0.0);
    }

    cv::Mat mask;
    const int type = invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
    cv::threshold(difference, mask, threshold, 255, type);
    int morphKernel = morphKernelSize | 1;
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(morphKernel, morphKernel)),
        cv::Point(-1, -1), morphIterations);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    result.success = true;
    double changedArea = 0.0;
    for (const auto& contour : contours)
    {
        const double area = std::abs(cv::contourArea(contour));
        if (area < minArea)
            continue;
        cv::Rect box = cv::boundingRect(contour);
        box.x += roi.x;
        box.y += roi.y;

        ToolResult::Region region;
        region.bbox = box;
        region.area = static_cast<float>(area);
        region.width = static_cast<float>(box.width);
        region.height = static_cast<float>(box.height);
        const cv::Moments moments = cv::moments(contour);
        region.center = moments.m00 > 1e-6
            ? cv::Point2f(static_cast<float>(moments.m10 / moments.m00 + roi.x),
                          static_cast<float>(moments.m01 / moments.m00 + roi.y))
            : cv::Point2f(static_cast<float>(box.x + box.width * 0.5f),
                          static_cast<float>(box.y + box.height * 0.5f));
        region.contour.reserve(contour.size());
        for (const auto& point : contour)
            region.contour.emplace_back(point.x + roi.x, point.y + roi.y);
        if (showLabels)
            region.label = "差异 " + std::to_string(static_cast<int>(area));
        changedArea += area;
        result.regions.push_back(std::move(region));
    }

    cv::Mat overlay;
    if (context.image.channels() == 1)
        cv::cvtColor(context.image, overlay, cv::COLOR_GRAY2BGR);
    else if (context.image.channels() == 4)
        cv::cvtColor(context.image, overlay, cv::COLOR_BGRA2BGR);
    else
        overlay = context.image.clone();
    for (const auto& region : result.regions)
        cv::rectangle(overlay, region.bbox, cv::Scalar(0, 0, 255), 2);
    result.debugImage = std::move(overlay);
    result.measurements.push_back({"differenceCount", static_cast<double>(result.regions.size()), ""});
    result.measurements.push_back({"differenceArea", changedArea, "px"});
    result.message = result.regions.empty() ? "未发现差异" : "发现差异";
    return result;
}
