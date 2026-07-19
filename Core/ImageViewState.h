#pragma once

#include "../include/imgui/imgui.h"

// ImageViewState — image canvas transform and display options.
// The UI renders this state but does not own it or export mutable globals.
namespace ImageViewState
{
    float& Zoom();
    ImVec2& Pan();
    ImVec2& CanvasSize();
    ImVec2& ImageScreenPos();
    bool& ShowPixelGrid();
    bool& ShowCoordGrid();
    int& GridStep();

    void Reset();
}
