#define NOMINMAX
#include "ContourDetector.h"
#include "ToolImageUtils.h"
#include "../Log/LogSystem.h"
#include <opencv2/geometry.hpp>
#include <chrono>
namespace ContourDetector
{
    float g_ContourTimeMs = 0;
    int g_ContourCount = 0;
    static double Circ(double a, double p)
    {
        if (p <= 0)
            return 0;
        return 4 * CV_PI * a / (p * p);
    }

    static float PrincipalDirectionDegrees(const std::vector<cv::Point2f>& points)
    {
        if (points.size() < 3)
            return 0.0f;
        const cv::Moments moments = cv::moments(points);
        if (std::abs(moments.m00) < 1.0e-9)
            return 0.0f;
        const double angle = 0.5 * std::atan2(2.0 * moments.mu11,
            moments.mu20 - moments.mu02) * 180.0 / CV_PI;
        return static_cast<float>(angle);
    }

    static std::vector<cv::Point2f> RefineBoundary(const std::vector<cv::Point>& points,
        const cv::Mat& gradientMagnitude)
    {
        std::vector<cv::Point2f> refined;
        refined.reserve(points.size());
        for (const cv::Point& point : points)
        {
            double sum = 0.0;
            cv::Point2d weighted;
            for (int dy = -1; dy <= 1; ++dy)
            {
                const int y = point.y + dy;
                if (y < 0 || y >= gradientMagnitude.rows)
                    continue;
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int x = point.x + dx;
                    if (x < 0 || x >= gradientMagnitude.cols)
                        continue;
                    const double weight = gradientMagnitude.at<float>(y, x);
                    sum += weight;
                    weighted.x += x * weight;
                    weighted.y += y * weight;
                }
            }
            refined.emplace_back(sum > 1.0e-6
                ? cv::Point2f(static_cast<float>(weighted.x / sum),
                    static_cast<float>(weighted.y / sum))
                : cv::Point2f(static_cast<float>(point.x),
                    static_cast<float>(point.y)));
        }
        return refined;
    }

    // 单张图上找轮廓
    static std::vector<ContourResult> Find(const cv::Mat &img, const Params &p,
                                           cv::Point off = cv::Point(0, 0),
                                           const cv::Mat &domainMask = cv::Mat())
    {
        cv::Mat gray;
        if (img.channels() == 1)
            gray = img;
        else
            cv::cvtColor(img, gray, img.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        if (p.blurSize > 0)
        {
            int k = p.blurSize * 2 + 1;
            if (k < 3)
                k = 3;
            gray = ToolImageUtils::DomainGaussianBlur(
                gray, domainMask, cv::Size(k, k));
        }
        cv::Mat bin;
        if (p.threshMode == 0)
        {
            const double threshold = ToolImageUtils::MaskedOtsuThreshold(gray, domainMask);
            cv::threshold(gray, bin, threshold, 255, cv::THRESH_BINARY);
        }
        else if (p.threshMode == 2)
        {
            int bs = p.adaptiveBlock;
            if (bs % 2 == 0)
                bs++;
            if (bs < 3)
                bs = 3;
            bin = ToolImageUtils::DomainAdaptiveThreshold(gray, domainMask, bs, 2);
        }
        else
            cv::threshold(gray, bin, p.threshValue, 255, cv::THRESH_BINARY);
        if (p.invertBinary)
            cv::bitwise_not(bin, bin);
        if (!domainMask.empty() && domainMask.type() == CV_8UC1 && domainMask.size() == bin.size())
            bin.setTo(0, domainMask == 0);
        int retrM[] = {cv::RETR_EXTERNAL, cv::RETR_LIST, cv::RETR_TREE}, appM[] = {cv::CHAIN_APPROX_NONE, cv::CHAIN_APPROX_SIMPLE, cv::CHAIN_APPROX_TC89_L1};
        std::vector<std::vector<cv::Point>> raw;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(bin, raw, hierarchy,
            retrM[std::clamp(p.retrMode, 0, 2)],
            appM[std::clamp(p.approxMethod, 0, 2)]);
        cv::Mat gradientMagnitude;
        if (p.subpixelBoundary)
        {
            cv::Mat gradientX, gradientY;
            cv::Sobel(gray, gradientX, CV_32F, 1, 0, 3);
            cv::Sobel(gray, gradientY, CV_32F, 0, 1, 3);
            cv::magnitude(gradientX, gradientY, gradientMagnitude);
        }
        std::vector<ContourResult> res;
        res.reserve(std::min((size_t)p.maxContours, raw.size()));
        for (std::size_t contourIndex = 0; contourIndex < raw.size(); ++contourIndex)
        {
            auto& pts = raw[contourIndex];
            if ((int)res.size() >= p.maxContours)
                break;
            ContourResult cr;
            int hierarchyDepth = 0;
            if (contourIndex < hierarchy.size())
            {
                for (int parent = hierarchy[contourIndex][3]; parent >= 0 &&
                    parent < static_cast<int>(hierarchy.size());
                    parent = hierarchy[static_cast<std::size_t>(parent)][3])
                    ++hierarchyDepth;
            }
            cr.isHole = hierarchyDepth % 2 != 0;
            double signedArea = cv::contourArea(pts, true);
            const bool directionMismatch = cr.isHole ? signedArea >= 0.0 : signedArea < 0.0;
            if (p.normalizeDirection && directionMismatch)
            {
                std::reverse(pts.begin(), pts.end());
                signedArea = -signedArea;
            }
            cr.points = pts;
            cr.signedArea = signedArea;
            cr.subpixelPoints = p.subpixelBoundary
                ? RefineBoundary(pts, gradientMagnitude)
                : std::vector<cv::Point2f>(pts.begin(), pts.end());
            cr.directionDegrees = PrincipalDirectionDegrees(cr.subpixelPoints);
            cr.area = cv::contourArea(pts);
            cr.bbox = cv::boundingRect(pts);
            if (cr.area < p.minArea || cr.area > p.maxArea)
                continue;
            cr.isConvex = cv::isContourConvex(pts);
            if (p.filterConvex && !cr.isConvex)
                continue;
            cr.perimeter = cv::arcLength(pts, true);
            cr.circularity = Circ(cr.area, cr.perimeter);
            std::vector<cv::Point> ap;
            cv::approxPolyDP(pts, ap, p.approxEpsilon * cr.perimeter, true);
            cr.vertices = (int)ap.size();
            if (off.x || off.y)
            {
                for (auto &pt : cr.points)
                {
                    pt.x += off.x;
                    pt.y += off.y;
                }
                for (auto& point : cr.subpixelPoints)
                {
                    point.x += static_cast<float>(off.x);
                    point.y += static_cast<float>(off.y);
                }
                cr.bbox.x += off.x;
                cr.bbox.y += off.y;
            }
            res.push_back(cr);
        }
        return res;
    }

    std::vector<ContourResult> Detect(const cv::Mat &img, const Params &p,
                                      const cv::Mat &domainMask)
    {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        g_ContourCount = 0;
        if (img.empty())
        {
            LogSystem::Add(LOG_ERROR, "Contour:空图");
            return {};
        }
        auto t1 = clock::now();
        std::vector<ContourResult> res;
        if (p.matchROI)
        {
            // ROI模式: 先从ROI找模板, 再全图匹配
            cv::Rect roi = p.matchRect & cv::Rect(0, 0, img.cols, img.rows);
            if (roi.empty())
            {
            LogSystem::Add(LOG_WARN, "Contour:请先框选ROI");
                return {};
            }
            cv::Mat crop = img(roi);
            auto templates = Find(crop, p, cv::Point(roi.x, roi.y), p.matchMask);
            if (templates.empty())
            {
            LogSystem::Add(LOG_WARN, "Contour:ROI内未找到轮廓");
                return {};
            }
            LogSystem::Add(LOG_INFO, "Contour:ROI提取%zu个模板", templates.size());
            // 全图找所有轮廓
            auto all = Find(img, p);
            // matchShapes 匹配
            for (auto &a : all)
            {
                if (a.points.size() < 3)
                    continue;
                for (size_t ti = 0; ti < templates.size(); ti++)
                {
                    if (templates[ti].points.size() < 3)
                        continue;
                    try
                    {
                        double sc = cv::matchShapes(templates[ti].points, a.points, p.matchMethod, 0);
                        if (sc < a.matchScore)
                        {
                            a.matchScore = sc;
                            a.srcIdx = (int)ti;
                        }
                    }
                    catch (const cv::Exception &)
                    {
                    }
                }
            }
            // 过滤: 按matchScore取top
            std::sort(all.begin(), all.end(), [](auto &a, auto &b)
                      { return a.matchScore < b.matchScore; });
            for (auto &a : all)
            {
                if ((int)res.size() >= p.maxContours)
                    break;
                if (a.matchScore < 999)
                    res.push_back(a);
            }
            // 把模板轮廓也加到结果里 (蓝色绘制)
            for (auto &t : templates)
            {
                t.matchScore = 0;
                t.srcIdx = -1;
                t.isTemplate = true;
                res.push_back(t);
            }
            auto t2 = clock::now();
            auto ms = [](auto a, auto b)
            { return std::chrono::duration<float, std::milli>(b - a).count(); };
            g_ContourTimeMs = ms(t0, t2);
            g_ContourCount = (int)res.size();
            int gm = 0;
            for (auto &c : res)
                if (c.matchScore < p.matchThreshold)
                    gm++;
            LogSystem::Add(LOG_INFO, "Contour:ROI匹配|%zu模板->%d结果(绿%d红%d)|%.3fms", templates.size(), g_ContourCount - templates.size(), gm, g_ContourCount - templates.size() - gm, g_ContourTimeMs);
        }
        else
        {
            // 普通模式: 全图找轮廓
            res = Find(img, p, cv::Point(0, 0), domainMask);
            auto t2 = clock::now();
            auto ms = [](auto a, auto b)
            { return std::chrono::duration<float, std::milli>(b - a).count(); };
            g_ContourTimeMs = ms(t0, t2);
            g_ContourCount = (int)res.size();
            LogSystem::Add(LOG_INFO, "轮廓:%d个|%.3fms", g_ContourCount, g_ContourTimeMs);
        }
        return res;
    }

    cv::Mat DrawContours(cv::Mat &img, const std::vector<ContourResult> &cs, const Params &p)
    {
        if (img.empty())
            return {};
        cv::Mat d = img.clone();
        int t = std::max(1, p.lineThickness);
        for (size_t i = 0; i < cs.size(); i++)
        {
            auto &c = cs[i];
            // 金色=模板轮廓(加粗)  绿色=匹配  红色=不匹配
            cv::Scalar co;
            int thick = t;
            if (c.isTemplate)
            {
                co = cv::Scalar(0, 200, 255);
                thick = t + 2;
            } // 金色加粗
            else if (c.matchScore < p.matchThreshold)
            {
                co = cv::Scalar(0, 255, 0);
            } // 绿色
            else
            {
                co = cv::Scalar(0, 0, 255);
            } // 红色
            if (p.fillContours)
            {
                cv::Mat ov = cv::Mat::zeros(d.size(), d.type());
                std::vector<std::vector<cv::Point>> w = {c.points};
                cv::drawContours(ov, w, 0, co, cv::FILLED);
                cv::addWeighted(d, 1.0, ov, 0.3, 0, d);
            }
            std::vector<std::vector<cv::Point>> w = {c.points};
            cv::drawContours(d, w, 0, co, thick);
            cv::rectangle(d, c.bbox, co, 1);
            if (p.showLabels)
            {
                char lb[64];
                if (c.isTemplate)
                    snprintf(lb, 64, "#%zu 模板", i + 1);
                else if (c.matchScore < 999)
                    snprintf(lb, 64, "#%zu T%d s%.3f", i + 1, c.srcIdx, c.matchScore);
                else
                    snprintf(lb, 64, "#%zu %.0fpx", i + 1, c.area);
                cv::Point lp(c.bbox.x, c.bbox.y - 4);
                if (lp.y < 10)
                    lp.y = c.bbox.y + c.bbox.height + 14;
                cv::putText(d, lb, lp, cv::FONT_HERSHEY_SIMPLEX, 0.4, co, 1, cv::LINE_AA);
            }
        }
        return d;
    }
    std::string Summary(const std::vector<ContourResult> &cs)
    {
        if (cs.empty())
            return "无轮廓";
        double t = 0, mn = 1e18, mx = 0;
        int gm = 0;
        for (auto &c : cs)
        {
            t += c.area;
            if (c.area < mn)
                mn = c.area;
            if (c.area > mx)
                mx = c.area;
            if (c.matchScore < 0.1)
                gm++;
        }
        char b[256];
        snprintf(b, 256, "%zu个|面积%.0f~%.0f|绿%d|%.3fms", cs.size(), mn, mx, gm, g_ContourTimeMs);
        return b;
    }
}
