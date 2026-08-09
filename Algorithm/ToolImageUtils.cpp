#define NOMINMAX
#include "ToolImageUtils.h"
#include "ToolResult.h"
#include "../Core/VisionContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

namespace ToolImageUtils
{
bool ValidateAreaContext(const VisionContext& ctx, bool enabled, std::string& error)
{
    error.clear();
    if (!enabled || ctx.rois.empty())
        return true;

    for (const ROI& roi : ctx.rois)
    {
        if (roi.type != ROI_TYPE_RECT && roi.type != ROI_TYPE_CIRCLE &&
            roi.type != ROI_TYPE_POLYGON)
        {
            error = "搜索 ROI 仅支持矩形、圆形或多边形；点/线 ROI 请用于测量工具";
            return false;
        }
        if (roi.IsEmpty() || (roi.type == ROI_TYPE_POLYGON && roi.points.size() < 3))
        {
            error = "绑定的搜索 ROI 无效";
            return false;
        }
    }
    return true;
}

cv::Rect PrimaryContextRect(const VisionContext& ctx, bool enabled)
{
    if (!enabled || ctx.image.empty())
        return {};

    const cv::Rect imageBounds(0, 0, ctx.image.cols, ctx.image.rows);
    const cv::Mat fullMask = FullContextMask(ctx, enabled);
    if (!fullMask.empty())
    {
        // A multi-ROI context is represented by the union domain mask. Cropping
        // to rois[0] would silently discard every later upstream result.
        const cv::Rect domainBounds = cv::boundingRect(fullMask) & imageBounds;
        return domainBounds.area() > 0 ? domainBounds : cv::Rect();
    }

    const auto isAreaROI = [](const ROI& roi)
    {
        return roi.type == ROI_TYPE_RECT || roi.type == ROI_TYPE_CIRCLE ||
               roi.type == ROI_TYPE_POLYGON;
    };

    cv::Rect r;
    if (ctx.HasROI() && isAreaROI(ctx.rois[ctx.selectedROI]))
    {
        r = ctx.GetActiveROIRect();
    }
    else
    {
        // Detached execution can carry bound ROI snapshots without a selected
        // item. Use all area ROI; point/line ROI remain measurement geometry.
        for (const ROI& roi : ctx.rois)
        {
            if (!isAreaROI(roi))
                continue;
            const cv::Rect candidate = roi.ToCvRect() & imageBounds;
            if (candidate.area() > 0)
                r = r.empty() ? candidate : (r | candidate);
        }
    }

    r &= imageBounds;
    return (r.width > 0 && r.height > 0) ? r : cv::Rect();
}

cv::Mat FullContextMask(const VisionContext& ctx, bool enabled)
{
    if (!enabled || ctx.domainMask.empty() || ctx.image.empty() ||
        ctx.domainMask.type() != CV_8UC1 || ctx.domainMask.size() != ctx.image.size())
    {
        return {};
    }

    return ctx.domainMask;
}

cv::Mat ContextMaskForRect(const VisionContext& ctx, const cv::Rect& rect, bool enabled)
{
    const cv::Mat fullMask = FullContextMask(ctx, enabled);
    if (fullMask.empty() || rect.empty())
        return fullMask;

    const cv::Rect clipped = rect & cv::Rect(0, 0, fullMask.cols, fullMask.rows);
    return clipped.size() == rect.size() ? fullMask(clipped) : cv::Mat();
}

cv::Mat PrimaryContextMask(const VisionContext& ctx, bool enabled)
{
    return ContextMaskForRect(ctx, PrimaryContextRect(ctx, enabled), enabled);
}

bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst)
{
    if (src.empty())
        return false;
    if (src.channels() == targetChannels)
    {
        dst = src;
        return true;
    }
    if (src.channels() == 1 && targetChannels == 3)
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
    else if (src.channels() == 1 && targetChannels == 4)
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGRA);
    else if (src.channels() == 3 && targetChannels == 1)
        cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);
    else if (src.channels() == 3 && targetChannels == 4)
        cv::cvtColor(src, dst, cv::COLOR_BGR2BGRA);
    else if (src.channels() == 4 && targetChannels == 1)
        cv::cvtColor(src, dst, cv::COLOR_BGRA2GRAY);
    else if (src.channels() == 4 && targetChannels == 3)
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
    else
        return false;
    return true;
}

