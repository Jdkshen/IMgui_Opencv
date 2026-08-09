#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

struct ContourResult
{
    std::vector<cv::Point> points;
    std::vector<cv::Point2f> subpixelPoints;
    cv::Rect bbox;
    double area = 0, perimeter = 0;
    int vertices = 0;
    bool isConvex = false;
    double circularity = 0;
    double matchScore = 999;
    int srcIdx = -1;
    bool isTemplate = false; // matchShapes分 + 来源模板编号 + 是否模板轮廓
    bool isHole = false;
    double signedArea = 0.0;
    float directionDegrees = 0.0f;
};

namespace ContourDetector
{
    struct Params
    {
        bool useGray = true;
        int blurSize = 5;
        int threshMode = 0, threshValue = 128, adaptiveBlock = 11;
        bool invertBinary = false;
        int retrMode = 0, approxMethod = 1;
        double minArea = 100, maxArea = 1e9;
        bool filterConvex = false;
        float approxEpsilon = 0.02f;
        int lineThickness = 2;
        bool showLabels = true, fillContours = false;
        int maxContours = 500;
        bool normalizeDirection = true;
        bool subpixelBoundary = true;
        // ROI模板匹配
        bool matchROI = false;
        double matchThreshold = 0.1;
        int matchMethod = 1;
        cv::Rect matchRect;
        cv::Mat matchMask;
    };
    std::vector<ContourResult> Detect(const cv::Mat &, const Params &,
                                      const cv::Mat &domainMask = cv::Mat());
    cv::Mat DrawContours(cv::Mat &, const std::vector<ContourResult> &, const Params &);
    std::string Summary(const std::vector<ContourResult> &);
    extern float g_ContourTimeMs;
    extern int g_ContourCount;
}
