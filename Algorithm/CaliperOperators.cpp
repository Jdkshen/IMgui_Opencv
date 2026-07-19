#include "CaliperOperators.h"

#include "ToolImageUtils.h"

#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

cv::Point2f Normalize(cv::Point2f value)
{
    const float length = std::hypot(value.x, value.y);
    return length > 1.0e-6f ? value * (1.0f / length) : cv::Point2f();
}

bool BilinearSample(const cv::Mat& image, cv::Point2f point, float& value)
{
    if (image.empty() || point.x < 0.0f || point.y < 0.0f ||
        point.x >= image.cols - 1.0f || point.y >= image.rows - 1.0f)
    {
        return false;
    }

    const int x = static_cast<int>(std::floor(point.x));
    const int y = static_cast<int>(std::floor(point.y));
    const float dx = point.x - x;
    const float dy = point.y - y;
    const float p00 = static_cast<float>(image.at<unsigned char>(y, x));
    const float p10 = static_cast<float>(image.at<unsigned char>(y, x + 1));
    const float p01 = static_cast<float>(image.at<unsigned char>(y + 1, x));
    const float p11 = static_cast<float>(image.at<unsigned char>(y + 1, x + 1));
    value = (p00 * (1.0f - dx) + p10 * dx) * (1.0f - dy) +
            (p01 * (1.0f - dx) + p11 * dx) * dy;
    return true;
}

struct Profile
{
    std::vector<float> values;
    float startOffset = 0.0f;
    float step = 1.0f;
};

Profile BuildProfile(
    const cv::Mat& gray,
    cv::Point2f center,
    cv::Point2f normal,
    cv::Point2f projectionDirection,
    const CaliperOperators::CaliperParams& params)
{
    Profile profile;
    normal = Normalize(normal);
    projectionDirection = Normalize(projectionDirection);
    if (normal == cv::Point2f() || projectionDirection == cv::Point2f())
        return profile;

    const int sampleCount = (std::max)(5, static_cast<int>(std::ceil(params.searchLength)) + 1);
    const int projectionSamples = (std::max)(1, static_cast<int>(std::ceil(params.projectionWidth)));
    profile.startOffset = -params.searchLength * 0.5f;
    profile.step = sampleCount > 1 ? params.searchLength / static_cast<float>(sampleCount - 1) : 1.0f;
    profile.values.assign(sampleCount, std::numeric_limits<float>::quiet_NaN());

    for (int i = 0; i < sampleCount; ++i)
    {
        const float offset = profile.startOffset + i * profile.step;
        float sum = 0.0f;
        int valid = 0;
        for (int p = 0; p < projectionSamples; ++p)
        {
            const float lateral = projectionSamples == 1
                ? 0.0f
                : -params.projectionWidth * 0.5f +
                  params.projectionWidth * static_cast<float>(p) / static_cast<float>(projectionSamples - 1);
            float sample = 0.0f;
            if (BilinearSample(gray, center + normal * offset + projectionDirection * lateral, sample))
            {
                sum += sample;
                ++valid;
            }
        }
        if (valid > 0)
            profile.values[i] = sum / valid;
    }

    if (params.smoothingSigma > 0.05f && !profile.values.empty())
    {
        cv::Mat row(1, static_cast<int>(profile.values.size()), CV_32F, profile.values.data());
        for (int i = 0; i < row.cols; ++i)
        {
            if (!std::isfinite(row.at<float>(0, i)))
                row.at<float>(0, i) = i > 0 ? row.at<float>(0, i - 1) : 0.0f;
        }
        cv::GaussianBlur(row, row, cv::Size(), params.smoothingSigma, 0.0, cv::BORDER_REPLICATE);
    }
    return profile;
}

bool PolarityMatches(float gradient, CaliperOperators::EdgePolarity polarity)
{
    if (polarity == CaliperOperators::EdgePolarity::DarkToBright)
        return gradient > 0.0f;
    if (polarity == CaliperOperators::EdgePolarity::BrightToDark)
        return gradient < 0.0f;
    return true;
}

float PeakScore(float gradient, CaliperOperators::EdgePolarity polarity)
{
    if (!PolarityMatches(gradient, polarity))
        return 0.0f;
    return std::abs(gradient);
}

struct Peak
{
    int index = -1;
    float subIndex = 0.0f;
    float strength = 0.0f;
    float gradient = 0.0f;
};

