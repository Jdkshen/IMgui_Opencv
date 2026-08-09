#include "ROIManager.h"
#include "DockSpaceHost.h"
#include "../Core/ROIState.h"
#include "../Core/ImageViewState.h"
#include "../Core/ImageState.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Core/VisionContext.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/ITool.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/LineDetector.h"

namespace UI
{

namespace
{
float& gZoom = ImageViewState::Zoom();
ImVec2& gPan = ImageViewState::Pan();
ImVec2& imageScreenPos = ImageViewState::ImageScreenPos();
const std::vector<ROI>& s_rois = ROIState::ReadOnlyItems();
}

    void BeginROIDrawSequence(std::initializer_list<int> roiTypes)
    {
        ROIEditorState::BeginDrawSequence(roiTypes);
    }

    std::uint64_t EnsureROIRuntimeId(ROI& roi)
    {
        return ROIEditorState::EnsureRuntimeId(roi);
    }

    void CancelROIDrawSequence()
    {
        ROIEditorState::CancelDrawSequence();
    }

    bool IsROIDrawSequenceActive()
    {
        return ROIEditorState::IsDrawSequenceActive();
    }

    int ROIDrawSequenceStep()
    {
        return ROIEditorState::DrawSequenceStep();
    }

    int ROIDrawSequenceCount()
    {
        return ROIEditorState::DrawSequenceCount();
    }

    void AdvanceROIDrawSequence(const ROI& completedROI)
    {
        ROIEditorState::AdvanceDrawSequence(completedROI);
    }

    bool ConsumeCompletedROIDrawSequence(std::vector<ROI>& completedROIs)
    {
        return ROIEditorState::ConsumeCompletedDrawSequence(completedROIs);
    }

    // =====================================================
    // 坐标转换函数实现
    // =====================================================
    ImVec2 ImageToScreenPos(const ImVec2 &p)
    {
        return ImVec2(
            imageScreenPos.x + gPan.x + p.x * gZoom,
            imageScreenPos.y + gPan.y + p.y * gZoom);
    }

    ImVec2 ScreenToImagePos(const ImVec2 &p)
    {
        return ImVec2(
            (p.x - imageScreenPos.x - gPan.x) / gZoom,
            (p.y - imageScreenPos.y - gPan.y) / gZoom);
    }

    void NormalizeROI(ROI &roi)
    {
        // 仅矩形需要确保 start 为左上角
        if (roi.type == ROI_TYPE_RECT || roi.type == ROI_TYPE_POLYGON)
        {
            if (roi.start.x > roi.end.x)
                std::swap(roi.start.x, roi.end.x);
            if (roi.start.y > roi.end.y)
                std::swap(roi.start.y, roi.end.y);
        }
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
        gZoom = std::clamp(gZoom, 0.005f, 50.0f); // 最小0.5%，最大5000%
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

        LogSystem::Add(LOG_INFO, "reproject=(%.6f,%.6f) len=%.6f", error.x, error.y, dist);

        for (int i = 0; i < (int)s_rois.size(); i++)
        {
            const auto &roi = s_rois[i];
            const char* tname = "?";
            switch (roi.type) {
            case ROI_TYPE_RECT: tname = "Rect"; break;
            case ROI_TYPE_POINT: tname = "Point"; break;
            case ROI_TYPE_LINE: tname = "Line"; break;
            case ROI_TYPE_CIRCLE: tname = "Circle"; break;
            case ROI_TYPE_POLYGON: tname = "Polygon"; break;
            }
            if (roi.type == ROI_TYPE_POINT)
                LogSystem::Add(LOG_INFO, "ROI[%d] %s pos=(%.1f,%.1f)",
                               i, tname, roi.start.x, roi.start.y);
            else if (roi.type == ROI_TYPE_CIRCLE)
                LogSystem::Add(LOG_INFO, "ROI[%d] %s center=(%.1f,%.1f) r=%.1f",
                               i, tname, roi.start.x, roi.start.y, roi.CircleRadius());
            else if (roi.type == ROI_TYPE_POLYGON)
                LogSystem::Add(LOG_INFO, "ROI[%d] %s pts=%zu",
                               i, tname, roi.points.size());
            else
            {
                float x1 = (std::min)(roi.start.x, roi.end.x);
                float y1 = (std::min)(roi.start.y, roi.end.y);
                float x2 = (std::max)(roi.start.x, roi.end.x);
                float y2 = (std::max)(roi.start.y, roi.end.y);
                LogSystem::Add(LOG_INFO, "ROI[%d] %s rect=(%.1f %.1f %.1f %.1f) angle=%.2f",
                               i, tname, x1, y1, x2 - x1, y2 - y1, roi.angle);
            }
        }
    }

