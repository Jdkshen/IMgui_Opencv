#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// =====================================================
// 检测结果
// =====================================================
struct DetectedObject
{
    cv::Rect box;            // 检测框（图像坐标）
    int classId = -1;        // 类别 ID
    float confidence = 0.0f; // 置信度
    std::string className;   // 类别名称
};

// =====================================================
// YOLODetector — 基于 OpenCV DNN 的 YOLO 检测器
// =====================================================
namespace YOLODetector
{
    // 加载 ONNX 模型 + 类别名称文件（每行一个类名）
    // useGPU=true → 优先 CUDA，失败后尝试 DirectML，仍失败则回退 CPU
    bool LoadModel(const std::string &onnxPath, const std::string &classesPath, bool useGPU = false);

    // 是否已加载
    bool IsLoaded();

    // 当前实际推理后端：CPU / CUDA / DML / 未加载
    const char *GetBackendName();

    // 当前加载的模型路径（用于判断是否需要切换）
    const std::string &GetModelPath();

    // 执行检测
    //   image          — 输入图像（BGR）
    //   confThreshold  — 置信度阈值
    //   nmsThreshold   — NMS 阈值
    //   roi            — 限定检测区域（空 Rect 表示全图）
    std::vector<DetectedObject> Detect(const cv::Mat &image,
                                       float confThreshold = 0.5f, float nmsThreshold = 0.4f,
                                       cv::Rect roi = cv::Rect());

    // 在图像上绘制检测框
    void DrawDetections(cv::Mat &image,
                        const std::vector<DetectedObject> &objects,
                        bool drawLabel = true);

    // 释放模型
    void Unload();
}

// 工具叠加层（保留 YOLO 实时检测用）
extern std::vector<DetectedObject> g_YoloOverlays;
extern bool g_YoloShowOverlay;
extern float g_YoloOverlayOffsetX;
extern float g_YoloDetailPreMs;
extern float g_YoloDetailInfMs;
extern float g_YoloDetailPostMs;
extern float g_YoloDetailTotalMs;
extern int& g_ImageVersion;
