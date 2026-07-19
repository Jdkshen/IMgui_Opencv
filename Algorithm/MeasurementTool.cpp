#include "MeasurementTool.h"

#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

cv::Point2f ToPoint(const ImVec2& point)
{
    return {point.x, point.y};
}

cv::Point ToIntPoint(cv::Point2f point)
{
    return {static_cast<int>(std::lround(point.x)), static_cast<int>(std::lround(point.y))};
}

CalibrationModel EffectiveCalibration(const MeasurementTool& tool)
{
    if (tool.calibration.enabled)
        return tool.calibration;
    CalibrationModel model;
    if (tool.mmPerPixel > 0.0f)
    {
        model.enabled = true;
        model.scaleX = tool.mmPerPixel;
        model.scaleY = tool.mmPerPixel;
    }
    return model;
}

const char* DistanceUnit(const CalibrationModel& calibration)
{
    const bool valid = calibration.enabled &&
        ((calibration.homographyEnabled && calibration.HasValidHomography()) ||
         calibration.HasValidScale());
    return valid ? "mm" : "px";
}

double Distance(const CalibrationModel& calibration, cv::Point2f first, cv::Point2f second)
{
    return calibration.Distance(first, second);
}

std::string ValueLabel(double value, const char* unit)
{
    char buffer[96] = {};
    std::snprintf(buffer, sizeof(buffer), "%.3f %s", value, unit);
    return buffer;
}

CaliperOperators::LinearCaliperRegion ToLinearRegion(const ROI& roi)
{
    using CaliperOperators::LinearCaliperRegion;
    LinearCaliperRegion region;
    std::vector<cv::Point2f> points;
    if (roi.type == ROI_TYPE_POLYGON && roi.points.size() >= 4)
    {
        for (size_t i = 0; i < 4; ++i)
            points.push_back(ToPoint(roi.points[i]));
    }
    else
    {
        const float left = (std::min)(roi.start.x, roi.end.x);
        const float top = (std::min)(roi.start.y, roi.end.y);
        const float right = (std::max)(roi.start.x, roi.end.x);
        const float bottom = (std::max)(roi.start.y, roi.end.y);
        points = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    }
    if (points.size() < 4)
        return region;

    region.center = std::accumulate(points.begin(), points.end(), cv::Point2f()) * 0.25f;
    cv::Point2f firstAxis = points[1] - points[0];
    cv::Point2f secondAxis = points[3] - points[0];
    float firstLength = static_cast<float>(cv::norm(firstAxis));
    float secondLength = static_cast<float>(cv::norm(secondAxis));
    if (firstLength >= secondLength)
    {
        region.tangent = firstLength > 0.0f ? firstAxis * (1.0f / firstLength) : cv::Point2f(1, 0);
        region.normal = secondLength > 0.0f ? secondAxis * (1.0f / secondLength) : cv::Point2f(0, 1);
        region.span = firstLength;
        region.searchLength = secondLength;
    }
    else
    {
        region.tangent = secondLength > 0.0f ? secondAxis * (1.0f / secondLength) : cv::Point2f(0, 1);
        region.normal = firstLength > 0.0f ? firstAxis * (1.0f / firstLength) : cv::Point2f(1, 0);
        region.span = secondLength;
        region.searchLength = firstLength;
    }
    return region;
}

const ROI* FindAreaROI(const VisionContext& context)
{
    for (const ROI& roi : context.rois)
    {
        if (roi.type == ROI_TYPE_RECT || roi.type == ROI_TYPE_POLYGON)
            return &roi;
    }
    return nullptr;
}

std::vector<const ROI*> FindLineROIs(const VisionContext& context)
{
    std::vector<const ROI*> lines;
    for (const ROI& roi : context.rois)
    {
        if (roi.type == ROI_TYPE_LINE &&
            (roi.start.x != roi.end.x || roi.start.y != roi.end.y))
            lines.push_back(&roi);
    }
    return lines;
}

std::vector<cv::Point2f> FindPointROIs(const VisionContext& context)
{
    std::vector<cv::Point2f> points;
    for (const ROI& roi : context.rois)
    {
        if (roi.type == ROI_TYPE_POINT)
            points.push_back(ToPoint(roi.start));
    }
    return points;
}

