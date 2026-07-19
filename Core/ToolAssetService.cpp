#include "ToolAssetService.h"

#include "ImageState.h"
#include "ROIEditorState.h"
#include "ROIState.h"

#include <map>
#include <utility>
#include <vector>

namespace
{
struct CaptureSession
{
    std::uint64_t roiRuntimeId = 0;
    ROI lastROI;
    bool hasLastROI = false;
};

using SessionKey = std::pair<std::uint64_t, int>;
std::map<SessionKey, CaptureSession> s_sessions;

SessionKey MakeKey(std::uint64_t toolId, ToolAssetKind kind)
{
    return {toolId, static_cast<int>(kind)};
}

ROI MakeDefaultROI(const ToolInstance& tool, ToolAssetKind kind, const CaptureSession& session)
{
    if ((kind == ToolAssetKind::TemplateMatch || kind == ToolAssetKind::ShapeTemplate) &&
        tool.hasTemplateROI)
    {
        return tool.templateROI;
    }
    if (session.hasLastROI)
        return session.lastROI;

    const cv::Mat& image = ImageState::Current();
    const float centerX = image.empty() ? 200.0f : image.cols * 0.5f;
    const float centerY = image.empty() ? 200.0f : image.rows * 0.5f;
    ROI roi;
    roi.type = ROI_TYPE_RECT;
    roi.start = ImVec2(centerX - 60.0f, centerY - 60.0f);
    roi.end = ImVec2(centerX + 60.0f, centerY + 60.0f);
    return roi;
}

void RemoveSessionROI(CaptureSession& session)
{
    const int roiIndex = ROIState::FindIndexByRuntimeId(session.roiRuntimeId);
    if (roiIndex >= 0)
        ROIState::RemoveAt(roiIndex);
    session.roiRuntimeId = 0;
}

bool IsSupportedROICaptureKind(ToolAssetKind kind)
{
    return kind == ToolAssetKind::TemplateMatch ||
        kind == ToolAssetKind::ShapeTemplate ||
        kind == ToolAssetKind::MultiColorReference;
}
}

namespace ToolAssetService
{
int BeginROICapture(ToolInstance& tool, ToolAssetKind kind)
{
    if (tool.toolId == 0 || !IsSupportedROICaptureKind(kind))
        return -1;

    CaptureSession& session = s_sessions[MakeKey(tool.toolId, kind)];
    const int activeIndex = ROIState::FindIndexByRuntimeId(session.roiRuntimeId);
    if (activeIndex >= 0)
    {
        ROIState::SetSelectedIndex(activeIndex);
        return activeIndex;
    }

    ROI roi = MakeDefaultROI(tool, kind, session);
    ROIEditorState::EnsureRuntimeId(roi);
    session.roiRuntimeId = roi.runtimeId;
    return ROIState::Add(std::move(roi), true);
}

int ActiveROIIndex(std::uint64_t toolId, ToolAssetKind kind)
{
    const auto found = s_sessions.find(MakeKey(toolId, kind));
    if (found == s_sessions.end())
        return -1;
    return ROIState::FindIndexByRuntimeId(found->second.roiRuntimeId);
}

bool IsROICaptureActive(std::uint64_t toolId, ToolAssetKind kind)
{
    return ActiveROIIndex(toolId, kind) >= 0;
}

ToolAssetCaptureResult ConfirmROICapture(ToolInstance& tool, ToolAssetKind kind)
{
    ToolAssetCaptureResult result;
    if (tool.toolId == 0 || !IsSupportedROICaptureKind(kind))
    {
        result.message = "unsupported asset capture request";
        return result;
    }

    CaptureSession& session = s_sessions[MakeKey(tool.toolId, kind)];
    const int roiIndex = ROIState::FindIndexByRuntimeId(session.roiRuntimeId);
    const ROI* roi = ROIState::At(roiIndex);
    const cv::Mat& image = ImageState::Current();
    if (!roi || image.empty())
    {
        result.message = image.empty() ? "no source image" : "capture ROI is missing";
        return result;
    }

    const cv::Rect bounds = roi->ToCvRect();
    result.bounds = bounds;
    if (bounds.width <= 0 || bounds.height <= 0 || bounds.x < 0 || bounds.y < 0 ||
        bounds.x + bounds.width > image.cols || bounds.y + bounds.height > image.rows)
    {
        result.message = "capture ROI is outside the source image";
        return result;
    }

    const cv::Mat captured = image(bounds).clone();
    switch (kind)
    {
    case ToolAssetKind::TemplateMatch:
        tool.templateImg = captured;
        tool.templateROI = *roi;
        tool.hasTemplateROI = true;
        tool.showTemplatePreview = true;
        break;
    case ToolAssetKind::ShapeTemplate:
        tool.shpTplImage = captured;
        tool.templateImg = captured.clone();
        tool.templateROI = *roi;
        tool.hasTemplateROI = true;
        break;
    case ToolAssetKind::MultiColorReference:
        tool.mcfRefImage = captured;
        tool.mcfAnchorX = 0;
        tool.mcfAnchorY = 0;
        break;
    default:
        result.message = "unsupported asset capture request";
        return result;
    }

    session.lastROI = *roi;
    session.hasLastROI = true;
    RemoveSessionROI(session);
    result.success = true;
    result.message = "asset captured";
    return result;
}

void CancelROICapture(std::uint64_t toolId, ToolAssetKind kind)
{
    const auto found = s_sessions.find(MakeKey(toolId, kind));
    if (found != s_sessions.end())
        RemoveSessionROI(found->second);
}

ToolAssetCaptureResult CaptureCurrentImage(ToolInstance& tool, ToolAssetKind kind)
{
    ToolAssetCaptureResult result;
    if (kind != ToolAssetKind::DifferenceReference)
    {
        result.message = "asset requires ROI capture";
        return result;
    }

    const cv::Mat& image = ImageState::Current();
    if (image.empty())
    {
        result.message = "no source image";
        return result;
    }

    tool.differenceReferenceImage = image.clone();
    result.bounds = cv::Rect(0, 0, image.cols, image.rows);
    result.success = true;
    result.message = "asset captured";
    return result;
}

void ClearAsset(ToolInstance& tool, ToolAssetKind kind)
{
    CancelROICapture(tool.toolId, kind);
    switch (kind)
    {
    case ToolAssetKind::TemplateMatch:
        tool.templateImg.release();
        tool.hasTemplateROI = false;
        tool.templateROI = ROI();
        break;
    case ToolAssetKind::ShapeTemplate:
        tool.shpTplImage.release();
        tool.templateImg.release();
        tool.hasTemplateROI = false;
        tool.templateROI = ROI();
        break;
    case ToolAssetKind::MultiColorReference:
        tool.mcfRefImage.release();
        tool.mcfAnchorX = 0;
        tool.mcfAnchorY = 0;
        break;
    case ToolAssetKind::DifferenceReference:
        tool.differenceReferenceImage.release();
        break;
    }
}

void ForgetTool(std::uint64_t toolId)
{
    for (auto it = s_sessions.begin(); it != s_sessions.end();)
    {
        if (it->first.first == toolId)
        {
            RemoveSessionROI(it->second);
            it = s_sessions.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ClearSessions()
{
    for (auto& item : s_sessions)
        RemoveSessionROI(item.second);
    s_sessions.clear();
}
}
