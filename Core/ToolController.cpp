#include "ToolController.h"
#include "ToolExecutor.h"
#include "FrameNavigation.h"
#include "ImageUtils.h"
#include "LegacyAppState.h"
#include "ToolChainState.h"
#include "UIStateBridge.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/TemplateMatch.h"
#include <opencv2/opencv.hpp>

namespace ToolController
{
    static Mode s_mode = Mode::Idle;
    static std::queue<int> s_queue;
    static int s_currentIndex = -1;
    static int s_stepCursor = 0;
    static bool s_isStep = false;
    static bool s_loop = false;
    static float s_stepTimeMs = 0;
    static float s_batchTotalMs = 0;
    static std::vector<float> s_toolTimesMs;
    static cv::Mat s_originalImage;
    static int s_originalVersion = -1;
    static cv::Mat s_lastInputImage;
    static cv::Mat s_lastOutputImage;
    static cv::Mat s_originalToolOutputImage;
    static bool s_imageDirty = false;
    static bool s_batchTimerStarted = false;
    static bool s_runtimeMode = false;
    static std::chrono::high_resolution_clock::time_point s_batchStart;

    static bool RestoreBatchOriginal()
    {
        if ((s_originalImage.empty() || s_originalVersion != g_ImageVersion) && !gOriginalImage.empty())
        {
            s_originalImage = gOriginalImage.clone();
            s_originalVersion = g_ImageVersion;
        }

        if (s_originalImage.empty())
        {
            LogSystem::Add(LOG_WARN, "原图: 本轮原图为空");
            return false;
        }

        s_originalImage.copyTo(gImage);
        cv::Mat rgba;
        SafeConvertToRGBA(gImage, rgba);
        if (!rgba.empty())
        {
            gPendingUpload = rgba;
            gNeedUpload = true;
        }
        s_lastInputImage = s_originalImage.clone();
        s_lastOutputImage = s_originalImage.clone();
        s_originalToolOutputImage = s_originalImage.clone();
        TemplateMatch::Clear();
        gContext.ClearUnifiedResults();
        g_YoloOverlays.clear();
        g_YoloShowOverlay = false;
        LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "原图: 已恢复本轮原图");
        return true;
    }

    static void EnsureToolTimesSize()
    {
        const auto& tools = ToolChainState::ReadOnlyTools();
        if (s_toolTimesMs.size() != tools.size())
            s_toolTimesMs.assign(tools.size(), 0.0f);
    }

    static bool ExecuteToolAt(int idx)
    {
        auto& tools = ToolChainState::Tools();
        if (idx < 0 || idx >= (int)tools.size())
            return false;

        EnsureToolTimesSize();
        auto& it = tools[idx];
        auto t0 = std::chrono::high_resolution_clock::now();
        bool dirty = (it.type == 12) ? RestoreBatchOriginal() : ToolExecutor::Execute(it.type, it);
        auto t1 = std::chrono::high_resolution_clock::now();
        s_stepTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        s_toolTimesMs[idx] = s_stepTimeMs;
        return dirty;
    }

    void RequestRun(int toolIndex) { s_queue.push(toolIndex); }

    void RequestRunAll(bool loop) {
        s_mode = Mode::Running; s_isStep = false;
        s_currentIndex = 0; s_stepCursor = 0; s_loop = loop;
        s_stepTimeMs = s_batchTotalMs = 0;
        s_toolTimesMs.assign(ToolChainState::ReadOnlyTools().size(), 0.0f);
        s_imageDirty = false;
        s_batchTimerStarted = false;
        s_originalImage = !gOriginalImage.empty() ? gOriginalImage.clone() : gImage.clone();
        s_originalVersion = g_ImageVersion;
        s_lastInputImage = s_originalImage.clone();
        s_lastOutputImage = s_originalImage.clone();
        s_originalToolOutputImage = s_originalImage.clone();
        LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[全部执行%s] %zu 个工具",
            s_runtimeMode ? "/运行模式" : "", ToolChainState::ReadOnlyTools().size());
    }

    void RequestStepNext() {
        if (s_stepCursor >= (int)ToolChainState::ReadOnlyTools().size()) { s_stepCursor = 0; s_stepTimeMs = 0; }
        if (s_stepCursor == 0) { s_originalImage = !gOriginalImage.empty() ? gOriginalImage.clone() : gImage.clone(); s_originalVersion = g_ImageVersion; s_lastInputImage = s_originalImage.clone(); s_lastOutputImage = s_originalImage.clone(); s_originalToolOutputImage = s_originalImage.clone(); s_imageDirty = false; }
        s_isStep = true;
        s_currentIndex = s_stepCursor;
        s_mode = Mode::Running;
    }

    void RequestStepReset() { s_stepCursor = 0; s_stepTimeMs = 0; s_mode = Mode::Idle; }

    void Tick() {
        // 处理独立按钮请求队列
        while (!s_queue.empty()) {
            int idx = s_queue.front(); s_queue.pop();
            if (idx >= 0 && idx < (int)ToolChainState::ReadOnlyTools().size())
            {
                ExecuteToolAt(idx);
            }
        }

        if (s_mode != Mode::Running) return;
        auto& tools = ToolChainState::Tools();
        if (s_currentIndex < 0 || s_currentIndex >= (int)tools.size()) { s_mode = Mode::Idle; return; }
        if (gImage.empty()) { LogSystem::Add(LOG_WARN, "执行中止：未加载图片"); s_mode = Mode::Idle; return; }

        auto& it = tools[s_currentIndex];
        const bool usePreviousOutput = (it.inputSourceMode == 1 && !s_lastOutputImage.empty());
        const bool useOriginalToolOutput = (it.inputSourceMode == 2 && !s_originalToolOutputImage.empty());
        const cv::Mat& selectedInput = usePreviousOutput ? s_lastOutputImage
                                     : useOriginalToolOutput ? s_originalToolOutputImage
                                     : s_lastInputImage;
        if (!selectedInput.empty())
        {
            selectedInput.copyTo(gImage);
            s_lastInputImage = selectedInput.clone();
            if (s_isStep)
            {
                cv::Mat rgba;
                SafeConvertToRGBA(gImage, rgba);
                if (!rgba.empty())
                {
                    gPendingUpload = rgba;
                    gNeedUpload = true;
                }
            }
        }

        if (!s_isStep && !s_batchTimerStarted) {
            s_batchStart = std::chrono::high_resolution_clock::now();
            s_batchTimerStarted = true;
        }

        if (s_isStep || !s_runtimeMode)
        {
            const char* baseName = (it.type == 12) ? "原图" : ToolRegistry::GetName(it.type);
            const std::string displayName = ToolInstanceLogName(baseName, it.label);
            LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[%s] %d/%zu: %s",
                           s_isStep ? "单步" : "执行", s_currentIndex + 1, tools.size(), displayName.c_str());
        }

        s_imageDirty = ExecuteToolAt(s_currentIndex);
        if (!s_isStep)
            s_batchTotalMs += s_stepTimeMs;
        if (!gImage.empty())
            s_lastOutputImage = gImage.clone();

        if (s_isStep) {
            s_stepCursor++;
            s_mode = Mode::Idle;  // 单步：执行一个就停
        } else {
            s_currentIndex++;
            if (s_currentIndex >= (int)tools.size()) {
                LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[全部执行%s] 完成 %.1fms",
                    s_runtimeMode ? "/运行模式" : "", s_batchTotalMs);
                if (s_loop && FrameNavigation::HasNextImage()) {
                    FrameNavigation::NavigateNextImage();
                    FrameNavigation::FitImageToWindow();
                    s_mode = Mode::Idle;  // 简化：循环等下一帧手动触发
                } else if (s_loop) {
                    s_currentIndex = 0; s_imageDirty = false;
                    s_originalImage = !gOriginalImage.empty() ? gOriginalImage.clone() : gImage.clone();
                    s_originalVersion = g_ImageVersion;
                    s_lastInputImage = s_originalImage.clone();
                    s_lastOutputImage = s_originalImage.clone();
                    s_originalToolOutputImage = s_originalImage.clone();
                    s_batchTimerStarted = false;
                } else {
                    s_mode = Mode::Idle;
                    s_batchTimerStarted = false;
                }
            }
        }
    }

    Mode GetMode() { return s_mode; }
    int  GetCurrentIndex() { return s_currentIndex; }
    int  GetStepCursor() { return s_stepCursor; }
    float GetTotalTimeMs() { return s_batchTotalMs; }
    float GetElapsedTimeMs()
    {
        if (s_mode == Mode::Running && s_batchTimerStarted)
        {
            return s_batchTotalMs;
        }
        return s_batchTotalMs;
    }
    float GetLastStepTimeMs() { return s_stepTimeMs; }
    float GetToolTimeMs(int toolIndex)
    {
        if (toolIndex < 0 || toolIndex >= (int)s_toolTimesMs.size())
            return 0.0f;
        return s_toolTimesMs[toolIndex];
    }
    void SetRuntimeMode(bool enabled) { s_runtimeMode = enabled; }
    bool IsRuntimeMode() { return s_runtimeMode; }

    void Reset() { s_mode = Mode::Idle; s_currentIndex = -1; s_stepCursor = 0; s_isStep = false; s_queue = {}; s_loop = false; s_batchTimerStarted = false; s_toolTimesMs.clear(); s_stepTimeMs = 0; s_batchTotalMs = 0; }
}
