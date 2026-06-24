#pragma once
#include <queue>
#include <chrono>

// =====================================================
// ToolController — 工具执行调度器
// 替代旧 ExecState 状态机，用 queue 驱动
// 由 ShowToolsWindow 的 UI 按钮触发，每帧 Tick 消费
// =====================================================
namespace ToolController
{
    enum class Mode { Idle, Running, Waiting };

    void RequestRun(int toolIndex);                // 单个工具执行（独立按钮）
    void RequestRunAll(bool loop = false);         // 全部执行
    void RequestStepNext();                        // 单步：执行下一个工具
    void RequestStepReset();                       // 单步：重置进度
    void Tick();                                   // 每帧调用

    Mode GetMode();
    int  GetCurrentIndex();                        // 批量/单步当前索引
    int  GetStepCursor();                          // 单步进度 (0=空闲, 1..N=已执行)
    float GetTotalTimeMs();
    float GetElapsedTimeMs();
    float GetLastStepTimeMs();
    float GetToolTimeMs(int toolIndex);
    void SetRuntimeMode(bool enabled);              // 运行模式：减少批量执行时的 UI/日志干扰
    bool IsRuntimeMode();

    void Reset();                                  // 重置到 Idle
}