void AddLine(ToolResult& result, cv::Point2f first, cv::Point2f second)
{
    ToolResult::Line line;
    line.p1 = ToIntPoint(first);
    line.p2 = ToIntPoint(second);
    line.length = static_cast<float>(cv::norm(second - first));
    line.angle = static_cast<float>(std::atan2(second.y - first.y, second.x - first.x) * 180.0 / kPi);
    result.lines.push_back(line);
}

void AddPoint(ToolResult& result, cv::Point2f point, const std::string& label)
{
    ToolResult::Region region;
    region.bbox = cv::Rect(ToIntPoint(point) - cv::Point(3, 3), cv::Size(7, 7));
    region.label = label;
    result.regions.push_back(region);
}

void AddQuality(ToolResult& result, const CaliperOperators::QualityMetrics& quality)
{
    result.measurements.push_back({"totalCalipers", static_cast<double>(quality.totalCalipers), "count"});
    result.measurements.push_back({"validCalipers", static_cast<double>(quality.validCalipers), "count"});
    result.measurements.push_back({"edgeStrength", quality.meanEdgeStrength, "gray/px"});
    result.measurements.push_back({"fitResidual", quality.rmsResidual, "px"});
    result.measurements.push_back({"standardDeviation", quality.standardDeviation, "px"});
    result.measurements.push_back({"maxError", quality.maxError, "px"});
    result.measurements.push_back({"confidence", quality.confidence, "ratio"});
}

CaliperOperators::QualityMetrics WidthQuality(
    const std::vector<double>& values,
    const CaliperOperators::QualityMetrics& sampleQuality,
    float inlierThreshold)
{
    CaliperOperators::QualityMetrics quality = sampleQuality;
    if (values.empty())
        return quality;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double squareSum = 0.0;
    double maxError = 0.0;
    for (double value : values)
    {
        const double error = std::abs(value - mean);
        squareSum += error * error;
        maxError = (std::max)(maxError, error);
    }
    quality.rmsResidual = static_cast<float>(std::sqrt(squareSum / values.size()));
    quality.standardDeviation = quality.rmsResidual;
    quality.maxError = static_cast<float>(maxError);
    const float validRatio = quality.totalCalipers > 0
        ? static_cast<float>(quality.validCalipers) / quality.totalCalipers
        : 0.0f;
    const float strengthScore = 1.0f - std::exp(-quality.meanEdgeStrength / 20.0f);
    const float stabilityScore = std::exp(-quality.standardDeviation / (std::max)(0.1f, inlierThreshold));
    quality.confidence = std::clamp(validRatio * strengthScore * stabilityScore, 0.0f, 1.0f);
    return quality;
}

double PointLineDistance(cv::Point2d point, cv::Point2d lineStart, cv::Point2d lineEnd)
{
    const cv::Point2d direction = lineEnd - lineStart;
    const double length = cv::norm(direction);
    if (length <= 1.0e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const cv::Point2d delta = point - lineStart;
    return std::abs(delta.x * direction.y - delta.y * direction.x) / length;
}

double LineAngleDegrees(cv::Point2d firstDirection, cv::Point2d secondDirection)
{
    const double denominator = cv::norm(firstDirection) * cv::norm(secondDirection);
    if (denominator <= 1.0e-12)
        return std::numeric_limits<double>::quiet_NaN();
    const double cosine = std::clamp(firstDirection.dot(secondDirection) / denominator, -1.0, 1.0);
    double angle = std::acos(cosine) * 180.0 / kPi;
    return angle > 90.0 ? 180.0 - angle : angle;
}

void SetFailure(ToolResult& result, const char* message)
{
    result.success = false;
    result.status = ToolResultStatus::Error;
    result.message = message;
    result.statusReason = message;
}
}

