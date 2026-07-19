#include "ImageViewState.h"

namespace
{
float s_zoom = 1.0f;
ImVec2 s_pan = {0.0f, 0.0f};
ImVec2 s_canvasSize = {0.0f, 0.0f};
ImVec2 s_imageScreenPos = {0.0f, 0.0f};
bool s_showPixelGrid = false;
bool s_showCoordGrid = false;
int s_gridStep = 1;
}

namespace ImageViewState
{
float& Zoom() { return s_zoom; }
ImVec2& Pan() { return s_pan; }
ImVec2& CanvasSize() { return s_canvasSize; }
ImVec2& ImageScreenPos() { return s_imageScreenPos; }
bool& ShowPixelGrid() { return s_showPixelGrid; }
bool& ShowCoordGrid() { return s_showCoordGrid; }
int& GridStep() { return s_gridStep; }

void Reset()
{
    s_zoom = 1.0f;
    s_pan = {0.0f, 0.0f};
    s_canvasSize = {0.0f, 0.0f};
    s_imageScreenPos = {0.0f, 0.0f};
    s_showPixelGrid = false;
    s_showCoordGrid = false;
    s_gridStep = 1;
}
}
