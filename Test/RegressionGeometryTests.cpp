#include "RegressionGeometryTests.h"

#include "../Algorithm/CaliperOperators.h"
#include "../Algorithm/ToolResult.h"
#include "../Core/CalibrationModel.h"
#include "../Core/FixtureTransform.h"
#include "../Core/ROI.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

void TestCaliperOperators()
{
    using namespace CaliperOperators;

    cv::Mat rising(80, 120, CV_8UC1, cv::Scalar(20));
    rising.colRange(51, rising.cols).setTo(cv::Scalar(220));
    CaliperParams params;
    params.searchLength = 60.0f;
    params.projectionWidth = 9.0f;
    params.smoothingSigma = 1.0f;
    params.edgeThreshold = 15.0f;
    params.polarity = EdgePolarity::DarkToBright;
    EdgePoint risingEdge = FindEdge(rising, {50.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(risingEdge.valid, "dark-to-bright caliper edge was not found");
    Require(std::abs(risingEdge.position.x - 50.5f) < 0.75f,
        "dark-to-bright subpixel edge position regressed");

    params.polarity = EdgePolarity::BrightToDark;
    EdgePoint rejected = FindEdge(rising, {50.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(!rejected.valid, "caliper polarity filter accepted the wrong edge");

    cv::Mat band(80, 120, CV_8UC1, cv::Scalar(20));
    band.colRange(35, 76).setTo(cv::Scalar(220));
    params.polarity = EdgePolarity::DarkToBright;
    params.searchLength = 80.0f;
    EdgePair pair = FindEdgePair(band, {55.0f, 40.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, params);
    Require(pair.valid, "edge-pair caliper did not find both edges");
    Require(std::abs(pair.distance - 41.0f) < 1.0f, "edge-pair width regressed");

    std::vector<cv::Point2f> linePoints;
    for (int x = 0; x < 20; ++x)
        linePoints.emplace_back(static_cast<float>(x), 2.0f * x + 3.0f);
    linePoints.emplace_back(5.0f, 80.0f);
    FittedLine line = FitLine(linePoints, FitMethod::Ransac, 0.5f);
    Require(line.valid && line.inliers.size() == 20, "RANSAC line inlier selection regressed");
    Require(line.quality.maxError < 0.1f, "RANSAC line residual regressed");

    std::vector<cv::Point2f> circlePoints;
    for (int i = 0; i < 24; ++i) {
        const float angle = static_cast<float>(2.0 * 3.14159265358979323846 * i / 24.0);
        circlePoints.emplace_back(40.0f + 15.0f * std::cos(angle), 30.0f + 15.0f * std::sin(angle));
    }
    circlePoints.emplace_back(100.0f, 100.0f);
    FittedCircle circle = FitCircle(circlePoints, FitMethod::Ransac, 0.5f);
    Require(circle.valid && circle.inliers.size() == 24, "RANSAC circle inlier selection regressed");
    Require(cv::norm(circle.center - cv::Point2f(40.0f, 30.0f)) < 0.1f &&
            std::abs(circle.radius - 15.0f) < 0.1f,
        "RANSAC circle fit regressed");
}

void TestCalibrationModel()
{
    CalibrationModel scale;
    scale.enabled = true;
    scale.scaleX = 0.1;
    scale.scaleY = 0.2;
    scale.pixelOrigin = {10.0, 20.0};
    scale.worldOrigin = {1.0, 2.0};
    const cv::Point2d scaled = scale.PixelToWorld({20.0, 30.0});
    Require(cv::norm(scaled - cv::Point2d(2.0, 4.0)) < 1.0e-9,
        "independent X/Y calibration scale regressed");

    CalibrationModel perspective;
    perspective.enabled = true;
    perspective.homographyEnabled = true;
    perspective.pixelToWorldHomography = cv::Matx33d(
        0.5, 0.0, 10.0,
        0.0, 0.25, 20.0,
        0.0, 0.0, 1.0);
    const cv::Point2d transformed = perspective.PixelToWorld({20.0, 40.0});
    Require(cv::norm(transformed - cv::Point2d(20.0, 30.0)) < 1.0e-9,
        "homography pixel-to-world conversion regressed");

    CalibrationModel distortion;
    distortion.distortionEnabled = true;
    distortion.fx = 800.0;
    distortion.fy = 800.0;
    distortion.cx = 320.0;
    distortion.cy = 240.0;
    const cv::Point2d unchanged = distortion.UndistortPixel({100.0, 120.0});
    Require(cv::norm(unchanged - cv::Point2d(100.0, 120.0)) < 1.0e-6,
        "zero lens distortion should preserve pixel coordinates");

    distortion.k1 = 0.10;
    distortion.k2 = -0.02;
    distortion.p1 = 0.001;
    distortion.p2 = -0.001;
    distortion.k3 = 0.005;
    const cv::Point2d idealPixel(500.0, 350.0);
    const double normalizedX = (idealPixel.x - distortion.cx) / distortion.fx;
    const double normalizedY = (idealPixel.y - distortion.cy) / distortion.fy;
    const double radius2 = normalizedX * normalizedX + normalizedY * normalizedY;
    const double radial = 1.0 + distortion.k1 * radius2 +
        distortion.k2 * radius2 * radius2 + distortion.k3 * radius2 * radius2 * radius2;
    const double distortedX = normalizedX * radial +
        2.0 * distortion.p1 * normalizedX * normalizedY +
        distortion.p2 * (radius2 + 2.0 * normalizedX * normalizedX);
    const double distortedY = normalizedY * radial +
        distortion.p1 * (radius2 + 2.0 * normalizedY * normalizedY) +
        2.0 * distortion.p2 * normalizedX * normalizedY;
    const cv::Point2d observedPixel(
        distortion.fx * distortedX + distortion.cx,
        distortion.fy * distortedY + distortion.cy);
    Require(cv::norm(distortion.UndistortPixel(observedPixel) - idealPixel) < 1.0e-4,
        "non-zero radial/tangential lens distortion correction regressed");
}

void TestFixtureTransform()
{
    ToolResult result;
    ToolResult::Region region;
    region.bbox = cv::Rect(90, 40, 20, 20);
    region.angle = 90.0f;
    result.regions.push_back(region);

    FixturePose current;
    Require(FixtureTransform::TryExtractPose(result, 0, current),
        "fixture pose extraction from region failed");
    FixturePose reference;
    reference.valid = true;
    reference.origin = {50.0f, 50.0f};
    reference.angleDegrees = 0.0f;

    const cv::Point2f transformed = FixtureTransform::TransformPoint({60.0f, 50.0f}, reference, current);
    Require(cv::norm(transformed - cv::Point2f(100.0f, 60.0f)) < 0.001f,
        "fixture rigid point transform regressed");

    ROI rectangle;
    rectangle.type = ROI_TYPE_RECT;
    rectangle.start = {55.0f, 45.0f};
    rectangle.end = {65.0f, 55.0f};
    const ROI transformedROI = FixtureTransform::TransformROI(rectangle, reference, current);
    Require(transformedROI.type == ROI_TYPE_POLYGON && transformedROI.points.size() == 4,
        "rotated fixture rectangle should become a polygon ROI");

    ToolResult detectionResult;
    detectionResult.detections.push_back({cv::Rect(10, 20, 30, 40), 1, 0.9f, "part"});
    FixturePose detectionPose;
    Require(FixtureTransform::TryExtractPose(detectionResult, 0, detectionPose) &&
        cv::norm(detectionPose.origin - cv::Point2f(25.0f, 40.0f)) < 0.001f,
        "fixture pose extraction from detection failed");

    ToolResult lineResult;
    lineResult.lines.push_back({cv::Point(10, 10), cv::Point(30, 20), 22.36f, 26.565f});
    FixturePose linePose;
    Require(FixtureTransform::TryExtractPose(lineResult, 0, linePose) &&
        cv::norm(linePose.origin - cv::Point2f(20.0f, 15.0f)) < 0.001f &&
        std::abs(linePose.angleDegrees - 26.565f) < 0.001f,
        "fixture pose extraction from line failed");

    ToolResult textResult;
    textResult.texts.push_back({"SN123", cv::Rect(40, 50, 20, 10), 0.95f});
    FixturePose textPose;
    Require(FixtureTransform::TryExtractPose(textResult, 0, textPose) &&
        cv::norm(textPose.origin - cv::Point2f(50.0f, 55.0f)) < 0.001f,
        "fixture pose extraction from text box failed");
}
