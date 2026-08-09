#include "ResultROIResolver.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

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
    ToolSpatialResultChannel channel = ToolSpatialResultChannel::Auto;
    int channelIndex = -1;
    std::string kind = "区域";
    bool isLine = false;
    cv::Point lineStart;
    cv::Point lineEnd;
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

cv::Point ClampPoint(cv::Point point, cv::Size imageSize)
{
    if (imageSize.width > 0)
        point.x = std::clamp(point.x, 0, imageSize.width - 1);
    if (imageSize.height > 0)
        point.y = std::clamp(point.y, 0, imageSize.height - 1);
    return point;
}

ROI CandidateToROI(const Candidate& candidate, const ResultROIRequest& request,
    cv::Size imageSize)
{
    if (request.outputGeometry == ResultROIOutputGeometry::Bounds)
        return RectToROI(candidate.box, imageSize);

    ROI roi;
    if (candidate.isLine &&
        request.outputGeometry == ResultROIOutputGeometry::CenterPointsOrPreserveLines)
    {
        const cv::Point start = ClampPoint(candidate.lineStart, imageSize);
        const cv::Point end = ClampPoint(candidate.lineEnd, imageSize);
        roi.type = ROI_TYPE_LINE;
        roi.start = ImVec2(static_cast<float>(start.x), static_cast<float>(start.y));
        roi.end = ImVec2(static_cast<float>(end.x), static_cast<float>(end.y));
        return roi;
    }

    cv::Rect box = candidate.box;
    if (imageSize.width > 0 && imageSize.height > 0)
        box &= cv::Rect(0, 0, imageSize.width, imageSize.height);
    const ImVec2 center(
        static_cast<float>(box.x) + static_cast<float>(box.width) * 0.5f,
        static_cast<float>(box.y) + static_cast<float>(box.height) * 0.5f);
    roi.type = ROI_TYPE_POINT;
    roi.start = center;
    roi.end = center;
    return roi;
}

bool ChannelEnabled(ToolSpatialResultChannel requested,
    ToolSpatialResultChannel candidate)
{
    return requested == ToolSpatialResultChannel::Auto || requested == candidate;
}

std::vector<Candidate> Candidates(const ToolResult& result,
    ToolSpatialResultChannel requestedChannel, bool requireLineResults)
{
    std::vector<Candidate> candidates;
    int sourceOrder = 0;
    if (!requireLineResults &&
        ChannelEnabled(requestedChannel, ToolSpatialResultChannel::Detections))
    {
        for (int index = 0; index < static_cast<int>(result.detections.size()); ++index)
        {
            const auto& item = result.detections[index];
            Candidate candidate;
            candidate.box = item.box;
            candidate.category = item.label;
            candidate.classId = item.classId;
            candidate.score = item.score;
            candidate.area = static_cast<float>(item.box.area());
            candidate.sourceOrder = sourceOrder++;
            candidate.channel = ToolSpatialResultChannel::Detections;
            candidate.channelIndex = index;
            candidate.kind = "检测框";
            candidates.push_back(std::move(candidate));
        }
    }
    if (!requireLineResults &&
        ChannelEnabled(requestedChannel, ToolSpatialResultChannel::Regions))
    {
        for (int index = 0; index < static_cast<int>(result.regions.size()); ++index)
        {
            const auto& item = result.regions[index];
            const float area = item.area > 0.0f ? item.area : static_cast<float>(item.bbox.area());
            Candidate candidate;
            candidate.box = item.bbox;
            candidate.category = item.label;
            candidate.score = item.score;
            candidate.area = area;
            candidate.sourceOrder = sourceOrder++;
            candidate.channel = ToolSpatialResultChannel::Regions;
            candidate.channelIndex = index;
            candidate.kind = "区域";
            candidates.push_back(std::move(candidate));
        }
    }
    if ((requireLineResults ||
         ChannelEnabled(requestedChannel, ToolSpatialResultChannel::Lines)) &&
        (requestedChannel == ToolSpatialResultChannel::Auto ||
         requestedChannel == ToolSpatialResultChannel::Lines))
    {
        for (int index = 0; index < static_cast<int>(result.lines.size()); ++index)
        {
            const auto& item = result.lines[index];
            const int left = std::min(item.p1.x, item.p2.x);
            const int top = std::min(item.p1.y, item.p2.y);
            const int right = std::max(item.p1.x, item.p2.x);
            const int bottom = std::max(item.p1.y, item.p2.y);
            const cv::Rect box(left, top, right - left + 1, bottom - top + 1);
            Candidate candidate;
            candidate.box = box;
            candidate.category = "线段";
            candidate.score = 1.0f;
            candidate.area = static_cast<float>(box.area());
            candidate.sourceOrder = sourceOrder++;
            candidate.channel = ToolSpatialResultChannel::Lines;
            candidate.channelIndex = index;
            candidate.kind = "线段";
            candidate.isLine = true;
            candidate.lineStart = item.p1;
            candidate.lineEnd = item.p2;
            candidates.push_back(std::move(candidate));
        }
    }
    if (!requireLineResults &&
        ChannelEnabled(requestedChannel, ToolSpatialResultChannel::Texts))
    {
        for (int index = 0; index < static_cast<int>(result.texts.size()); ++index)
        {
            const auto& item = result.texts[index];
            Candidate candidate;
            candidate.box = item.box;
            candidate.category = item.text;
            candidate.score = item.confidence;
            candidate.area = static_cast<float>(item.box.area());
            candidate.sourceOrder = sourceOrder++;
            candidate.channel = ToolSpatialResultChannel::Texts;
            candidate.channelIndex = index;
            candidate.kind = "文本";
            candidates.push_back(std::move(candidate));
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

std::vector<Candidate> FilteredCandidates(const ToolResult& source,
    const ResultROIRequest& request)
{
    const ToolSpatialResultChannel channel = request.requireLineResults
        ? ToolSpatialResultChannel::Lines : request.channel;
    std::vector<Candidate> candidates = Candidates(
        source, channel, request.requireLineResults);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&request](const Candidate& candidate) { return !Matches(candidate, request); }),
        candidates.end());

    const int sortMode = (std::clamp)(request.sortMode, 0, 2);
    if (sortMode != 0)
    {
        std::stable_sort(candidates.begin(), candidates.end(),
            [sortMode, descending = request.sortDescending](
                const Candidate& left, const Candidate& right)
            {
                const float leftValue = sortMode == 1 ? left.score : left.area;
                const float rightValue = sortMode == 1 ? right.score : right.area;
                if (leftValue == rightValue)
                    return left.sourceOrder < right.sourceOrder;
                return descending ? leftValue > rightValue : leftValue < rightValue;
            });
    }
    return candidates;
}