std::vector<Peak> FindPeaks(
    const Profile& profile,
    CaliperOperators::EdgePolarity polarity,
    float threshold,
    bool subpixel)
{
    std::vector<Peak> peaks;
    if (profile.values.size() < 5)
        return peaks;

    std::vector<float> gradients(profile.values.size(), 0.0f);
    std::vector<float> scores(profile.values.size(), 0.0f);
    for (size_t i = 1; i + 1 < profile.values.size(); ++i)
    {
        if (!std::isfinite(profile.values[i - 1]) || !std::isfinite(profile.values[i + 1]))
            continue;
        gradients[i] = (profile.values[i + 1] - profile.values[i - 1]) * 0.5f;
        scores[i] = PeakScore(gradients[i], polarity);
    }

    for (int i = 2; i + 2 < static_cast<int>(scores.size()); ++i)
    {
        if (scores[i] < threshold || scores[i] < scores[i - 1] || scores[i] < scores[i + 1])
            continue;
        float delta = 0.0f;
        if (subpixel)
        {
            const float denominator = scores[i - 1] - 2.0f * scores[i] + scores[i + 1];
            if (std::abs(denominator) > 1.0e-6f)
                delta = std::clamp(0.5f * (scores[i - 1] - scores[i + 1]) / denominator, -1.0f, 1.0f);
        }
        peaks.push_back({i, static_cast<float>(i) + delta, scores[i], gradients[i]});
    }
    return peaks;
}

CaliperOperators::EdgePoint PeakToEdge(
    const Peak& peak,
    const Profile& profile,
    cv::Point2f center,
    cv::Point2f normal)
{
    CaliperOperators::EdgePoint edge;
    if (peak.index < 0)
        return edge;
    edge.valid = true;
    edge.strength = peak.strength;
    edge.signedGradient = peak.gradient;
    const float offset = profile.startOffset + peak.subIndex * profile.step;
    edge.position = center + Normalize(normal) * offset;
    return edge;
}

float PointLineDistance(cv::Point2f point, cv::Point2f linePoint, cv::Point2f direction)
{
    direction = Normalize(direction);
    const cv::Point2f delta = point - linePoint;
    return std::abs(delta.x * direction.y - delta.y * direction.x);
}

void ResidualStats(const std::vector<float>& residuals, CaliperOperators::QualityMetrics& quality)
{
    if (residuals.empty())
        return;
    double sum = 0.0;
    double sumSquares = 0.0;
    float maximum = 0.0f;
    for (float residual : residuals)
    {
        sum += residual;
        sumSquares += residual * residual;
        maximum = (std::max)(maximum, std::abs(residual));
    }
    const double mean = sum / residuals.size();
    double variance = 0.0;
    for (float residual : residuals)
        variance += (residual - mean) * (residual - mean);
    quality.rmsResidual = static_cast<float>(std::sqrt(sumSquares / residuals.size()));
    quality.standardDeviation = static_cast<float>(std::sqrt(variance / residuals.size()));
    quality.maxError = maximum;
}

void FinalizeConfidence(CaliperOperators::QualityMetrics& quality, float inlierThreshold)
{
    const float validRatio = quality.totalCalipers > 0
        ? static_cast<float>(quality.validCalipers) / quality.totalCalipers
        : 0.0f;
    const float residualScore = std::exp(-quality.rmsResidual / (std::max)(0.1f, inlierThreshold));
    const float strengthScore = 1.0f - std::exp(-quality.meanEdgeStrength / 20.0f);
    quality.confidence = std::clamp(validRatio * residualScore * strengthScore, 0.0f, 1.0f);
}

bool CircleFromThree(cv::Point2f a, cv::Point2f b, cv::Point2f c, cv::Point2f& center, float& radius)
{
    const double determinant = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(determinant) < 1.0e-6)
        return false;
    const double aa = a.x * a.x + a.y * a.y;
    const double bb = b.x * b.x + b.y * b.y;
    const double cc = c.x * c.x + c.y * c.y;
    center.x = static_cast<float>((aa * (b.y - c.y) + bb * (c.y - a.y) + cc * (a.y - b.y)) / determinant);
    center.y = static_cast<float>((aa * (c.x - b.x) + bb * (a.x - c.x) + cc * (b.x - a.x)) / determinant);
    radius = std::hypot(a.x - center.x, a.y - center.y);
    return std::isfinite(radius) && radius > 0.0f;
}

