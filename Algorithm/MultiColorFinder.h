#pragma once
#include "ITool.h"
#include <vector>
#include <opencv2/core/types.hpp>
#include <opencv2/core/mat.hpp>

// =====================================================
// 多点找色 — 参考图点击取色，全图/ROI 内找到匹配位置
// =====================================================

struct ColorPoint
{
    int x = 0, y = 0;           // 相对偏移（相对于点击锚点）
    int b = 0, g = 0, r = 0;    // BGR 颜色
    int tolerance = 10;         // 容差 (0-255)
};

class MultiColorFinder : public ITool
{
public:
    const char* GetName() const override { return "多点找色"; }
    int GetType() const override { return 10; }
    ToolResult Execute(VisionContext& ctx) override;
    void DrawUI() override {}
    nlohmann::json Save() const override;
    void Load(const nlohmann::json& j) override;

    // ---- 参考图（类似模板匹配的模板图） ----
    cv::Mat refImage;               // 参考图（从主图截取的小图）
    int refAnchorX = 0, refAnchorY = 0; // 锚点像素在参考图中的位置

    // ---- 颜色点列表（从参考图点击取色） ----
    std::vector<ColorPoint> points;

    // ---- 预处理 ----
    bool imgUseGray   = false;   // 主图+参考图都转灰度
    bool imgUseBinary = false;   // 主图+参考图都二值化
    int  imgBinThresh = 128;

    // ---- 搜索参数 ----
    bool useROI = false;
    int maxResults = 1;
    float minDist = 5.0f;
    int crossSize = 10;
    int crossThick = 2;
    cv::Scalar markColor = cv::Scalar(0, 255, 0);

    // ---- 配方持久化 ----
    int roiX = 0, roiY = 0, roiW = 0, roiH = 0; // 搜索ROI位置
};

// 全局状态（UI 显示用）
extern float g_McfLastTimeMs;
extern int   g_McfLastCount;

// 实时预处理显示（UI勾选灰度/二值化时立即更新主图）
void McfApplyPreview(bool useGray, bool useBinary, int binThresh, const cv::Mat& src);

