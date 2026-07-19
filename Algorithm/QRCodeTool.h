#pragma once

#include "ITool.h"
#include "../Core/BarcodeTypes.h"

class QRCodeTool final : public ITool
{
public:
    bool useROI = true;
    bool detectMulti = true;
    bool enhance = true;
    int minSize = 24;
    bool showText = true;
    int engine = 0; // 0=Auto, 1=OpenCV, 2=ZXing-cpp
    std::uint32_t formatMask = BarcodeFormatAll;
    bool filterDuplicates = true;

    const char* GetName() const override { return "二维码/条码识别"; }
    int GetType() const override { return 14; }
    void DrawUI() override {}

    ToolResult Execute(VisionContext& ctx) override;
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;
};
