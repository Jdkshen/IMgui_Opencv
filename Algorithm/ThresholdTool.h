#pragma once

#include "ITool.h"

struct PipelineState
{
    bool enableBlur = false;
    bool enableThreshold = false;
    bool enableCanny = false;
    int blurSize = 5;
    int threshold = 128;
    int cannyLow = 50;
    int cannyHigh = 150;
};

namespace ThresholdTool
{
    void ApplyProcess(bool useGray, const PipelineState& pipeline);
    float LastTimeMs();
}

class ThresholdITool final : public ITool
{
public:
    bool useGray = false;
    bool enableBlur = false;
    int blurSize = 5;
    bool enableThreshold = false;
    int threshold = 128;
    bool enableCanny = false;
    int cannyLow = 50;
    int cannyHigh = 150;

    const char* GetName() const override { return "阈值调试"; }
    int GetType() const override { return 3; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
    ToolResult Execute(VisionContext& ctx) override;
};
