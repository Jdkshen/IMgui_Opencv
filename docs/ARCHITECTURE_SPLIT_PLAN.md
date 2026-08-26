# 大文件渐进拆分边界

> 文档同步日期：2026-08-25。应用入口、工作流窗口、任务组窗口、基础/检测/测量工具面板、硬件窗口壳和硬件纯策略已完成拆分；多相机运行时、视频检测、启动诊断和 DPI 调整继续遵循 Core/UI 边界。

拆分遵循“先提取无状态服务，再迁移状态所有权，最后缩减兼容入口”的顺序；
每一批必须保持完整回归通过，避免同时重写 UI、执行状态机和硬件线程。

| 原文件 | 第一目标模块 | 拥有的职责 | 迁移完成标准 |
| --- | --- | --- | --- |
| `UI/RunResultWindow.cpp` | `Core/UiPreferencesService.*` | 结果窗口偏好路径、加载、原子保存 | 已完成 |
| `UI/RunResultWindow.cpp` | `UI/RunResultLayout.*` | 多任务结果窗口排列、尺寸计算、标签避让 | 已完成；固定分辨率与标签碰撞已纳入无渲染回归 |
| `UI/RunResultWindow.cpp` | `UI/RunResultPresentation.*` | 状态文案/颜色、工具显示名、结果摘要和详情格式化 | 已完成；窗口保留快照、纹理和交互状态 |
| `UI/RunResultWindow.cpp` | `UI/RunResultSnapshot.*` | 批次/任务快照构建、结果聚合、失败原因和任务集合 | 已完成；窗口只持有已构建快照并负责交互状态 |
| `UI/RunResultWindow.cpp` | `UI/RunResultOverlayRenderer.*` | 检测框、区域、OCR、直线及避让标签覆盖绘制 | 已完成；覆盖层渲染复用独立快照模型与布局策略 |
| `UI/ToolsWindow.cpp` | `UI/Tools/BasicToolPanels.*`、`DetectionToolPanels.*`、`AdvancedDetectionToolPanels.*`、`MeasurementToolPanel.*` | 工具类型到面板绘制函数的注册与分派 | 已完成；type 0-17 的全部参数面板均由注册模块持有，主窗口只组装公共卡片上下文 |
| `UI/ToolsWindow.cpp` | `UI/WorkflowWindow.*` | 独立流程图窗口、节点布局与依赖连线 | 已完成；主窗口只提供当前筛选后的工具索引和标题 |
| `UI/ToolsWindow.cpp` | `UI/TaskGroupWindow.*` | 任务列表、工具分配、任务输入绑定、筛选和窗口状态 | 已完成；主窗口通过窄接口读取当前任务并打开管理窗口 |
| `UI/HardwareWindow.cpp` | `UI/HardwarePanel.cpp` | 全屏窗口导航与硬件配置面板分层 | 已完成第一层；窗口文件只保留显示、导航和页面分派，设备表单状态集中于面板编译单元 |
| `Core/ToolController.cpp` | `Core/ExecutionStateMachine.*` | Idle/Running/Paused/Step 状态转换、批次游标 | 状态转换可用纯事件序列回归测试 |
| `Core/ToolController.cpp` | `Core/ToolRunPolicy.*`、`Core/TaskImageProvider.*` | 执行顺序、异步工具分类、跨任务依赖、任务图片读取与文件夹轮换 | 已完成；调度器保留运行状态和相机帧选择 |
| `Core/HardwareRuntimeService.cpp` | `Core/HardwareRuntimePolicy.*` | 握手配置校验、二维码载荷和结果聚合 | 已完成纯逻辑首拆；线程状态机后续继续迁往独立运行控制器 |
| `Core/HardwareRuntimeService.cpp` | `Core/HardwareHandshakePlan.*` | 输出映射筛选、Start/Complete/Reset 信号序列和 ACK 配置判断 | 已完成无状态计划层；超时、重连和请求编号仍留在运行服务 |
| `Core/HardwareRuntimeService.cpp` | `Core/HardwareCameraPolicy.*` | 帧方向变换、触发/PTP/缓存能力校验 | 已完成；相机线程、重连和帧队列仍由运行服务持有 |
| `Core/HardwareRuntimeService.cpp` | `Core/CameraRuntime.*` | 相机工作线程、重连、帧队列和元数据 | 多相机任务绑定与断线测试独立通过 |
| `Test/regression_tests.cpp` | `Test/RegressionGeometryTests.*`、`Test/RegressionPolicyTests.*` | 工业几何/标定及渲染/布局/像素格式纯策略回归 | 已完成；命令行入口保持兼容，测试实现按领域独立编译 |

每次迁移限制在一个状态所有者内；旧入口保留薄转发层，调用方迁移完成后再删除。
