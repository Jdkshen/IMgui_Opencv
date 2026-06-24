#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/ITool.h"
#include "../Core/RecipeManager.h"
#include "../Core/ImageState.h"
#include "../Core/ROIState.h"
#include "../Core/ToolChainState.h"
#include "../Log/LogSystem.h"
#include "../UI/ROIManager.h"
#include "../UI/ToolsWindow.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdarg>
#include <cstdio>
#include <memory>

std::string pendingPath;
bool gUseGray = false;
int gThresholdValue = 128;
bool gThresholdBinaryInv = false;
int gBlurSize = 1;
int gCannyLow = 50;
int gCannyHigh = 150;
float gBrightness = 0.0f;
float gContrast = 1.0f;
int gProcessMode = 0;
PipelineState gPipe;
ImVec4 color = ImVec4(0, 1, 0.5f, 1);
cv::Mat g_FrozenTemplate;
cv::Mat gThresholdMat;
float gTimeTotal = 0.0f;
float g_NmsThreshold = 0.3f;
bool g_TplGray = false;
bool g_TplBinary = false;
int g_TplBinThresh = 128;
bool g_TplEdge = false;
int g_TplEdgeLow = 50;
int g_TplEdgeHigh = 150;
std::vector<ROI> gMatchROIs;
std::vector<double> gMatchScores;
// g_McfLastTimeMs/g_McfLastCount are defined by the real MultiColorFinder.cpp,
// which is part of RegressionTests.vcxproj. Do not duplicate them here.
cv::Mat& gImage = ImageState::CurrentRef();
cv::Mat& gOriginalImage = ImageState::OriginalRef();
cv::Mat& gPendingUpload = ImageState::PendingUploadRef();
bool& gNeedUpload = ImageState::NeedUploadRef();
int& gImageWidth = ImageState::WidthRef();
int& gImageHeight = ImageState::HeightRef();
int& g_ImageVersion = ImageState::VersionRef();
std::vector<std::string> gImageList;
int gCurrentImageIndex = -1;
bool g_ShowPreview = false;
bool g_TMEnableRotation = false;
int g_TMRotationStart = -5;
int g_TMRotationEnd = 5;
int g_TMRotationStep = 5;
int g_TMMaxResults = 10;
int g_TMMaxImageDim = 1000;
int g_TMSearchMode = 0;
float g_TMMatchThreshold = 0.75f;

// g_McfLastTimeMs / g_McfLastCount 仅在 ToolExecutor::PublishResult 中使用，
// 测试项目不编译 ToolExecutor.cpp，故不在此定义。

namespace UI
{
std::vector<ROI>& gROIs = ROIState::Items();
int& gSelectedROI = ROIState::SelectedIndexRef();
std::vector<ToolInstance>& g_ToolInstances = ToolChainState::Tools();
int& g_ActiveToolIndex = ToolChainState::ActiveIndexRef();
bool& g_YoloLiveDetect = ToolChainState::YoloLiveDetectRef();
int& g_YoloLiveInstanceIdx = ToolChainState::YoloLiveInstanceIndexRef();
float& g_YoloLastTimeMs = ToolChainState::YoloLastTimeMsRef();
float& g_YoloLiveFrameMs = ToolChainState::YoloLiveFrameMsRef();

void MoveOriginalToolToFront()
{
}

void ClearROIState()
{
    gROIs.clear();
    gSelectedROI = -1;
}

void FitImageToWindow()
{
}

void NavigateNextImage()
{
    if (gCurrentImageIndex >= 0 && gCurrentImageIndex < static_cast<int>(gImageList.size()) - 1)
        ++gCurrentImageIndex;
}
}

void LogSystem::Add(LogLevel, const char*, ...)
{
}

void LogSystem::Add(LogLevel, const ImVec4&, const char*, ...)
{
}

void LogSystem::Clear()
{
}

std::shared_ptr<std::vector<LogEntry>> LogSystem::GetLogs()
{
    return std::make_shared<std::vector<LogEntry>>();
}

namespace TemplateMatch
{
void Run()
{
}

bool SaveTemplate(const char* filepath)
{
    return !g_FrozenTemplate.empty() && cv::imwrite(filepath, g_FrozenTemplate);
}

bool LoadTemplate(const char* filepath)
{
    g_FrozenTemplate = cv::imread(filepath, cv::IMREAD_COLOR);
    return !g_FrozenTemplate.empty();
}
}

namespace YOLODetector
{
bool LoadModel(const std::string&, const std::string&, bool)
{
    return false;
}

bool IsLoaded()
{
    return false;
}

const char* GetBackendName()
{
    return "未加载";
}

const std::string& GetModelPath()
{
    static const std::string empty;
    return empty;
}

std::vector<DetectedObject> Detect(const cv::Mat&, float, float, cv::Rect)
{
    return {};
}

void DrawDetections(cv::Mat&, const std::vector<DetectedObject>&, bool)
{
}

void Unload()
{
}
}

std::vector<DetectedObject> g_YoloOverlays;
bool g_YoloShowOverlay = false;

namespace OpenCVYoloDetector
{
float g_OpenCVYoloPreMs = 0.0f;
float g_OpenCVYoloInfMs = 0.0f;
float g_OpenCVYoloPostMs = 0.0f;
float g_OpenCVYoloTotalMs = 0.0f;

bool LoadModel(const std::string&, const std::string&)
{
    return false;
}

std::vector<DetectedObject> Detect(const cv::Mat&, float, float, cv::Rect)
{
    return {};
}
}
