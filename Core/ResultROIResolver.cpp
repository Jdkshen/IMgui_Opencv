#include "ResultROIResolver.h"

#include <algorithm>

namespace
{
struct Candidate
{
    cv::Rect box;
    std::string category;
    int classId = -1;
    float score = 0.0f;
    float area = 0.0f;
    int sourceOrder = 0;
};

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

std::vector<Candidate> Candidates(const ToolResult& result)
{
    std::vector<Candidate> candidates;
    if (!result.detections.empty())
    {
        candidates.reserve(result.detections.size());
        for (int index = 0; index < static_cast<int>(result.detections.size()); ++index)
        {
            const auto& item = result.detections[index];
            candidates.push_back({item.box, item.label, item.classId, item.score,
                static_cast<float>(item.box.area()), index});
        }
    }
    else if (!result.regions.empty())
    {
        candidates.reserve(result.regions.size());
        for (int index = 0; index < static_cast<int>(result.regions.size()); ++index)
        {
            const auto& item = result.regions[index];
            const float area = item.area > 0.0f ? item.area : static_cast<float>(item.bbox.area());
            candidates.push_back({item.bbox, item.label, -1, item.score, area, index});
        }
    }
    else
    {
        candidates.reserve(result.texts.size());
        for (int index = 0; index < static_cast<int>(result.texts.size()); ++index)
        {
            const auto& item = result.texts[index];
            candidates.push_back({item.box, item.text, -1, item.confidence,
                static_cast<float>(item.box.area()), index});
        }
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [](const Candidate& candidate)
        {
            return candidate.box.width <= 0 || candidate.box.height <= 0;
        }), candidates.end());
    return candidates;
}

bool Matches(const Candidate& candidate, const ResultROIRequest& request)
{
    if (request.classId >= 0 && candidate.classId != request.classId)
        return false;
    if (!request.category.empty() && candidate.category != request.category)
        return false;
    if (request.minScore >= 0.0f && candidate.score < request.minScore)
        return false;
    if (request.minArea >= 0.0f && candidate.area < request.minArea)
        return false;
    return true;
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

    std::vector<Candidate> candidates = Candidates(source);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&request](const Candidate& candidate) { return !Matches(candidate, request); }), candidates.end());
    if (candidates.empty())
    {
        resolution.reason = "result ROI filter matched no results";
        return resolution;
    }

    const int sortMode = (std::clamp)(request.sortMode, 0, 2);
    if (sortMode != 0)
    {
        std::stable_sort(candidates.begin(), candidates.end(),
            [sortMode, descending = request.sortDescending](const Candidate& left, const Candidate& right)
            {
                float leftValue = sortMode == 1 ? left.score : left.area;
                float rightValue = sortMode == 1 ? right.score : right.area;
                if (leftValue == rightValue)
                    return left.sourceOrder < right.sourceOrder;
                return descending ? leftValue > rightValue : leftValue < rightValue;
            });
    }

    if (request.mode == ResultROIMode::NthResult)
    {
        const int index = (std::max)(0, request.resultIndex);
        if (index >= static_cast<int>(candidates.size()))
        {
            resolution.reason = "result ROI index is outside the filtered result set";
            return resolution;
        }
        resolution.rois.push_back(RectToROI(candidates[index].box, imageSize));
    }
    else
    {
        resolution.rois.reserve(candidates.size());
        for (const Candidate& candidate : candidates)
            resolution.rois.push_back(RectToROI(candidate.box, imageSize));
    }

    resolution.available = !resolution.rois.empty();
    return resolution;
}
}
