#include "../Windows_imgui.h"
#include "ROIManager.h"
#include "../Core/DX12Context.h"
#include "../Core/ROIState.h"
#include "../Core/VisionContext.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/ITool.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/LineDetector.h"

namespace UI
{

    // =====================================================
    // ROI 全局变量定义
    // =====================================================
    std::vector<ROI>& gROIs = ROIState::Items();
    bool gDrawingROI = false;
    ImVec2 gROIStart;
    int& gSelectedROI = ROIState::SelectedIndexRef();
    bool gDraggingROI = false;
    ImVec2 gLastMousePos;
    HandleType gActiveHandle = HANDLE_NONE;
    int gHoveredROI = -1;
    int gCurrentROIType = ROI_TYPE_RECT;

    namespace
    {
        std::vector<int> s_roiDrawSequence;
        std::vector<ROI> s_roiDrawSequenceROIs;
        int s_roiDrawSequenceStep = 0;
        std::uint64_t s_nextRuntimeROIId = 1;
    }

    void BeginROIDrawSequence(std::initializer_list<int> roiTypes)
    {
        s_roiDrawSequence.assign(roiTypes.begin(), roiTypes.end());
        s_roiDrawSequenceROIs.clear();
        s_roiDrawSequenceStep = 0;
        gDrawingROI = false;
        if (!s_roiDrawSequence.empty())
            gCurrentROIType = s_roiDrawSequence.front();
    }

    std::uint64_t EnsureROIRuntimeId(ROI& roi)
    {
        if (roi.runtimeId == 0)
            roi.runtimeId = s_nextRuntimeROIId++;
        return roi.runtimeId;
    }

    void CancelROIDrawSequence()
    {
        s_roiDrawSequence.clear();
        s_roiDrawSequenceROIs.clear();
        s_roiDrawSequenceStep = 0;
        gDrawingROI = false;
    }

    bool IsROIDrawSequenceActive()
    {
        return s_roiDrawSequenceStep >= 0 &&
            s_roiDrawSequenceStep < static_cast<int>(s_roiDrawSequence.size());
    }

    int ROIDrawSequenceStep()
    {
        return IsROIDrawSequenceActive() ? s_roiDrawSequenceStep : -1;
    }

    int ROIDrawSequenceCount()
    {
        return static_cast<int>(s_roiDrawSequence.size());
    }

    void AdvanceROIDrawSequence(const ROI& completedROI)
    {
        if (!IsROIDrawSequenceActive() ||
            s_roiDrawSequence[s_roiDrawSequenceStep] != completedROI.type)
            return;

        s_roiDrawSequenceROIs.push_back(completedROI);
        ++s_roiDrawSequenceStep;
        if (IsROIDrawSequenceActive())
            gCurrentROIType = s_roiDrawSequence[s_roiDrawSequenceStep];
        else
            gDrawingROI = false;
    }

    bool ConsumeCompletedROIDrawSequence(std::vector<ROI>& completedROIs)
    {
        if (IsROIDrawSequenceActive() || s_roiDrawSequence.empty() ||
            s_roiDrawSequenceROIs.size() != s_roiDrawSequence.size())
            return false;

        completedROIs = s_roiDrawSequenceROIs;
        CancelROIDrawSequence();
        return true;
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

        LogSystem::Add(LOG_INFO, color, "reproject=(%.6f,%.6f) len=%.6f", error.x, error.y, dist);

        for (int i = 0; i < (int)gROIs.size(); i++)
        {
            const auto &roi = gROIs[i];
            const char* tname = "?";
            switch (roi.type) {
            case ROI_TYPE_RECT: tname = "Rect"; break;
            case ROI_TYPE_POINT: tname = "Point"; break;
            case ROI_TYPE_LINE: tname = "Line"; break;
            case ROI_TYPE_CIRCLE: tname = "Circle"; break;
            case ROI_TYPE_POLYGON: tname = "Polygon"; break;
            }
            if (roi.type == ROI_TYPE_POINT)
                LogSystem::Add(LOG_INFO, color, "ROI[%d] %s pos=(%.1f,%.1f)",
                               i, tname, roi.start.x, roi.start.y);
            else if (roi.type == ROI_TYPE_CIRCLE)
                LogSystem::Add(LOG_INFO, color, "ROI[%d] %s center=(%.1f,%.1f) r=%.1f",
                               i, tname, roi.start.x, roi.start.y, roi.CircleRadius());
            else if (roi.type == ROI_TYPE_POLYGON)
                LogSystem::Add(LOG_INFO, color, "ROI[%d] %s pts=%zu",
                               i, tname, roi.points.size());
            else
            {
                float x1 = (std::min)(roi.start.x, roi.end.x);
                float y1 = (std::min)(roi.start.y, roi.end.y);
                float x2 = (std::max)(roi.start.x, roi.end.x);
                float y2 = (std::max)(roi.start.y, roi.end.y);
                LogSystem::Add(LOG_INFO, color, "ROI[%d] %s rect=(%.1f %.1f %.1f %.1f)",
                               i, tname, x1, y1, x2 - x1, y2 - y1);
            }
        }
    }

