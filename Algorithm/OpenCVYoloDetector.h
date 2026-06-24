#pragma once
#include "YOLODetector.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace OpenCVYoloDetector
{
    bool LoadModel(const std::string& onnxPath, const std::string& classesPath);
    bool IsLoaded();
    const std::string& GetModelPath();

    std::vector<DetectedObject> Detect(const cv::Mat& image,
        float confThreshold = 0.5f, float nmsThreshold = 0.4f,
        cv::Rect roi = cv::Rect());

    void Unload();

    extern float g_OpenCVYoloPreMs;
    extern float g_OpenCVYoloInfMs;
    extern float g_OpenCVYoloPostMs;
    extern float g_OpenCVYoloTotalMs;
}
