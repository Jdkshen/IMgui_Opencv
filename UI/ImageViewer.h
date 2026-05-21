#pragma once

// 图像显示/视图变换状态（定义在 ImageViewer.cpp）
extern float  gZoom;
extern ImVec2 gPan;
extern ImVec2 gCanvasSize;
extern ImVec2 gImageScreenPos;

namespace UI
{
    void ShowOpenCV();
    void FitImageToWindow();
    void ClearImage();
}
