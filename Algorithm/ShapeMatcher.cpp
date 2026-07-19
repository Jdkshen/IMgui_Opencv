#define NOMINMAX
#include "ShapeMatcher.h"
#include "../Log/LogSystem.h"
#include <opencv2/geometry.hpp>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cmath>
namespace ShapeMatcher
{
    float g_MatchTimeMs = 0;
    int g_MatchCount = 0;
    std::vector<ShapeMatch> g_LastMatches;

    static double ComputeShapeDistance(const std::vector<cv::Point> &c1, const std::vector<cv::Point> &c2, int method)
    {
        if (c1.size() < 3 || c2.size() < 3)
            return 999;
        if (method == 0)
            return cv::matchShapes(c1, c2, cv::CONTOURS_MATCH_I2, 0);
        if (method == 1)
        { // 简化ShapeContext: 极坐标直方图 + chi-square
            auto scHist = [](const std::vector<cv::Point> &c, int rbins = 5, int abins = 12) -> std::vector<float>
            {
            cv::Moments m=cv::moments(c); cv::Point2f center((float)(m.m10/m.m00),(float)(m.m01/m.m00));
            float maxR=0; for(auto&p:c){float dx=p.x-center.x,dy=p.y-center.y;float r=std::sqrt(dx*dx+dy*dy);if(r>maxR)maxR=r;}
            if(maxR<1)maxR=1; std::vector<float> hist(rbins*abins,0);
            for(auto&p:c){float dx=p.x-center.x,dy=p.y-center.y;float r=std::sqrt(dx*dx+dy*dy)/maxR;float a=std::atan2(dy,dx);if(a<0)a+=2.f*static_cast<float>(CV_PI);int ri=(int)(r*rbins);if(ri>=rbins)ri=rbins-1;int ai=(int)(a/(2.f*static_cast<float>(CV_PI))*abins);if(ai>=abins)ai=abins-1;hist[ri*abins+ai]+=1.f;}
            float sum=0; for(auto&v:hist)sum+=v; if(sum>0)for(auto&v:hist)v/=sum; return hist; };
            auto h1 = scHist(c1), h2 = scHist(c2);
            double d = 0;
            for (size_t i = 0; i < h1.size(); i++)
            {
                double s = h1[i] + h2[i];
                if (s > 0)
                {
                    double diff = h1[i] - h2[i];
                    d += diff * diff / s;
                }
            }
            return d / 2.0;
        }
        if (method == 2)
        { // Hausdorff距离
            auto haus = [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) -> double
            {
            double maxD=0; for(auto&pa:a){double minD=1e9;for(auto&pb:b){double dx=pa.x-pb.x,dy=pa.y-pb.y;double d=dx*dx+dy*dy;if(d<minD)minD=d;}if(minD>maxD)maxD=minD;}return std::sqrt(maxD); };
            double h1 = haus(c1, c2), h2 = haus(c2, c1);
            double raw = std::max(h1, h2);
            return raw / std::max((double)cv::boundingRect(c1).width, 1.0);
        }
        return cv::matchShapes(c1, c2, cv::CONTOURS_MATCH_I2, 0);
    }
    static const char *kMethodNames[] = {"Hu矩", "ShapeContext", "Hausdorff"};
    static constexpr int kMaxSearchDim = 800;
    static constexpr int kMaxCandidateMultiplier = 12;
    static constexpr int kMaxCandidatesHardLimit = 40;

    static void SuppressAround(cv::Mat& scores, const cv::Point& center, const cv::Size& tplSize)
    {
        const int rx = std::max(4, tplSize.width / 3);
        const int ry = std::max(4, tplSize.height / 3);
        cv::Rect suppress(center.x - rx, center.y - ry, rx * 2 + 1, ry * 2 + 1);
        suppress &= cv::Rect(0, 0, scores.cols, scores.rows);
        if (!suppress.empty())
            scores(suppress).setTo(-1.0f);
    }