    // =====================================================
    // 清理ROI状态
    // =====================================================
    void ClearROIState()
    {
        gROIs.clear();
        gDrawingROI = false;
        CancelROIDrawSequence();
        gSelectedROI = -1;
        gActiveHandle = HANDLE_NONE;
        gDraggingROI = false;
        g_YoloOverlays.clear();
        g_YoloShowOverlay = false;
        gContext.ClearUnifiedResults();
        gContext.rois.clear();
        gContext.selectedROI = -1;
    }

    // =====================================================
    // ROI 交互处理（创建/选中/拖动/删除/绘制）
    // =====================================================
    void HandleROIInteraction()
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 imageMouse = ScreenToImagePos(mouse);

        // 右键按下：开始绘制新ROI
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            gDrawingROI = true;
            gROIStart = imageMouse;
        }
        // 右键释放：完成ROI绘制（最小尺寸过滤）
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (gDrawingROI)
            {
                ROI roi;
                roi.start = gROIStart;
                roi.end = imageMouse;
                roi.type = gCurrentROIType; // 新ROI使用当前选中的类型
                NormalizeROI(roi);
                const float dx = roi.end.x - roi.start.x;
                const float dy = roi.end.y - roi.start.y;
                bool valid = false;
                switch (roi.type)
                {
                case ROI_TYPE_POINT:
                    roi.end = roi.start;
                    valid = true;
                    break;
                case ROI_TYPE_LINE:
                    valid = std::hypot(dx, dy) > 2.0f;
                    break;
                case ROI_TYPE_CIRCLE:
                {
                    const float radius = std::hypot(dx, dy);
                    roi.end = ImVec2(roi.start.x + radius, roi.start.y);
                    valid = radius > 2.0f;
                    break;
                }
                case ROI_TYPE_RECT:
                case ROI_TYPE_POLYGON:
                    valid = std::abs(dx) > 2.0f && std::abs(dy) > 2.0f;
                    break;
                default:
                    break;
                }
                if (valid)
                {
                    EnsureROIRuntimeId(roi);
                    gROIs.push_back(roi);
                    AdvanceROIDrawSequence(roi);
                }
            }
            gDrawingROI = false;
        }

        // 左键释放：停止拖动/缩放
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            gDraggingROI = false;
            gActiveHandle = HANDLE_NONE;
        }

        struct Box
        {
            ImVec2 lt, rt, lb, rb, t, b, l, r, c;
        };
        auto GetBox = [&](const ROI &roi) -> Box
        {
            float minX = std::min(roi.start.x, roi.end.x);
            float maxX = std::max(roi.start.x, roi.end.x);
            float minY = std::min(roi.start.y, roi.end.y);
            float maxY = std::max(roi.start.y, roi.end.y);
            return Box{
                {minX, minY}, {maxX, minY}, {minX, maxY}, {maxX, maxY}, {(minX + maxX) * 0.5f, minY}, {(minX + maxX) * 0.5f, maxY}, {minX, (minY + maxY) * 0.5f}, {maxX, (minY + maxY) * 0.5f}, {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f}};
        };

        auto CheckHandle = [&](ImVec2 p, HandleType type, int i) -> bool
        {
            ImVec2 sp = ImageToScreenPos(p);
            float dx = mouse.x - sp.x;
            float dy = mouse.y - sp.y;
            if (sqrtf(dx * dx + dy * dy) < HANDLE_SIZE * 2.0f)
            {
                gSelectedROI = i;
                gActiveHandle = type;
                return true;
            }
            return false;
        };

        // 左键点击：检测是否点击到ROI的控制点或内部区域
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            gSelectedROI = -1;
            gActiveHandle = HANDLE_NONE;

            // 先检查当前类型ROI的控制点（8方向+中心）
            for (int i = 0; i < (int)gROIs.size(); i++)
            {
                auto &roi = gROIs[i];
                if (roi.type != gCurrentROIType)
                    continue;
                Box box = GetBox(roi);

                if (CheckHandle(box.lt, HANDLE_LT, i))
                    break;
                if (CheckHandle(box.rt, HANDLE_RT, i))
                    break;
                if (CheckHandle(box.lb, HANDLE_LB, i))
                    break;
                if (CheckHandle(box.rb, HANDLE_RB, i))
                    break;
                if (CheckHandle(box.t, HANDLE_T, i))
                    break;
                if (CheckHandle(box.b, HANDLE_B, i))
                    break;
                if (CheckHandle(box.l, HANDLE_L, i))
                    break;
                if (CheckHandle(box.r, HANDLE_R, i))
                    break;
                if (CheckHandle(box.c, HANDLE_CENTER, i))
                    break;
            }

            // 控制点未命中：从后往前检查内部区域 → 直接进入移动模式
            if (gSelectedROI < 0)
            {
                for (int i = (int)gROIs.size() - 1; i >= 0; i--)
                {
                    auto &roi = gROIs[i];
                    float minX = std::min(roi.start.x, roi.end.x);
                    float maxX = std::max(roi.start.x, roi.end.x);
                    float minY = std::min(roi.start.y, roi.end.y);
                    float maxY = std::max(roi.start.y, roi.end.y);

                    if (imageMouse.x >= minX && imageMouse.x <= maxX &&
                        imageMouse.y >= minY && imageMouse.y <= maxY)
                    {
                        gSelectedROI = i;
                        gActiveHandle = HANDLE_CENTER; // 框内点击 = 直接移动
                        break;
                    }
                }
            }
        }

        // Delete键：删除选中的ROI
        if (gSelectedROI >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            gROIs.erase(gROIs.begin() + gSelectedROI);
            gSelectedROI = -1;
            gActiveHandle = HANDLE_NONE;
            gDraggingROI = false;
        }

        // 拖动/缩放：根据当前激活的控制点类型调整ROI
        if (gActiveHandle != HANDLE_NONE && gSelectedROI >= 0)
        {
            auto &roi = gROIs[gSelectedROI];

            if (gActiveHandle == HANDLE_CENTER)
            {
                if (!gDraggingROI)
                {
                    gDraggingROI = true;
                    gLastMousePos = imageMouse;
                }
                ImVec2 delta(imageMouse.x - gLastMousePos.x, imageMouse.y - gLastMousePos.y);
                roi.start.x += delta.x;
                roi.start.y += delta.y;
                roi.end.x += delta.x;
                roi.end.y += delta.y;
                gLastMousePos = imageMouse;
            }
            else
            {
                switch (gActiveHandle)
                {
                case HANDLE_LT: roi.start = imageMouse; break;
                case HANDLE_RT: roi.start.y = imageMouse.y; roi.end.x = imageMouse.x; break;
                case HANDLE_LB: roi.start.x = imageMouse.x; roi.end.y = imageMouse.y; break;
                case HANDLE_RB: roi.end = imageMouse; break;
                case HANDLE_T:  roi.start.y = imageMouse.y; break;
                case HANDLE_B:  roi.end.y = imageMouse.y; break;
                case HANDLE_L:  roi.start.x = imageMouse.x; break;
                case HANDLE_R:  roi.end.x = imageMouse.x; break;
                }
                NormalizeROI(roi);
            }
        }

        // ===== 绘制所有ROI（按类型区分形状） =====
        for (int i = 0; i < (int)gROIs.size(); i++)
        {
            auto &roi = gROIs[i];
            bool selected = (i == gSelectedROI);
            ImU32 col = GetROIColor(roi.type, selected);
            float thick = selected ? 2.5f : 2.0f;

            // 非当前类型的ROI用半透明
            if (roi.type != gCurrentROIType)
                col = (col & 0x00FFFFFF) | 0x80000000; // 50% 透明度

            ImVec2 sp = ImageToScreenPos(roi.start);
            ImVec2 ep = ImageToScreenPos(roi.end);

            switch (roi.type)
            {
            case ROI_TYPE_RECT:
            {
                drawList->AddRect(sp, ep, col, 0, 0, thick);

                // VisionPro 风格：选中时绘制 8 个控制点 + 黄色中心圆
                if (selected)
                {
                    Box box = GetBox(roi);
                    auto DrawHandle = [&](ImVec2 p)
                    {
                        ImVec2 hp = ImageToScreenPos(p);
                        drawList->AddRectFilled(
                            ImVec2(hp.x - HANDLE_SIZE, hp.y - HANDLE_SIZE),
                            ImVec2(hp.x + HANDLE_SIZE, hp.y + HANDLE_SIZE),
                            IM_COL32(255, 255, 255, 255));
                        drawList->AddRect(
                            ImVec2(hp.x - HANDLE_SIZE, hp.y - HANDLE_SIZE),
                            ImVec2(hp.x + HANDLE_SIZE, hp.y + HANDLE_SIZE),
                            IM_COL32(0, 0, 0, 255));
                    };
                    DrawHandle(box.lt); DrawHandle(box.rt);
                    DrawHandle(box.lb); DrawHandle(box.rb);
                    DrawHandle(box.t);  DrawHandle(box.b);
                    DrawHandle(box.l);  DrawHandle(box.r);

                    ImVec2 pc = ImageToScreenPos(box.c);
                    drawList->AddCircleFilled(pc, HANDLE_SIZE + 1, IM_COL32(255, 255, 0, 255));
                    drawList->AddCircle(pc, HANDLE_SIZE + 1, IM_COL32(0, 0, 0, 255), 0, 1.0f);
                }
                break;
            }
            case ROI_TYPE_POINT:
            {
                // 十字准星 + 中心圆
                float cs = 10.0f;
                drawList->AddLine(ImVec2(sp.x - cs, sp.y), ImVec2(sp.x + cs, sp.y), col, thick);
                drawList->AddLine(ImVec2(sp.x, sp.y - cs), ImVec2(sp.x, sp.y + cs), col, thick);
                drawList->AddCircleFilled(sp, 5.0f, col);
                drawList->AddCircle(sp, 5.0f, IM_COL32(255, 255, 255, 255), 0, 1.0f);
                break;
            }
            case ROI_TYPE_LINE:
            {
                drawList->AddLine(sp, ep, col, thick);
                // 端点圆
                drawList->AddCircleFilled(sp, 4.0f, col);
                drawList->AddCircleFilled(ep, 4.0f, col);
                if (selected)
                {
                    drawList->AddCircle(sp, 4.0f, IM_COL32(255, 255, 255, 255), 0, 1.0f);
                    drawList->AddCircle(ep, 4.0f, IM_COL32(255, 255, 255, 255), 0, 1.0f);
                }
                break;
            }
            case ROI_TYPE_CIRCLE:
            {
                float r = std::abs(ep.x - sp.x); // 屏幕像素半径
                drawList->AddCircle(sp, r, col, 0, thick);
                // 圆心
                drawList->AddCircleFilled(sp, 4.0f, col);
                drawList->AddCircle(sp, 4.0f, IM_COL32(255, 255, 255, 255), 0, 1.0f);
                // 半径线
                drawList->AddLine(sp, ImVec2(sp.x + r, sp.y), col, 1.0f);
                break;
            }
            case ROI_TYPE_POLYGON:
            {
                if (roi.points.size() >= 2)
                {
                    std::vector<ImVec2> screenPts;
                    screenPts.reserve(roi.points.size());
                    for (const auto& pt : roi.points)
                        screenPts.push_back(ImageToScreenPos(pt));
                    drawList->AddPolyline(screenPts.data(), (int)screenPts.size(), col, ImDrawFlags_Closed, thick);
                    // 顶点圆
                    for (const auto& spt : screenPts)
                        drawList->AddCircleFilled(spt, 3.5f, col);
                }
                else
                {
                    // 少于 2 点时退化为矩形框
                    drawList->AddRect(sp, ep, col, 0, 0, thick);
                }
                break;
            }
            } // switch

        }

        if (gDrawingROI)
        {
            ImVec2 p1 = ImageToScreenPos(gROIStart);
            ImVec2 p2 = ImageToScreenPos(imageMouse);
            ImU32 drawCol = GetROIColor(gCurrentROIType, true);
            switch (gCurrentROIType)
            {
            case ROI_TYPE_RECT:
                drawList->AddRect(p1, p2, drawCol, 0, 0, 2.0f);
                break;
            case ROI_TYPE_POINT:
                drawList->AddCircleFilled(p1, 5.0f, drawCol);
                drawList->AddLine(ImVec2(p1.x-10, p1.y), ImVec2(p1.x+10, p1.y), drawCol, 2.0f);
                drawList->AddLine(ImVec2(p1.x, p1.y-10), ImVec2(p1.x, p1.y+10), drawCol, 2.0f);
                break;
            case ROI_TYPE_LINE:
                drawList->AddLine(p1, p2, drawCol, 2.0f);
                break;
            case ROI_TYPE_CIRCLE:
            {
                float r = sqrtf((p2.x-p1.x)*(p2.x-p1.x) + (p2.y-p1.y)*(p2.y-p1.y));
                drawList->AddCircle(p1, r, drawCol, 0, 2.0f);
                drawList->AddLine(p1, p2, drawCol, 1.0f); // 半径线
                break;
            }
            case ROI_TYPE_POLYGON:
                drawList->AddRect(p1, p2, drawCol, 0, 0, 2.0f); // 先画包围框
                break;
            }
        }
    }

} // namespace UI
