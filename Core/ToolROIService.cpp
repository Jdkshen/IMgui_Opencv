#include "ToolROIService.h"

#include "ImageState.h"
#include "ROIEditorState.h"
#include "ROIState.h"

#include <map>
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
std::map<std::uint64_t, std::uint64_t> s_searchROIByToolId;

void RemoveEditingROI(std::uint64_t toolId)
{
    const auto found = s_searchROIByToolId.find(toolId);
    if (found == s_searchROIByToolId.end())
        return;
    const int index = ROIState::FindIndexByRuntimeId(found->second);
    if (index >= 0)
        ROIState::RemoveAt(index);
    s_searchROIByToolId.erase(found);
}

ROI MakeSearchROI(const ToolInstance& tool)
{
    if (!tool.searchROIs.empty())
        return tool.searchROIs.front();

    const cv::Mat& image = ImageState::Current();
    const float centerX = image.empty() ? 200.0f : image.cols * 0.5f;
    const float centerY = image.empty() ? 200.0f : image.rows * 0.5f;
    ROI roi;
    roi.type = ROI_TYPE_RECT;
    roi.start = ImVec2(centerX - 80.0f, centerY - 60.0f);
    roi.end = ImVec2(centerX + 80.0f, centerY + 60.0f);
    return roi;
}

void ApplySearchROI(ToolInstance& tool, const ROI& roi)
{
    tool.searchROIs.assign(1, roi);
    tool.lineSaveROIs = tool.searchROIs;
    tool.useSearchROI = true;
    tool.yoloUseROI = true;
    tool.lineUseROI = true;
    tool.mcfUseROI = true;
    tool.colorAnalysis.useROI = true;
    tool.ocrUseROI = true;
    tool.qrUseROI = true;
    const cv::Rect bounds = roi.ToCvRect();
    tool.mcfRoiX = bounds.x;
    tool.mcfRoiY = bounds.y;
    tool.mcfRoiW = bounds.width;
    tool.mcfRoiH = bounds.height;
}

bool SameGeometry(const ROI& first, const ROI& second)
{
    constexpr float epsilon = 0.01f;
    if (first.type != second.type || first.points.size() != second.points.size() ||
        std::abs(first.start.x - second.start.x) > epsilon ||
        std::abs(first.start.y - second.start.y) > epsilon ||
        std::abs(first.end.x - second.end.x) > epsilon ||
        std::abs(first.end.y - second.end.y) > epsilon ||
        std::abs(first.angle - second.angle) > epsilon)
        return false;
    for (std::size_t index = 0; index < first.points.size(); ++index)
    {
        if (std::abs(first.points[index].x - second.points[index].x) > epsilon ||
            std::abs(first.points[index].y - second.points[index].y) > epsilon)
            return false;
    }
    return true;
}

int FindMeasurementROIIndex(const ROI& boundROI)
{
    if (boundROI.runtimeId != 0)
    {
        const int runtimeIndex = ROIState::FindIndexByRuntimeId(boundROI.runtimeId);
        if (runtimeIndex >= 0)
            return runtimeIndex;
    }

    const auto& rois = ROIState::ReadOnlyItems();
    for (int index = 0; index < static_cast<int>(rois.size()); ++index)
    {
        if (SameGeometry(rois[index], boundROI))
            return index;
    }
    return -1;
}

void ApplyMeasurementROIs(ToolInstance& tool, std::vector<ROI> rois)
{
    tool.searchROIs = std::move(rois);
    tool.lineSaveROIs = tool.searchROIs;
    tool.useSearchROI = !tool.searchROIs.empty();
    tool.measureRuntimeROIIds.clear();
    tool.measureRuntimeROIIds.reserve(tool.searchROIs.size());
    for (const ROI& roi : tool.searchROIs)
        tool.measureRuntimeROIIds.push_back(roi.runtimeId);
}
}