nlohmann::json MeasurementTool::Save() const
{
    std::vector<double> homography(calibration.pixelToWorldHomography.val,
        calibration.pixelToWorldHomography.val + 9);
    return {
        {"type", GetType()}, {"mode", mode},
        {"caliperCount", caliperCount}, {"searchLength", caliper.searchLength},
        {"projectionWidth", caliper.projectionWidth}, {"smoothingSigma", caliper.smoothingSigma},
        {"edgeThreshold", caliper.edgeThreshold}, {"minPairDistance", caliper.minPairDistance},
        {"edgePolarity", static_cast<int>(caliper.polarity)}, {"subpixel", caliper.subpixel},
        {"fitMethod", static_cast<int>(fitMethod)}, {"fitInlierThreshold", fitInlierThreshold},
        {"minimumValidCalipers", minimumValidCalipers}, {"minimumConfidence", minimumConfidence},
        {"mmPerPixel", mmPerPixel},
        {"calibrationEnabled", calibration.enabled}, {"scaleX", calibration.scaleX},
        {"scaleY", calibration.scaleY}, {"pixelOriginX", calibration.pixelOrigin.x},
        {"pixelOriginY", calibration.pixelOrigin.y}, {"worldOriginX", calibration.worldOrigin.x},
        {"worldOriginY", calibration.worldOrigin.y}, {"homographyEnabled", calibration.homographyEnabled},
        {"homography", homography}, {"distortionEnabled", calibration.distortionEnabled},
        {"fx", calibration.fx}, {"fy", calibration.fy}, {"cx", calibration.cx}, {"cy", calibration.cy},
        {"k1", calibration.k1}, {"k2", calibration.k2}, {"p1", calibration.p1},
        {"p2", calibration.p2}, {"k3", calibration.k3},
        {"toleranceEnabled", toleranceEnabled}, {"nominal", nominal},
        {"toleranceMinus", toleranceMinus}, {"tolerancePlus", tolerancePlus},
    };
}

void MeasurementTool::Load(const nlohmann::json& json)
{
    mode = std::clamp(json.value("mode", 0), 0, 7);
    caliperCount = std::clamp(json.value("caliperCount", 16), 1, 256);
    caliper.searchLength = (std::max)(1.0f, json.value("searchLength", 30.0f));
    caliper.projectionWidth = (std::max)(1.0f, json.value("projectionWidth", 5.0f));
    caliper.smoothingSigma = (std::max)(0.0f, json.value("smoothingSigma", 1.0f));
    caliper.edgeThreshold = (std::max)(0.0f, json.value("edgeThreshold", 12.0f));
    caliper.minPairDistance = (std::max)(0.0f, json.value("minPairDistance", 3.0f));
    caliper.polarity = static_cast<CaliperOperators::EdgePolarity>(
        std::clamp(json.value("edgePolarity", 0), 0, 2));
    caliper.subpixel = json.value("subpixel", true);
    fitMethod = static_cast<CaliperOperators::FitMethod>(std::clamp(json.value("fitMethod", 1), 0, 1));
    fitInlierThreshold = (std::max)(0.1f, json.value("fitInlierThreshold", 1.5f));
    minimumValidCalipers = (std::max)(1, json.value("minimumValidCalipers", 3));
    minimumConfidence = std::clamp(json.value("minimumConfidence", 0.0f), 0.0f, 1.0f);
    mmPerPixel = (std::max)(0.0f, json.value("mmPerPixel", 0.0f));

    calibration.enabled = json.value("calibrationEnabled", false);
    calibration.scaleX = json.value("scaleX", 1.0);
    calibration.scaleY = json.value("scaleY", 1.0);
    calibration.pixelOrigin.x = json.value("pixelOriginX", 0.0);
    calibration.pixelOrigin.y = json.value("pixelOriginY", 0.0);
    calibration.worldOrigin.x = json.value("worldOriginX", 0.0);
    calibration.worldOrigin.y = json.value("worldOriginY", 0.0);
    calibration.homographyEnabled = json.value("homographyEnabled", false);
    const auto homography = json.value("homography", std::vector<double>());
    if (homography.size() == 9)
        std::copy(homography.begin(), homography.end(), calibration.pixelToWorldHomography.val);
    calibration.distortionEnabled = json.value("distortionEnabled", false);
    calibration.fx = json.value("fx", 1.0); calibration.fy = json.value("fy", 1.0);
    calibration.cx = json.value("cx", 0.0); calibration.cy = json.value("cy", 0.0);
    calibration.k1 = json.value("k1", 0.0); calibration.k2 = json.value("k2", 0.0);
    calibration.p1 = json.value("p1", 0.0); calibration.p2 = json.value("p2", 0.0);
    calibration.k3 = json.value("k3", 0.0);
    toleranceEnabled = json.value("toleranceEnabled", false);
    nominal = json.value("nominal", 0.0f);
    toleranceMinus = (std::max)(0.0f, json.value("toleranceMinus", 0.0f));
    tolerancePlus = (std::max)(0.0f, json.value("tolerancePlus", 0.0f));
}

