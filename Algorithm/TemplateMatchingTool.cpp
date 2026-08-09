#include "TemplateMatchingTool.h"

#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
struct Candidate
{
    cv::Rect box;
    cv::Point2f subpixelTopLeft;
    float score = 0.0f;
    float angle = 0.0f;
};

struct RotatedTemplate
{
    cv::Mat image;
    cv::Mat mask;
};

RotatedTemplate RotateTemplate(const cv::Mat& image, float angle)
{
    RotatedTemplate result;
    if (std::abs(angle) < 0.001f)
    {
        result.image = image;
        return result;
    }
    const cv::Point2f center(image.cols * 0.5f, image.rows * 0.5f);
    cv::Mat matrix = cv::getRotationMatrix2D(center, angle, 1.0);
    const cv::Rect bounds = cv::RotatedRect(center, image.size(), angle).boundingRect();
    matrix.at<double>(0, 2) += bounds.width * 0.5 - center.x;
    matrix.at<double>(1, 2) += bounds.height * 0.5 - center.y;
    cv::warpAffine(image, result.image, matrix, bounds.size(), cv::INTER_LINEAR,
        cv::BORDER_CONSTANT, cv::Scalar::all(0));
    const cv::Mat sourceMask(image.size(), CV_8UC1, cv::Scalar(255));
    cv::warpAffine(sourceMask, result.mask, matrix, bounds.size(), cv::INTER_NEAREST,
        cv::BORDER_CONSTANT, cv::Scalar(0));
    return result;
}

float QuadraticPeakOffset(float before, float center, float after)
{
    const float denominator = before - 2.0f * center + after;
    if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-6f)
        return 0.0f;
    return std::clamp(0.5f * (before - after) / denominator, -1.0f, 1.0f);
}

cv::Point2f RefinePeak(const cv::Mat& scores, const cv::Point& location)
{
    cv::Point2f refined(static_cast<float>(location.x), static_cast<float>(location.y));
    if (location.x > 0 && location.x + 1 < scores.cols)
    {
        refined.x += QuadraticPeakOffset(
            scores.at<float>(location.y, location.x - 1),
            scores.at<float>(location.y, location.x),
            scores.at<float>(location.y, location.x + 1));
    }
    if (location.y > 0 && location.y + 1 < scores.rows)
    {
        refined.y += QuadraticPeakOffset(
            scores.at<float>(location.y - 1, location.x),
            scores.at<float>(location.y, location.x),
            scores.at<float>(location.y + 1, location.x));
    }
    return refined;
}

void RefineAngles(std::vector<Candidate>& candidates, int angleStep)
{
    if (angleStep <= 0)
        return;
    const std::vector<Candidate> discrete = candidates;
    for (Candidate& candidate : candidates)
    {
        float before = -2.0f;
        float after = -2.0f;
        const cv::Point2f center = candidate.subpixelTopLeft +
            cv::Point2f(candidate.box.width * 0.5f, candidate.box.height * 0.5f);
        for (const Candidate& neighbor : discrete)
        {
            const float angleDifference = neighbor.angle - candidate.angle;
            if (std::abs(std::abs(angleDifference) - angleStep) > 0.01f)
                continue;
            const cv::Point2f neighborCenter = neighbor.subpixelTopLeft +
                cv::Point2f(neighbor.box.width * 0.5f, neighbor.box.height * 0.5f);
            const float maximumDistance = (std::max)(2.0f,
                0.08f * static_cast<float>((std::max)(candidate.box.width,
                    candidate.box.height)));
            if (cv::norm(neighborCenter - center) > maximumDistance)
                continue;
            if (angleDifference < 0.0f)
                before = (std::max)(before, neighbor.score);
            else
                after = (std::max)(after, neighbor.score);
        }
        if (before > -1.5f && after > -1.5f)
        {
            candidate.angle += QuadraticPeakOffset(before, candidate.score, after) *
                static_cast<float>(angleStep);
        }
    }
}

