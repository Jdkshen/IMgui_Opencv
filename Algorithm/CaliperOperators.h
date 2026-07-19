#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace CaliperOperators
{
enum class EdgePolarity : int
{
    Any = 0,
    DarkToBright = 1,
    BrightToDark = 2,
};

enum class FitMethod : int
{
    LeastSquares = 0,
    Ransac = 1,
};

struct CaliperParams
{
    float searchLength = 30.0f;
    float projectionWidth = 5.0f;
    float smoothingSigma = 1.0f;
    float edgeThreshold = 12.0f;
    float minPairDistance = 3.0f;
    EdgePolarity polarity = EdgePolarity::Any;
    bool subpixel = true;
};

struct EdgePoint
{
    bool valid = false;
    cv::Point2f position;
    float strength = 0.0f;
    float signedGradient = 0.0f;
};

struct EdgePair
{
    bool valid = false;
    EdgePoint first;
    EdgePoint second;
    float distance = 0.0f;
};

struct LinearCaliperRegion
{
    cv::Point2f center;
    cv::Point2f tangent = {1.0f, 0.0f};
    cv::Point2f normal = {0.0f, 1.0f};
    float span = 1.0f;
    float searchLength = 1.0f;
};

struct QualityMetrics
{
    int totalCalipers = 0;
    int validCalipers = 0;
    float meanEdgeStrength = 0.0f;
    float rmsResidual = 0.0f;
    float standardDeviation = 0.0f;
    float maxError = 0.0f;
    float confidence = 0.0f;
};

struct FittedLine
{
    bool valid = false;
    cv::Point2f point;
    cv::Point2f direction = {1.0f, 0.0f};
    std::vector<cv::Point2f> inliers;
    QualityMetrics quality;
};

struct FittedCircle
{
    bool valid = false;
    cv::Point2f center;
    float radius = 0.0f;
    std::vector<cv::Point2f> inliers;
    QualityMetrics quality;
};

EdgePoint FindEdge(
    const cv::Mat& gray,
    cv::Point2f center,
    cv::Point2f normal,
    cv::Point2f projectionDirection,
    const CaliperParams& params);

EdgePair FindEdgePair(
    const cv::Mat& gray,
    cv::Point2f center,
    cv::Point2f normal,
    cv::Point2f projectionDirection,
    const CaliperParams& params);

std::vector<EdgePoint> CollectLineEdges(
    const cv::Mat& gray,
    const cv::Rect2f& roi,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality);

std::vector<EdgePoint> CollectLineEdges(
    const cv::Mat& gray,
    const LinearCaliperRegion& region,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality);

std::vector<EdgePair> CollectEdgePairs(
    const cv::Mat& gray,
    const cv::Rect2f& roi,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality);

std::vector<EdgePair> CollectEdgePairs(
    const cv::Mat& gray,
    const LinearCaliperRegion& region,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality);

std::vector<EdgePoint> CollectCircleEdges(
    const cv::Mat& gray,
    cv::Point2f center,
    float nominalRadius,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality);

FittedLine FitLine(
    const std::vector<cv::Point2f>& points,
    FitMethod method,
    float inlierThreshold,
    const QualityMetrics& sampleQuality = {});

FittedCircle FitCircle(
    const std::vector<cv::Point2f>& points,
    FitMethod method,
    float inlierThreshold,
    const QualityMetrics& sampleQuality = {});
}
