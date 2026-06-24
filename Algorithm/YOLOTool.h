#pragma once
#include "ITool.h"
#include <string>

// =====================================================
// YOLO 工具 — 实现 ITool 接口
// =====================================================
class YOLOTool : public ITool
{
public:
    const char* GetName() const override { return "YOLO检测"; }
    int GetType() const override { return 4; }

    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override;

    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

    // ---- 公开参数（UI 绑定用） ----
    std::string modelPath;
    std::string classesPath;
    float confThreshold = 0.5f;
    float nmsThreshold = 0.4f;
    bool useROI = false;
    bool useGPU = false;
};