ToolResult MeasurementTool::Execute(VisionContext& context)
{
    using namespace CaliperOperators;
    ToolResult result;
    result.toolName = GetName();
    const CalibrationModel model = EffectiveCalibration(*this);
    const char* unit = DistanceUnit(model);
    double value = 0.0;
    double rawPixelValue = std::numeric_limits<double>::quiet_NaN();
    bool measured = false;
    bool angleValue = false;
    QualityMetrics quality;
    bool hasQuality = false;

    if (mode == 0)
    {
        std::vector<cv::Point2f> points = FindPointROIs(context);
        if (points.size() < 2)
        {
            const auto lines = FindLineROIs(context);
            if (!lines.empty())
                points = {ToPoint(lines.front()->start), ToPoint(lines.front()->end)};
        }
        if (points.size() >= 2)
        {
            value = Distance(model, points[0], points[1]);
            rawPixelValue = cv::norm(points[1] - points[0]);
            AddLine(result, points[0], points[1]);
            measured = true;
        }
        else
            SetFailure(result, "点点距离需要两个点 ROI 或一个线 ROI");
    }
    else if (mode == 1 || mode == 4 || mode == 5)
    {
        const ROI* roi = FindAreaROI(context);
        if (context.image.empty())
            SetFailure(result, "卡尺测量需要图像输入");
        else if (!roi)
            SetFailure(result, "卡尺测量需要矩形或四边形 ROI");
        else
        {
            const cv::Mat gray = context.image.channels() == 1 ? context.image : ToolImageUtils::ToGray(context.image);
            const LinearCaliperRegion region = ToLinearRegion(*roi);
            CaliperParams params = caliper;
            if (region.searchLength > 0.0f)
                params.searchLength = region.searchLength;

            if (mode == 1)
            {
                const auto pairs = CollectEdgePairs(gray, region, caliperCount, params, quality);
                std::vector<double> widths;
                std::vector<double> pixelWidths;
                for (const EdgePair& pair : pairs)
                {
                    widths.push_back(Distance(model, pair.first.position, pair.second.position));
                    pixelWidths.push_back(cv::norm(pair.second.position - pair.first.position));
                    AddLine(result, pair.first.position, pair.second.position);
                }
                if (!widths.empty())
                {
                    value = std::accumulate(widths.begin(), widths.end(), 0.0) / widths.size();
                    rawPixelValue = std::accumulate(pixelWidths.begin(), pixelWidths.end(), 0.0) /
                        pixelWidths.size();
                    quality = WidthQuality(pixelWidths, quality, fitInlierThreshold);
                    measured = true;
                }
                else
                    SetFailure(result, "没有找到有效的相反极性边缘对");
            }
            else if (mode == 4)
            {
                EdgePoint edge = FindEdge(gray, region.center, region.normal, region.tangent, params);
                quality.totalCalipers = 1;
                quality.validCalipers = edge.valid ? 1 : 0;
                quality.meanEdgeStrength = edge.strength;
                quality.confidence = edge.valid ? 1.0f - std::exp(-edge.strength / 20.0f) : 0.0f;
                if (edge.valid)
                {
                    const cv::Point2d world = model.PixelToWorld(edge.position);
                    result.measurements.push_back({"edgeX", world.x, unit});
                    result.measurements.push_back({"edgeY", world.y, unit});
                    result.measurements.push_back({"edgePixelX", edge.position.x, "px"});
                    result.measurements.push_back({"edgePixelY", edge.position.y, "px"});
                    value = world.x;
                    AddPoint(result, edge.position, "edge");
                    measured = true;
                }
                else
                    SetFailure(result, "没有找到满足阈值和极性的边缘点");
            }
            else
            {
                const auto edges = CollectLineEdges(gray, region, caliperCount, params, quality);
                std::vector<cv::Point2f> points;
                points.reserve(edges.size());
                for (const EdgePoint& edge : edges)
                    points.push_back(edge.position);
                const FittedLine line = FitLine(points, fitMethod, fitInlierThreshold, quality);
                quality = line.quality;
                if (line.valid)
                {
                    const cv::Point2f first = line.point - line.direction * (region.span * 0.5f);
                    const cv::Point2f second = line.point + line.direction * (region.span * 0.5f);
                    const cv::Point2d worldFirst = model.PixelToWorld(first);
                    const cv::Point2d worldSecond = model.PixelToWorld(second);
                    value = std::atan2(worldSecond.y - worldFirst.y, worldSecond.x - worldFirst.x) * 180.0 / kPi;
                    AddLine(result, first, second);
                    angleValue = true;
                    measured = true;
                }
                else
                    SetFailure(result, "有效卡尺点不足，无法拟合直线");
            }
            hasQuality = true;
        }
    }
    else if (mode == 3)
    {
        const ROI* circleROI = nullptr;
        for (const ROI& roi : context.rois)
        {
            if (roi.type == ROI_TYPE_CIRCLE) { circleROI = &roi; break; }
        }
        if (context.image.empty())
            SetFailure(result, "圆拟合需要图像输入");
        else if (!circleROI)
            SetFailure(result, "圆拟合需要圆 ROI");
        else
        {
            const cv::Mat gray = context.image.channels() == 1 ? context.image : ToolImageUtils::ToGray(context.image);
            const cv::Point2f center = ToPoint(circleROI->start);
            const float radius = circleROI->CircleRadius();
            const auto edges = CollectCircleEdges(gray, center, radius, caliperCount, caliper, quality);
            std::vector<cv::Point2f> points;
            for (const EdgePoint& edge : edges)
                points.push_back(edge.position);
            const FittedCircle circle = FitCircle(points, fitMethod, fitInlierThreshold, quality);
            quality = circle.quality;
            if (circle.valid)
            {
                const double diameterX = Distance(model,
                    circle.center - cv::Point2f(circle.radius, 0.0f),
                    circle.center + cv::Point2f(circle.radius, 0.0f));
                const double diameterY = Distance(model,
                    circle.center - cv::Point2f(0.0f, circle.radius),
                    circle.center + cv::Point2f(0.0f, circle.radius));
                value = (diameterX + diameterY) * 0.5;
                rawPixelValue = circle.radius * 2.0;
                ToolResult::Region region;
                region.bbox = cv::Rect(
                    ToIntPoint(circle.center - cv::Point2f(circle.radius, circle.radius)),
                    cv::Size(static_cast<int>(std::lround(circle.radius * 2.0f)),
                             static_cast<int>(std::lround(circle.radius * 2.0f))));
                region.area = static_cast<float>(kPi * circle.radius * circle.radius);
                result.regions.push_back(region);
                result.measurements.push_back({"centerX", model.PixelToWorld(circle.center).x, unit});
                result.measurements.push_back({"centerY", model.PixelToWorld(circle.center).y, unit});
                result.measurements.push_back({"fittedRadiusPixels", circle.radius, "px"});
                measured = true;
            }
            else
                SetFailure(result, "有效卡尺点不足，无法拟合圆");
            hasQuality = true;
        }
    }
    else
    {
        const auto lines = FindLineROIs(context);
        const auto points = FindPointROIs(context);
        if (mode == 6 && !points.empty() && !lines.empty())
        {
            const cv::Point2d point = model.PixelToWorld(points.front());
            const cv::Point2d first = model.PixelToWorld(ToPoint(lines.front()->start));
            const cv::Point2d second = model.PixelToWorld(ToPoint(lines.front()->end));
            value = PointLineDistance(point, first, second);
            rawPixelValue = PointLineDistance(
                points.front(), ToPoint(lines.front()->start), ToPoint(lines.front()->end));
            AddPoint(result, points.front(), "point");
            AddLine(result, ToPoint(lines.front()->start), ToPoint(lines.front()->end));
            measured = std::isfinite(value);
        }
        else if ((mode == 2 || mode == 7) && lines.size() >= 2)
        {
            const cv::Point2d a1 = model.PixelToWorld(ToPoint(lines[0]->start));
            const cv::Point2d a2 = model.PixelToWorld(ToPoint(lines[0]->end));
            const cv::Point2d b1 = model.PixelToWorld(ToPoint(lines[1]->start));
            const cv::Point2d b2 = model.PixelToWorld(ToPoint(lines[1]->end));
            if (mode == 2)
            {
                value = LineAngleDegrees(a2 - a1, b2 - b1);
                angleValue = true;
            }
            else
            {
                const double angle = LineAngleDegrees(a2 - a1, b2 - b1);
                value = angle < 0.1
                    ? (PointLineDistance(a1, b1, b2) + PointLineDistance(b1, a1, a2)) * 0.5
                    : 0.0;
                const cv::Point2d pixelA1 = ToPoint(lines[0]->start);
                const cv::Point2d pixelA2 = ToPoint(lines[0]->end);
                const cv::Point2d pixelB1 = ToPoint(lines[1]->start);
                const cv::Point2d pixelB2 = ToPoint(lines[1]->end);
                const double pixelAngle = LineAngleDegrees(pixelA2 - pixelA1, pixelB2 - pixelB1);
                rawPixelValue = pixelAngle < 0.1
                    ? (PointLineDistance(pixelA1, pixelB1, pixelB2) +
                       PointLineDistance(pixelB1, pixelA1, pixelA2)) * 0.5
                    : 0.0;
            }
            AddLine(result, ToPoint(lines[0]->start), ToPoint(lines[0]->end));
            AddLine(result, ToPoint(lines[1]->start), ToPoint(lines[1]->end));
            measured = std::isfinite(value);
        }
        else
            SetFailure(result, mode == 6 ? "点线距离需要一个点 ROI 和一个线 ROI" : "该测量需要两个线 ROI");
    }

    if (!measured)
        return result;

    const char* valueUnit = angleValue ? "deg" : unit;
    result.measurements.insert(result.measurements.begin(), {"value", value, valueUnit});
    if (!angleValue && std::isfinite(rawPixelValue))
        result.measurements.push_back({"rawPixels", rawPixelValue, "px"});
    if (!angleValue && std::string(valueUnit) == "mm")
        result.measurements.push_back({"calibrated", 1.0, "bool"});
    if (mmPerPixel > 0.0f)
        result.measurements.push_back({"mmPerPixel", mmPerPixel, "mm/px"});
    if (hasQuality)
    {
        AddQuality(result, quality);
        const int requiredValidCalipers = mode == 4 ? 1 : minimumValidCalipers;
        if (quality.validCalipers < requiredValidCalipers || quality.confidence < minimumConfidence)
        {
            result.status = ToolResultStatus::Fail;
            result.statusReason = quality.validCalipers < requiredValidCalipers
                ? "有效卡尺数量不足"
                : "测量可信度低于阈值";
        }
    }

    const std::string label = ValueLabel(value, valueUnit);
    for (auto& region : result.regions)
        region.label = label;

    if (toleranceEnabled)
    {
        const double lower = nominal - (std::max)(0.0f, toleranceMinus);
        const double upper = nominal + (std::max)(0.0f, tolerancePlus);
        result.measurements.push_back({"nominal", nominal, valueUnit});
        result.measurements.push_back({"lowerLimit", lower, valueUnit});
        result.measurements.push_back({"upperLimit", upper, valueUnit});
        if (value < lower || value > upper)
        {
            result.status = ToolResultStatus::Fail;
            result.statusReason = "测量值超出公差范围";
        }
    }

    result.success = true;
    result.message = label;
    return result;
}
