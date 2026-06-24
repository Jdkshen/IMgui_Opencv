#pragma once
#include <string>
#include <vector>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

// =====================================================
// 统一工具执行结果（最终版）
// =====================================================
struct ToolResult
{
    // ===== 基础状态 =====
    std::string toolName;
    bool success = true;
    std::string message;

    // ===== 通用测量（长度/面积/角度等）=====
    struct Measurement { std::string name; double value = 0; std::string unit; };
    std::vector<Measurement> measurements;

    // ===== 几何区域（轮廓/Blob/形状匹配）=====
    struct Region {
        std::vector<cv::Point> contour;  // 轮廓顶点
        cv::Rect bbox;                   // 包围盒
        float area = 0;                  // 面积
        float score = 0;                 // 匹配分数
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
