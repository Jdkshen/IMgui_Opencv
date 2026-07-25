#pragma once
#include "YOLODetector.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace OpenCVYoloDetector
{
    struct Timing
    {
        float preprocessMs = 0.0f;
        float inferenceMs = 0.0f;
        float postprocessMs = 0.0f;
        float totalMs = 0.0f;
    };

    bool LoadModel(const std::string& onnxPath, const std::string& classesPath);
    bool IsLoaded();
    const std::string& GetModelPath();

    std::vector<DetectedObject> Detect(const cv::Mat& image,
        float confThreshold = 0.5f, float nmsThreshold = 0.4f,
        cv::Rect roi = cv::Rect(), Timing* timing = nullptr);

    void Unload();

}
