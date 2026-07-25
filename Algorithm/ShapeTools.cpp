#include "ShapeTools.h"
#include "ContourDetector.h"
#include "LineDetector.h"
#include "ShapeMatcher.h"
#include "../Core/VisionContext.h"
#include <nlohmann/json.hpp>
#include <opencv2/geometry/2d.hpp>

namespace
{
    cv::Rect ActiveSearchRect(const VisionContext& ctx)
    {
        if (ctx.image.empty())
            return {};

        cv::Rect roi;
        if (ctx.HasROI())
            roi = ctx.GetActiveROIRect();
        else if (!ctx.rois.empty())
            roi = ctx.rois[0].ToCvRect();

        roi &= cv::Rect(0, 0, ctx.image.cols, ctx.image.rows);
        return (roi.width > 0 && roi.height > 0) ? roi : cv::Rect();
    }
}

// ==================== ContourTool ====================
ToolResult ContourTool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = "轮廓分析";
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
    cp.matchROI = matchROI;
    cp.matchThreshold = matchThresh;

    cv::Rect roi = ActiveSearchRect(ctx);
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    auto cs = ContourDetector::Detect(input, cp);
    for (const auto &c : cs)
    {
        ToolResult::Region reg;
        reg.bbox = c.bbox;
        reg.bbox.x += roi.x;
        reg.bbox.y += roi.y;
        reg.area = (float)c.area;
        reg.score = (float)(1.0 - std::min(c.matchScore / 10.0, 0.999));
        reg.label = "Ctr " + std::to_string((int)c.area) + "px";
        r.regions.push_back(reg);
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
    matchROI = j.value("matchROI", false);
    matchThresh = j.value("matchThresh", 0.1f);
}

// ==================== LineTool ====================
ToolResult LineTool::Execute(VisionContext& ctx)
{
    ToolResult r;
    r.toolName = "直线检测";
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
    if (useROI && ctx.HasROI())
        lp.roi = ctx.GetActiveROIRect();

    auto lines = LineDetector::Detect(ctx.image, lp);
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
        if (reg.contour.size() >= 3)
            reg.angle = cv::minAreaRect(reg.contour).angle;
        r.regions.push_back(reg);
    }
    r.success = true;
    return r;
}

nlohmann::json ShapeTool::Save() const
{
    return {{"type", 6}, {"blurSize", blurSize}, {"tplRetr", tplRetr}, {"tplMinArea", tplMinArea}, {"minScore", minScore}, {"shapeScore", shapeScore}, {"lineThick", lineThick}, {"method", method}, {"showLabels", showLabels}, {"maxResults", maxResults}, {"tplGray", tplGray}, {"tplBinary", tplBinary}, {"tplBinThresh", tplBinThresh}, {"tplBlur", tplBlur}, {"tplBlurK", tplBlurK}, {"tplInvert", tplInvert}};
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
    tplGray = j.value("tplGray", false);
    tplBinary = j.value("tplBinary", false);
    tplBinThresh = j.value("tplBinThresh", 128);
    tplBlur = j.value("tplBlur", false);
    tplBlurK = j.value("tplBlurK", 5);
    tplInvert = j.value("tplInvert", false);
}
