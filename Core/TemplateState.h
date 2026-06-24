#pragma once

#include <vector>

#include <opencv2/core/mat.hpp>

#include "ROI.h"

namespace TemplateState
{
    std::vector<ROI>& MatchROIs();
    std::vector<double>& MatchScores();
    std::vector<float>& MatchAngles();

    cv::Mat& FrozenTemplate();

    bool& PendingMatch();
    bool& ShowPreview();
    bool& ShowTemplateEditor();

    int& SearchMode();
    int& MaxResults();
    int& MaxImageDim();
    float& MatchThreshold();
    bool& EnableRotation();
    int& RotationStart();
    int& RotationEnd();
    int& RotationStep();

    bool& TemplateGray();
    bool& TemplateBinary();
    int& TemplateBinaryThreshold();
    bool& TemplateEdge();
    int& TemplateEdgeLow();
    int& TemplateEdgeHigh();

    float& NmsThreshold();
    float& LastMatchTime();
    double& LastBestScore();
}
