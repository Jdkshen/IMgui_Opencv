#include "../Windows_imgui.h"
#include "ROIManager.h"
#include "../Core/DX12Context.h"
#include "../log/LogSystem.h"

// =====================================================
// ROI 全局变量定义
// =====================================================
std::vector<ROI> gROIs;
bool   gDrawingROI     = false;
ImVec2 gROIStart;
int    gSelectedROI    = -1;
bool   gDraggingROI    = false;
ImVec2 gLastMousePos;
HandleType gActiveHandle = HANDLE_NONE;
int    gHoveredROI     = -1;

// 图像显示状态（extern 在 DockSpaceHost.h）
extern float  gZoom;
extern ImVec2 gPan;
extern ImVec2 gCanvasSize;
extern ImVec2 gImageScreenPos;
extern ImVec2 imageScreenPos;
extern ImVec4 color;

// =====================================================
// 坐标转换函数实现
// =====================================================
ImVec2 ImageToScreenPos(const ImVec2& p)
{
    return ImVec2(
        imageScreenPos.x + gPan.x + p.x * gZoom,
        imageScreenPos.y + gPan.y + p.y * gZoom
    );
}

ImVec2 ScreenToImagePos(const ImVec2& p)
{
    return ImVec2(
        (p.x - imageScreenPos.x - gPan.x) / gZoom,
        (p.y - imageScreenPos.y - gPan.y) / gZoom
    );
}

void NormalizeROI(ROI& roi)
{
    if (roi.start.x > roi.end.x) std::swap(roi.start.x, roi.end.x);
    if (roi.start.y > roi.end.y) std::swap(roi.start.y, roi.end.y);
}

// =====================================================
// 以鼠标位置为锚点缩放
// =====================================================
void ZoomAtCenter(float delta)
{
    ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 before = ScreenToImagePos(mouse);
    float oldZoom = gZoom;
    gZoom *= (1.0f + delta);
    gZoom = std::clamp(gZoom, 0.05f, 50.0f);
    ImVec2 after = ImageToScreenPos(before);
    gPan.x += mouse.x - after.x;
    gPan.y += mouse.y - after.y;
}

// =====================================================
// 打印所有ROI信息到日志
// =====================================================
void PrintROIToLog()
{
    ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 a = ScreenToImagePos(mouse);
    ImVec2 b = ImageToScreenPos(a);
    ImVec2 error(b.x - mouse.x, b.y - mouse.y);
    float dist = sqrtf(error.x * error.x + error.y * error.y);

    LogSystem::Add(LOG_INFO, color, "reproject=(%.6f,%.6f) len=%.6f", error.x, error.y, dist);

    for (int i = 0; i < (int)gROIs.size(); i++)
    {
        const auto& roi = gROIs[i];
        float x1 = std::min(roi.start.x, roi.end.x);
        float y1 = std::min(roi.start.y, roi.end.y);
        float x2 = std::max(roi.start.x, roi.end.x);
        float y2 = std::max(roi.start.y, roi.end.y);

        LogSystem::Add(LOG_INFO, color, "ROI[%d] rect=(%.6f %.6f %.6f %.6f)", i, x1, y1, x2 - x1, y2 - y1);
    }
}

// =====================================================
// 清理ROI状态
// =====================================================
void ClearROIState()
{
    gROIs.clear();
    gDrawingROI = false;
    gSelectedROI = -1;
    gActiveHandle = HANDLE_NONE;
    gDraggingROI = false;
}
