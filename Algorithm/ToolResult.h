#pragma once

// ToolResult is a public Core/Algorithm boundary and is often included after
// Windows headers. Keep the Win32 min/max macros from corrupting OpenCV.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>
#include <vector>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

// =====================================================
// 统一工具执行结果（最终版）
// =====================================================
enum class ToolResultStatus
{
    Pass,
    Fail,
    Error
};

inline const char* ToolResultStatusName(ToolResultStatus status)
{
    switch (status)
    {
    case ToolResultStatus::Pass: return "Pass";
    case ToolResultStatus::Fail: return "Fail";
    case ToolResultStatus::Error: return "Error";
    default: return "Error";
    }
}

struct ToolResult
{
    // ===== 基础状态 =====
    std::string toolName;
    int sourceToolIndex = -1;
    bool success = true;
    bool skipped = false;
    std::string message;
    ToolResultStatus status = ToolResultStatus::Pass;
    std::string statusReason;

    // ===== 通用测量（长度/面积/角度等）=====
    struct Measurement { std::string name; double value = 0; std::string unit; };
    std::vector<Measurement> measurements;

    // ===== 几何区域（轮廓/Blob/形状匹配）=====
    struct Region {
        std::vector<cv::Point> contour;  // 轮廓顶点
        cv::Rect bbox;                   // 包围盒
        float area = 0;                  // 面积
        float score = 0;                 // 匹配分数
        float angle = 0;                 // 定位角度（度），供 Fixture 使用
        std::string label;
    };
    std::vector<Region> regions;

    // ===== 检测框（YOLO/分类）=====
    struct Detection {
        cv::Rect box;
        int classId = -1;
        float score = 0;
        std::string label;
    };
    std::vector<Detection> detections;

    // ===== 线段（直线检测）=====
    struct Line {
        cv::Point p1, p2;
        float length = 0;
        float angle = 0;
    };
    std::vector<Line> lines;

    // ===== 文本（OCR）=====
    struct TextItem { std::string text; cv::Rect box; float confidence = 0; };
    std::vector<TextItem> texts;

    // ===== 可选调试图像 =====
    cv::Mat debugImage;
};
