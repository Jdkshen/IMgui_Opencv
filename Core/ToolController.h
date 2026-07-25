#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <opencv2/core/mat.hpp>

// =====================================================
// ToolController — 工具执行调度器
// 替代旧 ExecState 状态机，用 queue 驱动
// 由 ShowToolsWindow 的 UI 按钮触发，每帧 Tick 消费
// =====================================================
namespace ToolController
{
    void RequestForceRun(int toolIndex);
    void RequestForceRunAll(bool loop = false, bool triggerCamera = true);
    enum class Mode { Idle, Running, Waiting };

    void RequestRun(int toolIndex);                // 单个工具执行（独立按钮）
    void RequestRunAll(bool loop = false, bool triggerCamera = true); // 全部执行
    void RequestRunTaskGroup(const std::string& groupName,
        bool loop = false, bool triggerCamera = true); // 仅执行指定任务；空名称表示未分组
    void ResumeRunAfterCamera(bool loop = false);  // 相机触发完成后保持原执行范围
    void RequestRepeatLastRun(bool loop = false, bool triggerCamera = true);
    void RequestStepNext();                        // 单步：执行下一个工具
    void RequestStepNextTaskGroup(const std::string& groupName); // 单步：执行指定任务中的下一个工具
    void RequestStepReset();                       // 单步：重置进度
    void Tick();                                   // 每帧调用

    Mode GetMode();
    int  GetCurrentIndex();                        // 批量/单步当前索引
    int  GetRunProgressCurrent();                  // 当前批次中的序号（1..N）
    int  GetRunProgressTotal();                    // 当前批次实际执行的工具数
    bool WasLastRunTaskGroup();
    const std::string& GetLastRunTaskGroupName();
    int  GetStepCursor();                          // 单步进度 (0=空闲, 1..N=已执行)
    int  GetStepTotal();                           // 当前单步范围内的工具数
    bool IsStepTaskGroup();                        // 当前单步范围是否为指定任务
    const std::string& GetStepTaskGroupName();     // 当前单步任务名称
    float GetTotalTimeMs();
    float GetElapsedTimeMs();
    float GetLastStepTimeMs();
    float GetToolTimeMs(int toolIndex);
    cv::Mat GetTaskResultImage(const std::string& groupName);
    std::uint64_t GetCompletedBatchSerial();          // 每次整链/循环轮次完成后递增
    std::uint64_t GetLastCompletedLoopRound();        // 最近一次完成事件对应的循环轮次，非循环为 0
    void SetRuntimeMode(bool enabled);              // 运行模式：减少批量执行时的 UI/日志干扰
    bool IsRuntimeMode();
    void SetLoopEnabled(bool enabled);
    bool IsLoopEnabled();
    void SetLoopIntervalMs(int milliseconds);
    int GetLoopIntervalMs();
    std::uint64_t GetLoopIteration();
    int GetLoopWaitRemainingMs();
    bool IsWaitingForNextLoop();
    bool IsParallelExecutionActive();
    float GetLastParallelWallTimeMs();
    void SetTaskParallelEnabled(bool enabled);
    bool IsTaskParallelEnabled();
    int GetTaskParallelLimit();

    // 工具删除、移动或整条配方替换后调用，清除所有与工具索引相关的运行时缓存。
    void OnToolChainChanged();
    // 单图或图片序列切换时调用，停止旧输入上的队列并清除结果/耗时缓存。
    void OnInputImageChanged();
    void Reset();                                  // 重置到 Idle
    void Shutdown();                               // 等待后台工具退出
}
