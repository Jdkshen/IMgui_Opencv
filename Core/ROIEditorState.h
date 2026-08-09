#pragma once

#include "ROI.h"

#include <initializer_list>
#include <vector>

namespace ROIEditorState
{
    bool& Drawing();
    ImVec2& DrawStart();
    bool& Dragging();
    ImVec2& LastMousePosition();
    HandleType& ActiveHandle();
    int& ActivePointIndex();
    int& HoveredROI();
    int& CurrentROIType();
    std::vector<ImVec2>& PolygonDraftPoints();

    void BeginDrawSequence(std::initializer_list<int> roiTypes);
    void CancelDrawSequence();
    bool IsDrawSequenceActive();
    int DrawSequenceStep();
    int DrawSequenceCount();
    void AdvanceDrawSequence(const ROI& completedROI);
    bool ConsumeCompletedDrawSequence(std::vector<ROI>& completedROIs);

    std::uint64_t EnsureRuntimeId(ROI& roi);
    void ResetInteraction();
}