float IntersectionOverUnion(const cv::Rect& a, const cv::Rect& b)
{
    const cv::Rect intersection = a & b;
    const float intersectionArea = static_cast<float>(intersection.area());
    const float unionArea = static_cast<float>(a.area() + b.area()) - intersectionArea;
    return unionArea > 0.0f ? intersectionArea / unionArea : 0.0f;
}

cv::Mat PrepareTemplate(const cv::Mat& input, const TemplateMatchingTool& tool)
{
    cv::Mat output = input;
    if ((tool.tplGray || tool.tplBinary || tool.tplEdge) && output.channels() > 1)
        output = ToolImageUtils::ToGray(output);
    if (tool.tplBinary)
    {
        cv::Mat transformed;
        cv::threshold(output, transformed, tool.tplBinThresh, 255, cv::THRESH_BINARY);
        output = std::move(transformed);
    }
    if (tool.tplEdge)
    {
        cv::Mat transformed;
        cv::Canny(output, transformed, tool.tplEdgeLow, tool.tplEdgeHigh);
        output = std::move(transformed);
    }
    return output;
}

cv::Mat PrepareImage(const cv::Mat& input, const TemplateMatchingTool& tool)
{
    cv::Mat output = input;
    if ((tool.imgUseGray || tool.imgEnableThreshold) && output.channels() > 1)
        output = ToolImageUtils::ToGray(output);
    if (tool.imgEnableThreshold)
    {
        cv::Mat transformed;
        cv::threshold(output, transformed, tool.imgThreshold, 255, cv::THRESH_BINARY);
        output = std::move(transformed);
    }
    return output;
}
}

nlohmann::json TemplateMatchingTool::Save() const
{
    return {
        {"type", GetType()}, {"enableRotation", enableRotation},
        {"rotationStart", rotationStart}, {"rotationEnd", rotationEnd}, {"rotationStep", rotationStep},
        {"maxResults", maxResults}, {"matchThreshold", matchThreshold}, {"maxImageDim", maxImageDim},
        {"nmsThreshold", nmsThreshold}, {"subpixelRefinement", subpixelRefinement},
        {"tplGray", tplGray}, {"tplBinary", tplBinary},
        {"tplBinThresh", tplBinThresh}, {"tplEdge", tplEdge}, {"tplEdgeLow", tplEdgeLow},
        {"tplEdgeHigh", tplEdgeHigh}, {"imgUseGray", imgUseGray},
        {"imgEnableThreshold", imgEnableThreshold}, {"imgThreshold", imgThreshold},
        {"useSearchROI", useSearchROI},
    };
}

void TemplateMatchingTool::Load(const nlohmann::json& j)
{
    enableRotation = j.value("enableRotation", false);
    rotationStart = j.value("rotationStart", -45);
    rotationEnd = j.value("rotationEnd", 45);
    rotationStep = (std::max)(1, j.value("rotationStep", 1));
    maxResults = (std::max)(1, j.value("maxResults", 5));
    matchThreshold = std::clamp(j.value("matchThreshold", 0.7f), 0.0f, 1.0f);
    maxImageDim = (std::max)(64, j.value("maxImageDim", 1000));
    nmsThreshold = std::clamp(j.value("nmsThreshold", 0.3f), 0.0f, 1.0f);
    subpixelRefinement = j.value("subpixelRefinement", true);
    tplGray = j.value("tplGray", false);
    tplBinary = j.value("tplBinary", false);
    tplBinThresh = j.value("tplBinThresh", 128);
    tplEdge = j.value("tplEdge", false);
    tplEdgeLow = j.value("tplEdgeLow", 50);
    tplEdgeHigh = j.value("tplEdgeHigh", 150);
    imgUseGray = j.value("imgUseGray", false);
    imgEnableThreshold = j.value("imgEnableThreshold", false);
    imgThreshold = j.value("imgThreshold", 128);
    useSearchROI = j.value("useSearchROI", false);
}

