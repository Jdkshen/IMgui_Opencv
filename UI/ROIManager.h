#pragma once
#include "DockSpaceHost.h"   // ROI 结构体

// =====================================================
// 图像显示/视图变换状态（extern，定义在 ImageViewer.cpp）
// =====================================================
extern float  gZoom;
extern ImVec2 gPan;
extern ImVec2 gCanvasSize;
extern ImVec2 gImageScreenPos;
extern ImVec2 imageScreenPos;

namespace UI
{

// =====================================================
// ROI 数据与交互状态（extern 声明）
// =====================================================
extern std::vector<ROI> gROIs;
extern bool   gDrawingROI;
extern ImVec2 gROIStart;
extern int    gSelectedROI;
extern bool   gDraggingROI;
extern ImVec2 gLastMousePos;
extern HandleType gActiveHandle;
extern int    gHoveredROI;
extern int    gCurrentROIType;  // 当前操作的ROI类型 (0-4)

// 根据 ROI 类型返回对应颜色
inline ImU32 GetROIColor(int type, bool selected)
{
    switch (type)
    {
    case ROI_TYPE_TEMPLATE:    return selected ? IM_COL32(255,200,0,255)   : IM_COL32(200,160,0,255);   // 金黄
    case ROI_TYPE_RECOGNITION: return selected ? IM_COL32(0,220,220,255)  : IM_COL32(0,180,180,255);   // 青色
    case ROI_TYPE_RESERVED3:   return selected ? IM_COL32(255,140,0,255)  : IM_COL32(200,100,0,255);   // 橙色
    case ROI_TYPE_RESERVED4:   return selected ? IM_COL32(180,0,255,255)  : IM_COL32(140,0,200,255);   // 紫色
    default:                   return selected ? IM_COL32(255,0,0,255)    : IM_COL32(0,255,0,255);     // 绿/红（通用）
    }
}

// =====================================================
// 坐标转换辅助函数
// =====================================================
ImVec2 ImageToScreenPos(const ImVec2& p);
ImVec2 ScreenToImagePos(const ImVec2& p);
void   NormalizeROI(ROI& roi);

// =====================================================
// ROI 工具函数
// =====================================================
void   PrintROIToLog();       // 打印所有ROI到日志
void   ZoomAtCenter(float d); // 以鼠标为中心缩放
void   ClearROIState();       // 清理ROI状态
void   HandleROIInteraction(); // ROI 交互处理（创建/选中/拖动/删除/绘制）

} // namespace UI