bool LeastSquaresCircle(const std::vector<cv::Point2f>& points, cv::Point2f& center, float& radius)
{
    if (points.size() < 3)
        return false;
    cv::Mat a(static_cast<int>(points.size()), 3, CV_64F);
    cv::Mat b(static_cast<int>(points.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        a.at<double>(i, 0) = 2.0 * points[i].x;
        a.at<double>(i, 1) = 2.0 * points[i].y;
        a.at<double>(i, 2) = 1.0;
        b.at<double>(i, 0) = points[i].x * points[i].x + points[i].y * points[i].y;
    }
    cv::Mat solution;
    if (!cv::solve(a, b, solution, cv::DECOMP_SVD))
        return false;
    center = cv::Point2f(static_cast<float>(solution.at<double>(0)), static_cast<float>(solution.at<double>(1)));
    const double radiusSquared = solution.at<double>(2) + center.x * center.x + center.y * center.y;
    if (radiusSquared <= 0.0)
        return false;
    radius = static_cast<float>(std::sqrt(radiusSquared));
    return std::isfinite(radius);
}
}

namespace CaliperOperators
{
EdgePoint FindEdge(
    const cv::Mat& image,
    cv::Point2f center,
    cv::Point2f normal,
    cv::Point2f projectionDirection,
    const CaliperParams& params)
{
    const cv::Mat gray = image.channels() == 1 ? image : ToolImageUtils::ToGray(image);
    const Profile profile = BuildProfile(gray, center, normal, projectionDirection, params);
    std::vector<Peak> peaks = FindPeaks(profile, params.polarity, params.edgeThreshold, params.subpixel);
    if (peaks.empty())
        return {};
    const Peak& best = *std::max_element(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b)
    {
        return a.strength < b.strength;
    });
    return PeakToEdge(best, profile, center, normal);
}

EdgePair FindEdgePair(
    const cv::Mat& image,
    cv::Point2f center,
    cv::Point2f normal,
    cv::Point2f projectionDirection,
    const CaliperParams& params)
{
    const cv::Mat gray = image.channels() == 1 ? image : ToolImageUtils::ToGray(image);
    const Profile profile = BuildProfile(gray, center, normal, projectionDirection, params);
    const std::vector<Peak> peaks = FindPeaks(profile, EdgePolarity::Any, params.edgeThreshold, params.subpixel);
    float bestScore = -1.0f;
    Peak first;
    Peak second;
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        for (size_t j = i + 1; j < peaks.size(); ++j)
        {
            if ((peaks[j].subIndex - peaks[i].subIndex) * profile.step < params.minPairDistance)
                continue;
            if (peaks[i].gradient * peaks[j].gradient >= 0.0f)
                continue;
            if (params.polarity != EdgePolarity::Any && !PolarityMatches(peaks[i].gradient, params.polarity))
                continue;
            const float score = peaks[i].strength + peaks[j].strength;
            if (score > bestScore)
            {
                bestScore = score;
                first = peaks[i];
                second = peaks[j];
            }
        }
    }
    EdgePair pair;
    if (bestScore < 0.0f)
        return pair;
    pair.first = PeakToEdge(first, profile, center, normal);
    pair.second = PeakToEdge(second, profile, center, normal);
    pair.valid = pair.first.valid && pair.second.valid;
    pair.distance = pair.valid
        ? static_cast<float>(cv::norm(pair.second.position - pair.first.position))
        : 0.0f;
    return pair;
}

std::vector<EdgePoint> CollectLineEdges(
    const cv::Mat& gray,
    const cv::Rect2f& roi,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality)
{
    const bool horizontal = roi.width >= roi.height;
    LinearCaliperRegion region;
    region.center = {roi.x + roi.width * 0.5f, roi.y + roi.height * 0.5f};
    region.tangent = horizontal ? cv::Point2f(1, 0) : cv::Point2f(0, 1);
    region.normal = horizontal ? cv::Point2f(0, 1) : cv::Point2f(1, 0);
    region.span = horizontal ? roi.width : roi.height;
    region.searchLength = horizontal ? roi.height : roi.width;
    return CollectLineEdges(gray, region, caliperCount, params, quality);
}

std::vector<EdgePoint> CollectLineEdges(
    const cv::Mat& gray,
    const LinearCaliperRegion& region,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality)
{
    std::vector<EdgePoint> edges;
    caliperCount = (std::max)(2, caliperCount);
    quality = {};
    quality.totalCalipers = caliperCount;
    const cv::Point2f tangent = Normalize(region.tangent);
    const cv::Point2f normal = Normalize(region.normal);
    if (tangent == cv::Point2f() || normal == cv::Point2f())
        return edges;
    CaliperParams local = params;
    local.searchLength = region.searchLength > 0.0f ? region.searchLength : params.searchLength;
    float strengthSum = 0.0f;
    for (int i = 0; i < caliperCount; ++i)
    {
        const float t = caliperCount == 1 ? 0.5f : static_cast<float>(i) / (caliperCount - 1);
        const cv::Point2f center = region.center + tangent * ((t - 0.5f) * region.span);
        EdgePoint edge = FindEdge(gray, center, normal, tangent, local);
        if (edge.valid)
        {
            strengthSum += edge.strength;
            edges.push_back(edge);
        }
    }
    quality.validCalipers = static_cast<int>(edges.size());
    quality.meanEdgeStrength = edges.empty() ? 0.0f : strengthSum / edges.size();
    return edges;
}

