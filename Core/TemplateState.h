#pragma once

#include <vector>

#include <opencv2/core/mat.hpp>

#include "ROI.h"

namespace TemplateState
{
    void ClearResults();

    std::vector<ROI>& MatchROIs();
    std::vector<double>& MatchScores();
    std::vector<float>& MatchAngles();

    // 仅用于加载旧配方中的单一模板；新工具使用 ToolInstance::templateImg/shpTplImage。
    cv::Mat& FrozenTemplate();
}
