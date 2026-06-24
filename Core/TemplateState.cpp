#include "TemplateState.h"

namespace
{
std::vector<ROI> s_matchROIs;
std::vector<double> s_matchScores;
std::vector<float> s_matchAngles;

cv::Mat s_frozenTemplate;

bool s_pendingMatch = false;
bool s_showPreview = false;
bool s_showTemplateEditor = false;

float s_matchThreshold = 0.75f;
float s_nmsThreshold = 0.30f;
int s_maxImageDim = 1000;
float s_lastMatchTime = 0.0f;
double s_lastBestScore = 0.0;
int s_searchMode = 0;
int s_maxResults = 10;
bool s_enableRotation = false;
int s_rotationStart = -5;
int s_rotationEnd = 5;
int s_rotationStep = 5;

bool s_tplGray = false;
bool s_tplBinary = false;
int s_tplBinThresh = 128;
bool s_tplEdge = false;
int s_tplEdgeLow = 50;
int s_tplEdgeHigh = 150;
}

namespace TemplateState
{
std::vector<ROI>& MatchROIs() { return s_matchROIs; }
std::vector<double>& MatchScores() { return s_matchScores; }
std::vector<float>& MatchAngles() { return s_matchAngles; }

cv::Mat& FrozenTemplate() { return s_frozenTemplate; }

bool& PendingMatch() { return s_pendingMatch; }
bool& ShowPreview() { return s_showPreview; }
bool& ShowTemplateEditor() { return s_showTemplateEditor; }

int& SearchMode() { return s_searchMode; }
int& MaxResults() { return s_maxResults; }
int& MaxImageDim() { return s_maxImageDim; }
float& MatchThreshold() { return s_matchThreshold; }
bool& EnableRotation() { return s_enableRotation; }
int& RotationStart() { return s_rotationStart; }
int& RotationEnd() { return s_rotationEnd; }
int& RotationStep() { return s_rotationStep; }

bool& TemplateGray() { return s_tplGray; }
bool& TemplateBinary() { return s_tplBinary; }
int& TemplateBinaryThreshold() { return s_tplBinThresh; }
bool& TemplateEdge() { return s_tplEdge; }
int& TemplateEdgeLow() { return s_tplEdgeLow; }
int& TemplateEdgeHigh() { return s_tplEdgeHigh; }

float& NmsThreshold() { return s_nmsThreshold; }
float& LastMatchTime() { return s_lastMatchTime; }
double& LastBestScore() { return s_lastBestScore; }
}