std::vector<EdgePair> CollectEdgePairs(
    const cv::Mat& gray,
    const cv::Rect2f& roi,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality)
{
    const bool horizontal = roi.width >= roi.height;
    LinearCaliperRegion region;
    region.center = {roi.x + roi.width * 0.5f, roi.y + roi.height * 0.5f};
    region.tangent = horizontal ? cv::Point2f(1, 0) : cv::Point2f(0, 1);
    region.normal = horizontal ? cv::Point2f(0, 1) : cv::Point2f(1, 0);
    region.span = horizontal ? roi.width : roi.height;
    region.searchLength = horizontal ? roi.height : roi.width;
    return CollectEdgePairs(gray, region, caliperCount, params, quality);
}

std::vector<EdgePair> CollectEdgePairs(
    const cv::Mat& gray,
    const LinearCaliperRegion& region,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality)
{
    std::vector<EdgePair> pairs;
    caliperCount = (std::max)(2, caliperCount);
    quality = {};
    quality.totalCalipers = caliperCount;
    const cv::Point2f tangent = Normalize(region.tangent);
    const cv::Point2f normal = Normalize(region.normal);
    if (tangent == cv::Point2f() || normal == cv::Point2f())
        return pairs;
    CaliperParams local = params;
    local.searchLength = region.searchLength > 0.0f ? region.searchLength : params.searchLength;
    float strengthSum = 0.0f;
    for (int i = 0; i < caliperCount; ++i)
    {
        const float t = static_cast<float>(i) / (caliperCount - 1);
        const cv::Point2f center = region.center + tangent * ((t - 0.5f) * region.span);
        EdgePair pair = FindEdgePair(gray, center, normal, tangent, local);
        if (pair.valid)
        {
            strengthSum += (pair.first.strength + pair.second.strength) * 0.5f;
            pairs.push_back(pair);
        }
    }
    quality.validCalipers = static_cast<int>(pairs.size());
    quality.meanEdgeStrength = pairs.empty() ? 0.0f : strengthSum / pairs.size();
    return pairs;
}

std::vector<EdgePoint> CollectCircleEdges(
    const cv::Mat& gray,
    cv::Point2f center,
    float nominalRadius,
    int caliperCount,
    const CaliperParams& params,
    QualityMetrics& quality)
{
    std::vector<EdgePoint> edges;
    caliperCount = (std::max)(6, caliperCount);
    quality = {};
    quality.totalCalipers = caliperCount;
    float strengthSum = 0.0f;
    for (int i = 0; i < caliperCount; ++i)
    {
        const float angle = 2.0f * kPi * i / caliperCount;
        const cv::Point2f radial(std::cos(angle), std::sin(angle));
        const cv::Point2f tangent(-radial.y, radial.x);
        const cv::Point2f searchCenter = center + radial * nominalRadius;
        EdgePoint edge = FindEdge(gray, searchCenter, radial, tangent, params);
        if (edge.valid)
        {
            strengthSum += edge.strength;
            edges.push_back(edge);
        }
    }
    quality.validCalipers = static_cast<int>(edges.size());
    quality.meanEdgeStrength = edges.empty() ? 0.0f : strengthSum / edges.size();
    return edges;
}

