#define NOMINMAX
#include "ColorAnalyzer.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"
#include "../Log/LogSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace ColorAnalyzer
{
    float g_AnalyzeTimeMs = 0;
    ColorResult g_LastResult;
    ColorResult Analyze(const cv::Mat &img, const Params &p)
    {
        ColorResult r;
        if (img.empty())
            return r;
        auto t0 = std::chrono::high_resolution_clock::now();
        cv::Mat src;
        int colorSpace = std::clamp(p.colorSpace, 0, 4);
        int histBins = std::clamp(p.histBins, 1, 256);
        if (colorSpace == 0 || colorSpace == 4 || img.channels() == 1)
        {
            src = img.clone();
        }
        else
        {
            int c[] = {cv::COLOR_BGR2HSV, cv::COLOR_BGR2Lab, cv::COLOR_BGR2YCrCb};
            cv::cvtColor(img, src, c[colorSpace - 1]);
        }
        int nc = src.channels();
        for (int ch = 0; ch < 3 && ch < nc; ch++)
        {
            std::vector<float> hist(histBins, 0), &out = (ch == 0 ? r.hR : ch == 1 ? r.hG
                                                                                   : r.hB);
            double sum = 0, sum2 = 0;
            int total = src.rows * src.cols;
            for (int y = 0; y < src.rows; y++)
            {
                const uchar *row = src.ptr<uchar>(y);
                for (int x = 0; x < src.cols; x++)
                {
                    uchar v = row[x * nc + ch];
                    int bin = v * histBins / 256;
                    hist[bin]++;
                    sum += v;
                    sum2 += v * v;
                }
            }
            double mean = sum / total;
            double std = std::sqrt(sum2 / total - mean * mean);
            (ch == 0 ? r.meanR : ch == 1 ? r.meanG
                                         : r.meanB) = mean;
            (ch == 0 ? r.stdR : ch == 1 ? r.stdG
                                        : r.stdB) = std;
            float peak = 1;
            for (auto &v : hist)
                if (v > peak)
                    peak = v;
            out.resize(histBins);
            for (int i = 0; i < histBins; i++)
                out[i] = hist[i] / peak;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        g_AnalyzeTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        g_LastResult = r;
        static const char *cs[] = {"BGR", "HSV", "Lab", "YCbCr", "Gray"};
        LogSystem::Add(LOG_INFO, "Color[%s]:R=%.1f±%.1f G=%.1f±%.1f B=%.1f±%.1f|%.3fms", cs[colorSpace], r.meanR, r.stdR, r.meanG, r.stdG, r.meanB, r.stdB, g_AnalyzeTimeMs);
        return r;
    }
    cv::Mat DrawHistogram(const cv::Mat & /*img*/, const ColorResult &r, const Params &p)
    {
        int hh = std::clamp(p.histHeight, 40, 400), w = 256, pad = 20;
        int histBins = std::clamp(p.histBins, 1, 256);
        cv::Mat hist(hh + pad * 2, w + pad * 2, CV_8UC3, cv::Scalar(30, 30, 30));
        auto draw = [&](const std::vector<float> &vals, cv::Scalar col, int ch)
        {
            if (vals.empty())
                return;
            int bw = (std::max)(1, (w - 20) / histBins);
            int n = (std::min)(histBins, (int)vals.size());
            for (int i = 0; i < n; i++)
            {
                int barH = (int)(vals[i] * (hh - 20));
                cv::rectangle(hist, cv::Rect(pad + i * bw, pad + hh - 20 - barH, (std::max)(1, bw - 1), barH), col, cv::FILLED);
            }
        };
        draw(r.hB, cv::Scalar(255, 0, 0), 0);
        draw(r.hG, cv::Scalar(0, 255, 0), 1);
        draw(r.hR, cv::Scalar(0, 0, 255), 2);
        return hist;
    }
    std::string Summary(const ColorResult &r)
    {
        char b[128];
        snprintf(b, 128, "R:%.1f±%.1f G:%.1f±%.1f B:%.1f±%.1f", r.meanR, r.stdR, r.meanG, r.stdG, r.meanB, r.stdB);
        return b;
    }
}

nlohmann::json ColorAnalyzerITool::Save() const
{
    return {{"type", 9}, {"colorSpace", params.colorSpace}, {"histBins", params.histBins}, {"showHist", params.showHist}, {"useROI", params.useROI}, {"histHeight", params.histHeight}};
}

void ColorAnalyzerITool::Load(const nlohmann::json &j)
{
    params.colorSpace = j.value("colorSpace", 0);
    params.histBins = j.value("histBins", 32);
    params.showHist = j.value("showHist", true);
    params.useROI = j.value("useROI", false);
    params.histHeight = j.value("histHeight", 100);
}

ToolResult ColorAnalyzerITool::Execute(VisionContext &ctx)
{
    ToolResult r;
    r.toolName = GetName();
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }

    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx, params.useROI);
    const cv::Mat input = roi.empty() ? ctx.image : ctx.image(roi);
    auto cr = ColorAnalyzer::Analyze(input, params);
    r.measurements.push_back({"meanR", cr.meanR, ""});
    r.measurements.push_back({"meanG", cr.meanG, ""});
    r.measurements.push_back({"meanB", cr.meanB, ""});
    r.measurements.push_back({"stdR", cr.stdR, ""});
    r.measurements.push_back({"stdG", cr.stdG, ""});
    r.measurements.push_back({"stdB", cr.stdB, ""});
    r.debugImage = ColorAnalyzer::DrawHistogram(input, cr, params);
    r.success = true;
    return r;
}
