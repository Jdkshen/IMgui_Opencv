#pragma once

#include "ITool.h"

#include <string>

class OCRTool final : public ITool
{
public:
    std::string detModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
    std::string detParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
    std::string recModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
    std::string recParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
    std::string dictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
    float minConfidence = 0.30f;
    int maxItems = 8;
    int inputSize = 512;
    int maxCandidates = 220;
    int minBoxArea = 0;
    int minBoxHeight = 0;
    int roiPadding = 24;
    bool fastMode = true;
    bool detectOnly = false;
    bool useROI = true;

    const char* GetName() const override { return "文字识别"; }
    int GetType() const override { return 13; }
    void DrawUI() override {}

    ToolResult Execute(VisionContext& ctx) override;
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

private:
    bool cacheValid = false;
    int cacheImageVersion = -1;
    const unsigned char* cacheImageData = nullptr;
    int cacheImageRows = 0;
    int cacheImageCols = 0;
    int cacheImageType = 0;
    cv::Rect cacheRoi;
    std::string cacheConfigKey;
    ToolResult cacheResult;
};
