#include "ToolController.h"
#include "ToolExecutor.h"
#include "FrameNavigation.h"
#include "HardwareRuntimeService.h"
#include "ImageState.h"
#include "ImageUtils.h"
#include "OpenFileDialog.h"
#include "ROIState.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "RealtimeDetectionState.h"
#include "ResultROIResolver.h"
#include "ToolChainPreflight.h"
#include "ToolExecutionGraph.h"
#include "ToolJudgement.h"
#include "VisionContext.h"
#include "VideoCapture.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/YOLODetector.h"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace ToolController
{
    static Mode s_mode = Mode::Idle;
    struct QueuedToolRequest
    {
        std::uint64_t toolId = 0;
        std::uint64_t runRevision = 0;
        bool force = false;
    };

    static std::deque<QueuedToolRequest> s_queue;
    static int s_currentIndex = -1;
    static std::vector<int> s_batchExecutionOrder;
    static std::size_t s_batchExecutionCursor = 0;
    static bool s_batchRunActive = false;
    static bool s_runTaskGroup = false;
    static std::string s_runTaskGroupName;
    static bool s_lastRunTaskGroup = false;
    static std::string s_lastRunTaskGroupName;
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
    static cv::Mat s_runFallbackImage;
    static std::unordered_map<std::string, cv::Mat> s_taskRunImages;
    static std::unordered_map<std::string, cv::Mat> s_taskResultImages;
    static std::string s_activeInputTaskGroup;
    static bool s_activeInputTaskGroupValid = false;
    static bool s_cameraFrameAvailableForRun = false;
    static bool s_forceCameraFrameForRun = false;
    // Preserve the caller's trigger choice across loop rounds.
    static bool s_runTriggerCamera = true;
    static bool s_imageDirty = false;
    static bool s_batchTimerStarted = false;
    static bool s_runtimeMode = false;
    static std::chrono::high_resolution_clock::time_point s_batchStart;
    static std::chrono::high_resolution_clock::time_point s_nextLoopRunAt;
    static int s_loopIntervalMs = 150;
    static std::uint64_t s_loopCompletedRounds = 0;
    static std::uint64_t s_executionGeneration = 1;
    static std::uint64_t s_nextRunRevision = 1;
    static std::uint64_t s_activeRunRevision = 0;
    static std::uint64_t s_completedBatchSerial = 0;
    static std::uint64_t s_lastCompletedLoopRound = 0;
    static ToolExecutionGraphPlan s_executionPlan;

    struct AsyncCompletion
    {
        std::uint64_t generation = 0;
        std::uint64_t toolId = 0;
        int requestedIndex = -1;
        int imageVersion = -1;
        int imageWidth = 0;
        int imageHeight = 0;
        std::uint64_t streamSourceGeneration = 0;
        std::uint64_t parameterRevision = 0;
        ToolExecutionCacheKey cacheKey;
        bool batch = false;
        bool allowStreamingFrameAdvance = false;
        ToolExecutor::ToolExecutionOutput output;
        std::string error;
    };

    static std::future<AsyncCompletion> s_asyncFuture;
    static std::jthread s_asyncWorker;
    static bool s_asyncRunning = false;

    struct ParallelTask
    {
        std::future<AsyncCompletion> future;
        std::jthread worker;
    };

    static std::vector<ParallelTask> s_parallelTasks;
    static bool s_parallelRunning = false;
    static bool s_activeRunForce = false;
    static std::uint64_t s_forceNextToolId = 0;
    static bool s_forceNextRunAll = false;
    static float s_lastParallelWallMs = 0.0f;
    static std::chrono::high_resolution_clock::time_point s_parallelStart;

    struct TaskPipelineCompletion
    {
        std::uint64_t generation = 0;
        std::string groupName;
        std::vector<int> toolIndices;
        std::vector<ToolExecutor::ToolExecutionOutput> outputs;
        cv::Mat resultImage;
        std::string error;
    };

    struct TaskPipelineJob
    {
        std::future<TaskPipelineCompletion> future;
        std::jthread worker;
    };

    struct TaskPipelineInput
    {
        std::uint64_t generation = 0;
        std::string groupName;
        std::vector<int> toolIndices;
        std::vector<ToolInstance> tools;
        cv::Mat inputImage;
        std::vector<ROI> visibleROIs;
        int selectedROI = -1;
        cv::Mat frozenTemplate;
    };

    static bool s_taskParallelEnabled = false;
    static bool s_taskParallelRunning = false;
    static constexpr int kTaskParallelLimit = 4;
    static std::deque<TaskPipelineInput> s_taskParallelPending;
    static std::vector<TaskPipelineJob> s_taskParallelJobs;
    static std::chrono::high_resolution_clock::time_point s_taskParallelStart;
    static int s_taskParallelPublishedTools = 0;

    static bool MatchesRunScope(const ToolInstance& tool)
    {
        return !s_runTaskGroup || tool.groupName == s_runTaskGroupName;
    }

    static int TaskGroupCameraIndex(const std::string& groupName)
    {
        if (groupName.empty())
            return -1;
        const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
        if (groupIndex < 0 || !ToolChainState::ReadOnlyTaskGroups()[groupIndex].enabled)
            return -1;
        const TaskGroupDefinition& group =
            ToolChainState::ReadOnlyTaskGroups()[groupIndex];
        return group.cameraIndex >= 0
            ? group.cameraIndex : (group.cameraPreferred ? 0 : -1);
    }

    static bool TaskGroupPrefersCamera(const std::string& groupName)
    {
        return TaskGroupCameraIndex(groupName) >= 0;
    }

    static int EnabledTaskGroupCount()
    {
        return static_cast<int>(std::count_if(
            ToolChainState::ReadOnlyTaskGroups().begin(),
            ToolChainState::ReadOnlyTaskGroups().end(),
            [](const TaskGroupDefinition& group)
            {
                return group.enabled;
            }));
    }

    static int RunScopeCameraIndex()
    {
        int cameraIndex = -1;
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
        {
            if (!tool.enabled || !MatchesRunScope(tool))
                continue;
            const int toolCameraIndex = TaskGroupCameraIndex(tool.groupName);
            if (toolCameraIndex < 0)
                continue;
            if (cameraIndex < 0)
                cameraIndex = toolCameraIndex;
            else if (cameraIndex != toolCameraIndex)
            {
                return -2;
            }
        }
        return cameraIndex;
    }

    static bool RunScopePrefersCamera()
    {
        const int cameraIndex = RunScopeCameraIndex();
        const HardwareRuntimeSnapshot hardware = HardwareRuntimeService::Snapshot();
        return cameraIndex >= 0 &&
            hardware.cameraState == DeviceConnectionState::Connected &&
            hardware.cameraSlotIndex == cameraIndex;
    }

    static void BuildBatchExecutionOrder()
    {
        s_batchExecutionOrder.clear();
        const auto& tools = ToolChainState::ReadOnlyTools();
        if (s_runTaskGroup)
        {
            for (int index = 0; index < static_cast<int>(tools.size()); ++index)
            {
                if (MatchesRunScope(tools[index]))
                    s_batchExecutionOrder.push_back(index);
            }
        }
        else
        {
            // 全部执行按任务列表顺序运行，并保持每个任务内部的工具顺序。
            for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            {
                if (!group.enabled)
                    continue;
                for (int index = 0; index < static_cast<int>(tools.size()); ++index)
                {
                    if (tools[index].groupName == group.name)
                        s_batchExecutionOrder.push_back(index);
                }
            }
            // 未分组工具放在正式任务之后，保持原工具链顺序。
            for (int index = 0; index < static_cast<int>(tools.size()); ++index)
            {
                if (tools[index].groupName.empty())
                    s_batchExecutionOrder.push_back(index);
            }
        }
        s_batchExecutionCursor = 0;
    }

    static int BatchExecutionOrdinal(int toolIndex)
    {
        const auto found = std::find(
            s_batchExecutionOrder.begin(), s_batchExecutionOrder.end(), toolIndex);
        return found == s_batchExecutionOrder.end()
            ? -1 : static_cast<int>(std::distance(s_batchExecutionOrder.begin(), found));
    }

    static void LogHardwarePublish(const DeviceOperationResult& published, bool quiet)
    {
        if (published.success && quiet)
            return;
        LogSystem::Add(published.success ? LOG_INFO : LOG_ERROR,
            "硬件结果发布%s: %s",
            published.success ? "成功" : "失败",
            published.message.c_str());
    }

    static void PublishConfiguredHardwareStatus(ToolResultStatus status, bool quiet = false)
    {
        if (!HardwareRuntimeService::OutputAutoPublishEnabled())
            return;
        LogHardwarePublish(HardwareRuntimeService::EnqueueConfiguredStatus(status), quiet);
    }

    static void PublishConfiguredHardwareResult(bool quiet = false)
    {
        if (!HardwareRuntimeService::OutputAutoPublishEnabled())
            return;

        std::vector<ToolResult> results;
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
        {
            if (tool.hasLastResult)
                results.push_back(tool.lastResult);
        }
        const DeviceOperationResult published = HardwareRuntimeService::EnqueueConfiguredResults(results);
        LogHardwarePublish(published, quiet);
    }

    static bool ValidateToolChainForRun()
    {
        const ToolChainPreflightResult validation = ToolChainPreflight::Check(
            ToolChainState::ReadOnlyTools(), ImageState::HasImage(),
            ROIState::ReadOnlyItems().size());
        bool hasRelevantIssue = false;
        for (const ToolChainPreflightIssue& issue : validation.issues)
        {
            if (issue.toolIndex < 0 || !s_runTaskGroup ||
                BatchExecutionOrdinal(issue.toolIndex) >= 0)
            {
                hasRelevantIssue = true;
                break;
            }
        }
        if (!hasRelevantIssue)
            return true;

        for (const ToolChainPreflightIssue& issue : validation.issues)
        {
            if (s_runTaskGroup && issue.toolIndex >= 0 &&
                BatchExecutionOrdinal(issue.toolIndex) < 0)
            {
                continue;
            }
            if (issue.toolIndex >= 0)
                LogSystem::Add(LOG_ERROR, "运行前检查失败 [%d]: %s",
                    issue.toolIndex + 1, issue.message.c_str());
            else
                LogSystem::Add(LOG_ERROR, "运行前检查失败: %s", issue.message.c_str());
        }
        PublishConfiguredHardwareStatus(ToolResultStatus::Error);
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
        if (it.type == 12)
        {
            ToolResult originalResult;
            originalResult.toolName = ToolInstanceLogName("原图", it.label);
            originalResult.sourceToolIndex = idx;
            originalResult.sourceToolId = it.toolId;
            originalResult.success = dirty;
            originalResult.status = dirty ? ToolResultStatus::Pass : ToolResultStatus::Error;
            originalResult.message = dirty ? "原图已恢复" : "原图恢复失败";
            originalResult.statusReason = dirty ? std::string{} : "本轮原图为空";
            originalResult.timing.wallMs = s_stepTimeMs;
            it.lastResult = originalResult;
            it.hasLastResult = true;
            gContext.unifiedResults.push_back(std::move(originalResult));
        }
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

    static cv::Mat ReadTaskImageFile(const std::string& imagePath)
    {
        if (imagePath.empty())
            return {};
        const auto* begin = reinterpret_cast<const char8_t*>(imagePath.data());
        const std::filesystem::path path(
            std::u8string(begin, begin + imagePath.size()));
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return {};
        std::vector<uchar> bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        return bytes.empty() ? cv::Mat() : cv::imdecode(bytes, cv::IMREAD_COLOR);
    }

    static bool ResolveTaskImagePathForRun(const std::string& groupName,
        std::string& imagePath)
    {
        const int index = ToolChainState::TaskGroupIndexByName(groupName);
        if (index < 0)
            return false;

        const TaskGroupDefinition group =
            ToolChainState::ReadOnlyTaskGroups()[index];
        if (group.imageFolderPath.empty())
        {
            imagePath = group.imagePath;
            return !imagePath.empty();
        }

        const std::vector<std::string> images =
            ScanImageFiles(group.imageFolderPath, true);
        if (images.empty())
        {
            LogSystem::Add(LOG_ERROR,
                "任务图片文件夹中没有可用图片 [%s]: %s",
                groupName.c_str(), group.imageFolderPath.c_str());
            return false;
        }

        const int imageCount = static_cast<int>(images.size());
        const int nextIndex = group.imageFolderIndex < 0 ||
            group.imageFolderIndex >= imageCount
            ? 0 : (group.imageFolderIndex + 1) % imageCount;
        imagePath = images[static_cast<std::size_t>(nextIndex)];
        return ToolChainState::SetTaskGroupFolderImagePosition(
            index, imagePath, nextIndex, imageCount);
    }

    static bool PrepareTaskImagesForRun(bool preserveFallback = false)
    {
        s_taskRunImages.clear();
        s_activeInputTaskGroup.clear();
        s_activeInputTaskGroupValid = false;
        if (!preserveFallback)
        {
            s_runFallbackImage = !ImageState::Original().empty()
                ? ImageState::Original().clone() : ImageState::Current().clone();
        }

        const auto& tools = ToolChainState::ReadOnlyTools();
        for (int toolIndex : s_batchExecutionOrder)
        {
            if (toolIndex < 0 || toolIndex >= static_cast<int>(tools.size()))
                continue;
            const std::string& groupName = tools[toolIndex].groupName;
            if (groupName.empty() || s_taskRunImages.find(groupName) != s_taskRunImages.end())
                continue;
            const int cameraIndex = TaskGroupCameraIndex(groupName);
            if (cameraIndex >= 0 && TaskGroupPrefersCamera(groupName))
            {
                if (s_cameraFrameAvailableForRun &&
                    !s_runFallbackImage.empty())
                {
                    s_taskRunImages.emplace(groupName, s_runFallbackImage.clone());
                    continue;
                }
                cv::Mat cameraFrame;
                if (!s_runTaskGroup &&
                    HardwareRuntimeService::AcquireLatestCameraFrame(
                        cameraIndex, cameraFrame) && !cameraFrame.empty())
                {
                    s_taskRunImages.emplace(groupName, std::move(cameraFrame));
                    continue;
                }
            }
            std::string imagePath;
            if (!ResolveTaskImagePathForRun(groupName, imagePath))
            {
                const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
                if (groupIndex >= 0 &&
                    ToolChainState::ReadOnlyTaskGroups()[groupIndex].imageFolderPath.empty() &&
                    ToolChainState::ReadOnlyTaskGroups()[groupIndex].imagePath.empty())
                {
                    continue;
                }
                return false;
            }
            cv::Mat taskImage = ReadTaskImageFile(imagePath);
            if (taskImage.empty())
            {
                LogSystem::Add(LOG_ERROR, "任务图片无法读取 [%s]: %s",
                    groupName.c_str(), imagePath.c_str());
                return false;
            }
            s_taskRunImages.emplace(groupName, std::move(taskImage));
        }

        for (int toolIndex : s_batchExecutionOrder)
        {
            if (toolIndex < 0 || toolIndex >= static_cast<int>(tools.size()))
                continue;
            const std::string& groupName = tools[toolIndex].groupName;
            if (s_taskRunImages.find(groupName) == s_taskRunImages.end() &&
                s_runFallbackImage.empty())
            {
                LogSystem::Add(LOG_ERROR, groupName.empty()
                    ? "未分组工具没有可用输入图片"
                    : "任务没有绑定图片 [%s]",
                    groupName.c_str());
                return false;
            }
        }
        return true;
    }

    static void CaptureActiveTaskResultImage();

    static bool ActivateTaskImage(const ToolInstance& tool)
    {
        if (s_activeInputTaskGroupValid &&
            s_activeInputTaskGroup == tool.groupName)
        {
            return true;
        }

        if (s_activeInputTaskGroupValid)
            CaptureActiveTaskResultImage();

        const auto found = s_taskRunImages.find(tool.groupName);
        const cv::Mat& source = found != s_taskRunImages.end()
            ? found->second : s_runFallbackImage;
        if (source.empty())
            return false;

        ImageState::SetImage(source);
        ResetBatchImagesFromSource(ImageState::Original());
        ApplyInputImage(ImageState::Original(), true);
        s_imageDirty = false;
        s_activeInputTaskGroup = tool.groupName;
        s_activeInputTaskGroupValid = true;
        return true;
    }

    static void CaptureActiveTaskResultImage()
    {
        if (!s_activeInputTaskGroupValid || s_lastOutputImage.empty())
            return;
        s_taskResultImages[s_activeInputTaskGroup] = s_lastOutputImage.clone();
    }

    static bool RunScopeHasTaskImages()
    {
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
        {
            if (group.enabled &&
                (!group.imagePath.empty() || !group.imageFolderPath.empty()) &&
                (!s_runTaskGroup || group.name == s_runTaskGroupName))
                return true;
        }
        return false;
    }

    static bool IsAsyncTool(const ToolInstance& tool)
    {
        switch (tool.type)
        {
        case 1:  // Template match
        case 4:  // YOLO
        case 6:  // Shape match
        case 11: // OpenCV YOLO
        case 13: // OCR
        case 15: // Industrial measurement
            return true;
        default:
            return false;
        }
    }

    static bool StartAsyncExecution(int index, const cv::Mat& input, bool batch, bool force)
    {
        ToolInstance* target = ToolChainState::At(index);
        if (!target || !IsAsyncTool(*target) || ToolChainState::YoloLiveDetect())
            return false;

        ToolInstance snapshot;
        VisionContext context;
        const std::uint64_t generation = s_executionGeneration;
        const std::uint64_t toolId = target->toolId;
        const std::uint64_t parameterRevision = target->parameterRevision;
        const int imageVersion = ImageState::Version();
        const int imageWidth = input.cols;
        const int imageHeight = input.rows;
        const std::uint64_t streamSourceGeneration = VideoCapture::SourceGeneration();
        // A single YOLO tool can safely publish geometry from the most recently
        // completed video frame. Multi-tool pipelines must stay pinned to the
        // exact input version so downstream tools never mix different frames.
        const bool allowStreamingFrameAdvance = VideoCapture::IsOpen() &&
            (target->type == 4 || target->type == 11) &&
            (!batch || s_batchExecutionOrder.size() == 1);
        if (s_executionPlan.nodes.size() != ToolChainState::ReadOnlyTools().size())
            s_executionPlan = ToolExecutionGraph::Build(ToolChainState::ReadOnlyTools());
        ToolExecutionCacheKey cacheKey;
        cacheKey.toolId = toolId;
        cacheKey.parameterRevision = parameterRevision;
        cacheKey.runRevision = s_activeRunRevision;
        cacheKey.imageVersion = imageVersion;
        cacheKey.upstreamRevision = ToolExecutionGraph::ComputeUpstreamRevision(
            s_executionPlan, ToolChainState::ReadOnlyTools(), index);

        ToolResult cachedResult;
        if (!force && ToolExecutionGraph::TryGetCachedResult(cacheKey, cachedResult))
        {
            AsyncCompletion completion;
            completion.generation = generation;
            completion.toolId = toolId;
            completion.requestedIndex = index;
            completion.imageVersion = imageVersion;
            completion.imageWidth = imageWidth;
            completion.imageHeight = imageHeight;
            completion.streamSourceGeneration = streamSourceGeneration;
            completion.parameterRevision = parameterRevision;
            completion.cacheKey = cacheKey;
            completion.batch = batch;
            completion.allowStreamingFrameAdvance = allowStreamingFrameAdvance;
            completion.output.completed = true;
            completion.output.cacheHit = true;
            completion.output.result = std::move(cachedResult);
            std::promise<AsyncCompletion> cachedPromise;
            s_asyncFuture = cachedPromise.get_future();
            cachedPromise.set_value(std::move(completion));
            s_asyncRunning = true;
            return true;
        }

        if (!ToolExecutor::PrepareDetached(*target, input, index, snapshot, context))
            return false;

        std::promise<AsyncCompletion> completionPromise;
        s_asyncFuture = completionPromise.get_future();
        s_asyncRunning = true;
        s_asyncWorker = std::jthread(
            [snapshot = std::move(snapshot), context = std::move(context), generation,
             toolId, index, imageVersion, imageWidth, imageHeight,
             streamSourceGeneration, allowStreamingFrameAdvance,
             parameterRevision, cacheKey, batch,
             completionPromise = std::move(completionPromise)](std::stop_token stopToken) mutable
            {
                LogContext logContext;
                logContext.taskId = std::to_string(toolId);
                logContext.batchNumber = std::to_string(generation);
                if (!context.frame.sourcePath.empty())
                {
                    const std::size_t separator = context.frame.sourcePath.find_last_of("/\\");
                    logContext.imageName = separator == std::string::npos
                        ? context.frame.sourcePath
                        : context.frame.sourcePath.substr(separator + 1);
                }
                ScopedLogContext scopedLogContext(std::move(logContext));
                AsyncCompletion completion;
                completion.generation = generation;
                completion.toolId = toolId;
                completion.requestedIndex = index;
                completion.imageVersion = imageVersion;
                completion.imageWidth = imageWidth;
                completion.imageHeight = imageHeight;
                completion.streamSourceGeneration = streamSourceGeneration;
                completion.parameterRevision = parameterRevision;
                completion.cacheKey = cacheKey;
                completion.batch = batch;
                completion.allowStreamingFrameAdvance = allowStreamingFrameAdvance;
                context.stopToken = stopToken;
                try
                {
                    ToolExecutor::ExecuteDetached(snapshot, context, index, completion.output);
                    if (!completion.output.completed && !stopToken.stop_requested())
                        completion.error = "算法未产生执行结果";
                }
                catch (const cv::Exception& exception)
                {
                    completion.error = exception.what();
                }
                catch (const std::exception& exception)
                {
                    completion.error = exception.what();
                }
                catch (...)
                {
                    completion.error = "未知后台执行异常";
                }
                if (!completion.error.empty())
                    LogSystem::Add(LOG_ERROR, "event=algorithm_exception error=%s",
                        completion.error.c_str());
                completionPromise.set_value(std::move(completion));
            });
        return true;
    }

    static void FinishCurrentTool(int executedIndex);

    static bool HasCrossTaskDependencies()
    {
        const auto& tools = ToolChainState::ReadOnlyTools();
        auto sourceGroup = [&tools](int legacyIndex, std::uint64_t stableId,
            const std::string& targetGroup) -> bool
        {
            int sourceIndex = -1;
            if (stableId != 0)
            {
                for (int index = 0; index < static_cast<int>(tools.size()); ++index)
                {
                    if (tools[index].toolId == stableId)
                    {
                        sourceIndex = index;
                        break;
                    }
                }
            }
            else if (legacyIndex >= 0 && legacyIndex < static_cast<int>(tools.size()))
            {
                sourceIndex = legacyIndex;
            }
            return sourceIndex >= 0 && tools[sourceIndex].groupName != targetGroup;
        };

        for (int toolIndex = 0; toolIndex < static_cast<int>(tools.size()); ++toolIndex)
        {
            const ToolInstance& tool = tools[toolIndex];
            if (!tool.enabled || BatchExecutionOrdinal(toolIndex) < 0)
                continue;
            if (tool.resultRoiMode != 0 &&
                sourceGroup(tool.resultRoiSourceTool, tool.resultRoiSourceToolId,
                    tool.groupName))
            {
                return true;
            }
            if (tool.resultRoiMode == static_cast<int>(ResultROIMode::SelectedPair) &&
                sourceGroup(tool.resultRoiSecondSourceTool,
                    tool.resultRoiSecondSourceToolId, tool.groupName))
            {
                return true;
            }
            if (tool.fixture.enabled &&
                sourceGroup(tool.fixture.sourceToolIndex, tool.fixture.sourceToolId,
                    tool.groupName))
            {
                return true;
            }
        }
        return false;
    }

    static TaskPipelineCompletion ExecuteTaskPipeline(
        TaskPipelineInput input, std::stop_token stopToken)
    {
        static std::mutex modelRuntimeMutex;
        TaskPipelineCompletion completion;
        completion.generation = input.generation;
        completion.groupName = input.groupName;
        completion.toolIndices = input.toolIndices;
        completion.outputs.reserve(input.tools.size());

        cv::Mat original = input.inputImage;
        cv::Mat lastInput = original;
        cv::Mat lastOutput = original;
        cv::Mat originalToolOutput = original;

        for (int localIndex = 0;
            localIndex < static_cast<int>(input.tools.size()) &&
            !stopToken.stop_requested(); ++localIndex)
        {
            ToolInstance& tool = input.tools[localIndex];
            ToolExecutor::ToolExecutionOutput output;
            const int sourceIndex = input.toolIndices[localIndex];
            bool fatalExecutionError = false;

            if (!tool.enabled)
            {
                output.completed = true;
                output.result.toolName = ToolInstanceLogName(
                    tool.type == 12 ? "原图" : ToolRegistry::GetName(tool.type),
                    tool.label);
                output.result.sourceToolIndex = sourceIndex;
                output.result.sourceToolId = tool.toolId;
                output.result.success = true;
                output.result.skipped = true;
                output.result.status = ToolResultStatus::Pass;
                output.result.message = "工具已禁用";
            }
            else if (tool.type == 12)
            {
                lastInput = original;
                lastOutput = original;
                originalToolOutput = original;
                output.completed = true;
                output.result.toolName = ToolInstanceLogName("原图", tool.label);
                output.result.sourceToolIndex = sourceIndex;
                output.result.sourceToolId = tool.toolId;
                output.result.success = !original.empty();
                output.result.status = original.empty()
                    ? ToolResultStatus::Error : ToolResultStatus::Pass;
                output.result.message = original.empty()
                    ? "原图恢复失败" : "原图已恢复";
                output.result.statusReason = original.empty()
                    ? "本轮原图为空" : std::string{};
            }
            else
            {
                const cv::Mat& selectedInput = tool.inputSourceMode == 1
                    ? lastOutput
                    : (tool.inputSourceMode == 2 ? originalToolOutput : lastInput);
                lastInput = selectedInput;

                ToolInstance snapshot;
                VisionContext context;
                ToolExecutor::ToolPreparationFailure preparationFailure;
                if (!ToolExecutor::PrepareDetachedSnapshot(tool, selectedInput,
                    original, sourceIndex, input.tools, input.toolIndices,
                    input.visibleROIs, input.selectedROI, input.frozenTemplate,
                    snapshot, context, &preparationFailure))
                {
                    output.completed = true;
                    output.result.toolName = ToolInstanceLogName(
                        ToolRegistry::GetName(tool.type), tool.label);
                    output.result.sourceToolIndex = sourceIndex;
                    output.result.sourceToolId = tool.toolId;
                    const bool dependencyFailure = !preparationFailure.reason.empty();
                    output.result.success = dependencyFailure;
                    output.result.skipped = dependencyFailure && preparationFailure.skip;
                    output.result.status = dependencyFailure
                        ? (preparationFailure.skip ? ToolResultStatus::Pass : ToolResultStatus::Fail)
                        : ToolResultStatus::Error;
                    output.result.message = dependencyFailure
                        ? (preparationFailure.skip
                            ? "已跳过: " + preparationFailure.reason
                            : preparationFailure.reason)
                        : "任务并行上下文准备失败";
                    output.result.statusReason = output.result.message;
                }
                else
                {
                    context.stopToken = stopToken;
                    try
                    {
                        // These backends own shared model/session state. Serialize only
                        // their inference section while other task pipelines keep running.
                        if (tool.type == 4 || tool.type == 11 || tool.type == 13)
                        {
                            std::lock_guard<std::mutex> lock(modelRuntimeMutex);
                            ToolExecutor::ExecuteDetached(
                                snapshot, context, sourceIndex, output);
                        }
                        else
                        {
                            ToolExecutor::ExecuteDetached(
                                snapshot, context, sourceIndex, output);
                        }
                    }
                    catch (const cv::Exception& exception)
                    {
                        completion.error = exception.what();
                    }
                    catch (const std::exception& exception)
                    {
                        completion.error = exception.what();
                    }
                    catch (...)
                    {
                        completion.error = "未知任务线程异常";
                    }
                    if (!completion.error.empty())
                    {
                        fatalExecutionError = true;
                        output.completed = true;
                        output.result.toolName = ToolInstanceLogName(
                            ToolRegistry::GetName(tool.type), tool.label);
                        output.result.sourceToolIndex = sourceIndex;
                        output.result.sourceToolId = tool.toolId;
                        output.result.success = false;
                        output.result.status = ToolResultStatus::Error;
                        output.result.message = completion.error;
                        output.result.statusReason = completion.error;
                    }
                }

                lastOutput = !output.result.debugImage.empty()
                    ? output.result.debugImage : selectedInput;
            }

            tool.lastResult = output.result;
            tool.hasLastResult = output.completed;
            completion.outputs.push_back(std::move(output));
            if (fatalExecutionError || (tool.hasLastResult &&
                ToolJudgement::ShouldStop(tool.lastResult, tool.judgement)))
            {
                break;
            }
        }

        completion.resultImage = lastOutput.clone();
        return completion;
    }

    static void LaunchPendingTaskPipelines()
    {
        while (!s_taskParallelPending.empty() &&
            s_taskParallelJobs.size() < static_cast<std::size_t>(kTaskParallelLimit))
        {
            TaskPipelineInput input = std::move(s_taskParallelPending.front());
            s_taskParallelPending.pop_front();
            TaskPipelineJob job;
            std::promise<TaskPipelineCompletion> promise;
            job.future = promise.get_future();
            job.worker = std::jthread(
                [input = std::move(input), promise = std::move(promise)](
                    std::stop_token stopToken) mutable
                {
                    promise.set_value(ExecuteTaskPipeline(
                        std::move(input), stopToken));
                });
            s_taskParallelJobs.push_back(std::move(job));
        }
    }

    static bool StartTaskParallelRun()
    {
        if (!s_taskParallelEnabled || s_runTaskGroup || s_loop || s_isStep ||
            EnabledTaskGroupCount() < 2 ||
            ToolChainState::YoloLiveDetect())
        {
            return false;
        }
        if (HasCrossTaskDependencies())
        {
            LogSystem::Add(LOG_WARN,
                "任务并行已回退顺序执行：检测到跨任务结果依赖");
            return false;
        }

        std::vector<std::string> groupOrder;
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
        {
            if (group.enabled)
                groupOrder.push_back(group.name);
        }
        if (std::any_of(ToolChainState::ReadOnlyTools().begin(),
            ToolChainState::ReadOnlyTools().end(),
            [](const ToolInstance& tool) { return tool.groupName.empty(); }))
        {
            groupOrder.push_back({});
        }

        const std::vector<ROI> visibleROIs = ROIState::ReadOnlyItems();
        const int selectedROI = ROIState::SelectedIndex();
        const cv::Mat frozenTemplate = TemplateState::FrozenTemplate().clone();
        const auto& tools = ToolChainState::ReadOnlyTools();
        s_taskParallelPending.clear();
        s_taskParallelJobs.clear();
        for (const std::string& groupName : groupOrder)
        {
            TaskPipelineInput input;
            input.generation = s_executionGeneration;
            input.groupName = groupName;
            input.visibleROIs = visibleROIs;
            input.selectedROI = selectedROI;
            input.frozenTemplate = frozenTemplate;
            const auto taskImage = s_taskRunImages.find(groupName);
            input.inputImage = (taskImage != s_taskRunImages.end()
                ? taskImage->second : s_runFallbackImage).clone();
            for (int index : s_batchExecutionOrder)
            {
                if (index >= 0 && index < static_cast<int>(tools.size()) &&
                    tools[index].groupName == groupName)
                {
                    input.toolIndices.push_back(index);
                    input.tools.push_back(tools[index]);
                }
            }
            if (!input.tools.empty())
                s_taskParallelPending.push_back(std::move(input));
        }
        if (s_taskParallelPending.size() < 2)
        {
            s_taskParallelPending.clear();
            return false;
        }

        s_taskParallelRunning = true;
        s_taskParallelPublishedTools = 0;
        s_taskParallelStart = std::chrono::high_resolution_clock::now();
        s_batchStart = s_taskParallelStart;
        s_batchTimerStarted = true;
        LaunchPendingTaskPipelines();
        LogSystem::Add(LOG_INFO, "任务并行: %zu 个任务，最大并发 %d",
            s_taskParallelPending.size() + s_taskParallelJobs.size(),
            kTaskParallelLimit);
        return true;
    }

    static bool PollTaskParallelRun()
    {
        if (!s_taskParallelRunning)
            return false;

        bool published = false;
        for (std::size_t jobIndex = 0; jobIndex < s_taskParallelJobs.size();)
        {
            TaskPipelineJob& job = s_taskParallelJobs[jobIndex];
            if (!job.future.valid() || job.future.wait_for(std::chrono::milliseconds(0)) !=
                std::future_status::ready)
            {
                ++jobIndex;
                continue;
            }

            TaskPipelineCompletion completion = job.future.get();
            if (job.worker.joinable())
                job.worker.join();
            s_taskParallelJobs.erase(s_taskParallelJobs.begin() + jobIndex);
            published = true;
            if (completion.generation != s_executionGeneration)
                continue;

            for (std::size_t outputIndex = 0;
                outputIndex < completion.outputs.size() &&
                outputIndex < completion.toolIndices.size(); ++outputIndex)
            {
                const int toolIndex = completion.toolIndices[outputIndex];
                ToolInstance* target = ToolChainState::At(toolIndex);
                if (!target || target->toolId !=
                    completion.outputs[outputIndex].result.sourceToolId)
                {
                    continue;
                }
                const float executionMs = completion.outputs[outputIndex].prepareMs +
                    completion.outputs[outputIndex].executeMs;
                EnsureToolTimesSize();
                s_toolTimesMs[toolIndex] = executionMs;
                s_stepTimeMs = executionMs;
                ToolExecutor::PublishDetached(
                    *target, std::move(completion.outputs[outputIndex]));
                ++s_taskParallelPublishedTools;
            }
            if (!completion.resultImage.empty())
                s_taskResultImages[completion.groupName] = completion.resultImage.clone();
        }

        LaunchPendingTaskPipelines();
        if (s_taskParallelJobs.empty() && s_taskParallelPending.empty())
        {
            s_taskParallelRunning = false;
            s_batchTotalMs = std::chrono::duration<float, std::milli>(
                std::chrono::high_resolution_clock::now() -
                s_taskParallelStart).count();
            s_lastParallelWallMs = s_batchTotalMs;
            s_lastCompletedLoopRound = 0;
            ++s_completedBatchSerial;
            PublishConfiguredHardwareResult();
            LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
                "[任务并行] 完成 %.1fms", s_batchTotalMs);
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            s_batchTimerStarted = false;
        }
        return published || s_taskParallelRunning;
    }

    static bool StartParallelGraphLevel(int firstIndex, const cv::Mat& input, bool force)
    {
        if (s_isStep || s_runTaskGroup ||
            !ToolChainState::ReadOnlyTaskGroups().empty() ||
            s_executionPlan.levels.empty() ||
            ToolChainState::YoloLiveDetect())
        {
            return false;
        }

        const std::vector<int>* level = nullptr;
        for (const std::vector<int>& candidate : s_executionPlan.levels)
        {
            if (std::find(candidate.begin(), candidate.end(), firstIndex) != candidate.end())
            {
                level = &candidate;
                break;
            }
        }
        if (!level || level->size() < 2 || level->front() != firstIndex)
            return false;
        for (int index : *level)
        {
            if (index < 0 || index >= static_cast<int>(s_executionPlan.nodes.size()) ||
                !s_executionPlan.nodes[index].parallelizable)
            {
                return false;
            }
        }

        struct PreparedTask
        {
            int index = -1;
            std::uint64_t generation = 0;
            std::uint64_t toolId = 0;
            std::uint64_t parameterRevision = 0;
            int imageVersion = -1;
            ToolExecutionCacheKey cacheKey;
            ToolInstance snapshot;
            VisionContext context;
            ToolResult cachedResult;
            bool cached = false;
        };

        std::vector<PreparedTask> prepared;
        prepared.reserve(level->size());
        for (int index : *level)
        {
            ToolInstance* target = ToolChainState::At(index);
            if (!target || !target->enabled || !IsAsyncTool(*target))
                return false;

            PreparedTask task;
            task.index = index;
            task.generation = s_executionGeneration;
            task.toolId = target->toolId;
            task.parameterRevision = target->parameterRevision;
            task.imageVersion = ImageState::Version();
            task.cacheKey.toolId = target->toolId;
            task.cacheKey.parameterRevision = target->parameterRevision;
            task.cacheKey.runRevision = s_activeRunRevision;
            task.cacheKey.imageVersion = task.imageVersion;
            task.cacheKey.upstreamRevision = ToolExecutionGraph::ComputeUpstreamRevision(
                s_executionPlan, ToolChainState::ReadOnlyTools(), index);
            task.cached = !force && ToolExecutionGraph::TryGetCachedResult(
                task.cacheKey, task.cachedResult);
            if (!task.cached && !ToolExecutor::PrepareDetached(
                *target, input, index, task.snapshot, task.context))
            {
                return false;
            }
            prepared.push_back(std::move(task));
        }

        s_parallelTasks.clear();
        s_parallelTasks.reserve(prepared.size());
        for (PreparedTask& preparedTask : prepared)
        {
            ParallelTask task;
            std::promise<AsyncCompletion> completionPromise;
            task.future = completionPromise.get_future();
            if (preparedTask.cached)
            {
                AsyncCompletion completion;
                completion.generation = preparedTask.generation;
                completion.toolId = preparedTask.toolId;
                completion.requestedIndex = preparedTask.index;
                completion.imageVersion = preparedTask.imageVersion;
                completion.parameterRevision = preparedTask.parameterRevision;
                completion.cacheKey = preparedTask.cacheKey;
                completion.batch = true;
                completion.output.completed = true;
                completion.output.cacheHit = true;
                completion.output.result = std::move(preparedTask.cachedResult);
                completionPromise.set_value(std::move(completion));
            }
            else
            {
                task.worker = std::jthread(
                    [preparedTask = std::move(preparedTask),
                     completionPromise = std::move(completionPromise)](
                        std::stop_token stopToken) mutable
                    {
                        AsyncCompletion completion;
                        completion.generation = preparedTask.generation;
                        completion.toolId = preparedTask.toolId;
                        completion.requestedIndex = preparedTask.index;
                        completion.imageVersion = preparedTask.imageVersion;
                        completion.parameterRevision = preparedTask.parameterRevision;
                        completion.cacheKey = preparedTask.cacheKey;
                        completion.batch = true;
                        preparedTask.context.stopToken = stopToken;
                        try
                        {
                            ToolExecutor::ExecuteDetached(preparedTask.snapshot,
                                preparedTask.context, preparedTask.index, completion.output);
                            if (!completion.output.completed && !stopToken.stop_requested())
                                completion.error = "算法未产生执行结果";
                        }
                        catch (const cv::Exception& exception)
                        {
                            completion.error = exception.what();
                        }
                        catch (const std::exception& exception)
                        {
                            completion.error = exception.what();
                        }
                        catch (...)
                        {
                            completion.error = "未知后台执行异常";
                        }
                        completionPromise.set_value(std::move(completion));
                    });
            }
            s_parallelTasks.push_back(std::move(task));
        }
        s_parallelRunning = true;
        s_parallelStart = std::chrono::high_resolution_clock::now();
        LogSystem::Add(LOG_INFO, "DAG 并行层: 同时执行 %zu 个工具", level->size());
        return true;
    }

    static bool PollParallelGraphLevel()
    {
        if (!s_parallelRunning)
            return false;
        for (const ParallelTask& task : s_parallelTasks)
        {
            if (!task.future.valid() ||
                task.future.wait_for(std::chrono::milliseconds(0)) !=
                    std::future_status::ready)
            {
                return false;
            }
        }

        std::vector<AsyncCompletion> completions;
        completions.reserve(s_parallelTasks.size());
        for (ParallelTask& task : s_parallelTasks)
        {
            completions.push_back(task.future.get());
            if (task.worker.joinable())
                task.worker.join();
        }
        s_parallelTasks.clear();
        s_parallelRunning = false;
        s_lastParallelWallMs = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - s_parallelStart).count();
        LogSystem::Add(LOG_INFO, "DAG parallel layer wall time: %.3f ms", s_lastParallelWallMs);
        std::sort(completions.begin(), completions.end(),
            [](const AsyncCompletion& left, const AsyncCompletion& right)
            {
                return left.requestedIndex < right.requestedIndex;
            });

        for (AsyncCompletion& completion : completions)
        {
            const int currentToolIndex = ToolChainState::IndexOfToolId(completion.toolId);
            ToolInstance* target = ToolChainState::At(currentToolIndex);
            if (completion.generation != s_executionGeneration ||
                completion.imageVersion != ImageState::Version() || !target ||
                target->parameterRevision != completion.parameterRevision)
            {
                LogSystem::Add(LOG_INFO, "DAG 并行结果已丢弃: 输入、参数或工具链已变化");
                s_mode = Mode::Idle;
                return true;
            }
            if (!completion.error.empty())
            {
                ToolResult errorResult;
                const char* baseName = target->type == 12
                    ? "原图" : ToolRegistry::GetName(target->type);
                errorResult.toolName = ToolInstanceLogName(baseName, target->label);
                errorResult.sourceToolIndex = currentToolIndex;
                errorResult.sourceToolId = target->toolId;
                errorResult.success = false;
                errorResult.status = ToolResultStatus::Error;
                errorResult.message = completion.error;
                errorResult.statusReason = completion.error;
                completion.output.result = std::move(errorResult);
                completion.output.completed = true;
            }

            if (completion.output.completed && completion.error.empty())
            {
                ToolExecutionGraph::StoreCachedResult(
                    completion.cacheKey, completion.output.result);
            }
            const float executionMs = completion.output.prepareMs +
                completion.output.executeMs;
            s_imageDirty |= ToolExecutor::PublishDetached(
                *target, std::move(completion.output));
            s_stepTimeMs = executionMs;
            EnsureToolTimesSize();
            s_toolTimesMs[currentToolIndex] = executionMs;
            FinishCurrentTool(completion.requestedIndex);
        }
        return true;
    }

    static bool PollAsyncExecution()
    {
        if (!s_asyncRunning || !s_asyncFuture.valid() ||
            s_asyncFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            return false;
        }

        AsyncCompletion completion = s_asyncFuture.get();
        if (s_asyncWorker.joinable())
            s_asyncWorker.join();
        s_asyncRunning = false;
        const int currentToolIndex = ToolChainState::IndexOfToolId(completion.toolId);
        const bool sameImageVersion = completion.imageVersion == ImageState::Version();
        const bool validStreamingAdvance = completion.allowStreamingFrameAdvance &&
            VideoCapture::IsOpen() &&
            completion.streamSourceGeneration == VideoCapture::SourceGeneration() &&
            completion.imageWidth == ImageState::Width() &&
            completion.imageHeight == ImageState::Height();
        if (completion.generation != s_executionGeneration ||
            (!sameImageVersion && !validStreamingAdvance) || currentToolIndex < 0)
        {
            LogSystem::Add(LOG_INFO, "后台结果已丢弃: 输入或工具链已变化");
            if (completion.batch)
                s_mode = Mode::Idle;
            return true;
        }

        ToolInstance* target = ToolChainState::At(currentToolIndex);
        if (!target)
            return true;
        if (target->parameterRevision != completion.parameterRevision)
        {
            const char* baseName = target->type == 12 ? "原图" : ToolRegistry::GetName(target->type);
            LogSystem::Add(LOG_INFO, "%s 后台结果已丢弃: 参数已变化",
                ToolInstanceLogName(baseName, target->label).c_str());
            if (completion.batch)
                s_mode = Mode::Idle;
            return true;
        }

        if (!completion.error.empty())
        {
            ToolResult errorResult;
            const char* baseName = target->type == 12 ? "原图" : ToolRegistry::GetName(target->type);
            errorResult.toolName = ToolInstanceLogName(baseName, target->label);
            errorResult.sourceToolIndex = currentToolIndex;
            errorResult.sourceToolId = target->toolId;
            errorResult.success = false;
            errorResult.status = ToolResultStatus::Error;
            errorResult.message = completion.error;
            errorResult.statusReason = completion.error;
            completion.output.result = std::move(errorResult);
            completion.output.completed = true;
        }

        const float executionMs = completion.output.prepareMs + completion.output.executeMs;
        if (completion.output.completed && completion.error.empty())
            ToolExecutionGraph::StoreCachedResult(
                completion.cacheKey, completion.output.result);
        s_imageDirty = ToolExecutor::PublishDetached(*target, std::move(completion.output));
        s_stepTimeMs = executionMs;
        EnsureToolTimesSize();
        s_toolTimesMs[currentToolIndex] = s_stepTimeMs;
        if (!ImageState::Current().empty())
            s_lastOutputImage = ImageState::Current();

        if (completion.batch)
            FinishCurrentTool(completion.requestedIndex);
        return true;
    }

    void RequestRun(int toolIndex)
    {
        if (toolIndex < 0 || toolIndex >= static_cast<int>(ToolChainState::ReadOnlyTools().size()))
            return;
        s_runTaskGroup = false;
        s_runTaskGroupName.clear();
        s_batchRunActive = false;
        if (!ValidateToolChainForRun())
            return;
        const std::uint64_t toolId = ToolChainState::ReadOnlyTools()[toolIndex].toolId;
        if (toolId == 0)
            return;
        s_queue.erase(std::remove_if(s_queue.begin(), s_queue.end(),
            [toolId](const QueuedToolRequest& request)
            {
                return request.toolId == toolId;
            }), s_queue.end());
        const bool force = s_forceNextToolId == toolId;
        if (force)
            s_forceNextToolId = 0;
        s_queue.push_back({toolId, ++s_nextRunRevision, force});
    }

    void RequestForceRun(int toolIndex)
    {
        if (toolIndex < 0 || toolIndex >= static_cast<int>(ToolChainState::ReadOnlyTools().size()))
            return;
        s_forceNextToolId = ToolChainState::ReadOnlyTools()[toolIndex].toolId;
        RequestRun(toolIndex);
    }

    static void BeginRunAll(bool loop, bool triggerCamera,
        bool forceTaskCameraCapture = false) {
        const bool force = s_forceNextRunAll;
        s_forceNextRunAll = false;
        s_activeRunForce = force;
        const int boundCameraIndex = RunScopeCameraIndex();
        std::set<int> runCameraSlots;
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
        {
            if (!tool.enabled || !MatchesRunScope(tool))
                continue;
            const int cameraIndex = TaskGroupCameraIndex(tool.groupName);
            if (cameraIndex >= 0)
                runCameraSlots.insert(cameraIndex);
        }
        for (const int cameraIndex : runCameraSlots)
        {
            const DeviceOperationResult activation =
                HardwareRuntimeService::ActivateCameraSlot(cameraIndex);
            if (!activation.success)
            {
                LogSystem::Add(LOG_WARN,
                    "绑定相机 %02d 自动连接失败，将使用任务图片: %s",
                    cameraIndex + 1, activation.message.c_str());
            }
        }
        if (triggerCamera && boundCameraIndex >= 0)
        {
            const DeviceOperationResult activation =
                HardwareRuntimeService::ActivateCameraSlot(boundCameraIndex);
            if (!activation.success)
            {
                LogSystem::Add(LOG_WARN,
                    "绑定相机 %02d 自动连接失败，将使用任务图片: %s",
                    boundCameraIndex + 1, activation.message.c_str());
            }
        }
        else if (triggerCamera && boundCameraIndex == -2)
        {
            LogSystem::Add(LOG_WARN,
                "全部执行包含多个相机绑定，将按各任务绑定槽使用对应最新相机帧");
        }
        const HardwareRuntimeSnapshot hardware = HardwareRuntimeService::Snapshot();
        const bool taskCameraPreferred = boundCameraIndex >= 0 &&
            hardware.cameraState == DeviceConnectionState::Connected &&
            hardware.cameraSlotIndex == boundCameraIndex;
        if (triggerCamera)
        {
            s_cameraFrameAvailableForRun = false;
            s_forceCameraFrameForRun = forceTaskCameraCapture;
        }
        const bool requestTaskCamera = triggerCamera &&
            (taskCameraPreferred || (forceTaskCameraCapture && s_runTaskGroup)) &&
            hardware.cameraState == DeviceConnectionState::Connected;
        const bool requestLegacyCamera = triggerCamera && !s_runTaskGroup &&
            !taskCameraPreferred &&
            !RunScopeHasTaskImages() &&
            hardware.cameraState == DeviceConnectionState::Connected &&
            HardwareRuntimeService::CameraTriggerOnInspectionEnabled();
        if (requestTaskCamera || requestLegacyCamera)
        {
            if (force)
                s_forceNextRunAll = true;
            s_mode = Mode::Idle;
            s_loop = loop;
            HardwareRuntimeService::RequestCameraFrame(true, loop);
            LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
                "[全部执行] 已请求相机新帧，等待发布后执行工具链");
            return;
        }
        BuildBatchExecutionOrder();
        if (s_batchExecutionOrder.empty())
        {
            LogSystem::Add(LOG_WARN, s_runTaskGroup
                ? "当前任务没有可执行工具: %s" : "工具链为空",
                s_runTaskGroup ? (s_runTaskGroupName.empty()
                    ? "未分组" : s_runTaskGroupName.c_str()) : "");
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            s_forceCameraFrameForRun = false;
            return;
        }
        if (!PrepareTaskImagesForRun() ||
            !ActivateTaskImage(
                ToolChainState::ReadOnlyTools()[s_batchExecutionOrder.front()]))
        {
            LogSystem::Add(LOG_ERROR, "执行中止：任务输入图片准备失败");
            PublishConfiguredHardwareStatus(ToolResultStatus::Error);
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            s_forceCameraFrameForRun = false;
            return;
        }
        s_forceCameraFrameForRun = false;
        if (s_runTaskGroup)
            s_taskResultImages.erase(s_runTaskGroupName);
        else
            s_taskResultImages.clear();
        s_executionPlan = ToolExecutionGraph::Build(ToolChainState::ReadOnlyTools());
        if (!s_executionPlan.valid)
        {
            LogSystem::Add(LOG_ERROR, "工具链 DAG 构建失败: %s",
                s_executionPlan.error.c_str());
            PublishConfiguredHardwareStatus(ToolResultStatus::Error);
            s_mode = Mode::Idle;
            return;
        }
        if (!ValidateToolChainForRun())
        {
            s_mode = Mode::Idle;
            return;
        }
        s_activeRunRevision = ++s_nextRunRevision;
        s_mode = Mode::Running; s_isStep = false;
        s_batchRunActive = true;
        s_currentIndex = s_batchExecutionOrder.front();
        s_stepCursor = 0; s_loop = loop;
        s_loopCompletedRounds = 0;
        s_stepTimeMs = s_batchTotalMs = 0;
        EnsureToolTimesSize();
        if (s_runTaskGroup)
        {
            for (int toolIndex : s_batchExecutionOrder)
            {
                if (toolIndex >= 0 && toolIndex < static_cast<int>(s_toolTimesMs.size()))
                    s_toolTimesMs[toolIndex] = 0.0f;
            }
        }
        else
        {
            std::fill(s_toolTimesMs.begin(), s_toolTimesMs.end(), 0.0f);
        }
        s_imageDirty = false;
        s_batchTimerStarted = false;
        s_nextLoopRunAt = std::chrono::high_resolution_clock::now();
        ResetBatchImagesFromSource(!ImageState::Original().empty() ? ImageState::Original() : ImageState::Current());
        for (auto& tool : ToolChainState::Tools())
        {
            if (!s_runTaskGroup || tool.groupName == s_runTaskGroupName)
                tool.hasLastResult = false;
        }
        s_lastRunTaskGroup = s_runTaskGroup;
        s_lastRunTaskGroupName = s_runTaskGroupName;
        if (s_runTaskGroup)
        {
            LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1),
                "[执行当前任务%s] %s | %zu 个工具",
                s_runtimeMode ? "/运行模式" : "",
                s_runTaskGroupName.empty() ? "未分组" : s_runTaskGroupName.c_str(),
                s_batchExecutionOrder.size());
        }
        else
        {
            LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[全部执行%s] %zu 个工具",
                s_runtimeMode ? "/运行模式" : "", s_batchExecutionOrder.size());
        }
        if (StartTaskParallelRun())
            return;
    }

    void RequestRunAll(bool loop, bool triggerCamera)
    {
        s_runTriggerCamera = triggerCamera;
        s_cameraFrameAvailableForRun = false;
        s_forceCameraFrameForRun = false;
        s_runTaskGroup = false;
        s_runTaskGroupName.clear();
        BeginRunAll(loop, triggerCamera);
    }

    void RequestRunTaskGroup(const std::string& groupName, bool loop,
        bool triggerCamera, bool forceCameraCapture)
    {
        if (!groupName.empty())
        {
            const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
            if (groupIndex < 0)
            {
                LogSystem::Add(LOG_WARN, "任务不存在: %s", groupName.c_str());
                return;
            }
            if (!ToolChainState::ReadOnlyTaskGroups()[groupIndex].enabled)
            {
                LogSystem::Add(LOG_WARN, "任务已禁用: %s", groupName.c_str());
                return;
            }
        }
        s_runTriggerCamera = triggerCamera;
        s_cameraFrameAvailableForRun = false;
        s_forceCameraFrameForRun = forceCameraCapture;
        s_runTaskGroup = true;
        s_runTaskGroupName = groupName;
        BeginRunAll(loop, triggerCamera, forceCameraCapture);
    }

    void ResumeRunAfterCamera(bool loop, bool cameraFrameAvailable)
    {
        s_cameraFrameAvailableForRun = cameraFrameAvailable;
        BeginRunAll(loop, false);
    }

    void RequestRepeatLastRun(bool loop, bool triggerCamera)
    {
        if (s_lastRunTaskGroup)
            RequestRunTaskGroup(s_lastRunTaskGroupName, loop, triggerCamera);
        else
            RequestRunAll(loop, triggerCamera);
    }

    static void BeginStepNext()
    {
        s_batchRunActive = false;
        BuildBatchExecutionOrder();
        if (s_batchExecutionOrder.empty())
        {
            LogSystem::Add(LOG_WARN, s_runTaskGroup
                ? "当前任务没有可单步执行的工具: %s" : "工具链为空",
                s_runTaskGroup ? s_runTaskGroupName.c_str() : "");
            s_mode = Mode::Idle;
            return;
        }
        if (s_stepCursor >= static_cast<int>(s_batchExecutionOrder.size()))
        {
            s_stepCursor = 0;
            s_stepTimeMs = 0;
        }
        s_currentIndex = s_batchExecutionOrder[s_stepCursor];
        if (s_stepCursor == 0)
        {
            if (!PrepareTaskImagesForRun() ||
                !ActivateTaskImage(
                    ToolChainState::ReadOnlyTools()[s_currentIndex]))
            {
                LogSystem::Add(LOG_ERROR, "单步中止：任务输入图片准备失败");
                s_mode = Mode::Idle;
                return;
            }
        }
        if (!ValidateToolChainForRun())
            return;
        s_isStep = true;
        s_activeRunRevision = ++s_nextRunRevision;
        s_mode = Mode::Running;
    }

    void RequestStepNext()
    {
        s_runTriggerCamera = false;
        s_cameraFrameAvailableForRun = false;
        s_forceCameraFrameForRun = false;
        if (s_runTaskGroup)
            s_stepCursor = 0;
        s_runTaskGroup = false;
        s_runTaskGroupName.clear();
        BeginStepNext();
    }

    void RequestStepNextTaskGroup(const std::string& groupName)
    {
        s_cameraFrameAvailableForRun = false;
        const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
        if (groupIndex < 0)
        {
            LogSystem::Add(LOG_WARN, "任务不存在: %s", groupName.c_str());
            return;
        }
        if (!ToolChainState::ReadOnlyTaskGroups()[groupIndex].enabled)
        {
            LogSystem::Add(LOG_WARN, "任务已禁用: %s", groupName.c_str());
            return;
        }
        if (!s_runTaskGroup || s_runTaskGroupName != groupName)
            s_stepCursor = 0;
        s_runTaskGroup = true;
        s_runTaskGroupName = groupName;
        BeginStepNext();
    }

    void RequestStepReset() { s_stepCursor = 0; s_stepTimeMs = 0; s_mode = Mode::Idle; }

    static void FinishCurrentTool(int executedIndex)
    {
        auto& tools = ToolChainState::Tools();
        if (executedIndex < 0 || executedIndex >= static_cast<int>(tools.size()))
        {
            s_mode = Mode::Idle;
            return;
        }

        ToolInstance& tool = tools[executedIndex];
        if (!s_isStep)
            s_batchTotalMs += s_stepTimeMs;
        if (!ImageState::Current().empty())
            s_lastOutputImage = ImageState::Current();

        if (!s_isStep && tool.hasLastResult &&
            ToolJudgement::ShouldStop(tool.lastResult, tool.judgement))
        {
            CaptureActiveTaskResultImage();
            const bool stoppedLoopRound = s_loop;
            const char* baseName = tool.type == 12 ? "原图" : ToolRegistry::GetName(tool.type);
            const std::string displayName = ToolInstanceLogName(baseName, tool.label);
            LogSystem::Add(
                tool.lastResult.status == ToolResultStatus::Error ? LOG_ERROR : LOG_WARN,
                ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                "[全部执行] 停止于 %d/%zu: %s | %s%s%s",
                (std::max)(1, BatchExecutionOrdinal(executedIndex) + 1),
                s_batchExecutionOrder.empty() ? tools.size() : s_batchExecutionOrder.size(),
                displayName.c_str(),
                ToolResultStatusName(tool.lastResult.status),
                tool.lastResult.statusReason.empty() ? "" : " | ",
                tool.lastResult.statusReason.c_str());
            if (s_batchTimerStarted)
            {
                s_batchTotalMs = std::chrono::duration<float, std::milli>(
                    std::chrono::high_resolution_clock::now() - s_batchStart).count();
            }
            if (stoppedLoopRound)
                ++s_loopCompletedRounds;
            s_lastCompletedLoopRound = stoppedLoopRound ? s_loopCompletedRounds : 0;
            ++s_completedBatchSerial;
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            s_loop = false;
            s_batchTimerStarted = false;
            PublishConfiguredHardwareResult();
            return;
        }

        if (s_isStep)
        {
            ++s_stepCursor;
            s_mode = Mode::Idle;
            return;
        }

        if (s_batchRunActive)
        {
            const int executedOrdinal = BatchExecutionOrdinal(executedIndex);
            s_batchExecutionCursor = executedOrdinal >= 0
                ? static_cast<std::size_t>(executedOrdinal + 1)
                : s_batchExecutionOrder.size();
            if (s_batchExecutionCursor < s_batchExecutionOrder.size())
            {
                s_currentIndex = s_batchExecutionOrder[s_batchExecutionCursor];
                return;
            }
        }
        else
        {
            s_currentIndex = executedIndex + 1;
            if (s_currentIndex < static_cast<int>(tools.size()))
                return;
        }

        if (s_loop)
            ++s_loopCompletedRounds;

        CaptureActiveTaskResultImage();

        const float batchWallMs = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - s_batchStart).count();
        s_batchTotalMs = batchWallMs;

        s_lastCompletedLoopRound = s_loop ? s_loopCompletedRounds : 0;
        ++s_completedBatchSerial;
        PublishConfiguredHardwareResult(s_loop);
        if (!s_loop)
        {
            LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1), "[全部执行%s] 完成 %.1fms",
                s_runtimeMode ? "/运行模式" : "", s_batchTotalMs);
        }
        const bool requestNextTaskCameraFrame = s_loop &&
            RunScopePrefersCamera() &&
            HardwareRuntimeService::Snapshot().cameraState ==
                DeviceConnectionState::Connected;
        if (requestNextTaskCameraFrame)
        {
            s_cameraFrameAvailableForRun = false;
            s_batchExecutionCursor = 0;
            s_currentIndex = s_batchExecutionOrder.front();
            s_imageDirty = false;
            s_batchTimerStarted = false;
            HardwareRuntimeService::RequestCameraFrame(true, true);
            s_mode = Mode::Idle;
            LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
                "[循环] 相机优先任务等待下一帧");
        }
        else if (s_loop && s_taskRunImages.empty() && FrameNavigation::HasNextImage())
        {
            FrameNavigation::NavigateNextImage();
            FrameNavigation::FitImageToWindow();
            s_mode = Mode::Idle;
        }
        else if (s_loop && s_runTriggerCamera && !s_runTaskGroup &&
            s_taskRunImages.empty() &&
            HardwareRuntimeService::Snapshot().cameraState == DeviceConnectionState::Connected &&
            HardwareRuntimeService::CameraTriggerOnInspectionEnabled())
        {
            s_batchExecutionCursor = 0;
            s_currentIndex = s_batchExecutionOrder.front();
            s_imageDirty = false;
            s_batchTimerStarted = false;
            HardwareRuntimeService::RequestCameraFrame(true, true);
            s_mode = Mode::Idle;
            LogSystem::Add(LOG_INFO, ImVec4(0, 1, 0.5f, 1),
                "[循环] 当前帧完成，等待相机下一帧");
        }
        else if (s_loop)
        {
            if (!PrepareTaskImagesForRun(true))
            {
                LogSystem::Add(LOG_ERROR,
                    "循环已停止：下一轮任务图片准备失败");
                s_mode = Mode::Idle;
                s_batchRunActive = false;
                s_loop = false;
                return;
            }
            s_batchExecutionCursor = 0;
            s_currentIndex = s_batchExecutionOrder.front();
            s_imageDirty = false;
            s_activeInputTaskGroup.clear();
            s_activeInputTaskGroupValid = false;
            s_batchTimerStarted = false;
            s_activeRunRevision = ++s_nextRunRevision;
            s_nextLoopRunAt = std::chrono::high_resolution_clock::now()
                + std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
                            std::chrono::duration<float, std::milli>(static_cast<float>(s_loopIntervalMs)));
        }
        else
        {
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            s_batchTimerStarted = false;
        }
    }

    void RequestForceRunAll(bool loop, bool triggerCamera)
    {
        s_forceNextRunAll = true;
        RequestRunAll(loop, triggerCamera);
    }

    void Tick() {
        if (PollTaskParallelRun())
            return;
        if (s_taskParallelRunning)
            return;
        if (PollParallelGraphLevel())
            return;
        if (s_parallelRunning)
            return;
        if (PollAsyncExecution())
            return;
        if (s_asyncRunning)
            return;

        // 独立执行只在批处理空闲时启动；同一工具的待执行请求只保留最新一次。
        if (s_mode == Mode::Idle && !s_queue.empty()) {
            const QueuedToolRequest request = s_queue.front();
            s_queue.pop_front();
            s_activeRunRevision = request.runRevision;
            const int idx = ToolChainState::IndexOfToolId(request.toolId);
            if (idx >= 0 && idx < (int)ToolChainState::ReadOnlyTools().size())
            {
                auto& tool = ToolChainState::Tools()[idx];
                const cv::Mat& selectedInput = SelectStandaloneInput(tool);
                s_lastInputImage = selectedInput;
                if (tool.enabled && StartAsyncExecution(idx, selectedInput, false, request.force))
                    return;
                ApplyInputImage(selectedInput, false);
                ExecuteToolAt(idx);
                if (!ImageState::Current().empty())
                    s_lastOutputImage = ImageState::Current();
            }
        }

        if (s_mode != Mode::Running) return;
        auto& tools = ToolChainState::Tools();
        if (s_currentIndex < 0 || s_currentIndex >= (int)tools.size())
        {
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            return;
        }
        if (ImageState::Current().empty())
        {
            LogSystem::Add(LOG_WARN, "执行中止：未加载图片");
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            PublishConfiguredHardwareStatus(ToolResultStatus::Error);
            return;
        }

        const auto now = std::chrono::high_resolution_clock::now();
        if (s_loop && s_batchExecutionCursor == 0 && now < s_nextLoopRunAt)
            return;

        auto& it = tools[s_currentIndex];
        if ((s_batchRunActive || s_isStep) && !ActivateTaskImage(it))
        {
            LogSystem::Add(LOG_ERROR, "执行中止：无法切换任务图片 [%s]",
                it.groupName.empty() ? "未分组" : it.groupName.c_str());
            s_mode = Mode::Idle;
            s_batchRunActive = false;
            return;
        }
        const cv::Mat& selectedInput = SelectBatchInput(it);
        if (!selectedInput.empty())
        {
            ApplyInputImage(selectedInput, s_isStep);
            s_lastInputImage = selectedInput;
        }

        if (!s_isStep && !s_batchTimerStarted) {
            if (s_loop && s_batchExecutionCursor == 0)
                s_batchTotalMs = 0.0f;
            s_batchStart = std::chrono::high_resolution_clock::now();
            s_batchTimerStarted = true;
        }

        const bool quietLoop = s_loop && !s_isStep;
        if (!quietLoop && (s_isStep || !s_runtimeMode))
        {
            const char* baseName = (it.type == 12) ? "原图" : ToolRegistry::GetName(it.type);
            const std::string displayName = ToolInstanceLogName(baseName, it.label);
            LogSystem::Add(LOG_INFO, ImVec4(0,1,0.5f,1), "[%s] %d/%d: %s",
                           s_isStep ? "单步" : "执行",
                           s_isStep ? s_stepCursor + 1 : GetRunProgressCurrent(),
                           s_isStep ? static_cast<int>(s_batchExecutionOrder.size()) : GetRunProgressTotal(),
                           displayName.c_str());
        }

        const int executingIndex = s_currentIndex;
        if (it.enabled && it.type != 12 &&
            StartParallelGraphLevel(executingIndex, selectedInput, s_activeRunForce))
        {
            return;
        }
        if (it.enabled && it.type != 12 && StartAsyncExecution(
                executingIndex, selectedInput, true, s_activeRunForce))
            return;

        s_imageDirty = ExecuteToolAt(executingIndex);
        FinishCurrentTool(executingIndex);
    }

    Mode GetMode() { return s_mode; }
    int  GetCurrentIndex() { return s_currentIndex; }
    int GetRunProgressCurrent()
    {
        if (s_taskParallelRunning)
            return s_taskParallelPublishedTools;
        if (!s_batchRunActive || s_batchExecutionOrder.empty())
            return s_currentIndex >= 0 ? s_currentIndex + 1 : 0;
        const int ordinal = BatchExecutionOrdinal(s_currentIndex);
        return ordinal >= 0 ? ordinal + 1 : 0;
    }
    int GetRunProgressTotal()
    {
        return s_batchRunActive
            ? static_cast<int>(s_batchExecutionOrder.size())
            : static_cast<int>(ToolChainState::Count());
    }
    bool WasLastRunTaskGroup() { return s_lastRunTaskGroup; }
    const std::string& GetLastRunTaskGroupName() { return s_lastRunTaskGroupName; }
    int  GetStepCursor() { return s_stepCursor; }
    int  GetStepToolIndex()
    {
        if (!s_isStep || s_stepCursor <= 0 ||
            s_stepCursor > static_cast<int>(s_batchExecutionOrder.size()))
            return -1;
        return s_batchExecutionOrder[static_cast<std::size_t>(s_stepCursor - 1)];
    }
    int  GetStepTotal()
    {
        return s_isStep
            ? static_cast<int>(s_batchExecutionOrder.size())
            : static_cast<int>(ToolChainState::Count());
    }
    bool IsStepTaskGroup() { return s_isStep && s_runTaskGroup; }
    const std::string& GetStepTaskGroupName() { return s_runTaskGroupName; }
    float GetTotalTimeMs() { return s_batchTotalMs; }
    float GetElapsedTimeMs()
    {
        if (s_taskParallelRunning && s_batchTimerStarted)
        {
            return std::chrono::duration<float, std::milli>(
                std::chrono::high_resolution_clock::now() -
                s_taskParallelStart).count();
        }
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
    cv::Mat GetTaskResultImage(const std::string& groupName)
    {
        const auto found = s_taskResultImages.find(groupName);
        return found != s_taskResultImages.end() ? found->second : cv::Mat{};
    }
    std::uint64_t GetCompletedBatchSerial() { return s_completedBatchSerial; }
    std::uint64_t GetLastCompletedLoopRound() { return s_lastCompletedLoopRound; }
    void SetRuntimeMode(bool enabled) { s_runtimeMode = enabled; }
    bool IsRuntimeMode() { return s_runtimeMode; }
    void SetLoopEnabled(bool enabled)
    {
        if (!enabled)
        {
            Reset();
            return;
        }
        s_loop = true;
    }
    bool IsLoopEnabled() { return s_loop; }
    void SetLoopIntervalMs(int milliseconds)
    {
        s_loopIntervalMs = std::clamp(milliseconds, 0, 60000);
    }
    int GetLoopIntervalMs() { return s_loopIntervalMs; }
    int GetLoopWaitRemainingMs()
    {
        if (!s_loop || s_mode != Mode::Running || s_batchExecutionCursor != 0 ||
            s_asyncRunning || s_parallelRunning)
            return 0;
        const auto now = std::chrono::high_resolution_clock::now();
        if (now >= s_nextLoopRunAt)
            return 0;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            s_nextLoopRunAt - now).count();
        return static_cast<int>((std::max)(std::int64_t{1}, remaining));
    }
    bool IsWaitingForNextLoop() { return GetLoopWaitRemainingMs() > 0; }
    bool IsParallelExecutionActive()
    {
        return s_parallelRunning || s_taskParallelRunning;
    }
    float GetLastParallelWallTimeMs() { return s_lastParallelWallMs; }
    void SetTaskParallelEnabled(bool enabled)
    {
        if (s_mode == Mode::Idle)
            s_taskParallelEnabled = enabled;
    }
    bool IsTaskParallelEnabled() { return s_taskParallelEnabled; }
    int GetTaskParallelLimit() { return kTaskParallelLimit; }
    std::uint64_t GetLoopIteration()
    {
        if (!s_loop)
            return 0;
        if (IsWaitingForNextLoop())
            return (std::max)(std::uint64_t{1}, s_loopCompletedRounds);
        return s_loopCompletedRounds + 1;
    }

    static void ClearRuntimeCaches()
    {
        Reset();
        s_originalImage.release();
        s_originalVersion = -1;
        s_lastInputImage.release();
        s_lastOutputImage.release();
        s_originalToolOutputImage.release();
        s_runFallbackImage.release();
        s_taskRunImages.clear();
        s_taskResultImages.clear();
        s_activeInputTaskGroup.clear();
        s_activeInputTaskGroupValid = false;
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
        ToolExecutionGraph::ClearCache();
        s_executionPlan = {};
        s_batchExecutionOrder.clear();
        s_batchExecutionCursor = 0;
        s_batchRunActive = false;
        s_runTaskGroup = false;
        s_runTaskGroupName.clear();
        s_lastRunTaskGroup = false;
        s_lastRunTaskGroupName.clear();
    }

    void OnToolChainChanged()
    {
        ClearRuntimeCaches();
    }

    void OnInputImageChanged()
    {
        ClearRuntimeCaches();
    }

    void Reset()
    {
        ++s_executionGeneration;
        if (s_asyncWorker.joinable())
            s_asyncWorker.request_stop();
        for (ParallelTask& task : s_parallelTasks)
        {
            if (task.worker.joinable())
                task.worker.request_stop();
        }
        for (TaskPipelineJob& job : s_taskParallelJobs)
        {
            if (job.worker.joinable())
                job.worker.request_stop();
        }

        s_taskParallelJobs.clear();
        s_taskParallelPending.clear();
        s_taskParallelRunning = false;
        HardwareRuntimeService::CancelPendingCameraToolRun();
        s_mode = Mode::Idle;
        s_currentIndex = -1;
        s_batchExecutionOrder.clear();
        s_batchExecutionCursor = 0;
        s_batchRunActive = false;
        s_runTaskGroup = false;
        s_runTaskGroupName.clear();
        s_stepCursor = 0;
        s_isStep = false;
        s_queue = {};
        s_loop = false;
        s_loopCompletedRounds = 0;
        s_batchTimerStarted = false;
        s_nextLoopRunAt = std::chrono::high_resolution_clock::now();
        s_toolTimesMs.clear();
        s_stepTimeMs = 0;
        s_batchTotalMs = 0;
        s_runFallbackImage.release();
        s_taskRunImages.clear();
        s_activeInputTaskGroup.clear();
        s_activeInputTaskGroupValid = false;
        s_cameraFrameAvailableForRun = false;
        s_forceCameraFrameForRun = false;
        s_runTriggerCamera = false;
    }

    void Shutdown()
    {
        ++s_executionGeneration;
        if (s_asyncWorker.joinable())
        {
            s_asyncWorker.request_stop();
            s_asyncWorker.join();
        }
        if (s_asyncFuture.valid())
        {
            try
            {
                s_asyncFuture.get();
            }
            catch (...)
            {
            }
        }
        s_asyncRunning = false;
        for (ParallelTask& task : s_parallelTasks)
        {
            if (task.worker.joinable())
            {
                task.worker.request_stop();
                task.worker.join();
            }
            if (task.future.valid())
            {
                try
                {
                    task.future.get();
                }
                catch (...)
                {
                }
            }
        }
        s_parallelTasks.clear();
        s_parallelRunning = false;
        for (TaskPipelineJob& job : s_taskParallelJobs)
        {
            if (job.worker.joinable())
            {
                job.worker.request_stop();
                job.worker.join();
            }
            if (job.future.valid())
            {
                try
                {
                    job.future.get();
                }
                catch (...)
                {
                }
            }
        }
        s_taskParallelJobs.clear();
        s_taskParallelPending.clear();
        s_taskParallelRunning = false;
    }
}
