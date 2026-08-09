#include "ShapeTools.h"
#include "ContourDetector.h"
#include "LineDetector.h"
#include "ShapeMatcher.h"
#include "ToolImageUtils.h"
#include "../Core/RotatedROI.h"
#include "../Core/VisionContext.h"
#include <nlohmann/json.hpp>
#include <opencv2/geometry/2d.hpp>

namespace
{
    cv::Rect ActiveSearchRect(const VisionContext& ctx)
    {
        return ToolImageUtils::PrimaryContextRect(ctx);
    }
}

// ==================== ContourTool ====================
ToolResult ContourTool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = "轮廓分析";
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, true, r.message))
    {
        r.success = false;
        return r;
    }
    ContourDetector::Params cp;
    cp.useGray = useGray;
    cp.blurSize = blurSize;
    cp.threshMode = threshMode;
    cp.threshValue = threshValue;
    cp.adaptiveBlock = adaptBlock;
    cp.invertBinary = invert;
    cp.retrMode = retrMode;
    cp.approxMethod = approxMethod;
    cp.minArea = minArea;
    cp.maxContours = maxContours;
    cp.filterConvex = filterConvex;
    cp.approxEpsilon = approxEps;
    cp.lineThickness = lineThick;
    cp.showLabels = showLabels;
    cp.fillContours = fillContours;
    cp.normalizeDirection = normalizeDirection;
    cp.subpixelBoundary = subpixelBoundary;
    cp.matchROI = matchROI;
    cp.matchThreshold = matchThresh;

    cv::Rect roi;
    if (matchROI && !ctx.rois.empty())
    {
        const int index = ctx.HasROI() ? ctx.selectedROI : 0;
        const ROI& templateROI = ctx.rois[
            std::clamp(index, 0, static_cast<int>(ctx.rois.size()) - 1)];
        cp.matchRect = templateROI.ToCvRect() &
            cv::Rect(0, 0, ctx.image.cols, ctx.image.rows);
        cv::Mat fullMask = RotatedROI::BuildDomainMask(ctx.image.size(), {templateROI});
        if (!fullMask.empty() && !cp.matchRect.empty())
            cp.matchMask = fullMask(cp.matchRect);
    }
    else
    {
        roi = ActiveSearchRect(ctx);
    }
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    auto cs = ContourDetector::Detect(input, cp,
        matchROI ? cv::Mat() : ToolImageUtils::PrimaryContextMask(ctx));
    for (const auto &c : cs)
    {
        ToolResult::Region reg;
        reg.bbox = c.bbox;
        reg.bbox.x += roi.x;
        reg.bbox.y += roi.y;
        reg.area = (float)c.area;
        reg.contour.reserve(c.points.size());
        for (const cv::Point& point : c.points)
            reg.contour.emplace_back(point.x + roi.x, point.y + roi.y);
        const cv::Moments moments = cv::moments(
            c.subpixelPoints.empty() ? std::vector<cv::Point2f>(c.points.begin(), c.points.end())
                                     : c.subpixelPoints);
        reg.center = moments.m00 != 0.0
            ? cv::Point2f(static_cast<float>(moments.m10 / moments.m00 + roi.x),
                          static_cast<float>(moments.m01 / moments.m00 + roi.y))
            : cv::Point2f(reg.bbox.x + reg.bbox.width * 0.5f,
                          reg.bbox.y + reg.bbox.height * 0.5f);
        reg.score = (float)(1.0 - std::min(c.matchScore / 10.0, 0.999));
        reg.angle = c.directionDegrees;
        reg.width = static_cast<float>(reg.bbox.width);
        reg.height = static_cast<float>(reg.bbox.height);
        reg.circularity = static_cast<float>(c.circularity);
        reg.aspectRatio = reg.bbox.height > 0
            ? static_cast<float>(reg.bbox.width) / static_cast<float>(reg.bbox.height)
            : 0.0f;
        reg.label = "Ctr " + std::to_string((int)c.area) + "px";
        r.regions.push_back(reg);
        const std::string prefix = "Contour " + std::to_string(r.regions.size()) + " ";
        r.measurements.push_back({prefix + "Direction", c.directionDegrees, "deg"});
        r.measurements.push_back({prefix + "SignedArea", c.signedArea, "px2"});
        r.measurements.push_back({prefix + "BoundaryPoints",
            static_cast<double>(c.subpixelPoints.size()), "count"});
    }
    r.success = true;
    return r;
}

