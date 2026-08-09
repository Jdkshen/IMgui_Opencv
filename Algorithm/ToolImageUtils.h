#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <string>

struct VisionContext;
struct ToolResult;

namespace ToolImageUtils
{
    // Validates ROI bindings for pixel-processing tools. Point/line ROI are
    // measurement geometry and must not silently turn into full-image input.
    bool ValidateAreaContext(const VisionContext& ctx, bool enabled,
                             std::string& error);
    // Empty means no area ROI is bound, so callers should process the full image.
    cv::Rect PrimaryContextRect(const VisionContext& ctx, bool enabled = true);
    // Full-image mask for algorithms that use absolute image coordinates.
    cv::Mat FullContextMask(const VisionContext& ctx, bool enabled = true);
    // Domain mask expressed in the coordinate system of rect.
    cv::Mat ContextMaskForRect(const VisionContext& ctx, const cv::Rect& rect,
                               bool enabled = true);
    cv::Mat PrimaryContextMask(const VisionContext& ctx, bool enabled = true);
    bool ConvertForCopyTo(const cv::Mat& src, int targetChannels, cv::Mat& dst);
    cv::Mat ToGray(const cv::Mat& src);
    void ApplyDomainMask(cv::Mat& image, const cv::Mat& mask, uchar outsideValue = 0);
    double MaskedOtsuThreshold(const cv::Mat& gray, const cv::Mat& mask);
    cv::Mat DomainGaussianBlur(const cv::Mat& gray, const cv::Mat& mask,
                               cv::Size kernelSize, double sigmaX = 0.0);
    cv::Mat DomainAdaptiveThreshold(const cv::Mat& gray, const cv::Mat& mask,
                                    int blockSize, double c);
    bool PointInDomain(const cv::Mat& mask, const cv::Point2f& point);
    double RectDomainCoverage(const cv::Mat& mask, const cv::Rect& box);
    bool AcceptRectByDomain(const cv::Mat& mask, const cv::Rect& box,
                            int policy, float minimumCoverage);
    void FilterResultToDomain(ToolResult& result, const cv::Mat& mask,
                              int policy = 0, float minimumCoverage = 0.5f);
}
