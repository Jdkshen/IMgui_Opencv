#pragma once

#include "ITool.h"
#include "../Core/GeometryPrimitive.h"

class GeometryDrawTool final : public ITool
{
public:
    std::vector<GeometryPrimitive> primitives;

    const char* GetName() const override { return "几何绘制"; }
    int GetType() const override { return 17; }
    void DrawUI() override {}

    ToolResult Execute(VisionContext& ctx) override;
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& json) override;

    static bool DrawPrimitive(cv::Mat& image, const GeometryPrimitive& primitive);
};
