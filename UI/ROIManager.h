#pragma once
#include "../include/imgui/imgui.h"
#include "../Core/ROI.h"
#include "../Core/ROIEditorState.h"

#include <initializer_list>

// =====================================================
namespace UI
{

    inline bool& gDrawingROI = ROIEditorState::Drawing();
    inline ImVec2& gROIStart = ROIEditorState::DrawStart();
    inline bool& gDraggingROI = ROIEditorState::Dragging();
    inline ImVec2& gLastMousePos = ROIEditorState::LastMousePosition();
    inline HandleType& gActiveHandle = ROIEditorState::ActiveHandle();
    inline int& gActivePointIndex = ROIEditorState::ActivePointIndex();
    inline int& gHoveredROI = ROIEditorState::HoveredROI();
    inline int& gCurrentROIType = ROIEditorState::CurrentROIType(); // 当前操作的ROI类型 (0-4)
    inline std::vector<ImVec2>& gPolygonDraftPoints = ROIEditorState::PolygonDraftPoints();

    // 根据 ROI 类型返回对应颜色
    inline ImU32 GetROIColor(int type, bool selected)
    {
        switch (type)
        {
        case ROI_TYPE_RECT:    return selected ? IM_COL32(0, 255, 0, 255) : IM_COL32(0, 180, 0, 255);    // 绿色
        case ROI_TYPE_POINT:   return selected ? IM_COL32(255, 200, 0, 255) : IM_COL32(200, 160, 0, 255); // 金黄
        case ROI_TYPE_LINE:    return selected ? IM_COL32(0, 220, 220, 255) : IM_COL32(0, 180, 180, 255); // 青色
        case ROI_TYPE_CIRCLE:  return selected ? IM_COL32(255, 140, 0, 255) : IM_COL32(200, 100, 0, 255); // 橙色
        case ROI_TYPE_POLYGON: return selected ? IM_COL32(180, 0, 255, 255) : IM_COL32(140, 0, 200, 255); // 紫色
        default:               return selected ? IM_COL32(255, 0, 0, 255) : IM_COL32(0, 255, 0, 255);
        }
    }

    // =====================================================
    // 坐标转换辅助函数
    // =====================================================
    ImVec2 ImageToScreenPos(const ImVec2 &p);
    ImVec2 ScreenToImagePos(const ImVec2 &p);
    void NormalizeROI(ROI &roi);

    // =====================================================
    // ROI 工具函数
    // =====================================================
    void PrintROIToLog();        // 打印所有ROI到日志
    void ZoomAtCenter(float d);  // 以鼠标为中心缩放
    void ClearROIState();        // 清理ROI状态
    void HandleROIInteraction(); // ROI 交互处理（创建/选中/拖动/删除/绘制）
    void BeginROIDrawSequence(std::initializer_list<int> roiTypes);
    void CancelROIDrawSequence();
    bool IsROIDrawSequenceActive();
    int ROIDrawSequenceStep();
    int ROIDrawSequenceCount();
    void AdvanceROIDrawSequence(const ROI& completedROI);
    bool ConsumeCompletedROIDrawSequence(std::vector<ROI>& completedROIs);
    std::uint64_t EnsureROIRuntimeId(ROI& roi);

} // namespace UI
