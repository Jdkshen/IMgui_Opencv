#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
struct LineResult
{
    cv::Point2f pt1, pt2;
    float length = 0, angle = 0;
};
namespace LineDetector
{
    struct Params
    {
        int cannyLow = 50, cannyHigh = 150;
        float minLineLength = 100, maxLineGap = 20, minAngle = 0, maxAngle = 180;
        int lineThickness = 2, maxLines = 1;
        bool showLabels = true;
        cv::Rect roi;
    };
    std::vector<LineResult> Detect(const cv::Mat &, const Params &);
    cv::Mat DrawLines(const cv::Mat &, const std::vector<LineResult> &, const Params &);
    std::string Summary(const std::vector<LineResult> &);
    extern float g_LineTimeMs;
    extern int g_LineCount;
}
