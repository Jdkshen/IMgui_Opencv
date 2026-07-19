#include "TemplateState.h"

namespace
{
std::vector<ROI> s_matchROIs;
std::vector<double> s_matchScores;
std::vector<float> s_matchAngles;
cv::Mat s_frozenTemplate;
}

namespace TemplateState
{
void ClearResults()
{
    s_matchROIs.clear();
    s_matchScores.clear();
    s_matchAngles.clear();
}

std::vector<ROI>& MatchROIs() { return s_matchROIs; }
std::vector<double>& MatchScores() { return s_matchScores; }
std::vector<float>& MatchAngles() { return s_matchAngles; }
cv::Mat& FrozenTemplate() { return s_frozenTemplate; }
}
