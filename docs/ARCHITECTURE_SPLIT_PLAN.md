# 大文件渐进拆分边界

> 文档同步日期：2026-08-09。旧 `FrameRenderer`、`OpenCVTest` 和 `TemplateMatch` 实现已清理；界面调整继续集中在 `ToolsWindow`/公共工具表单，后续拆分仍遵循本文边界。

拆分遵循“先提取无状态服务，再迁移状态所有权，最后缩减兼容入口”的顺序；
每一批必须保持完整回归通过，避免同时重写 UI、执行状态机和硬件线程。

| 原文件 | 第一目标模块 | 拥有的职责 | 迁移完成标准 |
| --- | --- | --- | --- |
| `UI/RunResultWindow.cpp` | `Core/UiPreferencesService.*` | 结果窗口偏好路径、加载、原子保存 | 已完成 |
| `UI/RunResultWindow.cpp` | `UI/RunResultLayout.*` | 多任务结果窗口排列、尺寸计算、标签避让 | 已完成；固定分辨率与标签碰撞已纳入无渲染回归 |
| `UI/ToolsWindow.cpp` | `UI/ToolPanelRegistry.*` | 工具类型到面板绘制函数的注册与分派 | 主窗口只保留注册调用和公共上下文 |
| `Core/ToolController.cpp` | `Core/ExecutionStateMachine.*` | Idle/Running/Paused/Step 状态转换、批次游标 | 状态转换可用纯事件序列回归测试 |
| `Core/HardwareRuntimeService.cpp` | `Core/PlcHandshakeController.*` | Trigger/Busy/Done/ACK、超时、重连和请求编号 | PLC 回归仅依赖新控制器，运行服务只做 IO 调度 |
| `Core/HardwareRuntimeService.cpp` | `Core/CameraRuntime.*` | 相机工作线程、重连、帧队列和元数据 | 多相机任务绑定与断线测试独立通过 |

每次迁移限制在一个状态所有者内；旧入口保留薄转发层，调用方迁移完成后再删除。