nlohmann::json ContourTool::Save() const
{
    nlohmann::json j;
    j["type"] = 5;
    j["useGray"] = useGray;
    j["blurSize"] = blurSize;
    j["threshMode"] = threshMode;
    j["threshValue"] = threshValue;
    j["adaptBlock"] = adaptBlock;
    j["invert"] = invert;
    j["retrMode"] = retrMode;
    j["approxMethod"] = approxMethod;
    j["minArea"] = minArea;
    j["maxContours"] = maxContours;
    j["filterConvex"] = filterConvex;
    j["approxEps"] = approxEps;
    j["lineThick"] = lineThick;
    j["showLabels"] = showLabels;
    j["fillContours"] = fillContours;
    j["normalizeDirection"] = normalizeDirection;
    j["subpixelBoundary"] = subpixelBoundary;
    j["matchROI"] = matchROI;
    j["matchThresh"] = matchThresh;
    return j;
}
void ContourTool::Load(const nlohmann::json &j)
{
    useGray = j.value("useGray", true);
    blurSize = j.value("blurSize", 5);
    threshMode = j.value("threshMode", 0);
    threshValue = j.value("threshValue", 128);
    adaptBlock = j.value("adaptBlock", 11);
    invert = j.value("invert", false);
    retrMode = j.value("retrMode", 0);
    approxMethod = j.value("approxMethod", 1);
    minArea = j.value("minArea", 100.0f);
    maxContours = j.value("maxContours", 500);
    filterConvex = j.value("filterConvex", false);
    approxEps = j.value("approxEps", 0.02f);
    lineThick = j.value("lineThick", 2);
    showLabels = j.value("showLabels", true);
    fillContours = j.value("fillContours", false);
    normalizeDirection = j.value("normalizeDirection", true);
    subpixelBoundary = j.value("subpixelBoundary", true);
    matchROI = j.value("matchROI", false);
    matchThresh = j.value("matchThresh", 0.1f);
}

// ==================== LineTool ====================
ToolResult LineTool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = "直线检测";
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, useROI, r.message))
    {
        r.success = false;
        return r;
    }
    LineDetector::Params lp;
    lp.cannyLow = cannyLow;
    lp.cannyHigh = cannyHigh;
    lp.minLineLength = minLength;
    lp.maxLineGap = maxGap;
    lp.minAngle = minAngle;
    lp.maxAngle = maxAngle;
    lp.lineThickness = thickness;
    lp.maxLines = maxLines;
    lp.showLabels = showLabels;
    if (useROI)
        lp.roi = ToolImageUtils::PrimaryContextRect(ctx);

    auto lines = LineDetector::Detect(ctx.image, lp, ctx.domainMask);
    for (const auto &l : lines)
    {
        ToolResult::Line rl;
        rl.p1 = cv::Point((int)l.pt1.x, (int)l.pt1.y);
        rl.p2 = cv::Point((int)l.pt2.x, (int)l.pt2.y);
        rl.length = (float)l.length;
        rl.angle = l.angle;
        r.lines.push_back(rl);
    }
    r.success = true;
    return r;
}

nlohmann::json LineTool::Save() const
{
    return {{"type", 7}, {"cannyLow", cannyLow}, {"cannyHigh", cannyHigh}, {"minLength", minLength}, {"maxGap", maxGap}, {"minAngle", minAngle}, {"maxAngle", maxAngle}, {"thickness", thickness}, {"maxLines", maxLines}, {"showLabels", showLabels}, {"useROI", useROI}};
}
void LineTool::Load(const nlohmann::json &j)
{
    cannyLow = j.value("cannyLow", 50);
    cannyHigh = j.value("cannyHigh", 150);
    minLength = j.value("minLength", 100.0f);
    maxGap = j.value("maxGap", 20.0f);
    minAngle = j.value("minAngle", 0.0f);
    maxAngle = j.value("maxAngle", 180.0f);
    thickness = j.value("thickness", 2);
    maxLines = j.value("maxLines", 100);
    showLabels = j.value("showLabels", true);
    useROI = j.value("useROI", false);
}

// ==================== ShapeTool ====================
bool ShapeTool::IsTemplateCacheValid(const cv::Mat& tpl, const ShapeMatcher::Params& params) const
{
    if (!cachedTplReady || tpl.empty() || cachedTplImage.empty())
        return false;

    if (tpl.size() != cachedTplSize || tpl.type() != cachedTplType)
        return false;

    if (params.blurSize != cachedBlurSize ||
        params.tplRetrMode != cachedTplRetr ||
        params.tplMinArea != cachedTplMinArea ||
        params.tplGray != cachedTplGray ||
        params.tplBinary != cachedTplBinary ||
        params.tplBinThresh != cachedTplBinThresh ||
        params.tplBlur != cachedTplBlur ||
        params.tplBlurK != cachedTplBlurK ||
        params.tplInvert != cachedTplInvert)
    {
        return false;
    }

    cv::Mat diff;
    cv::compare(tpl, cachedTplImage, diff, cv::CMP_NE);
    return cv::countNonZero(diff.reshape(1)) == 0;
}

