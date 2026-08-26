#include "RegressionTaskSchedulingTests.h"

#include "../Core/ImageState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolController.h"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
namespace fs = std::filesystem;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

void TestToolControllerRunsTaskGroupsInParallel()
{
    ToolController::Reset();
    ToolController::SetTaskParallelEnabled(true);
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
    ImageState::SetImage(cv::Mat(32, 32, CV_8UC1, cv::Scalar(9)));

    const fs::path suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path taskAPath = fs::temp_directory_path() /
        ("imgui_opencv_parallel_a_" + suffix.string() + ".png");
    const fs::path taskBPath = fs::temp_directory_path() /
        ("imgui_opencv_parallel_b_" + suffix.string() + ".png");
    Require(cv::imwrite(taskAPath.string(),
            cv::Mat(32, 32, CV_8UC1, cv::Scalar(23))) &&
        cv::imwrite(taskBPath.string(),
            cv::Mat(32, 32, CV_8UC1, cv::Scalar(187))),
        "task-parallel test could not create input images");
    Require(ToolChainState::CreateTaskGroup("任务A") >= 0 &&
        ToolChainState::CreateTaskGroup("任务B") >= 0 &&
        ToolChainState::SetTaskGroupImagePath(0, taskAPath.string()) &&
        ToolChainState::SetTaskGroupImagePath(1, taskBPath.string()),
        "task-parallel group setup failed");

    auto addThreshold = [](const char* groupName)
    {
        ToolInstance tool;
        tool.type = 3;
        tool.groupName = groupName;
        tool.inputSourceMode = 0;
        tool.threshold.useGray = true;
        tool.threshold.enableThreshold = true;
        tool.threshold.threshold = 100;
        ToolChainState::AddTool(std::move(tool));
    };
    addThreshold("任务A");
    addThreshold("任务B");
    addThreshold("任务A");
    addThreshold("任务B");

    ToolController::RequestRunAll(false, false);
    Require(ToolController::IsParallelExecutionActive(),
        "run-all did not launch independent task groups in parallel");
    for (int attempt = 0; attempt < 500 &&
        ToolController::GetMode() != ToolController::Mode::Idle; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ToolController::Tick();
    }

    const cv::Mat taskAResult = ToolController::GetTaskResultImage("任务A");
    const cv::Mat taskBResult = ToolController::GetTaskResultImage("任务B");
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolChainState::AtReadOnly(0)->hasLastResult &&
        ToolChainState::AtReadOnly(1)->hasLastResult &&
        ToolChainState::AtReadOnly(2)->hasLastResult &&
        ToolChainState::AtReadOnly(3)->hasLastResult,
        "task-parallel run did not publish every task result");
    Require(!taskAResult.empty() && !taskBResult.empty() &&
        cv::countNonZero(taskAResult.reshape(1)) == 0 &&
        cv::countNonZero(taskBResult.reshape(1)) ==
            static_cast<int>(taskBResult.total() * taskBResult.channels()),
        "task-parallel result images were mixed between tasks");

    std::error_code removeError;
    fs::remove(taskAPath, removeError);
    removeError.clear();
    fs::remove(taskBPath, removeError);
    ToolController::SetTaskParallelEnabled(false);
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}
void TestTaskParallelBoundsInputCopiesToActiveWorkers()
{
    ToolController::Reset();
    ToolController::SetTaskParallelEnabled(true);
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});

    const cv::Mat input(1024, 1024, CV_8UC1, cv::Scalar(127));
    ImageState::SetImage(input);
    constexpr int taskCount = 8;
    for (int index = 0; index < taskCount; ++index)
    {
        const std::string groupName = "内存任务" + std::to_string(index + 1);
        Require(ToolChainState::CreateTaskGroup(groupName) >= 0,
            "task-parallel memory group setup failed");
        ToolInstance tool;
        tool.type = 3;
        tool.groupName = groupName;
        tool.threshold.useGray = true;
        tool.threshold.enableThreshold = true;
        tool.threshold.threshold = 100;
        ToolChainState::AddTool(std::move(tool));
    }

    ToolController::RequestRunAll(false, false);
    Require(ToolController::IsParallelExecutionActive(),
        "bounded task-parallel run did not start");
    for (int attempt = 0; attempt < 2000 &&
        ToolController::GetMode() != ToolController::Mode::Idle; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ToolController::Tick();
    }

    const std::uint64_t inputBytes = static_cast<std::uint64_t>(
        input.total() * input.elemSize());
    const std::uint64_t peakBytes =
        ToolController::GetLastTaskParallelPeakInputBytes();
    Require(ToolController::GetMode() == ToolController::Mode::Idle &&
        ToolController::GetLastTaskParallelEagerInputBytes() == 0,
        "pending task queue eagerly copied full input images");
    Require(peakBytes >= inputBytes &&
        peakBytes <= inputBytes *
            static_cast<std::uint64_t>(ToolController::GetTaskParallelLimit()),
        "task-parallel input copies exceeded the active worker limit");

    ToolController::SetTaskParallelEnabled(false);
    ToolController::Reset();
    ToolChainState::ClearTools();
    ToolChainState::ReplaceTaskGroups({});
}
