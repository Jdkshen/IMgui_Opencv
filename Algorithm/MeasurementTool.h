#pragma once

#include "CaliperOperators.h"
#include "ITool.h"
#include "../Core/CalibrationModel.h"

class MeasurementTool final : public ITool
{
public:
    // 0=point-point, 1=edge-pair width, 2=line-line angle, 3=circle fit diameter,
    // 4=edge point, 5=line fit, 6=point-line distance, 7=line-line distance.
    int mode = 0;

    int caliperCount = 16;
    CaliperOperators::CaliperParams caliper;
    CaliperOperators::FitMethod fitMethod = CaliperOperators::FitMethod::Ransac;
    float fitInlierThreshold = 1.5f;
    int minimumValidCalipers = 3;
    float minimumConfidence = 0.0f;

    // Kept for old recipes. New recipes should use calibration.
    float mmPerPixel = 0.0f;
    CalibrationModel calibration;

    bool toleranceEnabled = false;
    float nominal = 0.0f;
    float toleranceMinus = 0.0f;
    float tolerancePlus = 0.0f;

    const char* GetName() const override { return "工业测量"; }
    int GetType() const override { return 15; }
    void DrawUI() override {}

    ToolResult Execute(VisionContext& ctx) override;
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
};
