#pragma once

#include <opencv2/core.hpp>

struct CalibrationModel
{
    bool enabled = false;

    double scaleX = 1.0;
    double scaleY = 1.0;
    cv::Point2d pixelOrigin = {0.0, 0.0};
    cv::Point2d worldOrigin = {0.0, 0.0};

    bool homographyEnabled = false;
    cv::Matx33d pixelToWorldHomography = cv::Matx33d::eye();

    bool distortionEnabled = false;
    double fx = 1.0;
    double fy = 1.0;
    double cx = 0.0;
    double cy = 0.0;
    double k1 = 0.0;
    double k2 = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    double k3 = 0.0;

    bool HasValidScale() const;
    bool HasValidCamera() const;
    bool HasValidHomography() const;
    cv::Point2d UndistortPixel(cv::Point2d pixel) const;
    cv::Point2d PixelToWorld(cv::Point2d pixel) const;
    double Distance(cv::Point2d first, cv::Point2d second) const;
};