    cv::Mat Preprocess(const cv::Mat &s, const Params &p)
    {
        if (s.empty())
            return {};
        cv::Mat g;
        if (s.channels() == 1)
            g = s;
        else
            cv::cvtColor(s, g, s.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        if (p.blurSize > 0)
        {
            int k = p.blurSize * 2 + 1;
            if (k < 3)
                k = 3;
            cv::GaussianBlur(g, g, cv::Size(k, k), 0);
        }
        return g;
    }
    std::vector<std::vector<cv::Point>> ExtractTemplates(const cv::Mat &t, const Params &pp)
    {
        if (t.empty() || t.cols < 3 || t.rows < 3)
            return {};
        // 模板预处理
        cv::Mat proc;
        if (t.channels() >= 3 && pp.tplGray)
            cv::cvtColor(t, proc, cv::COLOR_BGR2GRAY);
        else if (t.channels() >= 3)
            cv::cvtColor(t, proc, cv::COLOR_BGR2GRAY);
        else
            proc = t.clone();
        if (pp.tplBlur)
        {
            int k = pp.tplBlurK * 2 + 1;
            if (k < 3)
                k = 3;
            cv::GaussianBlur(proc, proc, cv::Size(k, k), 0);
        }
        cv::Mat b;
        if (pp.tplBinary)
        {
            cv::threshold(proc, b, pp.tplBinThresh, 255, cv::THRESH_BINARY);
        }
        else
        {
            cv::threshold(proc, b, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        }
        if (pp.tplInvert)
            cv::bitwise_not(b, b);
        int retM[] = {cv::RETR_EXTERNAL, cv::RETR_LIST};
        std::vector<std::vector<cv::Point>> rw;
        cv::findContours(b, rw, retM[std::clamp(pp.tplRetrMode, 0, 1)], cv::CHAIN_APPROX_SIMPLE);
        std::vector<std::pair<double, std::vector<cv::Point>>> sc;
        for (auto &p : rw)
        {
            double a = cv::contourArea(p);
            if (a < pp.tplMinArea || p.size() < 3)
                continue;
            sc.push_back({a, p});
        }
        std::sort(sc.begin(), sc.end(), [](auto &a, auto &b)
                  { return a.first > b.first; });
        std::vector<std::vector<cv::Point>> o;
        for (int i = 0; i < (int)sc.size() && i < 20; i++)
            o.push_back(sc[i].second);
        return o;
    }

    std::vector<ShapeMatch> Search(const cv::Mat &img, const cv::Mat &tplImg, const Params &p, const std::vector<std::vector<cv::Point>> &tplContours)
    {
        using clock = std::chrono::high_resolution_clock;
        auto t0 = clock::now();
        g_MatchCount = 0;
        if (img.empty() || tplImg.empty())
            return {};

        // --- 第1步：提取模板轮廓 ---
        auto tplCont = tplContours.empty() ? ExtractTemplates(tplImg, p) : tplContours;
        if (tplCont.empty())
        {
            LogSystem::Add(LOG_WARN, "形状匹配:未提取到模板轮廓");
            return {};
        }

        // --- 第2步：matchTemplate 快速定位候选区域 ---
        cv::Mat sg = Preprocess(img, p), tg = Preprocess(tplImg, p);
        if (sg.empty() || tg.empty())
            return {};
        int md = std::max(img.cols, img.rows);
        float sc = 1;
        cv::Mat s = sg, t = tg;
        if (md > kMaxSearchDim)
        {
            sc = static_cast<float>(kMaxSearchDim) / md;
            cv::resize(sg, s, cv::Size(), sc, sc, cv::INTER_AREA);
            int tw = (int)(tg.cols * sc), th = (int)(tg.rows * sc);
            if (tw < 5 || th < 5)
                return {};
            cv::resize(tg, t, cv::Size(tw, th), 0, 0, cv::INTER_AREA);
        }
        if (s.empty() || t.empty() || s.type() != t.type() || s.cols < t.cols || s.rows < t.rows)
        {
            LogSystem::Add(LOG_WARN, "形状匹配:搜索区域小于模板或图像类型不一致 search=%dx%d tpl=%dx%d", s.cols, s.rows, t.cols, t.rows);
            return {};
        }
        cv::Mat res;
        cv::matchTemplate(s, t, res, cv::TM_CCOEFF_NORMED);
        if (res.empty())
            return {};
        const int requestedResults = std::max(1, p.maxResults);
        const int mc = std::clamp(requestedResults * kMaxCandidateMultiplier, requestedResults, kMaxCandidatesHardLimit);
        std::vector<std::pair<cv::Point, double>> cand;
        cand.reserve(mc);
        cv::Mat scoreMap = res.clone();
        for (int i = 0; i < mc; ++i)
        {
            double minVal = 0.0, maxVal = 0.0;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(scoreMap, &minVal, &maxVal, &minLoc, &maxLoc);
            if (maxVal < p.minScore)
                break;

            cand.push_back({maxLoc, maxVal});
            SuppressAround(scoreMap, maxLoc, t.size());
        }

        // NMS（基于模板面积的30%作为去重半径）
        float isc = 1.f / sc;
        std::vector<char> sup(cand.size(), 0);
        for (size_t i = 0; i < cand.size(); i++)
        {
            if (sup[i])
                continue;
            int xi = cand[i].first.x, yi = cand[i].first.y;
            int nms = t.cols * t.rows / 8;
            for (size_t j = i + 1; j < cand.size(); j++)
            {
                if (sup[j])
                    continue;
                int dx = xi - cand[j].first.x, dy = yi - cand[j].first.y;
                if (dx * dx + dy * dy < nms)
                    sup[j] = 1;
            }
        }

        // --- 第3步：每个候选区内找轮廓 → matchShapes 比对模板轮廓 ---
        std::vector<ShapeMatch> fn;
        for (size_t i = 0; i < cand.size() && (int)fn.size() < p.maxResults; i++)
        {
            if (sup[i])
                continue;
            cv::Rect b((int)(cand[i].first.x * isc), (int)(cand[i].first.y * isc), tg.cols, tg.rows);
            b &= cv::Rect(0, 0, img.cols, img.rows);
            if (b.width < 5 || b.height < 5)
                continue;

            // 候选区提取轮廓并比对
            cv::Mat roi = img(b);
            cv::Mat roiGray;
            if (roi.channels() >= 3)
                cv::cvtColor(roi, roiGray, cv::COLOR_BGR2GRAY);
            else
                roiGray = roi;
            cv::Mat roiBin;
            cv::GaussianBlur(roiGray, roiGray, cv::Size(5, 5), 0);
            cv::threshold(roiGray, roiBin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            std::vector<std::vector<cv::Point>> roiContours;
            cv::findContours(roiBin, roiContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            // matchShapes 逐一比对，取最佳
            double bestShape = 999;
            int bestContourIdx = -1;
            for (int ci = 0; ci < (int)roiContours.size(); ci++)
            {
                auto &rc = roiContours[ci];
                if (cv::contourArea(rc) < p.tplMinArea || rc.size() < 3)
                    continue;
                for (auto &tc : tplCont)
                {
                    double ms = ComputeShapeDistance(tc, rc, p.shapeMethod);
                    if (ms < bestShape)
                    {
                        bestShape = ms;
                        bestContourIdx = ci;
                    }
                }
            }
            bool foundContour = (bestContourIdx >= 0);
            // 绿色 = 轮廓对上了 且 形状得分低于阈值
            bool isGreen = foundContour && (bestShape <= p.minShapeScore);

            ShapeMatch sm;
            sm.bbox = b;
            sm.score = cand[i].second;
            sm.shapeScore = foundContour ? bestShape : 999;
            sm.isGreen = isGreen;
            if (foundContour)
            {
                cv::approxPolyDP(roiContours[bestContourIdx], sm.points, 1.5, true);
                sm.area = cv::contourArea(sm.points);
            }
            fn.push_back(sm);
        }

        auto te = clock::now();
        auto ms = [](auto a, auto b)
        { return std::chrono::duration<float, std::milli>(b - a).count(); };
        g_MatchTimeMs = ms(t0, te);
        g_MatchCount = (int)fn.size();
        g_LastMatches = fn;
        int ng = 0, nr = 0;
        for (auto &m : fn)
        {
            if (m.isGreen)
                ng++;
            else
                nr++;
        }
            LogSystem::Add(LOG_INFO, "ShapeMatcher[%s]:%d个(绿%d红%d)|%.3fms|tpl=%dx%d|minScore=%.2f shapeThres=%.3f candidates=%d", kMethodNames[p.shapeMethod], g_MatchCount, ng, nr, g_MatchTimeMs, tg.cols, tg.rows, p.minScore, p.minShapeScore, (int)cand.size());
        return fn;
    }
    cv::Mat DrawMatches(cv::Mat &img, const std::vector<ShapeMatch> &ms, const Params &p)
    {
        if (img.empty())
            return {};
        cv::Mat d = img.clone();
        static const cv::Scalar GREEN(0, 255, 0), RED(0, 0, 255);
        int t = std::max(1, p.lineThickness);
        for (size_t i = 0; i < ms.size(); i++)
        {
            auto &m = ms[i];
            cv::Scalar co = m.isGreen ? GREEN : RED;
            cv::rectangle(d, m.bbox, co, t);
            if (!m.points.empty())
            {
                std::vector<std::vector<cv::Point>> wrap = {m.points};
                for (auto &pt : wrap[0])
                {
                    pt.x += m.bbox.x;
                    pt.y += m.bbox.y;
                }
                cv::drawContours(d, wrap, -1, m.isGreen ? GREEN : cv::Scalar(0, 0, 200), std::max(1, t - 1));
            }
            if (p.showLabels)
            {
                char lb[128];
                snprintf(lb, 128, "#%zu t=%.3f s=%.3f %s", i + 1, m.score, m.shapeScore, m.isGreen ? "OK" : "NG");
                cv::Point lp(m.bbox.x, m.bbox.y - 4);
                if (lp.y < 10)
                    lp.y = m.bbox.y + m.bbox.height + 14;
                cv::putText(d, lb, lp, cv::FONT_HERSHEY_SIMPLEX, 0.35, co, 1, cv::LINE_AA);
            }
        }
        return d;
    }
    std::string Summary(const std::vector<ShapeMatch> &ms)
    {
        if (ms.empty())
            return "无匹配";
        int ng = 0;
        for (auto &m : ms)
            if (m.isGreen)
                ng++;
        char b[192];
        snprintf(b, 192, "%zu个(绿%d红%d)", ms.size(), ng, (int)ms.size() - ng);
        return b;
    }
}
