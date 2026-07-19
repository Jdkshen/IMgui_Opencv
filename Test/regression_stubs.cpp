#include "../Algorithm/MultiColorFinder.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/ITool.h"
#include "../Core/RecipeManager.h"
#include "../Core/ImageState.h"
#include "../Core/FrameNavigation.h"
#include "../Core/ROIState.h"
#include "../Core/ToolChainState.h"
#include "../Core/VideoCapture.h"
#include "../Log/LogSystem.h"
#include "../UI/ROIManager.h"
#include "../UI/ToolsWindow.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdarg>
#include <cstdio>
#include <memory>

// g_McfLastTimeMs/g_McfLastCount are defined by the real MultiColorFinder.cpp,
// which is part of RegressionTests.vcxproj. Do not duplicate them here.

// g_McfLastTimeMs / g_McfLastCount 仅在 ToolExecutor::PublishResult 中使用，
// 测试项目不编译 ToolExecutor.cpp，故不在此定义。

namespace UI
{
void MoveOriginalToolToFront()
{
}

void ClearROIState()
{
    ROIState::ClearInteraction();
}

void FitImageToWindow()
{
}

void NavigateNextImage()
{
    FrameNavigation::NavigateNextImage();
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

namespace VideoCapture
{
bool OpenVideo(const std::string&) { return false; }
bool OpenCamera(int) { return false; }
void Close() {}
bool IsOpen() { return false; }
bool IsPlaying() { return false; }
bool IsCamera() { return false; }
void TogglePlay() {}
void Stop() {}
void SetLoop(bool) {}
bool IsLooping() { return false; }
int GetFrameCount() { return 0; }
int GetCurrentFrame() { return 0; }
double GetFPS() { return 0.0; }
void SeekFrame(int) {}
}
