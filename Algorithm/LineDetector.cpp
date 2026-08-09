#define NOMINMAX
#include "LineDetector.h"
#include "../Log/LogSystem.h"
#include <chrono>
namespace LineDetector
{
    float g_LineTimeMs = 0;
    int g_LineCount = 0;
    static float Angle(const cv::Point2f &a, const cv::Point2f &b)
    {
        float d = b.x - a.x, dy = b.y - a.y, r = atan2(dy, d) * 180.f / (float)CV_PI;
        if (r < 0)
            r += 180;
        if (r >= 180)
            r -= 180;
        return r;
    }
    static float Len(const cv::Point2f &a, const cv::Point2f &b)
    {
        float dx = b.x - a.x, dy = b.y - a.y;
        return sqrt(dx * dx + dy * dy);
    }
    std::vector<LineResult> Detect(const cv::Mat &img, const Params &p,
                                   const cv::Mat &domainMask)
    {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        g_LineCount = 0;
        if (img.empty())
        {
            LogSystem::Add(LOG_ERROR, "Line:空图");
            return {};
        }
        cv::Rect r = p.roi;
        if (r.width <= 0 || r.height <= 0)
            r = cv::Rect(0, 0, img.cols, img.rows);
        r &= cv::Rect(0, 0, img.cols, img.rows);
        cv::Point off = r.tl();
        cv::Mat crop = img(r), gray;
        if (crop.channels() == 1)
            gray = crop;
        else
            cv::cvtColor(crop, gray, crop.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        cv::Mat edges;
        cv::Canny(gray, edges, p.cannyLow, p.cannyHigh);
        if (!domainMask.empty() && domainMask.type() == CV_8UC1 &&
            domainMask.size() == img.size())
        {
            edges.setTo(0, domainMask(r) == 0);
        }
        auto t1 = clock::now();
        std::vector<cv::Vec4i> raw;
        cv::HoughLinesP(edges, raw, 1, CV_PI / 180, 50, p.minLineLength, p.maxLineGap);
        std::vector<LineResult> res;
        for (auto &l : raw)
        {
            cv::Point2f p1(static_cast<float>(l[0] + off.x), static_cast<float>(l[1] + off.y)), p2(static_cast<float>(l[2] + off.x), static_cast<float>(l[3] + off.y));
            float len = Len(p1, p2);
            if (len < p.minLineLength)
                continue;
            float ang = Angle(p1, p2), na = std::min(ang, 180.f - ang), nmi = std::min(p.minAngle, 180.f - p.minAngle), nma = std::min(p.maxAngle, 180.f - p.maxAngle);
            float a = std::min(nmi, nma), b = std::max(nmi, nma);
            if (na < a || na > b)
                continue;
            LineResult lr;
            lr.pt1 = p1;
            lr.pt2 = p2;
            lr.length = len;
            lr.angle = ang;
            res.push_back(lr);
        }
        std::sort(res.begin(), res.end(), [](auto &a, auto &b)
                  { return a.length > b.length; });
        if ((int)res.size() > p.maxLines)
            res.resize(p.maxLines);
        auto t2 = clock::now();
        auto ms = [](auto a, auto b)
        { return std::chrono::duration<float, std::milli>(b - a).count(); };
        g_LineTimeMs = ms(t0, t2);
        g_LineCount = (int)res.size();
        LogSystem::Add(LOG_INFO, "直线:Canny%.3fms+Hough%.3fms|%zu->%d", ms(t0, t1), ms(t1, t2), raw.size(), g_LineCount);
        return res;
    }
    cv::Mat DrawLines(const cv::Mat &img, const std::vector<LineResult> &ls, const Params &p)
    {
        if (img.empty())
            return {};
        cv::Mat d = img.clone();
        int t = std::max(1, p.lineThickness);
        for (size_t i = 0; i < ls.size(); i++)
        {
            auto &l = ls[i];
            cv::line(d, cv::Point(l.pt1), cv::Point(l.pt2), cv::Scalar(0, 255, 0), t);
            if (p.showLabels)
            {
                cv::Point m(static_cast<int>((l.pt1.x + l.pt2.x) * .5f), static_cast<int>((l.pt1.y + l.pt2.y) * .5f));
                char b[32];
                snprintf(b, 32, "#%zu %.0f", i + 1, l.length);
                cv::putText(d, b, m, cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
            }
        }
        return d;
    }
    std::string Summary(const std::vector<LineResult> &ls)
    {
        if (ls.empty())
            return "无直线";
        double t = 0, mn = 1e18, mx = 0;
        for (auto &l : ls)
        {
            t += l.length;
            if (l.length < mn)
                mn = l.length;
            if (l.length > mx)
                mx = l.length;
        }
        char b[256];
        snprintf(b, 256, "%zu条|总长%.0f|%.0f~%.0f|%.3fms", ls.size(), t, mn, mx, g_LineTimeMs);
        return b;
    }
}