void ShapeTool::UpdateTemplateCache(const cv::Mat& tpl, const ShapeMatcher::Params& params)
{
    cachedTplContours = ShapeMatcher::ExtractTemplates(tpl, params);
    cachedTplImage = tpl.clone();
    cachedTplType = tpl.type();
    cachedTplSize = tpl.size();
    cachedBlurSize = params.blurSize;
    cachedTplRetr = params.tplRetrMode;
    cachedTplMinArea = params.tplMinArea;
    cachedTplGray = params.tplGray;
    cachedTplBinary = params.tplBinary;
    cachedTplBinThresh = params.tplBinThresh;
    cachedTplBlur = params.tplBlur;
    cachedTplBlurK = params.tplBlurK;
    cachedTplInvert = params.tplInvert;
    cachedTplReady = true;
}

ToolResult ShapeTool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = "形状匹配";
    if (ctx.IsCancellationRequested())
    {
        r.success = false;
        r.message = "执行已取消";
        return r;
    }
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }
    if (!ToolImageUtils::ValidateAreaContext(ctx, true, r.message))
    {
        r.success = false;
        return r;
    }
    cv::Mat tpl = tplImage.empty() ? ctx.frozenTemplate : tplImage;
    if (tpl.empty())
    {
        r.success = false;
        r.message = "请先抓取模板";
        return r;
    }

    ShapeMatcher::Params sp;
    sp.blurSize = blurSize;
    sp.tplRetrMode = tplRetr;
    sp.tplMinArea = tplMinArea;
    sp.minScore = minScore;
    sp.minShapeScore = shapeScore;
    sp.lineThickness = lineThick;
    sp.showLabels = showLabels;
    sp.maxResults = maxResults;
    sp.enableRotation = enableRotation;
    sp.rotationStart = rotationStart;
    sp.rotationEnd = rotationEnd;
    sp.rotationStep = rotationStep;
    sp.shapeMethod = method;
    sp.tplGray = tplGray;
    sp.tplBinary = tplBinary;
    sp.tplBinThresh = tplBinThresh;
    sp.tplBlur = tplBlur;
    sp.tplBlurK = tplBlurK;
    sp.tplInvert = tplInvert;

    if (!IsTemplateCacheValid(tpl, sp))
        UpdateTemplateCache(tpl, sp);

    cv::Rect roi = ActiveSearchRect(ctx);
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    auto ms = ShapeMatcher::Search(input, tpl, sp, cachedTplContours);
    if (ctx.IsCancellationRequested())
    {
        r.success = false;
        r.message = "执行已取消";
        return r;
    }
    for (const auto &m : ms)
    {
        if (ctx.IsCancellationRequested())
        {
            r.success = false;
            r.message = "执行已取消";
            return r;
        }
        ToolResult::Region reg;
        reg.bbox = m.bbox;
        reg.bbox.x += roi.x;
        reg.bbox.y += roi.y;
        reg.score = (float)m.score;
        char buf[64];
        snprintf(buf, sizeof(buf), "M#%d %.2f", m.tmplIdx, m.score);
        reg.label = buf;
        // 顶点存入 contour
        reg.contour.reserve(m.points.size());
        for (const auto &pt : m.points)
            reg.contour.push_back(cv::Point(pt.x + m.bbox.x + roi.x, pt.y + m.bbox.y + roi.y));
        reg.angle = m.angle;
        r.regions.push_back(reg);
    }
    r.success = true;
    return r;
}

nlohmann::json ShapeTool::Save() const
{
    return {{"type", 6}, {"blurSize", blurSize}, {"tplRetr", tplRetr}, {"tplMinArea", tplMinArea}, {"minScore", minScore}, {"shapeScore", shapeScore}, {"lineThick", lineThick}, {"method", method}, {"showLabels", showLabels}, {"maxResults", maxResults}, {"enableRotation", enableRotation}, {"rotationStart", rotationStart}, {"rotationEnd", rotationEnd}, {"rotationStep", rotationStep}, {"tplGray", tplGray}, {"tplBinary", tplBinary}, {"tplBinThresh", tplBinThresh}, {"tplBlur", tplBlur}, {"tplBlurK", tplBlurK}, {"tplInvert", tplInvert}};
}
void ShapeTool::Load(const nlohmann::json &j)
{
    blurSize = j.value("blurSize", 5);
    tplRetr = j.value("tplRetr", 0);
    tplMinArea = j.value("tplMinArea", 30.0f);
    minScore = j.value("minScore", 0.5f);
    shapeScore = j.value("shapeScore", 0.1f);
    lineThick = j.value("lineThick", 2);
    method = j.value("method", 0);
    showLabels = j.value("showLabels", true);
    maxResults = j.value("maxResults", 50);
    enableRotation = j.value("enableRotation", false);
    rotationStart = j.value("rotationStart", -45);
    rotationEnd = j.value("rotationEnd", 45);
    rotationStep = (std::max)(1, j.value("rotationStep", 5));
    tplGray = j.value("tplGray", false);
    tplBinary = j.value("tplBinary", false);
    tplBinThresh = j.value("tplBinThresh", 128);
    tplBlur = j.value("tplBlur", false);
    tplBlurK = j.value("tplBlurK", 5);
    tplInvert = j.value("tplInvert", false);
}