ToolResult TemplateMatchingTool::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = GetName();
    const auto cancelled = [&]()
    {
        result.success = false;
        result.message = "执行已取消";
        return result;
    };
    if (ctx.IsCancellationRequested())
        return cancelled();
    if (ctx.image.empty() || templateImg.empty())
    {
        result.success = false;
        result.message = ctx.image.empty() ? "请先加载图片" : "请先抓取模板";
        return result;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, useSearchROI, result.message))
    {
        result.success = false;
        return result;
    }

    cv::Mat source = PrepareImage(ctx.image, *this);
    cv::Mat templ = PrepareTemplate(templateImg, *this);
    if (ctx.IsCancellationRequested())
        return cancelled();
    if (source.empty() || templ.empty())
    {
        result.success = false;
        result.message = "模板或输入预处理失败";
        return result;
    }
    if (source.channels() != templ.channels())
    {
        source = ToolImageUtils::ToGray(source);
        templ = ToolImageUtils::ToGray(templ);
    }

    float scale = 1.0f;
    const int sourceMaxDim = (std::max)(source.cols, source.rows);
    if (sourceMaxDim > (std::max)(64, maxImageDim))
    {
        scale = static_cast<float>(maxImageDim) / sourceMaxDim;
        cv::resize(source, source, cv::Size(), scale, scale, cv::INTER_AREA);
        cv::resize(templ, templ, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    cv::Mat scaledDomain;
    if (!ctx.domainMask.empty() && ctx.domainMask.type() == CV_8UC1 &&
        ctx.domainMask.size() == ctx.image.size())
    {
        if (scale == 1.0f)
            scaledDomain = ctx.domainMask;
        else
            cv::resize(ctx.domainMask, scaledDomain, source.size(), 0.0, 0.0,
                       cv::INTER_NEAREST);
    }
    if (templ.cols < 2 || templ.rows < 2 || templ.cols > source.cols || templ.rows > source.rows)
    {
        result.success = false;
        result.message = "模板尺寸超出输入图像";
        return result;
    }

    std::vector<cv::Rect> searchRects;
    const std::vector<ROI>& rois = !searchROIs.empty() ? searchROIs : ctx.rois;
    if (useSearchROI)
    {
        for (const ROI& roi : rois)
        {
            cv::Rect rect = roi.ToCvRect();
            rect = cv::Rect(static_cast<int>(std::floor(rect.x * scale)),
                            static_cast<int>(std::floor(rect.y * scale)),
                            static_cast<int>(std::ceil(rect.width * scale)),
                            static_cast<int>(std::ceil(rect.height * scale))) &
                   cv::Rect(0, 0, source.cols, source.rows);
            if (rect.width >= templ.cols && rect.height >= templ.rows)
                searchRects.push_back(rect);
        }
    }
    if (searchRects.empty())
        searchRects.push_back(cv::Rect(0, 0, source.cols, source.rows));

    int angleStart = enableRotation ? rotationStart : 0;
    int angleEnd = enableRotation ? rotationEnd : 0;
    const int angleStep = enableRotation ? (std::max)(1, std::abs(rotationStep)) : 1;
    if (angleStart > angleEnd)
        std::swap(angleStart, angleEnd);

    std::vector<Candidate> candidates;
    const int candidateLimit = (std::max)(maxResults * 8, maxResults);
    for (int angle = angleStart; angle <= angleEnd; angle += angleStep)
    {
        if (ctx.IsCancellationRequested())
            return cancelled();
        const RotatedTemplate rotated = RotateTemplate(templ, static_cast<float>(angle));
        if (rotated.image.empty())
            continue;
        for (const cv::Rect& searchRect : searchRects)
        {
            if (ctx.IsCancellationRequested())
                return cancelled();
            if (rotated.image.cols > searchRect.width ||
                rotated.image.rows > searchRect.height)
                continue;
            cv::Mat scores;
            cv::matchTemplate(source(searchRect), rotated.image, scores,
                cv::TM_CCOEFF_NORMED, rotated.mask);
            cv::patchNaNs(scores, -1.0);
            cv::Mat invalid;
            cv::compare(scores, 1.0, invalid, cv::CMP_GT);
            scores.setTo(-1.0f, invalid);
            cv::compare(scores, -1.0, invalid, cv::CMP_LT);
            scores.setTo(-1.0f, invalid);
            for (int i = 0; i < candidateLimit; ++i)
            {
                if (ctx.IsCancellationRequested())
                    return cancelled();
                double score = 0.0;
                cv::Point location;
                cv::minMaxLoc(scores, nullptr, &score, nullptr, &location);
                if (score < matchThreshold)
                    break;
                Candidate candidate;
                const cv::Point2f refined = subpixelRefinement
                    ? RefinePeak(scores, location)
                    : cv::Point2f(static_cast<float>(location.x),
                        static_cast<float>(location.y));
                candidate.subpixelTopLeft = refined + cv::Point2f(
                    static_cast<float>(searchRect.x), static_cast<float>(searchRect.y));
                candidate.box = cv::Rect(
                    static_cast<int>(std::lround(candidate.subpixelTopLeft.x)),
                    static_cast<int>(std::lround(candidate.subpixelTopLeft.y)),
                    rotated.image.cols, rotated.image.rows);
                candidate.score = static_cast<float>(score);
                candidate.angle = static_cast<float>(angle);
                if (ToolImageUtils::AcceptRectByDomain(
                        scaledDomain, candidate.box,
                        ctx.roiResultPolicy, ctx.roiMinimumCoverage))
                    candidates.push_back(candidate);
                cv::Rect suppress(location.x - rotated.image.cols / 2,
                                  location.y - rotated.image.rows / 2,
                                  rotated.image.cols, rotated.image.rows);
                suppress &= cv::Rect(0, 0, scores.cols, scores.rows);
                scores(suppress).setTo(-1.0f);
            }
        }
    }

    if (enableRotation && subpixelRefinement)
        RefineAngles(candidates, angleStep);

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        return a.score > b.score;
    });
    std::vector<Candidate> accepted;
    for (const Candidate& candidate : candidates)
    {
        if (ctx.IsCancellationRequested())
            return cancelled();
        bool overlaps = false;
        for (const Candidate& previous : accepted)
        {
            if (IntersectionOverUnion(candidate.box, previous.box) > nmsThreshold)
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
            continue;
        accepted.push_back(candidate);
        if (accepted.size() >= static_cast<size_t>((std::max)(1, maxResults)))
            break;
    }

    for (size_t i = 0; i < accepted.size(); ++i)
    {
        if (ctx.IsCancellationRequested())
            return cancelled();
        const Candidate& candidate = accepted[i];
        ToolResult::Region region;
        region.bbox = cv::Rect(static_cast<int>(std::lround(candidate.box.x / scale)),
                               static_cast<int>(std::lround(candidate.box.y / scale)),
                               (std::max)(1, static_cast<int>(std::lround(candidate.box.width / scale))),
                               (std::max)(1, static_cast<int>(std::lround(candidate.box.height / scale))));
        region.area = static_cast<float>(region.bbox.area());
        region.center = cv::Point2f(
            (candidate.subpixelTopLeft.x + candidate.box.width * 0.5f) / scale,
            (candidate.subpixelTopLeft.y + candidate.box.height * 0.5f) / scale);
        region.width = candidate.box.width / scale;
        region.height = candidate.box.height / scale;
        region.score = candidate.score;
        region.angle = candidate.angle;
        char label[64] = {};
        std::snprintf(label, sizeof(label), "#%zu %.3f %.1fdeg", i + 1, candidate.score, candidate.angle);
        region.label = label;
        result.regions.push_back(std::move(region));
    }

    result.success = true;
    result.message = result.regions.empty() ? "未找到匹配结果" : "OK";
    result.measurements.push_back({"matchCount", static_cast<double>(result.regions.size()), ""});
    result.measurements.push_back({"imageScale", scale, ""});
    return result;
}