    // =====================================================
    // 清理ROI状态
    // =====================================================
    void ClearROIState()
    {
        ROIState::ClearInteraction();
        ROIEditorState::ResetInteraction();
        RealtimeDetectionState::Clear();
        gContext.ClearUnifiedResults();
        gContext.rois.clear();
        gContext.selectedROI = -1;
    }

    // =====================================================
    // HALCON 风格 ROI 交互：右键创建，左键选择/编辑。
    // 不同形状使用各自的控制点，不再套用矩形的 8 点缩放模型。
    // =====================================================
    void HandleROIInteraction()
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 imageMouse = ScreenToImagePos(mouse);
        const bool canvasHovered = ImGui::IsWindowHovered();
        const float hitTolerance = 10.0f / (std::max)(gZoom, 0.01f);

        struct Box { ImVec2 lt, rt, lb, rb, t, b, l, r, rotate, c; };

        auto Distance = [](const ImVec2& a, const ImVec2& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        };
        auto SegmentDistance = [&](const ImVec2& p, const ImVec2& a, const ImVec2& b)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.0001f)
                return Distance(p, a);
            const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) /
                                       lengthSquared, 0.0f, 1.0f);
            return Distance(p, ImVec2(a.x + t * dx, a.y + t * dy));
        };
        auto UpdatePolygonBounds = [](ROI& roi)
        {
            if (roi.points.empty())
                return;
            float minX = roi.points.front().x;
            float maxX = minX;
            float minY = roi.points.front().y;
            float maxY = minY;
            for (const ImVec2& point : roi.points)
            {
                minX = (std::min)(minX, point.x);
                maxX = (std::max)(maxX, point.x);
                minY = (std::min)(minY, point.y);
                maxY = (std::max)(maxY, point.y);
            }
            roi.start = ImVec2(minX, minY);
            roi.end = ImVec2(maxX, maxY);
        };
        auto GetBox = [&](const ROI& roi) -> Box
        {
            const auto corners = roi.Corners();
            const ImVec2 center = roi.Center();
            const ImVec2 top((corners[0].x + corners[1].x) * 0.5f,
                             (corners[0].y + corners[1].y) * 0.5f);
            const ImVec2 bottom((corners[3].x + corners[2].x) * 0.5f,
                                (corners[3].y + corners[2].y) * 0.5f);
            const ImVec2 left((corners[0].x + corners[3].x) * 0.5f,
                              (corners[0].y + corners[3].y) * 0.5f);
            const ImVec2 right((corners[1].x + corners[2].x) * 0.5f,
                               (corners[1].y + corners[2].y) * 0.5f);
            const float handleDistance = 30.0f / (std::max)(gZoom, 0.01f);
            const float topLength = (std::max)(1.0f, Distance(top, center));
            const ImVec2 rotate(top.x + (top.x - center.x) * handleDistance / topLength,
                                top.y + (top.y - center.y) * handleDistance / topLength);
            return Box{corners[0], corners[1], corners[3], corners[2],
                       top, bottom, left, right, rotate, center};
        };
        auto ResizeRotatedRect = [&](ROI& roi, HandleType handle, const ImVec2& target)
        {
            const ImVec2 oldCenter = roi.Center();
            const float radians = -roi.angle * static_cast<float>(CV_PI / 180.0);
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const float dx = target.x - oldCenter.x;
            const float dy = target.y - oldCenter.y;
            const float localX = dx * cosine - dy * sine;
            const float localY = dx * sine + dy * cosine;
            float left = -roi.Width() * 0.5f;
            float right = roi.Width() * 0.5f;
            float top = -roi.Height() * 0.5f;
            float bottom = roi.Height() * 0.5f;
            if (handle == HANDLE_LT || handle == HANDLE_LB || handle == HANDLE_L)
                left = (std::min)(localX, right - gMinROIWidth);
            if (handle == HANDLE_RT || handle == HANDLE_RB || handle == HANDLE_R)
                right = (std::max)(localX, left + gMinROIWidth);
            if (handle == HANDLE_LT || handle == HANDLE_RT || handle == HANDLE_T)
                top = (std::min)(localY, bottom - gMinROIHeight);
            if (handle == HANDLE_LB || handle == HANDLE_RB || handle == HANDLE_B)
                bottom = (std::max)(localY, top + gMinROIHeight);
            const float localCenterX = (left + right) * 0.5f;
            const float localCenterY = (top + bottom) * 0.5f;
            const float forwardRadians = roi.angle * static_cast<float>(CV_PI / 180.0);
            const float forwardCosine = std::cos(forwardRadians);
            const float forwardSine = std::sin(forwardRadians);
            const ImVec2 newCenter(
                oldCenter.x + localCenterX * forwardCosine - localCenterY * forwardSine,
                oldCenter.y + localCenterX * forwardSine + localCenterY * forwardCosine);
            roi.start = ImVec2(newCenter.x - (right - left) * 0.5f,
                               newCenter.y - (bottom - top) * 0.5f);
            roi.end = ImVec2(newCenter.x + (right - left) * 0.5f,
                             newCenter.y + (bottom - top) * 0.5f);
        };
        auto TranslateROI = [](ROI& roi, const ImVec2& delta)
        {
            roi.start.x += delta.x; roi.start.y += delta.y;
            roi.end.x += delta.x; roi.end.y += delta.y;
            for (ImVec2& point : roi.points)
            {
                point.x += delta.x;
                point.y += delta.y;
            }
        };
        auto PointInPolygon = [&](const ROI& roi, const ImVec2& point)
        {
            bool inside = false;
            const std::size_t count = roi.points.size();
            if (count < 3)
                return false;
            for (std::size_t i = 0, j = count - 1; i < count; j = i++)
            {
                const ImVec2& a = roi.points[i];
                const ImVec2& b = roi.points[j];
                const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
                    (point.x < (b.x - a.x) * (point.y - a.y) /
                                   ((b.y - a.y) == 0.0f ? 0.0001f : (b.y - a.y)) + a.x);
                if (crosses)
                    inside = !inside;
            }
            return inside;
        };
        auto HitShape = [&](const ROI& roi)
        {
            switch (roi.type)
            {
            case ROI_TYPE_POINT:
                return Distance(imageMouse, roi.start) <= hitTolerance;
            case ROI_TYPE_LINE:
                return SegmentDistance(imageMouse, roi.start, roi.end) <= hitTolerance;
            case ROI_TYPE_CIRCLE:
                return Distance(imageMouse, roi.start) <= roi.CircleRadius() + hitTolerance;
            case ROI_TYPE_POLYGON:
                if (PointInPolygon(roi, imageMouse))
                    return true;
                for (std::size_t i = 0; i < roi.points.size(); ++i)
                    if (SegmentDistance(imageMouse, roi.points[i],
                            roi.points[(i + 1) % roi.points.size()]) <= hitTolerance)
                        return true;
                return false;
            case ROI_TYPE_RECT:
            default:
                return roi.Contains(imageMouse);
            }
        };

        static int previousROIType = gCurrentROIType;
        static bool geometryChanged = false;
        if (previousROIType != gCurrentROIType)
        {
            gDrawingROI = false;
            gPolygonDraftPoints.clear();
            previousROIType = gCurrentROIType;
        }
        if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            gDrawingROI = false;
            gPolygonDraftPoints.clear();
        }

        auto AddCompletedROI = [&](ROI roi)
        {
            roi.ClampToImage(ImageState::Current().size());
            EnsureROIRuntimeId(roi);
            const int index = ROIState::Add(roi, true);
            AdvanceROIDrawSequence(roi);
            ROIState::SetSelectedIndex(index);
            MarkCurrentRecipeDirty();
        };
        auto CommitPolygon = [&]()
        {
            if (gPolygonDraftPoints.size() >= 3)
            {
                ROI roi;
                roi.type = ROI_TYPE_POLYGON;
                roi.points = gPolygonDraftPoints;
                UpdatePolygonBounds(roi);
                AddCompletedROI(std::move(roi));
            }
            gPolygonDraftPoints.clear();
            gDrawingROI = false;
        };

        // 创建：点为单击；矩形/线/圆为拖动；多边形为逐点单击，双击/Enter/首点闭合。
        if (gCurrentROIType == ROI_TYPE_POLYGON)
        {
            if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Enter))
                CommitPolygon();
            else if (canvasHovered && gPolygonDraftPoints.size() >= 3 &&
                     ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
                CommitPolygon();
            else if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                if (gPolygonDraftPoints.size() >= 3 &&
                    Distance(mouse, ImageToScreenPos(gPolygonDraftPoints.front())) <= HANDLE_SIZE * 2.0f)
                {
                    CommitPolygon();
                }
                else
                {
                    gPolygonDraftPoints.push_back(imageMouse);
                    gDrawingROI = true;
                }
            }
        }
        else if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            if (gCurrentROIType == ROI_TYPE_POINT)
            {
                ROI roi;
                roi.type = ROI_TYPE_POINT;
                roi.start = roi.end = imageMouse;
                AddCompletedROI(std::move(roi));
                gDrawingROI = false;
            }
            else
            {
                gDrawingROI = true;
                gROIStart = imageMouse;
            }
        }
        if (gCurrentROIType != ROI_TYPE_POLYGON &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right) && gDrawingROI)
        {
            ROI roi;
            roi.type = gCurrentROIType;
            roi.start = gROIStart;
            roi.end = imageMouse;
            const float dx = roi.end.x - roi.start.x;
            const float dy = roi.end.y - roi.start.y;
            bool valid = false;
            if (roi.type == ROI_TYPE_LINE)
                valid = std::hypot(dx, dy) > 2.0f;
            else if (roi.type == ROI_TYPE_CIRCLE)
            {
                const float radius = std::hypot(dx, dy);
                roi.end = ImVec2(roi.start.x + radius, roi.start.y);
                valid = radius > 2.0f;
            }
            else if (roi.type == ROI_TYPE_RECT)
            {
                NormalizeROI(roi);
                valid = roi.Width() > 2.0f && roi.Height() > 2.0f;
            }
            if (valid)
                AddCompletedROI(std::move(roi));
            gDrawingROI = false;
        }

        auto CheckHandle = [&](const ImVec2& point, HandleType handle, int roiIndex,
                               int pointIndex = -1)
        {
            if (!s_rois[roiIndex].visible || s_rois[roiIndex].locked)
                return false;
            if (Distance(mouse, ImageToScreenPos(point)) > HANDLE_SIZE * 2.0f)
                return false;
            ROIState::BeginHistoryTransaction();
            ROIState::SetSelectedIndex(roiIndex);
            gActiveHandle = handle;
            gActivePointIndex = pointIndex;
            gDraggingROI = true;
            gLastMousePos = imageMouse;
            return true;
        };

        // 选择和编辑：先命中控制点，再命中形状本体；后绘制的 ROI 优先。
        if (canvasHovered && !gDrawingROI && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            gActiveHandle = HANDLE_NONE;
            gActivePointIndex = -1;
            geometryChanged = false;
            bool hit = false;
            const int i = ROIState::SelectedIndex();
            if (i >= 0 && i < static_cast<int>(s_rois.size()))
            {
                const ROI& roi = s_rois[i];
                if (roi.type == ROI_TYPE_RECT)
                {
                    const Box box = GetBox(roi);
                    hit = CheckHandle(box.rotate, HANDLE_ROTATE, i) ||
                          CheckHandle(box.lt, HANDLE_LT, i) || CheckHandle(box.rt, HANDLE_RT, i) ||
                          CheckHandle(box.lb, HANDLE_LB, i) || CheckHandle(box.rb, HANDLE_RB, i) ||
                          CheckHandle(box.t, HANDLE_T, i) || CheckHandle(box.b, HANDLE_B, i) ||
                          CheckHandle(box.l, HANDLE_L, i) || CheckHandle(box.r, HANDLE_R, i);
                }
                else if (roi.type == ROI_TYPE_POINT)
                    hit = CheckHandle(roi.start, HANDLE_CENTER, i);
                else if (roi.type == ROI_TYPE_LINE)
                    hit = CheckHandle(roi.start, HANDLE_LT, i) || CheckHandle(roi.end, HANDLE_RB, i);
                else if (roi.type == ROI_TYPE_CIRCLE)
                    hit = CheckHandle(roi.end, HANDLE_R, i) || CheckHandle(roi.start, HANDLE_CENTER, i);
                else if (roi.type == ROI_TYPE_POLYGON)
                {
                    for (int pointIndex = static_cast<int>(roi.points.size()) - 1;
                         pointIndex >= 0 && !hit; --pointIndex)
                        hit = CheckHandle(roi.points[pointIndex], HANDLE_LT, i, pointIndex);
                }
            }
            if (!hit)
            {
                for (int i = static_cast<int>(s_rois.size()) - 1; i >= 0; --i)
                {
                    if (!s_rois[i].visible)
                        continue;
                    if (!HitShape(s_rois[i]))
                        continue;
                    ROIState::SetSelectedIndex(i);
                    if (!s_rois[i].locked)
                    {
                        ROIState::BeginHistoryTransaction();
                        gActiveHandle = HANDLE_CENTER;
                        gDraggingROI = true;
                        gLastMousePos = imageMouse;
                    }
                    hit = true;
                    break;
                }
            }
            if (!hit)
                ROIState::SetSelectedIndex(-1);
        }

        if (canvasHovered && ROIState::SelectedIndex() >= 0 &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
        {
            const ROI* selected = ROIState::At(ROIState::SelectedIndex());
            if (selected && !selected->locked &&
                ROIState::RemoveAt(ROIState::SelectedIndex()))
                MarkCurrentRecipeDirty();
            gActiveHandle = HANDLE_NONE;
            gActivePointIndex = -1;
            gDraggingROI = false;
            geometryChanged = false;
        }

        const int selectedROIIndex = ROIState::SelectedIndex();
        if (gActiveHandle != HANDLE_NONE && selectedROIIndex >= 0 &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !s_rois[selectedROIIndex].locked)
        {
            ROI roi = s_rois[selectedROIIndex];
            if (Distance(imageMouse, gLastMousePos) > 0.001f)
                geometryChanged = true;
            if (gActiveHandle == HANDLE_CENTER)
            {
                const ImVec2 delta(imageMouse.x - gLastMousePos.x,
                                   imageMouse.y - gLastMousePos.y);
                TranslateROI(roi, delta);
                gLastMousePos = imageMouse;
            }
            else if (roi.type == ROI_TYPE_RECT && gActiveHandle == HANDLE_ROTATE)
            {
                const ImVec2 center = roi.Center();
                roi.angle = static_cast<float>(std::atan2(imageMouse.y - center.y,
                    imageMouse.x - center.x) * 180.0 / CV_PI + 90.0);
                roi.angle = ROI::NormalizeRectangle2AngleDegrees(roi.angle);
            }
            else if (roi.type == ROI_TYPE_RECT)
                ResizeRotatedRect(roi, gActiveHandle, imageMouse);
            else if (roi.type == ROI_TYPE_LINE)
            {
                if (gActiveHandle == HANDLE_LT) roi.start = imageMouse;
                if (gActiveHandle == HANDLE_RB) roi.end = imageMouse;
            }
            else if (roi.type == ROI_TYPE_CIRCLE && gActiveHandle == HANDLE_R)
            {
                const float radius = (std::max)(2.0f, Distance(roi.start, imageMouse));
                roi.end = ImVec2(roi.start.x + radius, roi.start.y);
            }
            else if (roi.type == ROI_TYPE_POLYGON &&
                     gActivePointIndex >= 0 &&
                     gActivePointIndex < static_cast<int>(roi.points.size()))
            {
                roi.points[gActivePointIndex] = imageMouse;
                UpdatePolygonBounds(roi);
            }
            roi.ClampToImage(ImageState::Current().size());
            ROIState::Update(selectedROIIndex, std::move(roi));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (geometryChanged && gActiveHandle != HANDLE_NONE && selectedROIIndex >= 0)
            {
                ROIState::CommitHistoryTransaction();
                MarkCurrentRecipeDirty();
            }
            else
            {
                ROIState::CancelHistoryTransaction();
            }
            gDraggingROI = false;
            gActiveHandle = HANDLE_NONE;
            gActivePointIndex = -1;
            geometryChanged = false;
        }

        auto DrawSquareHandle = [&](const ImVec2& point)
        {
            const ImVec2 p = ImageToScreenPos(point);
            const float size = HANDLE_SIZE * 0.7f;
            drawList->AddRectFilled(ImVec2(p.x - size, p.y - size), ImVec2(p.x + size, p.y + size),
                                    IM_COL32(255, 255, 255, 255));
            drawList->AddRect(ImVec2(p.x - size, p.y - size), ImVec2(p.x + size, p.y + size),
                              IM_COL32(0, 0, 0, 255));
        };

        for (int i = 0; i < static_cast<int>(s_rois.size()); ++i)
        {
            const ROI& roi = s_rois[i];
            if (!roi.visible)
                continue;
            const bool selected = i == ROIState::SelectedIndex();
            const ImU32 color = GetROIColor(roi.type, selected);
            const float thickness = selected ? 2.5f : 2.0f;
            const ImVec2 start = ImageToScreenPos(roi.start);
            const ImVec2 end = ImageToScreenPos(roi.end);
            if (roi.type == ROI_TYPE_RECT)
            {
                const auto corners = roi.Corners();
                ImVec2 screenCorners[4] = {ImageToScreenPos(corners[0]), ImageToScreenPos(corners[1]),
                                           ImageToScreenPos(corners[2]), ImageToScreenPos(corners[3])};
                drawList->AddPolyline(screenCorners, 4, color, ImDrawFlags_Closed, thickness);
                if (selected && !roi.locked)
                {
                    const Box box = GetBox(roi);
                    DrawSquareHandle(box.lt); DrawSquareHandle(box.rt);
                    DrawSquareHandle(box.lb); DrawSquareHandle(box.rb);
                    DrawSquareHandle(box.t); DrawSquareHandle(box.b);
                    DrawSquareHandle(box.l); DrawSquareHandle(box.r);
                    const ImVec2 top = ImageToScreenPos(box.t);
                    const ImVec2 rotate = ImageToScreenPos(box.rotate);
                    drawList->AddLine(top, rotate, IM_COL32(255, 170, 0, 255), 1.5f);
                    drawList->AddCircleFilled(rotate, HANDLE_SIZE * 0.75f, IM_COL32(255, 140, 0, 255));
                    drawList->AddCircle(rotate, HANDLE_SIZE * 0.75f, IM_COL32(0, 0, 0, 255));
                }
            }
            else if (roi.type == ROI_TYPE_POINT)
            {
                drawList->AddLine(ImVec2(start.x - 10, start.y), ImVec2(start.x + 10, start.y), color, thickness);
                drawList->AddLine(ImVec2(start.x, start.y - 10), ImVec2(start.x, start.y + 10), color, thickness);
                drawList->AddCircleFilled(start, 4.0f, color);
                if (selected && !roi.locked) drawList->AddCircle(start, 7.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
            }
            else if (roi.type == ROI_TYPE_LINE)
            {
                drawList->AddLine(start, end, color, thickness);
                if (selected && !roi.locked) { DrawSquareHandle(roi.start); DrawSquareHandle(roi.end); }
            }
            else if (roi.type == ROI_TYPE_CIRCLE)
            {
                const float radius = roi.CircleRadius() * gZoom;
                drawList->AddCircle(start, radius, color, 0, thickness);
                drawList->AddLine(start, end, color, 1.0f);
                drawList->AddCircleFilled(start, 4.0f, color);
                if (selected && !roi.locked) { DrawSquareHandle(roi.start); DrawSquareHandle(roi.end); }
            }
            else if (roi.type == ROI_TYPE_POLYGON && roi.points.size() >= 2)
            {
                std::vector<ImVec2> points;
                points.reserve(roi.points.size());
                for (const ImVec2& point : roi.points) points.push_back(ImageToScreenPos(point));
                drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color,
                                      ImDrawFlags_Closed, thickness);
                if (selected && !roi.locked)
                    for (const ImVec2& point : roi.points) DrawSquareHandle(point);
            }
        }

        const ImU32 previewColor = GetROIColor(gCurrentROIType, true);
        if (gDrawingROI && gCurrentROIType != ROI_TYPE_POLYGON)
        {
            const ImVec2 start = ImageToScreenPos(gROIStart);
            if (gCurrentROIType == ROI_TYPE_RECT)
            {
                const ImVec2 minimum((std::min)(start.x, mouse.x), (std::min)(start.y, mouse.y));
                const ImVec2 maximum((std::max)(start.x, mouse.x), (std::max)(start.y, mouse.y));
                drawList->AddRect(minimum, maximum, previewColor, 0, 0, 2.0f);
            }
            else if (gCurrentROIType == ROI_TYPE_LINE)
                drawList->AddLine(start, mouse, previewColor, 2.0f);
            else if (gCurrentROIType == ROI_TYPE_CIRCLE)
            {
                drawList->AddCircle(start, Distance(start, mouse), previewColor, 0, 2.0f);
                drawList->AddLine(start, mouse, previewColor, 1.0f);
            }
        }
        if (!gPolygonDraftPoints.empty())
        {
            std::vector<ImVec2> points;
            points.reserve(gPolygonDraftPoints.size() + 1);
            for (const ImVec2& point : gPolygonDraftPoints)
            {
                points.push_back(ImageToScreenPos(point));
                DrawSquareHandle(point);
            }
            points.push_back(mouse);
            if (points.size() >= 2)
                drawList->AddPolyline(points.data(), static_cast<int>(points.size()), previewColor, 0, 2.0f);
        }
    }

} // namespace UI
