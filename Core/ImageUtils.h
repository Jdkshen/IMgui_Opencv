#pragma once

#include <opencv2/imgproc.hpp>

inline bool SafeConvertToRGBA(const cv::Mat& src, cv::Mat& rgba)
{
    if (src.empty()) return false;
    int ch = src.channels();
    if (ch == 1)      cv::cvtColor(src, rgba, cv::COLOR_GRAY2RGBA);
    else if (ch == 3) cv::cvtColor(src, rgba, cv::COLOR_BGR2RGBA);
    else if (ch == 4) cv::cvtColor(src, rgba, cv::COLOR_BGRA2RGBA);
    else return false;
    return !rgba.empty();
}