namespace ToolROIService
{
int BeginSearchROIEdit(ToolInstance& tool)
{
    if (tool.toolId == 0)
        return -1;
    const int activeIndex = ActiveSearchROIIndex(tool.toolId);
    if (activeIndex >= 0)
    {
        ROIState::SetSelectedIndex(activeIndex);
        return activeIndex;
    }

    ROI roi = MakeSearchROI(tool);
    ROIEditorState::EnsureRuntimeId(roi);
    s_searchROIByToolId[tool.toolId] = roi.runtimeId;
    return ROIState::Add(std::move(roi), true);
}

int ActiveSearchROIIndex(std::uint64_t toolId)
{
    const auto found = s_searchROIByToolId.find(toolId);
    if (found == s_searchROIByToolId.end())
        return -1;
    return ROIState::FindIndexByRuntimeId(found->second);
}

bool IsSearchROIEditActive(std::uint64_t toolId)
{
    return ActiveSearchROIIndex(toolId) >= 0;
}

ToolROIEditResult ConfirmSearchROIEdit(ToolInstance& tool)
{
    ToolROIEditResult result;
    const int index = ActiveSearchROIIndex(tool.toolId);
    const ROI* roi = ROIState::At(index);
    if (!roi)
    {
        result.message = "search ROI is missing";
        return result;
    }

    result.bounds = roi->ToCvRect();
    if (result.bounds.width <= 1 || result.bounds.height <= 1)
    {
        result.message = "search ROI is empty";
        return result;
    }

    ApplySearchROI(tool, *roi);
    tool.MarkParametersChanged();
    RemoveEditingROI(tool.toolId);
    result.success = true;
    result.message = "search ROI updated";
    return result;
}

void CancelSearchROIEdit(std::uint64_t toolId)
{
    RemoveEditingROI(toolId);
}

void ClearSearchROIs(ToolInstance& tool)
{
    CancelSearchROIEdit(tool.toolId);
    tool.searchROIs.clear();
    tool.lineSaveROIs.clear();
    tool.useSearchROI = false;
    tool.yoloUseROI = false;
    tool.lineUseROI = false;
    tool.mcfUseROI = false;
    tool.colorAnalysis.useROI = false;
    tool.ocrUseROI = false;
    tool.qrUseROI = false;
    tool.mcfRoiX = 0;
    tool.mcfRoiY = 0;
    tool.mcfRoiW = 0;
    tool.mcfRoiH = 0;
    tool.MarkParametersChanged();
}

bool SyncMeasurementROIs(ToolInstance& tool)
{
    if (tool.measureRuntimeROIIds.empty() && !tool.searchROIs.empty())
    {
        std::vector<std::uint64_t> restoredIds;
        restoredIds.reserve(tool.searchROIs.size());
        for (ROI& boundROI : tool.searchROIs)
        {
            const int index = FindMeasurementROIIndex(boundROI);
            const ROI* runtimeROI = ROIState::At(index);
            if (!runtimeROI)
            {
                restoredIds.clear();
                break;
            }
            ROI updatedROI = *runtimeROI;
            const std::uint64_t id = ROIEditorState::EnsureRuntimeId(updatedROI);
            ROIState::Update(index, updatedROI);
            boundROI.runtimeId = id;
            restoredIds.push_back(id);
        }
        if (restoredIds.size() == tool.searchROIs.size())
            tool.measureRuntimeROIIds = std::move(restoredIds);
    }

    if (tool.measureRuntimeROIIds.empty())
        return false;

    std::vector<ROI> synced;
    synced.reserve(tool.measureRuntimeROIIds.size());
    for (std::uint64_t id : tool.measureRuntimeROIIds)
    {
        const ROI* roi = ROIState::At(ROIState::FindIndexByRuntimeId(id));
        if (!roi)
        {
            tool.measureRuntimeROIIds.clear();
            return false;
        }
        synced.push_back(*roi);
    }
    ApplyMeasurementROIs(tool, std::move(synced));
    return true;
}

void RestoreMeasurementROIs(ToolInstance& tool)
{
    std::vector<ROI> restored = tool.searchROIs;
    for (ROI& boundROI : restored)
    {
        const int index = FindMeasurementROIIndex(boundROI);
        const ROI* runtimeROI = ROIState::At(index);
        if (!runtimeROI)
        {
            ROIEditorState::EnsureRuntimeId(boundROI);
            ROIState::Add(boundROI, false);
        }
        else
        {
            ROI updatedROI = *runtimeROI;
            boundROI.runtimeId = ROIEditorState::EnsureRuntimeId(updatedROI);
            ROIState::Update(index, std::move(updatedROI));
        }
    }
    ApplyMeasurementROIs(tool, std::move(restored));
}

void RemoveMeasurementROIs(ToolInstance& tool)
{
    std::vector<int> indices;
    indices.reserve(tool.measureRuntimeROIIds.size());
    for (std::uint64_t id : tool.measureRuntimeROIIds)
    {
        const int index = ROIState::FindIndexByRuntimeId(id);
        if (index >= 0)
            indices.push_back(index);
    }
    std::sort(indices.rbegin(), indices.rend());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int index : indices)
        ROIState::RemoveAt(index);

    ROIState::SetSelectedIndex(-1);
    ROIEditorState::ActiveHandle() = HANDLE_NONE;
    tool.measureRuntimeROIIds.clear();
    tool.searchROIs.clear();
    tool.lineSaveROIs.clear();
    tool.useSearchROI = false;
}

void RestoreMeasurementROIBackup(ToolInstance& tool, const std::vector<ROI>& backup)
{
    for (const ROI& original : backup)
    {
        const int index = ROIState::FindIndexByRuntimeId(original.runtimeId);
        if (ROIState::At(index))
            ROIState::Update(index, original);
        else
            ROIState::Add(original, false);
    }
    ApplyMeasurementROIs(tool, backup);
}

bool SelectMeasurementROI(const ToolInstance& tool, std::size_t ordinal)
{
    if (ordinal >= tool.measureRuntimeROIIds.size())
        return false;
    const int index = ROIState::FindIndexByRuntimeId(tool.measureRuntimeROIIds[ordinal]);
    ROIState::SetSelectedIndex(index);
    return index >= 0;
}

void ForgetTool(std::uint64_t toolId)
{
    CancelSearchROIEdit(toolId);
}

void ClearSessions()
{
    while (!s_searchROIByToolId.empty())
        RemoveEditingROI(s_searchROIByToolId.begin()->first);
}
}