FittedLine FitLine(
    const std::vector<cv::Point2f>& points,
    FitMethod method,
    float inlierThreshold,
    const QualityMetrics& sampleQuality)
{
    FittedLine fitted;
    if (points.size() < 2)
        return fitted;
    inlierThreshold = (std::max)(0.1f, inlierThreshold);
    std::vector<cv::Point2f> inliers = points;
    if (method == FitMethod::Ransac && points.size() > 2)
    {
        size_t bestCount = 0;
        float bestError = std::numeric_limits<float>::max();
        for (size_t i = 0; i < points.size(); ++i)
        {
            for (size_t j = i + 1; j < points.size(); ++j)
            {
                const cv::Point2f direction = Normalize(points[j] - points[i]);
                if (direction == cv::Point2f())
                    continue;
                std::vector<cv::Point2f> candidate;
                float error = 0.0f;
                for (const auto& point : points)
                {
                    const float distance = PointLineDistance(point, points[i], direction);
                    if (distance <= inlierThreshold)
                    {
                        candidate.push_back(point);
                        error += distance;
                    }
                }
                if (candidate.size() > bestCount || (candidate.size() == bestCount && error < bestError))
                {
                    bestCount = candidate.size();
                    bestError = error;
                    inliers = std::move(candidate);
                }
            }
        }
    }
    if (inliers.size() < 2)
        return fitted;

    cv::Vec4f line;
    cv::fitLine(inliers, line, cv::DIST_L2, 0, 0.01, 0.01);
    fitted.point = cv::Point2f(line[2], line[3]);
    fitted.direction = Normalize(cv::Point2f(line[0], line[1]));
    fitted.inliers = inliers;
    fitted.quality = sampleQuality;
    fitted.quality.validCalipers = static_cast<int>(inliers.size());
    std::vector<float> residuals;
    residuals.reserve(inliers.size());
    for (const auto& point : inliers)
        residuals.push_back(PointLineDistance(point, fitted.point, fitted.direction));
    ResidualStats(residuals, fitted.quality);
    FinalizeConfidence(fitted.quality, inlierThreshold);
    fitted.valid = true;
    return fitted;
}

FittedCircle FitCircle(
    const std::vector<cv::Point2f>& points,
    FitMethod method,
    float inlierThreshold,
    const QualityMetrics& sampleQuality)
{
    FittedCircle fitted;
    if (points.size() < 3)
        return fitted;
    inlierThreshold = (std::max)(0.1f, inlierThreshold);
    std::vector<cv::Point2f> inliers = points;
    if (method == FitMethod::Ransac && points.size() > 3)
    {
        size_t bestCount = 0;
        float bestError = std::numeric_limits<float>::max();
        auto evaluate = [&](size_t i, size_t j, size_t k)
        {
            cv::Point2f center;
            float radius = 0.0f;
            if (!CircleFromThree(points[i], points[j], points[k], center, radius))
                return;
            std::vector<cv::Point2f> candidate;
            float error = 0.0f;
            for (const auto& point : points)
            {
                const float residual = static_cast<float>(
                    std::abs(cv::norm(point - center) - radius));
                if (residual <= inlierThreshold)
                {
                    candidate.push_back(point);
                    error += residual;
                }
            }
            if (candidate.size() > bestCount || (candidate.size() == bestCount && error < bestError))
            {
                bestCount = candidate.size();
                bestError = error;
                inliers = std::move(candidate);
            }
        };

        const size_t count = points.size();
        const size_t combinations = count * (count - 1) * (count - 2) / 6;
        if (combinations <= 4096)
        {
            for (size_t i = 0; i < count; ++i)
                for (size_t j = i + 1; j < count; ++j)
                    for (size_t k = j + 1; k < count; ++k)
                        evaluate(i, j, k);
        }
        else
        {
            constexpr size_t kMaxHypotheses = 2048;
            std::uint32_t state = 0x6d2b79f5u;
            for (size_t hypothesis = 0; hypothesis < kMaxHypotheses; ++hypothesis)
            {
                auto nextIndex = [&]()
                {
                    state = state * 1664525u + 1013904223u;
                    return static_cast<size_t>(state % static_cast<std::uint32_t>(count));
                };
                size_t i = nextIndex();
                size_t j = nextIndex();
                size_t k = nextIndex();
                if (i == j || i == k || j == k)
                    continue;
                if (i > j) std::swap(i, j);
                if (j > k) std::swap(j, k);
                if (i > j) std::swap(i, j);
                evaluate(i, j, k);
            }
        }
    }
    if (inliers.size() < 3 || !LeastSquaresCircle(inliers, fitted.center, fitted.radius))
        return fitted;

    fitted.inliers = inliers;
    fitted.quality = sampleQuality;
    fitted.quality.validCalipers = static_cast<int>(inliers.size());
    std::vector<float> residuals;
    residuals.reserve(inliers.size());
    for (const auto& point : inliers)
        residuals.push_back(static_cast<float>(
            std::abs(cv::norm(point - fitted.center) - fitted.radius)));
    ResidualStats(residuals, fitted.quality);
    FinalizeConfidence(fitted.quality, inlierThreshold);
    fitted.valid = true;
    return fitted;
}
}
