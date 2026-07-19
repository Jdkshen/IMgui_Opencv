#pragma once

#include "ITool.h"

class DifferenceTool final : public ITool
{
public:
    cv::Mat referenceImage;
    int threshold = 30;
    int minArea = 20;
    int blurSize = 0;
    int morphKernelSize = 3;
    int morphIterations = 1;
    bool invert = false;
    bool showLabels = true;

    const char* GetName() const override { return "图像差分"; }
    int GetType() const override { return 16; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& json) override;
    ToolResult Execute(VisionContext& context) override;
};
