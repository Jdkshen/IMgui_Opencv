#pragma once
#include "ITool.h"
#include "ShapeMatcher.h"

class ContourTool : public ITool
{
public:
    const char* GetName() const override { return "轮廓分析"; }
    int GetType() const override { return 5; }
    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

    // 参数
    bool useGray = true;
    int blurSize = 5, threshMode = 0, threshValue = 128, adaptBlock = 11;
    bool invert = false;
    int retrMode = 0, approxMethod = 1;
    float minArea = 100;
    int maxContours = 500;
    bool filterConvex = false;
    float approxEps = 0.02f;
    int lineThick = 2;
    bool showLabels = true, fillContours = false;
    bool matchROI = false;
    float matchThresh = 0.1f;
};

class LineTool : public ITool
{
public:
    const char* GetName() const override { return "直线检测"; }
    int GetType() const override { return 7; }
    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

    int cannyLow = 50, cannyHigh = 150;
    float minLength = 100, maxGap = 20, minAngle = 0, maxAngle = 180;
    int thickness = 2, maxLines = 100;
    bool showLabels = true, useROI = false;
};

class ShapeTool : public ITool
{
public:
    const char* GetName() const override { return "形状匹配"; }
    int GetType() const override { return 6; }
    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

    cv::Mat tplImage;
    int blurSize = 5, tplRetr = 0;
    float tplMinArea = 30, minScore = 0.5f, shapeScore = 0.1f;
    int lineThick = 2, method = 0;
    bool showLabels = true;
    int maxResults = 1;
    bool tplGray = false, tplBinary = false;
    int tplBinThresh = 128;
    bool tplBlur = false;
    int tplBlurK = 5;
    bool tplInvert = false;

private:
    bool IsTemplateCacheValid(const cv::Mat& tpl, const ShapeMatcher::Params& params) const;
    void UpdateTemplateCache(const cv::Mat& tpl, const ShapeMatcher::Params& params);

    bool cachedTplReady = false;
    cv::Mat cachedTplImage;
    int cachedTplType = -1;
    cv::Size cachedTplSize;
    int cachedBlurSize = -1;
    int cachedTplRetr = -1;
    double cachedTplMinArea = -1.0;
    bool cachedTplGray = false;
    bool cachedTplBinary = false;
    int cachedTplBinThresh = -1;
    bool cachedTplBlur = false;
    int cachedTplBlurK = -1;
    bool cachedTplInvert = false;
    std::vector<std::vector<cv::Point>> cachedTplContours;
};
