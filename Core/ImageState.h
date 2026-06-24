#pragma once

#include <opencv2/core/mat.hpp>

namespace ImageState
{
    bool HasImage();
    cv::Mat& CurrentRef();
    cv::Mat& OriginalRef();
    cv::Mat& PendingUploadRef();
    bool& NeedUploadRef();
    const cv::Mat& Current();
    const cv::Mat& Original();
    int Width();
    int Height();
    int Version();

    void SetImage(const cv::Mat& image);
    void SetDebugImage(const cv::Mat& image);
    void Clear();
}
