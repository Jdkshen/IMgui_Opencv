#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
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
    bool locked = false;
    bool visible = true;
    bool constrainToImage = true;
    ImVec2 start = {0, 0};
    ImVec2 end = {0, 0};
    float angle = 0.0f;         // HALCON rectangle2 Phi（度，规范范围 (-90, 90]）
    std::vector<ImVec2> points;  // 多边形顶点（type==POLYGON 时有效）

    ImVec2 Center() const
    {
        return ImVec2((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
    }

    float Width() const { return std::abs(end.x - start.x); }
    float Height() const { return std::abs(end.y - start.y); }

    // HALCON rectangle2: (Row, Column, Phi, Length1, Length2).
    // Positive Phi follows the image row/column convention used by HALCON.
    static float NormalizeRectangle2AngleDegrees(float degrees)
    {
        while (degrees > 90.0f) degrees -= 180.0f;
        while (degrees <= -90.0f) degrees += 180.0f;
        return degrees;
    }
    float HalconRow() const { return Center().y; }
    float HalconColumn() const { return Center().x; }
    double HalconPhi() const
    {
        return NormalizeRectangle2AngleDegrees(angle) * CV_PI / 180.0;
    }
    float HalconLength1() const { return Width() * 0.5f; }
    float HalconLength2() const { return Height() * 0.5f; }

    std::vector<ImVec2> Corners() const
    {
        const ImVec2 center = Center();
        const float halfWidth = Width() * 0.5f;
        const float halfHeight = Height() * 0.5f;
        const float radians = angle * static_cast<float>(CV_PI / 180.0);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const ImVec2 local[4] = {
            {-halfWidth, -halfHeight}, {halfWidth, -halfHeight},
            {halfWidth, halfHeight}, {-halfWidth, halfHeight}};

        std::vector<ImVec2> corners;
        corners.reserve(4);
        for (const ImVec2& point : local)
        {
            corners.emplace_back(
                center.x + point.x * cosine - point.y * sine,
                center.y + point.x * sine + point.y * cosine);
        }
        return corners;
    }

    bool Contains(const ImVec2& point) const
    {
        if (type == ROI_TYPE_CIRCLE)
        {
            const float dx = point.x - start.x;
            const float dy = point.y - start.y;
            const float radius = CircleRadius();
            return dx * dx + dy * dy <= radius * radius;
        }
        if (type == ROI_TYPE_POLYGON)
        {
            if (points.size() < 3)
                return false;
            bool inside = false;
            for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++)
            {
                const ImVec2& a = points[j];
                const ImVec2& b = points[i];
                const float cross = (point.x - a.x) * (b.y - a.y) -
                                    (point.y - a.y) * (b.x - a.x);
                const float minX = (std::min)(a.x, b.x);
                const float maxX = (std::max)(a.x, b.x);
                const float minY = (std::min)(a.y, b.y);
                const float maxY = (std::max)(a.y, b.y);
                if (std::abs(cross) <= 1e-4f && point.x >= minX && point.x <= maxX &&
                    point.y >= minY && point.y <= maxY)
                    return true;
                const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
                    point.x < (b.x - a.x) * (point.y - a.y) /
                                  ((b.y - a.y) == 0.0f ? 1.0f : (b.y - a.y)) + a.x;
                if (crosses)
                    inside = !inside;
            }
            return inside;
        }
        if (type != ROI_TYPE_RECT || std::abs(angle) < 0.0001f)
        {
            const float minX = (std::min)(start.x, end.x);
            const float maxX = (std::max)(start.x, end.x);
            const float minY = (std::min)(start.y, end.y);
            const float maxY = (std::max)(start.y, end.y);
            return point.x >= minX && point.x <= maxX &&
                   point.y >= minY && point.y <= maxY;
        }

        const ImVec2 center = Center();
        const float radians = -angle * static_cast<float>(CV_PI / 180.0);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        const float localX = dx * cosine - dy * sine;
        const float localY = dx * sine + dy * cosine;
        return std::abs(localX) <= Width() * 0.5f &&
               std::abs(localY) <= Height() * 0.5f;
    }

    cv::Rect ToCvRect() const
    {
        if (type == ROI_TYPE_CIRCLE)
        {
            const float radius = CircleRadius();
            const int left = static_cast<int>(std::floor(start.x - radius));
            const int top = static_cast<int>(std::floor(start.y - radius));
            const int right = static_cast<int>(std::ceil(start.x + radius));
            const int bottom = static_cast<int>(std::ceil(start.y + radius));
            return cv::Rect(left, top, (std::max)(0, right - left),
                            (std::max)(0, bottom - top));
        }
        if (type == ROI_TYPE_POLYGON && !points.empty())
        {
            float minX = points.front().x;
            float maxX = minX;
            float minY = points.front().y;
            float maxY = minY;
            for (const ImVec2& point : points)
            {
                minX = (std::min)(minX, point.x);
                maxX = (std::max)(maxX, point.x);
                minY = (std::min)(minY, point.y);
                maxY = (std::max)(maxY, point.y);
            }
            const int left = static_cast<int>(std::floor(minX));
            const int top = static_cast<int>(std::floor(minY));
            const int right = static_cast<int>(std::ceil(maxX));
            const int bottom = static_cast<int>(std::ceil(maxY));
            return cv::Rect(left, top, (std::max)(0, right - left),
                            (std::max)(0, bottom - top));
        }
        if (type == ROI_TYPE_RECT && std::abs(angle) >= 0.0001f)
        {
            const auto corners = Corners();
            float minX = corners[0].x;
            float maxX = corners[0].x;
            float minY = corners[0].y;
            float maxY = corners[0].y;
            for (const ImVec2& point : corners)
            {
                minX = (std::min)(minX, point.x);
                maxX = (std::max)(maxX, point.x);
                minY = (std::min)(minY, point.y);
                maxY = (std::max)(maxY, point.y);
            }
            const int left = static_cast<int>(std::floor(minX));
            const int top = static_cast<int>(std::floor(minY));
            const int right = static_cast<int>(std::ceil(maxX));
            const int bottom = static_cast<int>(std::ceil(maxY));
            return cv::Rect(left, top, (std::max)(0, right - left),
                            (std::max)(0, bottom - top));
        }
        return cv::Rect(
            (int)(std::min)(start.x, end.x), (int)(std::min)(start.y, end.y),
            (int)std::abs(end.x - start.x), (int)std::abs(end.y - start.y));
    }

    float CircleRadius() const { return std::abs(end.x - start.x); }

    void ClampToImage(cv::Size imageSize)
    {
        if (!constrainToImage || imageSize.width <= 0 || imageSize.height <= 0)
            return;
        const float maxX = static_cast<float>(imageSize.width);
        const float maxY = static_cast<float>(imageSize.height);
        auto clampPoint = [&](ImVec2& point)
        {
            point.x = std::clamp(point.x, 0.0f, maxX);
            point.y = std::clamp(point.y, 0.0f, maxY);
        };
        if (type == ROI_TYPE_POINT || type == ROI_TYPE_LINE)
        {
            clampPoint(start);
            if (type == ROI_TYPE_LINE)
                clampPoint(end);
            return;
        }
        if (type == ROI_TYPE_CIRCLE)
        {
            const float originalRadius = CircleRadius();
            clampPoint(start);
            const float radius = (std::max)(0.0f, (std::min)({originalRadius,
                start.x, maxX - start.x, start.y, maxY - start.y}));
            end = ImVec2(start.x + radius, start.y);
            return;
        }
        if (type == ROI_TYPE_POLYGON)
        {
            for (ImVec2& point : points)
                clampPoint(point);
            if (!points.empty())
            {
                start = end = points.front();
                for (const ImVec2& point : points)
                {
                    start.x = (std::min)(start.x, point.x);
                    start.y = (std::min)(start.y, point.y);
                    end.x = (std::max)(end.x, point.x);
                    end.y = (std::max)(end.y, point.y);
                }
            }
            return;
        }

        ImVec2 center = Center();
        float halfWidth = Width() * 0.5f;
        float halfHeight = Height() * 0.5f;
        const float radians = angle * static_cast<float>(CV_PI / 180.0);
        float extentX = std::abs(std::cos(radians)) * halfWidth +
                        std::abs(std::sin(radians)) * halfHeight;
        float extentY = std::abs(std::sin(radians)) * halfWidth +
                        std::abs(std::cos(radians)) * halfHeight;
        const float scale = (std::min)({1.0f,
            extentX > 0.0f ? maxX * 0.5f / extentX : 1.0f,
            extentY > 0.0f ? maxY * 0.5f / extentY : 1.0f});
        halfWidth *= scale;
        halfHeight *= scale;
        extentX *= scale;
        extentY *= scale;
        center.x = std::clamp(center.x, extentX, (std::max)(extentX, maxX - extentX));
        center.y = std::clamp(center.y, extentY, (std::max)(extentY, maxY - extentY));
        start = ImVec2(center.x - halfWidth, center.y - halfHeight);
        end = ImVec2(center.x + halfWidth, center.y + halfHeight);
    }

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
    HANDLE_ROTATE,
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
