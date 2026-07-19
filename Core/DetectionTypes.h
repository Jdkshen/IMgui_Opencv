#pragma once

#include <opencv2/core/types.hpp>

#include <string>

struct DetectedObject
{
    cv::Rect box;
    int classId = -1;
    float confidence = 0.0f;
    std::string className;
};
