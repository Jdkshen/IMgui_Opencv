#include "../Windows_imgui.h"
#include "ROIManager.h"
#include "../Core/DX12Context.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/TemplateMatch.h"

namespace UI
{

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
int    gCurrentROIType = ROI_TYPE_GENERAL;

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
    gZoom = std::clamp(gZoom, 0.005f, 50.0f);  // 最小0.5%，最大5000%
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

        LogSystem::Add(LOG_INFO, color, "ROI[%d] type=%d rect=(%.6f %.6f %.6f %.6f)",
            i, roi.type, x1, y1, x2 - x1, y2 - y1);
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

// =====================================================
// ROI 交互处理（创建/选中/拖动/删除/绘制）
// =====================================================
void HandleROIInteraction()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
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
            roi.end   = imageMouse;
            roi.type  = gCurrentROIType;  // 新ROI使用当前选中的类型
            NormalizeROI(roi);
            if (fabs(roi.start.x - roi.end.x) > 2 && fabs(roi.start.y - roi.end.y) > 2)
                gROIs.push_back(roi);
        }
        gDrawingROI = false;
    }

    // 左键释放：停止拖动/缩放
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        gDraggingROI = false;
        gActiveHandle = HANDLE_NONE;
    }

    struct Box { ImVec2 lt, rt, lb, rb, t, b, l, r, c; };
    auto GetBox = [&](const ROI& roi) -> Box
    {
        float minX = std::min(roi.start.x, roi.end.x);
        float maxX = std::max(roi.start.x, roi.end.x);
        float minY = std::min(roi.start.y, roi.end.y);
        float maxY = std::max(roi.start.y, roi.end.y);
        return Box{
            {minX,minY},{maxX,minY},{minX,maxY},{maxX,maxY},
            {(minX+maxX)*0.5f,minY},{(minX+maxX)*0.5f,maxY},
            {minX,(minY+maxY)*0.5f},{maxX,(minY+maxY)*0.5f},
            {(minX+maxX)*0.5f,(minY+maxY)*0.5f}
        };
    };

    auto CheckHandle = [&](ImVec2 p, HandleType type, int i) -> bool
    {
        ImVec2 sp = ImageToScreenPos(p);
        float dx = mouse.x - sp.x;
        float dy = mouse.y - sp.y;
        if (sqrtf(dx*dx + dy*dy) < HANDLE_SIZE * 2.0f)
        { gSelectedROI = i; gActiveHandle = type; return true; }
        return false;
    };

    // 左键点击：检测是否点击到ROI的控制点或内部区域
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        gSelectedROI = -1;
        gActiveHandle = HANDLE_NONE;

        // 只对当前类型的ROI进行交互
        for (int i = 0; i < (int)gROIs.size(); i++)
        {
            auto& roi = gROIs[i];
            if (roi.type != gCurrentROIType) continue;
            Box box = GetBox(roi);

            if (CheckHandle(box.lt, HANDLE_LT, i)) break;
            if (CheckHandle(box.rt, HANDLE_RT, i)) break;
            if (CheckHandle(box.lb, HANDLE_LB, i)) break;
            if (CheckHandle(box.rb, HANDLE_RB, i)) break;
            if (CheckHandle(box.t, HANDLE_T, i)) break;
            if (CheckHandle(box.b, HANDLE_B, i)) break;
            if (CheckHandle(box.l, HANDLE_L, i)) break;
            if (CheckHandle(box.r, HANDLE_R, i)) break;
            if (CheckHandle(box.c, HANDLE_CENTER, i)) break;

            float minX = std::min(roi.start.x, roi.end.x);
            float maxX = std::max(roi.start.x, roi.end.x);
            float minY = std::min(roi.start.y, roi.end.y);
            float maxY = std::max(roi.start.y, roi.end.y);

            if (imageMouse.x >= minX && imageMouse.x <= maxX &&
                imageMouse.y >= minY && imageMouse.y <= maxY)
            { gSelectedROI = i; break; }
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
        auto& roi = gROIs[gSelectedROI];

        // 四边中点/中心：整体移动ROI
        if (gActiveHandle >= HANDLE_T)
        {
            if (!gDraggingROI) { gDraggingROI = true; gLastMousePos = imageMouse; }
            ImVec2 delta(imageMouse.x - gLastMousePos.x, imageMouse.y - gLastMousePos.y);
            roi.start.x += delta.x; roi.start.y += delta.y;
            roi.end.x += delta.x;   roi.end.y += delta.y;
            gLastMousePos = imageMouse;
        }
        // 四角：拉伸/缩放ROI
        else switch (gActiveHandle)
        {
        case HANDLE_LT: roi.start = imageMouse; break;
        case HANDLE_RB: roi.end = imageMouse; break;
        case HANDLE_RT: roi.start.y = imageMouse.y; roi.end.x = imageMouse.x; break;
        case HANDLE_LB: roi.start.x = imageMouse.x; roi.end.y = imageMouse.y; break;
        }

        if (gActiveHandle < HANDLE_T) NormalizeROI(roi);
    }

    // ===== 绘制所有ROI矩形 + 标签 =====
    for (int i = 0; i < (int)gROIs.size(); i++)
    {
        auto& roi = gROIs[i];
        bool selected = (i == gSelectedROI);
        ImU32 col = GetROIColor(roi.type, selected);

        // 非当前类型的ROI用半透明，当前类型实线
        if (roi.type != gCurrentROIType)
            col = (col & 0x00FFFFFF) | 0x80000000;  // 50% 透明度

        ImVec2 p1 = ImageToScreenPos(roi.start);
        ImVec2 p2 = ImageToScreenPos(roi.end);
        drawList->AddRect(p1, p2, col, 0, 0, 2.0f);

        ImVec2 pc = ImageToScreenPos(ImVec2(
            (roi.start.x+roi.end.x)*0.5f, (roi.start.y+roi.end.y)*0.5f));
        drawList->AddCircleFilled(pc, 4.0f, col);
        drawList->AddCircle(pc, 4.0f, IM_COL32(255,255,255,255), 0, 1.0f);

        // 在当前类型ROI旁显示类型标签
        if (roi.type == gCurrentROIType)
        {
            const char* typeLabel = "?";
            switch (roi.type)
            {
            case ROI_TYPE_TEMPLATE:    typeLabel = "模板"; break;
            case ROI_TYPE_RECOGNITION: typeLabel = "识别"; break;
            case ROI_TYPE_RESERVED3:   typeLabel = "类型3"; break;
            case ROI_TYPE_RESERVED4:   typeLabel = "类型4"; break;
            default:                   typeLabel = "通用"; break;
            }
            drawList->AddText(ImVec2(p1.x + 3, p1.y - ImGui::GetTextLineHeight() - 2),
                IM_COL32(255,255,255,255), typeLabel);
        }
    }

    TemplateMatch::DrawMatches(drawList);

    if (gDrawingROI)
    {
        ImVec2 p1 = ImageToScreenPos(gROIStart);
        ImVec2 p2 = ImageToScreenPos(imageMouse);
        ImU32 drawCol = GetROIColor(gCurrentROIType, true);
        drawList->AddRect(p1, p2, drawCol);
    }
}

} // namespace UI
