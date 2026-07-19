#pragma once

#include "ITool.h"

class BlobTool final : public ITool
{
public:
    int minArea = 100;
    int maxArea = 10000;
    int thresholdMode = 0;
    int threshold = 128;
    bool invert = false;
    int connectivity = 8;
    float minCircularity = 0.0f;
    float maxCircularity = 1.0f;
    float minAspectRatio = 0.0f;
    float maxAspectRatio = 100.0f;
    bool showLabels = true;

    const char *GetName() const override { return "Blob"; }
    int GetType() const override { return 2; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json &j) override;
    ToolResult Execute(VisionContext &ctx) override;
};
