#include "ToolController.h"
#include "ToolExecutor.h"
#include "FrameNavigation.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "ROIState.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "RealtimeDetectionState.h"
#include "ToolChainPreflight.h"
#include "ToolJudgement.h"
#include "VisionContext.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/YOLODetector.h"
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
    static std::chrono::high_resolution_clock::time_point s_nextLoopRunAt;
    static constexpr float kLoopMinIntervalMs = 150.0f;

    static bool ValidateToolChainForRun()
    {
        const ToolChainPreflightResult validation = ToolChainPreflight::Check(
            ToolChainState::ReadOnlyTools(), ImageState::HasImage(),
            ROIState::ReadOnlyItems().size());
        if (validation.valid())
            return true;

        for (const ToolChainPreflightIssue& issue : validation.issues)
        {
            if (issue.toolIndex >= 0)
                LogSystem::Add(LOG_ERROR, "运行前检查失败 [%d]: %s",
                    issue.toolIndex + 1, issue.message.c_str());
            else
                LogSystem::Add(LOG_ERROR, "运行前检查失败: %s", issue.message.c_str());
        }
        return false;
    }

    static const cv::Mat& OriginalOrCurrent()
    {
        return !ImageState::Original().empty() ? ImageState::Original() : ImageState::Current();
    }

    static const cv::Mat& SelectBatchInput(const ToolInstance& tool)
    {
        if (tool.inputSourceMode == 1 && !s_lastOutputImage.empty())
            return s_lastOutputImage;
        if (tool.inputSourceMode == 2)
        {
            if (!s_originalToolOutputImage.empty())
                return s_originalToolOutputImage;
            return OriginalOrCurrent();
        }
        if (!s_lastInputImage.empty())
            return s_lastInputImage;
        return OriginalOrCurrent();
    }

    static const cv::Mat& SelectStandaloneInput(const ToolInstance& tool)
    {
        // 单工具执行没有可重放的“上一步”。处理图模式使用当前显示图，
        // 其余两种模式统一回退到 ImageState 保存的原图。
        if (tool.inputSourceMode == 1 && !ImageState::Current().empty())
            return ImageState::Current();
        return OriginalOrCurrent();
    }

    static void ApplyInputImage(const cv::Mat& selectedInput, bool requestUpload)
    {
        if (selectedInput.empty())
            return;

        auto& currentImage = ImageState::CurrentRef();
        if (selectedInput.data != currentImage.data)
            selectedInput.copyTo(currentImage);
        if (!requestUpload)
            return;

        cv::Mat rgba;
        SafeConvertToRGBA(currentImage, rgba);
        if (!rgba.empty())
        {
            ImageState::PendingUploadRef() = rgba;
            ImageState::NeedUploadRef() = true;
        }
    }

    static bool RestoreBatchOriginal()
    {
        if ((s_originalImage.empty() || s_originalVersion != ImageState::Version()) && !ImageState::Original().empty())
        {
            s_originalImage = ImageState::Original().clone();
            s_originalVersion = ImageState::Version();
        }

        if (s_originalImage.empty())
        {
            LogSystem::Add(LOG_WARN, "原图: 本轮原图为空");
            return false;
        }

        auto& currentImage = ImageState::CurrentRef();
        s_originalImage.copyTo(currentImage);
        cv::Mat rgba;
        SafeConvertToRGBA(currentImage, rgba);
        if (!rgba.empty())
        {
            ImageState::PendingUploadRef() = rgba;
            ImageState::NeedUploadRef() = true;
        }
        s_lastInputImage = s_originalImage.clone();
        s_lastOutputImage = s_originalImage.clone();
         s_originalToolOutputImage = s_originalImage.clone();
         TemplateState::ClearResults();
         gContext.ClearUnifiedResults();
         RealtimeDetectionState::Clear();
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
        if (!it.enabled)
        {
            ToolResult skipped;
            const char* baseName = it.type == 12 ? "原图" : ToolRegistry::GetName(it.type);
            skipped.toolName = ToolInstanceLogName(baseName, it.label);
            skipped.sourceToolIndex = idx;
            skipped.sourceToolId = it.toolId;
            skipped.success = true;
            skipped.skipped = true;
            skipped.status = ToolResultStatus::Pass;
            skipped.message = "工具已禁用";
            it.lastResult = skipped;
            it.hasLastResult = true;
            gContext.unifiedResults.push_back(std::move(skipped));
            s_stepTimeMs = 0.0f;
            s_toolTimesMs[idx] = 0.0f;
            return false;
        }
        auto t0 = std::chrono::high_resolution_clock::now();
        bool dirty = (it.type == 12) ? RestoreBatchOriginal() : ToolExecutor::Execute(it.type, it);
        auto t1 = std::chrono::high_resolution_clock::now();
        s_stepTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        s_toolTimesMs[idx] = s_stepTimeMs;
        return dirty;
    }

    static void ResetBatchImagesFromSource(const cv::Mat& source)
    {
        s_originalImage = source;
        s_originalVersion = ImageState::Version();
        s_lastInputImage = s_originalImage;
        s_lastOutputImage = s_originalImage;
        s_originalToolOutputImage = s_originalImage;
    }

    void RequestRun(int toolIndex)
    {
        if (toolIndex < 0 || toolIndex >= static_cast<int>(ToolChainState::ReadOnlyTools().size()))
            return;
        if (!ValidateToolChainForRun())
            return;
        s_queue.push(toolIndex);
    }

    void RequestRunAll(bool loop) {
        if (!ValidateToolChainForRun())
        {
            s_mode = Mode::Idle;
            return;
        }
        s_mode = Mode::Running; s_isStep = false;
        s_currentIndex = 0; s_stepCursor = 0; s_loop = loop;
        s_stepTimeMs = s_batchTotalMs = 0;
        s_toolTimesMs.assign(ToolChainState::ReadOnlyTools().size(), 0.0f);
        s_imageDirty = false;
        s_batchTimerStarted = false;
        s_nextLoopRunAt = std::chrono::high_resolution_clock::now();
        ResetBatchImagesFromSource(!ImageState::Original().empty() ? ImageState::Original() : ImageState::Current());
        for (auto& tool : ToolChainState::Tools())
            tool.hasLastResult = false;
        LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[全部执行%s] %zu 个工具",
            s_runtimeMode ? "/运行模式" : "", ToolChainState::ReadOnlyTools().size());
    }

    void RequestStepNext() {
        if (!ValidateToolChainForRun())
            return;
        if (s_stepCursor >= (int)ToolChainState::ReadOnlyTools().size()) { s_stepCursor = 0; s_stepTimeMs = 0; }
        if (s_stepCursor == 0) { ResetBatchImagesFromSource(!ImageState::Original().empty() ? ImageState::Original() : ImageState::Current()); s_imageDirty = false; }
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
                auto& tool = ToolChainState::Tools()[idx];
                const cv::Mat& selectedInput = SelectStandaloneInput(tool);
                ApplyInputImage(selectedInput, false);
                s_lastInputImage = selectedInput;
                ExecuteToolAt(idx);
                if (!ImageState::Current().empty())
                    s_lastOutputImage = ImageState::Current();
            }
        }

        if (s_mode != Mode::Running) return;
        auto& tools = ToolChainState::Tools();
        if (s_currentIndex < 0 || s_currentIndex >= (int)tools.size()) { s_mode = Mode::Idle; return; }
        if (ImageState::Current().empty()) { LogSystem::Add(LOG_WARN, "执行中止：未加载图片"); s_mode = Mode::Idle; return; }

        const auto now = std::chrono::high_resolution_clock::now();
        if (s_loop && s_currentIndex == 0 && now < s_nextLoopRunAt)
            return;

        auto& it = tools[s_currentIndex];
        const cv::Mat& selectedInput = SelectBatchInput(it);
        if (!selectedInput.empty())
        {
            ApplyInputImage(selectedInput, s_isStep);
            s_lastInputImage = selectedInput;
        }

        if (!s_isStep && !s_batchTimerStarted) {
            s_batchStart = std::chrono::high_resolution_clock::now();
            s_batchTimerStarted = true;
        }

        const bool quietLoop = s_loop && !s_isStep;
        if (!quietLoop && (s_isStep || !s_runtimeMode))
        {
            const char* baseName = (it.type == 12) ? "原图" : ToolRegistry::GetName(it.type);
            const std::string displayName = ToolInstanceLogName(baseName, it.label);
            LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[%s] %d/%zu: %s",
                           s_isStep ? "单步" : "执行", s_currentIndex + 1, tools.size(), displayName.c_str());
        }

        s_imageDirty = ExecuteToolAt(s_currentIndex);
        if (!s_isStep)
            s_batchTotalMs += s_stepTimeMs;
        if (!ImageState::Current().empty())
            s_lastOutputImage = ImageState::Current();

        if (!s_isStep && it.hasLastResult && ToolJudgement::ShouldStop(it.lastResult, it.judgement))
        {
            const char* baseName = (it.type == 12) ? "原图" : ToolRegistry::GetName(it.type);
            const std::string displayName = ToolInstanceLogName(baseName, it.label);
            LogSystem::Add(
                it.lastResult.status == ToolResultStatus::Error ? LOG_ERROR : LOG_WARN,
                ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                "[全部执行] 停止于 %d/%zu: %s | %s%s%s",
                s_currentIndex + 1,
                tools.size(),
                displayName.c_str(),
                ToolResultStatusName(it.lastResult.status),
                it.lastResult.statusReason.empty() ? "" : " | ",
                it.lastResult.statusReason.c_str());
            s_mode = Mode::Idle;
            s_loop = false;
            s_batchTimerStarted = false;
            return;
        }

        if (s_isStep) {
            s_stepCursor++;
            s_mode = Mode::Idle;  // 单步：执行一个就停
        } else {
            s_currentIndex++;
            if (s_currentIndex >= (int)tools.size()) {
                if (!s_loop)
                {
                    LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[全部执行%s] 完成 %.1fms",
                        s_runtimeMode ? "/运行模式" : "", s_batchTotalMs);
                }
                if (s_loop && FrameNavigation::HasNextImage()) {
                    FrameNavigation::NavigateNextImage();
                    FrameNavigation::FitImageToWindow();
                    s_mode = Mode::Idle;  // 简化：循环等下一帧手动触发
                } else if (s_loop) {
                    s_currentIndex = 0; s_imageDirty = false;
                    ResetBatchImagesFromSource(!ImageState::Original().empty() ? ImageState::Original() : ImageState::Current());
                    s_batchTimerStarted = false;
                    s_nextLoopRunAt = std::chrono::high_resolution_clock::now()
                        + std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
                            std::chrono::duration<float, std::milli>(kLoopMinIntervalMs));
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

    static void ClearRuntimeCaches()
    {
        Reset();
        s_originalImage.release();
        s_originalVersion = -1;
        s_lastInputImage.release();
        s_lastOutputImage.release();
        s_originalToolOutputImage.release();
        s_imageDirty = false;

        for (ToolInstance& tool : ToolChainState::Tools())
        {
            tool.lastResult = ToolResult{};
            tool.hasLastResult = false;
        }
        gContext.ClearUnifiedResults();
        TemplateState::ClearResults();
        RealtimeDetectionState::Clear();
        ToolChainState::SetYoloLiveDetect(false);
        ToolChainState::SetYoloLiveInstanceIndex(-1);
        ToolChainState::SetYoloLastTimeMs(0.0f);
        ToolChainState::SetYoloLiveFrameMs(0.0f);
    }

    void OnToolChainChanged()
    {
        ClearRuntimeCaches();
    }

    void OnInputImageChanged()
    {
        ClearRuntimeCaches();
    }

    void Reset() { s_mode = Mode::Idle; s_currentIndex = -1; s_stepCursor = 0; s_isStep = false; s_queue = {}; s_loop = false; s_batchTimerStarted = false; s_nextLoopRunAt = std::chrono::high_resolution_clock::now(); s_toolTimesMs.clear(); s_stepTimeMs = 0; s_batchTotalMs = 0; }
}
