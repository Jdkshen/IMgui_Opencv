#include "ResultROIResolver.h"

#include <algorithm>

namespace
{
ROI RectToROI(cv::Rect rect, cv::Size imageSize)
{
    if (imageSize.width > 0 && imageSize.height > 0)
        rect &= cv::Rect(0, 0, imageSize.width, imageSize.height);

    ROI roi;
    roi.type = ROI_TYPE_RECT;
    roi.start = ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y));
    roi.end = ImVec2(static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height));
    return roi;
}

std::vector<cv::Rect> PrimaryBoxes(const ToolResult& result)
{
    std::vector<cv::Rect> boxes;
    if (!result.detections.empty())
    {
        boxes.reserve(result.detections.size());
        for (const auto& item : result.detections)
            boxes.push_back(item.box);
    }
    else if (!result.regions.empty())
    {
        boxes.reserve(result.regions.size());
        for (const auto& item : result.regions)
            boxes.push_back(item.bbox);
    }
    else
    {
        boxes.reserve(result.texts.size());
        for (const auto& item : result.texts)
            boxes.push_back(item.box);
    }
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [](const cv::Rect& box)
    {
        return box.width <= 0 || box.height <= 0;
    }), boxes.end());
    return boxes;
}
}

namespace ResultROIResolver
{
ResultROIResolution Resolve(const ToolResult& source, const ResultROIRequest& request, cv::Size imageSize)
{
    ResultROIResolution resolution;
    if (request.mode == ResultROIMode::Disabled)
    {
        resolution.available = true;
        return resolution;
    }

    const std::vector<cv::Rect> boxes = PrimaryBoxes(source);
    if (boxes.empty())
    {
        resolution.reason = "上游工具没有可用检测框";
        return resolution;
    }

    if (request.mode == ResultROIMode::NthResult)
    {
        const int index = (std::max)(0, request.resultIndex);
        if (index >= static_cast<int>(boxes.size()))
        {
            resolution.reason = "上游结果数量不足，无法取得指定序号";
            return resolution;
        }
        resolution.rois.push_back(RectToROI(boxes[index], imageSize));
    }
    else
    {
        resolution.rois.reserve(boxes.size());
        for (const cv::Rect& box : boxes)
            resolution.rois.push_back(RectToROI(box, imageSize));
    }

    resolution.available = !resolution.rois.empty();
    return resolution;
}
}

