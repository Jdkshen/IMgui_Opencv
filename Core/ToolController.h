#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <opencv2/core/mat.hpp>

// =====================================================
// ToolController — 工具执行调度器
// 替代旧 ExecState 状态机，统一管理单工具、批次、单步和任务调度请求。
// 由 UI 或硬件运行服务发起请求，每帧 Tick 在主线程推进。
// =====================================================
namespace ToolController
{
    void RequestForceRun(int toolIndex);
    void RequestForceRunAll(bool loop = false, bool triggerCamera = true);
    enum class Mode { Idle, Running, Waiting };

    void RequestRun(int toolIndex);                // 单个工具执行（独立按钮）
    void RequestRunAll(bool loop = false, bool triggerCamera = true); // 全部执行
    void RequestRunTaskGroup(const std::string& groupName,
        bool loop = false, bool triggerCamera = true,
        bool forceCameraCapture = false); // 仅执行指定任务；PLC 触发可强制先抓帧
    void ResumeRunAfterCamera(bool loop = false,
        bool cameraFrameAvailable = true);          // 相机触发完成后保持原执行范围；失败时走任务回退输入
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
    int  GetStepToolIndex();                       // 最近一次单步对应的全局工具索引
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