cv::Mat ToGray(const cv::Mat& src)
{
    if (src.empty())
        return {};
    cv::Mat gray;
    if (src.channels() == 4)
        cv::cvtColor(src, gray, cv::COLOR_BGRA2GRAY);
    else if (src.channels() == 3)
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else
        gray = src;
    return gray;
}

void ApplyDomainMask(cv::Mat& image, const cv::Mat& mask, uchar outsideValue)
{
    if (image.empty() || mask.empty() || mask.type() != CV_8UC1 ||
        image.size() != mask.size())
    {
        return;
    }
    cv::Mat outside;
    cv::compare(mask, 0, outside, cv::CMP_EQ);
    image.setTo(cv::Scalar::all(outsideValue), outside);
}

double MaskedOtsuThreshold(const cv::Mat& gray, const cv::Mat& mask)
{
    if (gray.empty() || gray.type() != CV_8UC1)
        return 0.0;
    if (mask.empty() || mask.type() != CV_8UC1 || mask.size() != gray.size())
    {
        cv::Mat ignored;
        return cv::threshold(gray, ignored, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    std::array<std::uint64_t, 256> histogram{};
    std::uint64_t total = 0;
    long double weightedSum = 0.0;
    for (int y = 0; y < gray.rows; ++y)
    {
        const uchar* pixels = gray.ptr<uchar>(y);
        const uchar* domain = mask.ptr<uchar>(y);
        for (int x = 0; x < gray.cols; ++x)
        {
            if (domain[x] == 0)
                continue;
            ++histogram[pixels[x]];
            ++total;
            weightedSum += pixels[x];
        }
    }
    if (total == 0)
        return 0.0;

    std::uint64_t backgroundCount = 0;
    long double backgroundSum = 0.0;
    long double bestVariance = -1.0;
    int bestThreshold = 0;
    for (int threshold = 0; threshold < 256; ++threshold)
    {
        backgroundCount += histogram[threshold];
        backgroundSum += static_cast<long double>(threshold) * histogram[threshold];
        if (backgroundCount == 0)
            continue;
        const std::uint64_t foregroundCount = total - backgroundCount;
        if (foregroundCount == 0)
            break;
        const long double backgroundMean = backgroundSum / backgroundCount;
        const long double foregroundMean = (weightedSum - backgroundSum) / foregroundCount;
        const long double difference = backgroundMean - foregroundMean;
        const long double variance = static_cast<long double>(backgroundCount) *
            foregroundCount * difference * difference;
        if (variance > bestVariance)
        {
            bestVariance = variance;
            bestThreshold = threshold;
        }
    }
    return static_cast<double>(bestThreshold);
}

cv::Mat DomainGaussianBlur(const cv::Mat& gray, const cv::Mat& mask,
                           cv::Size kernelSize, double sigmaX)
{
    if (gray.empty())
        return {};
    if (mask.empty() || mask.type() != CV_8UC1 || mask.size() != gray.size())
    {
        cv::Mat blurred;
        cv::GaussianBlur(gray, blurred, kernelSize, sigmaX);
        return blurred;
    }

    cv::Mat source32f;
    gray.convertTo(source32f, CV_32F);
    cv::Mat weight;
    mask.convertTo(weight, CV_32F, 1.0 / 255.0);
    cv::Mat weightedSource;
    cv::multiply(source32f, weight, weightedSource);
    cv::GaussianBlur(weightedSource, weightedSource, kernelSize, sigmaX);
    cv::GaussianBlur(weight, weight, kernelSize, sigmaX);
    cv::max(weight, 1.0e-6, weight);
    cv::divide(weightedSource, weight, weightedSource);

    cv::Mat result;
    weightedSource.convertTo(result, gray.type());
    ApplyDomainMask(result, mask);
    return result;
}

cv::Mat DomainAdaptiveThreshold(const cv::Mat& gray, const cv::Mat& mask,
                                int blockSize, double c)
{
    if (gray.empty() || gray.type() != CV_8UC1)
        return {};
    if (mask.empty() || mask.type() != CV_8UC1 || mask.size() != gray.size())
    {
        cv::Mat result;
        cv::adaptiveThreshold(gray, result, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY, blockSize, c);
        return result;
    }

    cv::Mat localMean = DomainGaussianBlur(gray, mask, cv::Size(blockSize, blockSize));
    cv::Mat result;
    cv::Mat adjustedMean;
    localMean.convertTo(adjustedMean, CV_32F);
    adjustedMean -= static_cast<float>(c);
    cv::Mat gray32f;
    gray.convertTo(gray32f, CV_32F);
    cv::compare(gray32f, adjustedMean, result, cv::CMP_GT);
    ApplyDomainMask(result, mask);
    return result;
}

bool PointInDomain(const cv::Mat& mask, const cv::Point2f& point)
{
    if (mask.empty())
        return true;
    const int x = cvRound(point.x);
    const int y = cvRound(point.y);
    return x >= 0 && y >= 0 && x < mask.cols && y < mask.rows &&
           mask.at<uchar>(y, x) != 0;
}

double RectDomainCoverage(const cv::Mat& mask, const cv::Rect& box)
{
    if (mask.empty())
        return 1.0;
    if (mask.type() != CV_8UC1 || box.width <= 0 || box.height <= 0)
        return 0.0;
    const cv::Rect clipped = box & cv::Rect(0, 0, mask.cols, mask.rows);
    if (clipped.empty())
        return 0.0;
    return static_cast<double>(cv::countNonZero(mask(clipped))) /
           static_cast<double>(box.area());
}

bool AcceptRectByDomain(const cv::Mat& mask, const cv::Rect& box,
                        int policy, float minimumCoverage)
{
    if (mask.empty())
        return true;
    if (box.empty())
        return false;
    if (policy <= 0)
    {
        return PointInDomain(mask, cv::Point2f(
            box.x + box.width * 0.5f, box.y + box.height * 0.5f));
    }
    const double coverage = RectDomainCoverage(mask, box);
    if (policy == 1)
        return coverage > 0.0;
    if (policy == 2)
        return coverage >= 1.0 - 1.0e-9;
    return coverage >= std::clamp(static_cast<double>(minimumCoverage), 0.0, 1.0);
}

void FilterResultToDomain(ToolResult& result, const cv::Mat& mask,
                          int policy, float minimumCoverage)
{
    if (mask.empty() || mask.type() != CV_8UC1)
        return;

    auto acceptPointOrBox = [&](const cv::Point2f& point, const cv::Rect& box)
    {
        if (policy == 0 && (point != cv::Point2f() || box.empty()))
            return PointInDomain(mask, point);
        return AcceptRectByDomain(mask, box, policy, minimumCoverage);
    };

    result.regions.erase(std::remove_if(result.regions.begin(), result.regions.end(),
        [&](const ToolResult::Region& region)
        {
            cv::Rect box = region.bbox;
            if (box.empty() && !region.contour.empty())
                box = cv::boundingRect(region.contour);
            return !acceptPointOrBox(region.center, box);
        }), result.regions.end());

    result.detections.erase(std::remove_if(result.detections.begin(), result.detections.end(),
        [&](const ToolResult::Detection& detection)
        {
            return !AcceptRectByDomain(mask, detection.box, policy, minimumCoverage);
        }),
        result.detections.end());
    result.texts.erase(std::remove_if(result.texts.begin(), result.texts.end(),
        [&](const ToolResult::TextItem& item)
        {
            return !AcceptRectByDomain(mask, item.box, policy, minimumCoverage);
        }),
        result.texts.end());
    result.lines.erase(std::remove_if(result.lines.begin(), result.lines.end(),
        [&](const ToolResult::Line& line)
        {
            const int samples = (std::max)(1, cvRound(cv::norm(line.p2 - line.p1)));
            int inside = 0;
            for (int i = 0; i <= samples; ++i)
            {
                const float t = static_cast<float>(i) / samples;
                const cv::Point2f point(
                    line.p1.x + (line.p2.x - line.p1.x) * t,
                    line.p1.y + (line.p2.y - line.p1.y) * t);
                if (PointInDomain(mask, point))
                    ++inside;
            }
            if (policy == 0)
                return !PointInDomain(mask, cv::Point2f(
                    (line.p1.x + line.p2.x) * 0.5f,
                    (line.p1.y + line.p2.y) * 0.5f));
            if (policy == 1)
                return inside == 0;
            if (policy == 2)
                return inside != samples + 1;
            return static_cast<float>(inside) / (samples + 1) <
                   std::clamp(minimumCoverage, 0.0f, 1.0f);
        }), result.lines.end());
}
}
