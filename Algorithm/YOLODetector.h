#pragma once

#include <opencv2/opencv.hpp>

#include "../Core/DetectionTypes.h"

#include <string>
#include <vector>

namespace YOLODetector
{
    bool LoadModel(const std::string& onnxPath, const std::string& classesPath, bool useGPU = false);
    bool IsLoaded();
    const char* GetBackendName();
    const std::string& GetModelPath();
    std::vector<DetectedObject> Detect(const cv::Mat& image,
                                       float confThreshold = 0.5f,
                                       float nmsThreshold = 0.4f,
                                       cv::Rect roi = cv::Rect());
    void DrawDetections(cv::Mat& image,
                        const std::vector<DetectedObject>& objects,
                        bool drawLabel = true);
    void Unload();
}
