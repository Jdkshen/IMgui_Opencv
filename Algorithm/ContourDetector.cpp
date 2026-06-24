#define NOMINMAX
#include "ContourDetector.h"
#include "../Log/LogSystem.h"
#include <opencv2/geometry.hpp>
#include <chrono>
extern ImVec4 color;
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

    // 单张图上找轮廓
    static std::vector<ContourResult> Find(const cv::Mat &img, const Params &p, cv::Point off = cv::Point(0, 0))
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
            cv::GaussianBlur(gray, gray, cv::Size(k, k), 0);
        }
        cv::Mat bin;
        if (p.threshMode == 0)
            cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        else if (p.threshMode == 2)
        {
            int bs = p.adaptiveBlock;
            if (bs % 2 == 0)
                bs++;
            if (bs < 3)
                bs = 3;
            cv::adaptiveThreshold(gray, bin, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, bs, 2);
        }
        else
            cv::threshold(gray, bin, p.threshValue, 255, cv::THRESH_BINARY);
        if (p.invertBinary)
            cv::bitwise_not(bin, bin);
        int retrM[] = {cv::RETR_EXTERNAL, cv::RETR_LIST, cv::RETR_TREE}, appM[] = {cv::CHAIN_APPROX_NONE, cv::CHAIN_APPROX_SIMPLE, cv::CHAIN_APPROX_TC89_L1};
        std::vector<std::vector<cv::Point>> raw;
        cv::findContours(bin, raw, retrM[std::clamp(p.retrMode, 0, 2)], appM[std::clamp(p.approxMethod, 0, 2)]);
        std::vector<ContourResult> res;
        res.reserve(std::min((size_t)p.maxContours, raw.size()));
        for (auto &pts : raw)
        {
            if ((int)res.size() >= p.maxContours)
                break;
            ContourResult cr;
            cr.points = pts;
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
                cr.bbox.x += off.x;
                cr.bbox.y += off.y;
            }
            res.push_back(cr);
        }
        return res;
    }

    std::vector<ContourResult> Detect(const cv::Mat &img, const Params &p)
    {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        g_ContourCount = 0;
        if (img.empty())
        {
            LogSystem::Add(LOG_ERROR, color, "Contour:空图");
            return {};
        }
        auto t1 = clock::now();
        std::vector<ContourResult> res;
        if (p.matchROI)
        {
            // ROI模式: 先从ROI找模板, 再全图匹配
            if (UI::gROIs.empty())
            {
                LogSystem::Add(LOG_WARN, color, "Contour:请先框选ROI");
                return {};
            }
            int ri = UI::gSelectedROI >= 0 ? UI::gSelectedROI : 0;
            auto &r = UI::gROIs[ri];
            cv::Rect roi((int)std::min(r.start.x, r.end.x), (int)std::min(r.start.y, r.end.y), (int)std::abs(r.end.x - r.start.x), (int)std::abs(r.end.y - r.start.y));
            roi &= cv::Rect(0, 0, img.cols, img.rows);
            cv::Mat crop = img(roi);
            auto templates = Find(crop, p, cv::Point(roi.x, roi.y));
            if (templates.empty())
            {
                LogSystem::Add(LOG_WARN, color, "Contour:ROI内未找到轮廓");
                return {};
            }
            LogSystem::Add(LOG_INFO, color, "Contour:ROI提取%zu个模板", templates.size());
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
            LogSystem::Add(LOG_INFO, color, "Contour:ROI匹配|%zu模板->%d结果(绿%d红%d)|%.3fms", templates.size(), g_ContourCount - templates.size(), gm, g_ContourCount - templates.size() - gm, g_ContourTimeMs);
        }
        else
        {
            // 普通模式: 全图找轮廓
            res = Find(img, p);
            auto t2 = clock::now();
            auto ms = [](auto a, auto b)
            { return std::chrono::duration<float, std::milli>(b - a).count(); };
            g_ContourTimeMs = ms(t0, t2);
            g_ContourCount = (int)res.size();
            LogSystem::Add(LOG_INFO, color, "轮廓:%d个|%.3fms", g_ContourCount, g_ContourTimeMs);
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
