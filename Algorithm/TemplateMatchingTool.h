#pragma once

#include "ITool.h"
#include "../Core/ROI.h"

class TemplateMatchingTool final : public ITool
{
public:
    bool enableRotation = false;
    int rotationStart = -45;
    int rotationEnd = 45;
    int rotationStep = 1;
    int maxResults = 5;
    float matchThreshold = 0.7f;
    int maxImageDim = 1000;
    float nmsThreshold = 0.3f;
    bool subpixelRefinement = true;
    bool tplGray = false;
    bool tplBinary = false;
    int tplBinThresh = 128;
    bool tplEdge = false;
    int tplEdgeLow = 50;
    int tplEdgeHigh = 150;
    bool imgUseGray = false;
    bool imgEnableThreshold = false;
    int imgThreshold = 128;
    cv::Mat templateImg;
    bool useSearchROI = false;
    std::vector<ROI> searchROIs;

    const char* GetName() const override { return "模板匹配"; }
    int GetType() const override { return 1; }
    void DrawUI() override {}
    ToolResult Execute(VisionContext& ctx) override;
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
};
