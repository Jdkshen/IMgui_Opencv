#include "ROIEditorState.h"

#include <algorithm>

namespace
{
bool s_drawing = false;
ImVec2 s_drawStart;
bool s_dragging = false;
ImVec2 s_lastMousePosition;
HandleType s_activeHandle = HANDLE_NONE;
int s_activePointIndex = -1;
int s_hoveredROI = -1;
int s_currentROIType = ROI_TYPE_RECT;
std::vector<ImVec2> s_polygonDraftPoints;

std::vector<int> s_drawSequence;
std::vector<ROI> s_completedROIs;
int s_drawSequenceStep = 0;
std::uint64_t s_nextRuntimeId = 1;
}

namespace ROIEditorState
{
bool& Drawing() { return s_drawing; }
ImVec2& DrawStart() { return s_drawStart; }
bool& Dragging() { return s_dragging; }
ImVec2& LastMousePosition() { return s_lastMousePosition; }
HandleType& ActiveHandle() { return s_activeHandle; }
int& ActivePointIndex() { return s_activePointIndex; }
int& HoveredROI() { return s_hoveredROI; }
int& CurrentROIType() { return s_currentROIType; }
std::vector<ImVec2>& PolygonDraftPoints() { return s_polygonDraftPoints; }

void BeginDrawSequence(std::initializer_list<int> roiTypes)
{
    s_drawSequence.assign(roiTypes.begin(), roiTypes.end());
    s_completedROIs.clear();
    s_drawSequenceStep = 0;
    s_drawing = false;
    s_polygonDraftPoints.clear();
    if (!s_drawSequence.empty())
        s_currentROIType = s_drawSequence.front();
}

void CancelDrawSequence()
{
    s_drawSequence.clear();
    s_completedROIs.clear();
    s_drawSequenceStep = 0;
    s_drawing = false;
    s_polygonDraftPoints.clear();
}

bool IsDrawSequenceActive()
{
    return s_drawSequenceStep >= 0 &&
        s_drawSequenceStep < static_cast<int>(s_drawSequence.size());
}

int DrawSequenceStep()
{
    return IsDrawSequenceActive() ? s_drawSequenceStep : -1;
}

int DrawSequenceCount()
{
    return static_cast<int>(s_drawSequence.size());
}

void AdvanceDrawSequence(const ROI& completedROI)
{
    if (!IsDrawSequenceActive() ||
        s_drawSequence[s_drawSequenceStep] != completedROI.type)
        return;

    s_completedROIs.push_back(completedROI);
    ++s_drawSequenceStep;
    if (IsDrawSequenceActive())
        s_currentROIType = s_drawSequence[s_drawSequenceStep];
    else
        s_drawing = false;
}

bool ConsumeCompletedDrawSequence(std::vector<ROI>& completedROIs)
{
    if (IsDrawSequenceActive() || s_drawSequence.empty() ||
        s_completedROIs.size() != s_drawSequence.size())
        return false;

    completedROIs = s_completedROIs;
    CancelDrawSequence();
    return true;
}

std::uint64_t EnsureRuntimeId(ROI& roi)
{
    if (roi.runtimeId == 0)
        roi.runtimeId = s_nextRuntimeId++;
    else if (roi.runtimeId >= s_nextRuntimeId)
        s_nextRuntimeId = roi.runtimeId + 1;
    return roi.runtimeId;
}

void ResetInteraction()
{
    CancelDrawSequence();
    s_drawing = false;
    s_dragging = false;
    s_activeHandle = HANDLE_NONE;
    s_activePointIndex = -1;
    s_hoveredROI = -1;
    s_polygonDraftPoints.clear();
}
}
