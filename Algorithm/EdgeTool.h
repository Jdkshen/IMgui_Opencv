#pragma once

#include "ITool.h"

class EdgeTool final : public ITool
{
public:
    int cannyLow = 50;
    int cannyHigh = 150;
    bool useGray = false;

    const char* GetName() const override { return "Edge"; }
    int GetType() const override { return 0; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
    ToolResult Execute(VisionContext& ctx) override;
};
