#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "ITool.h"

namespace ColorAnalyzer
{
    struct Params
    {
        int colorSpace = 0;
        int histBins = 32;
        bool showHist = true;
        bool useROI = false;
        int histHeight = 100;
    }; // 0=BGR 1=HSV 2=Lab 3=YCbCr 4=Gray
    struct ColorResult
    {
        std::vector<float> hR, hG, hB;
        double meanR = 0, meanG = 0, meanB = 0, stdR = 0, stdG = 0, stdB = 0;
    };
    ColorResult Analyze(const cv::Mat &, const Params &,
                        const cv::Mat &domainMask = cv::Mat());
    cv::Mat DrawHistogram(const cv::Mat &, const ColorResult &, const Params &);
    std::string Summary(const ColorResult &);
    extern float g_AnalyzeTimeMs;
    extern ColorResult g_LastResult;
}

class ColorAnalyzerITool final : public ITool
{
public:
    ColorAnalyzer::Params params;

    const char* GetName() const override { return "颜色分析"; }
    int GetType() const override { return 9; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
    ToolResult Execute(VisionContext& ctx) override;
};
