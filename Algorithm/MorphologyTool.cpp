#define NOMINMAX
#include "MorphologyTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"
#include "../Log/LogSystem.h"
#include <chrono>
extern ImVec4 color;

namespace MorphologyTool
{
    float g_ProcTimeMs = 0;
    cv::Mat Process(const cv::Mat &img, const Params &p)
    {
        if (img.empty())
            return {};
        auto t0 = std::chrono::high_resolution_clock::now();
        cv::Mat src;
        if (p.useGray && img.channels() >= 3)
            cv::cvtColor(img, src, cv::COLOR_BGR2GRAY);
        else
            src = img.clone();
        int ks = p.kernelSize * 2 + 1;
        if (ks < 3)
            ks = 3;
        int sh[] = {cv::MORPH_RECT, cv::MORPH_ELLIPSE, cv::MORPH_CROSS};
        cv::Mat k = cv::getStructuringElement(sh[p.kernelShape], cv::Size(ks, ks));
        cv::Mat dst;
        if (p.opType == 0)
            cv::erode(src, dst, k, cv::Point(-1, -1), p.iterations);
        else if (p.opType == 1)
            cv::dilate(src, dst, k, cv::Point(-1, -1), p.iterations);
        else if (p.opType == 2)
        {
            cv::Mat tmp;
            cv::erode(src, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::dilate(tmp, dst, k, cv::Point(-1, -1), p.iterations);
        }
        else if (p.opType == 3)
        {
            cv::Mat tmp;
            cv::dilate(src, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::erode(tmp, dst, k, cv::Point(-1, -1), p.iterations);
        }
        else if (p.opType == 4)
        {
            cv::Mat e, d;
            cv::erode(src, e, k, cv::Point(-1, -1), p.iterations);
            cv::dilate(src, d, k, cv::Point(-1, -1), p.iterations);
            cv::absdiff(d, e, dst);
        }
        else if (p.opType == 5)
        {
            cv::Mat tmp;
            cv::erode(src, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::dilate(tmp, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::absdiff(src, tmp, dst);
        }
        else
        {
            cv::Mat tmp;
            cv::dilate(src, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::erode(tmp, tmp, k, cv::Point(-1, -1), p.iterations);
            cv::absdiff(tmp, src, dst);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        g_ProcTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        static const char *nm[] = {"Erode", "Dilate", "Open", "Close", "Gradient", "TopHat", "BlackHat"};
        LogSystem::Add(LOG_INFO, color, "MorphTool[%s]:k=%dx%d iter=%d|%.3fms|%dx%d", nm[p.opType], ks, ks, p.iterations, g_ProcTimeMs, dst.cols, dst.rows);
        return dst;
    }
    std::string Summary(const Params &p)
    {
        static const char *nm[] = {"Erode", "Dilate", "Open", "Close", "Gradient", "TopHat", "BlackHat"};
        char b[64];
        snprintf(b, 64, "%s %dx%d*%d", nm[p.opType], p.kernelSize, p.kernelSize, p.iterations);
        return b;
    }
}

nlohmann::json MorphologyITool::Save() const
{
    return {{"type", 8}, {"opType", params.opType}, {"kernelSize", params.kernelSize}, {"kernelShape", params.kernelShape}, {"iterations", params.iterations}, {"useGray", params.useGray}};
}

void MorphologyITool::Load(const nlohmann::json &j)
{
    params.opType = j.value("opType", 0);
    params.kernelSize = j.value("kernelSize", 3);
    params.kernelShape = j.value("kernelShape", 0);
    params.iterations = j.value("iterations", 1);
    params.useGray = j.value("useGray", false);
}

ToolResult MorphologyITool::Execute(VisionContext &ctx)
{
    ToolResult r;
    r.toolName = GetName();
    if (ctx.image.empty())
    {
        r.success = false;
        r.message = "请先加载图片";
        return r;
    }

    cv::Mat out = ctx.image.clone();
    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    cv::Mat dst = MorphologyTool::Process(roi.empty() ? out : out(roi), params);
    if (!dst.empty())
    {
        if (!roi.empty())
        {
            cv::Mat converted;
            if (ToolImageUtils::ConvertForCopyTo(dst, out.channels(), converted))
                converted.copyTo(out(roi));
        }
        else
        {
            out = dst;
        }
    }

    r.debugImage = out;
    r.success = !r.debugImage.empty();
    return r;
}