std::string ChoiceLabel(const Candidate& candidate, int resultIndex)
{
    std::ostringstream label;
    label << resultIndex + 1 << ". " << candidate.kind;
    if (!candidate.category.empty() && candidate.category != candidate.kind)
        label << "｜" << candidate.category;
    if (candidate.isLine)
    {
        label << "｜(" << candidate.lineStart.x << "," << candidate.lineStart.y
              << ")-(" << candidate.lineEnd.x << "," << candidate.lineEnd.y << ")";
    }
    else
    {
        const float centerX = static_cast<float>(candidate.box.x) +
            static_cast<float>(candidate.box.width) * 0.5f;
        const float centerY = static_cast<float>(candidate.box.y) +
            static_cast<float>(candidate.box.height) * 0.5f;
        label << "｜中心(" << std::fixed << std::setprecision(1)
              << centerX << "," << centerY << ")";
    }
    label << "｜分数 " << std::fixed << std::setprecision(3) << candidate.score;
    return label.str();
}

bool CandidateFitsImage(const Candidate& candidate, cv::Size imageSize)
{
    if (imageSize.width <= 0 || imageSize.height <= 0)
        return true;
    return (candidate.box & cv::Rect(0, 0, imageSize.width, imageSize.height)).area() > 0;
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

    std::vector<Candidate> candidates = FilteredCandidates(source, request);
    if (candidates.empty())
    {
        resolution.reason = "result ROI filter matched no results";
        return resolution;
    }

    if (request.mode == ResultROIMode::NthResult ||
        request.mode == ResultROIMode::SelectedPair)
    {
        const int index = (std::max)(0, request.resultIndex);
        if (index >= static_cast<int>(candidates.size()))
        {
            resolution.reason = "result ROI index is outside the filtered result set";
            return resolution;
        }
        if (!CandidateFitsImage(candidates[index], imageSize))
        {
            resolution.reason = "selected result ROI is outside the input image";
            return resolution;
        }
        resolution.rois.push_back(CandidateToROI(candidates[index], request, imageSize));
    }
    else
    {
        resolution.rois.reserve(candidates.size());
        for (const Candidate& candidate : candidates)
        {
            if (CandidateFitsImage(candidate, imageSize))
                resolution.rois.push_back(CandidateToROI(candidate, request, imageSize));
        }
        if (resolution.rois.empty())
        {
            resolution.reason = "all matching result ROIs are outside the input image";
            return resolution;
        }
    }

    resolution.available = !resolution.rois.empty();
    return resolution;
}

std::vector<ResultROIChoice> ListChoices(const ToolResult& source,
    const ResultROIRequest& request)
{
    const std::vector<Candidate> candidates = FilteredCandidates(source, request);
    std::vector<ResultROIChoice> choices;
    choices.reserve(candidates.size());
    for (int index = 0; index < static_cast<int>(candidates.size()); ++index)
        choices.push_back({index, ChoiceLabel(candidates[index], index),
            candidates[index].channel, candidates[index].channelIndex});
    return choices;
}
}
