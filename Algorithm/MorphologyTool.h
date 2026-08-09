#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include "ITool.h"

namespace MorphologyTool {
    struct Params { int opType=0; int kernelSize=3, kernelShape=0; int iterations=1; bool useGray=false; }; // opType 0=erode 1=dilate 2=open 3=close 4=gradient 5=tophat 6=blackhat
    cv::Mat Process(const cv::Mat&, const Params&, const cv::Mat& domainMask = cv::Mat());
    std::string Summary(const Params&);
    extern float g_ProcTimeMs;
}

class MorphologyITool final : public ITool
{
public:
    MorphologyTool::Params params;

    const char* GetName() const override { return "形态学"; }
    int GetType() const override { return 8; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
    ToolResult Execute(VisionContext& ctx) override;
};
