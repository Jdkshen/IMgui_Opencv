#pragma once

#include "ITool.h"

class BlobTool final : public ITool
{
public:
    int minArea = 100;
    int maxArea = 10000;

    const char *GetName() const override { return "Blob"; }
    int GetType() const override { return 2; }
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json &j) override;
    ToolResult Execute(VisionContext &ctx) override;
};
