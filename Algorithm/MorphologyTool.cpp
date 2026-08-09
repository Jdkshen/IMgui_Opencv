#define NOMINMAX
#include "MorphologyTool.h"
#include "ToolImageUtils.h"
#include "../Core/VisionContext.h"
#include "../Log/LogSystem.h"
#include <chrono>
#include <cfloat>
#include <climits>

namespace MorphologyTool
{
    float g_ProcTimeMs = 0;
    cv::Mat Process(const cv::Mat &img, const Params &p, const cv::Mat &domainMask)
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
        const bool hasDomain = !domainMask.empty() && domainMask.type() == CV_8UC1 &&
            domainMask.size() == src.size();
        auto neutralValue = [&](bool erosion)
        {
            if (!erosion)
                return src.depth() == CV_16S ? -32768.0 :
                       src.depth() == CV_32S ? static_cast<double>(INT_MIN) :
                       src.depth() == CV_32F ? -static_cast<double>(FLT_MAX) :
                       src.depth() == CV_64F ? -DBL_MAX : 0.0;
            return src.depth() == CV_8U ? 255.0 :
                   src.depth() == CV_16U ? 65535.0 :
                   src.depth() == CV_16S ? 32767.0 :
                   src.depth() == CV_32S ? static_cast<double>(INT_MAX) :
                   src.depth() == CV_32F ? static_cast<double>(FLT_MAX) : DBL_MAX;
        };
        auto prepareStage = [&](const cv::Mat& input, bool erosion)
        {
            cv::Mat prepared = input.clone();
            if (hasDomain)
                prepared.setTo(cv::Scalar::all(neutralValue(erosion)), domainMask == 0);
            return prepared;
        };
        auto erodeStage = [&](const cv::Mat& input, cv::Mat& output)
        {
            cv::Mat prepared = prepareStage(input, true);
            cv::erode(prepared, output, k, cv::Point(-1, -1), p.iterations);
            if (hasDomain)
                ToolImageUtils::ApplyDomainMask(output, domainMask);
        };
        auto dilateStage = [&](const cv::Mat& input, cv::Mat& output)
        {
            cv::Mat prepared = prepareStage(input, false);
            cv::dilate(prepared, output, k, cv::Point(-1, -1), p.iterations);
            if (hasDomain)
                ToolImageUtils::ApplyDomainMask(output, domainMask);
        };
        cv::Mat dst;
        if (p.opType == 0)
            erodeStage(src, dst);
        else if (p.opType == 1)
            dilateStage(src, dst);
        else if (p.opType == 2)
        {
            cv::Mat tmp;
            erodeStage(src, tmp);
            dilateStage(tmp, dst);
        }
        else if (p.opType == 3)
        {
            cv::Mat tmp;
            dilateStage(src, tmp);
            erodeStage(tmp, dst);
        }
        else if (p.opType == 4)
        {
            cv::Mat e, d;
            erodeStage(src, e);
            dilateStage(src, d);
            cv::absdiff(d, e, dst);
        }
        else if (p.opType == 5)
        {
            cv::Mat tmp;
            erodeStage(src, tmp);
            dilateStage(tmp, tmp);
            cv::absdiff(src, tmp, dst);
        }
        else
        {
            cv::Mat tmp;
            dilateStage(src, tmp);
            erodeStage(tmp, tmp);
            cv::absdiff(tmp, src, dst);
        }
        if (hasDomain)
            ToolImageUtils::ApplyDomainMask(dst, domainMask);
        auto t1 = std::chrono::high_resolution_clock::now();
        g_ProcTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        static const char *nm[] = {"Erode", "Dilate", "Open", "Close", "Gradient", "TopHat", "BlackHat"};
        LogSystem::Add(LOG_INFO, "MorphTool[%s]:k=%dx%d iter=%d|%.3fms|%dx%d", nm[p.opType], ks, ks, p.iterations, g_ProcTimeMs, dst.cols, dst.rows);
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
    if (!ToolImageUtils::ValidateAreaContext(ctx, true, r.message))
    {
        r.success = false;
        return r;
    }

    cv::Mat out = ctx.image.clone();
    const cv::Rect roi = ToolImageUtils::PrimaryContextRect(ctx);
    const cv::Mat domainMask = ToolImageUtils::PrimaryContextMask(ctx);
    cv::Mat dst = MorphologyTool::Process(
        roi.empty() ? out : out(roi), params, domainMask);
    ToolImageUtils::ApplyDomainMask(dst, domainMask);
    if (!dst.empty())
    {
        if (!roi.empty())
        {
            cv::Mat converted;
            if (ToolImageUtils::ConvertForCopyTo(dst, out.channels(), converted))
            {
                if (domainMask.empty())
                    converted.copyTo(out(roi));
                else
                    converted.copyTo(out(roi), domainMask);
            }
        }
        else
        {
            if (domainMask.empty())
                out = dst;
            else
            {
                cv::Mat converted;
                if (ToolImageUtils::ConvertForCopyTo(dst, out.channels(), converted))
                    converted.copyTo(out, domainMask);
            }
        }
    }

    r.debugImage = out;
    r.success = !r.debugImage.empty();
    return r;
}
