#pragma once
#include "DockSpaceHost.h"   // ROI 结构体

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
