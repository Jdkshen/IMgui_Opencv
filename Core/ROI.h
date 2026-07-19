#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/core/mat.hpp>
#include "imgui/imgui.h"

// =====================================================
// ROI 类型枚举
// =====================================================
enum ROIType : int
{
    ROI_TYPE_RECT = 0,       // 矩形（默认）
    ROI_TYPE_POINT = 1,      // 点
    ROI_TYPE_LINE = 2,       // 线段
    ROI_TYPE_CIRCLE = 3,     // 圆
    ROI_TYPE_POLYGON = 4,    // 多边形
    ROI_TYPE_COUNT = 5
};

// =====================================================
// ROI 数据结构（存储图像坐标）
// =====================================================
struct ROI
{
    std::uint64_t runtimeId = 0; // UI 运行时关联标识，不写入配方
    int type = ROI_TYPE_RECT;    // ROI 类型

    // 矩形/线段：start/end 为对角/端点
    // 点：仅 start 有效
    // 圆：start 为圆心，(end.x-start.x) 为半径
    // 多边形：points 存储所有顶点（也存 start/end 为包围盒）
    ImVec2 start = {0, 0};
    ImVec2 end = {0, 0};
    std::vector<ImVec2> points;  // 多边形顶点（type==POLYGON 时有效）

    cv::Rect ToCvRect() const
    {
        return cv::Rect(
            (int)(std::min)(start.x, end.x), (int)(std::min)(start.y, end.y),
            (int)std::abs(end.x - start.x), (int)std::abs(end.y - start.y));
    }

    float CircleRadius() const { return std::abs(end.x - start.x); }

    bool IsEmpty() const
    {
        if (type == ROI_TYPE_POLYGON) return points.empty();
        if (type == ROI_TYPE_POINT)   return false;
        return start.x == end.x && start.y == end.y;
    }
};

// =====================================================
// 控制点类型（8方向，类似VisionPro）
// =====================================================
enum HandleType
{
    HANDLE_NONE = 0,
    HANDLE_LT,
    HANDLE_RT,
    HANDLE_LB,
    HANDLE_RB,
    HANDLE_T,
    HANDLE_B,
    HANDLE_L,
    HANDLE_R,
    HANDLE_CENTER // 中心点（拖动移动整个ROI）
};

// =====================================================
// ROI UI参数常量
// =====================================================
constexpr float HANDLE_SIZE = 8.0f;
constexpr float gMinROIWidth = 5.0f;
constexpr float gMinROIHeight = 5.0f;

// =====================================================
// ROIBox 结构（预计算8个控制点位置）
// =====================================================
struct ROIBox
{
    ImVec2 lt, rt, lb, rb; // 四角
    ImVec2 t, b, l, r;     // 四边中点
};
