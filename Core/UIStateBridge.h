#pragma once

#include <vector>

#include "ROI.h"
#include "ToolInstance.h"

namespace UI
{
    extern std::vector<ROI> gROIs;
    extern int gSelectedROI;

    extern int g_ActiveToolIndex;
    extern std::vector<ToolInstance> g_ToolInstances;

    extern bool g_YoloLiveDetect;
    extern int  g_YoloLiveInstanceIdx;
    extern float g_YoloLastTimeMs;
    extern float g_YoloLiveFrameMs;

    void FitImageToWindow();
    void ClearImage();
    void ClearROIState();
    void NavigateNextImage();
    void MoveOriginalToolToFront();
}
