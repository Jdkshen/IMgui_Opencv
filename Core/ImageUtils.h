#pragma once

#include <opencv2/imgproc.hpp>

// =====================================================
// ImageUtils — 图像格式转换工具
// =====================================================

// 安全地将任意通道 OpenCV Mat 转换为 RGBA 格式
// 支持：单通道(GRAY)、三通道(BGR)、四通道(BGRA)
// 返回 true 表示转换成功，rgba 包含结果
inline bool SafeConvertToRGBA(const cv::Mat& src, cv::Mat& rgba)
{
    if (src.empty()) return false;
    int ch = src.channels();
    if (ch == 1)      cv::cvtColor(src, rgba, cv::COLOR_GRAY2RGBA);   // 灰度 → RGBA
    else if (ch == 3) cv::cvtColor(src, rgba, cv::COLOR_BGR2RGBA);    // BGR → RGBA
    else if (ch == 4) cv::cvtColor(src, rgba, cv::COLOR_BGRA2RGBA);   // BGRA → RGBA
    else return false;
    return !rgba.empty();
}
