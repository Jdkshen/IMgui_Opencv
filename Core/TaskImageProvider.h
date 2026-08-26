#pragma once

#include <opencv2/core/mat.hpp>

#include <string>

namespace TaskImageProvider
{
    cv::Mat ReadImageFile(const std::string& imagePath);
    bool ResolvePathForRun(const std::string& groupName, std::string& imagePath);
}
