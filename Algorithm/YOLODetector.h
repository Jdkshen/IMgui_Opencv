#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>

// =====================================================
// 检测结果
// =====================================================
struct DetectedObject
{
    cv::Rect box;           // 检测框（图像坐标）
    int   classId   = -1;   // 类别 ID
    float confidence = 0.0f; // 置信度
    std::string className;   // 类别名称
};

// =====================================================
// YOLODetector — 基于 OpenCV DNN 的 YOLO 检测器
// =====================================================
namespace YOLODetector
{
    // 加载 ONNX 模型 + 类别名称文件（每行一个类名）
    bool LoadModel(const std::string& onnxPath, const std::string& classesPath);

    // 是否已加载
    bool IsLoaded();

    // 执行检测
    //   image          — 输入图像（BGR）
    //   confThreshold  — 置信度阈值
    //   nmsThreshold   — NMS 阈值
    //   roi            — 限定检测区域（空 Rect 表示全图）
    std::vector<DetectedObject> Detect(const cv::Mat& image,
        float confThreshold = 0.5f, float nmsThreshold = 0.4f,
        cv::Rect roi = cv::Rect());

    // 在图像上绘制检测框
    void DrawDetections(cv::Mat& image,
        const std::vector<DetectedObject>& objects,
        bool drawLabel = true);

    // 释放模型
    void Unload();
}
