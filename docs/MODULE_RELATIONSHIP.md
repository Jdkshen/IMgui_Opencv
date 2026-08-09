# 模块关系图

> 文档同步日期：2026-08-09。DX12/DX11 双后端、工具范围、独立流程图窗口、工具图标资源、统一结果能力、整轮计时、任务独立输入和 PLC 单槽握手已按当前实现更新。


本文整理 `IMgui_Opencv` 当前代码结构中的核心模块关系，重点说明：

- 核心模块分层与职责
- 运行时数据流
- 当前架构的核心中轴线
- 结果通路说明
- 关键文件索引

> 说明：本图基于当前代码实际状态整理，强调“现在怎么工作”，而不是只描述理想目标架构。

---

## 1）总体模块关系图

```text
┌──────────────────────────── 应用壳 / 主循环 ────────────────────────────┐
│ Windows_imgui.cpp                                                     │
│ - Win32 窗口创建                                                      │
│ - GraphicsBackend 初始化（DX12 优先，DX11 回退）                      │
│ - ImGui 初始化                                                        │
│ - 每帧驱动 UI / 视频 / 推理 / 上传 / 渲染                             │
└───────────────┬──────────────────────────────┬─────────────────────────┘
                │                              │
                │ 驱动界面                     │ 驱动系统服务
                ▼                              ▼
┌──────────────────────────┐      ┌─────────────────────────────────────┐
│ UI 层                    │      │ Core / Service 层                  │
│ - DockSpaceHost          │      │ - GraphicsBackend / RenderBackend  │
│ - Sidebar                │      │ - AsyncImageLoader                 │
│ - ImageViewer            │      │ - VideoCapture / AudioPlayer       │
│ - ROIManager             │      │ - OpenFileDialog                   │
│ - ToolsWindow            │      │ - ThemeManager                     │
│ - LogWindow              │      │ - LogSystem                        │
│ - StatsWindow            │      │ - RecipeManager                    │
└──────────────┬───────────┘      └──────────────────┬──────────────────┘
               │                                      │
               └────────────────┬─────────────────────┘
                                ▼
                    ┌────────────────────────────┐
                    │ VisionContext              │
                    │ - image / originalImage    │
                    │ - rois / selectedROI       │
                    │ - frozenTemplate           │
                    │ - unifiedResults           │
                    │ - zoom / pan / canvas      │
                    └──────────────┬─────────────┘
                                   │
                      ┌────────────┴────────────┐
                      ▼                         ▼
            ┌──────────────────┐      ┌──────────────────────┐
            │ ToolController   │      │ ToolExecutor         │
            │ - 请求执行/单步   │─────▶│ - 按 type 分发工具    │
            │ - 批次/状态控制   │      │ - type 0-11、13-17 统一通路 │
            └──────────────────┘      └──────────┬───────────┘
                                                  │
                        ┌─────────────────────────┼────────────────────────┐
                        ▼                         ▼                        ▼
             ┌──────────────────┐     ┌─────────────────────┐   ┌──────────────────┐
             │ ITool 统一接口    │     │ OpenCV/工具算法      │   │ 深度学习推理      │
             │ - type 0-11、13-17 │     │ - ThresholdTool     │   │ - YOLODetector    │
             │ - Edge/Template  │     │ - TemplateMatching  │   │ - ONNX Runtime    │
             │ - Blob/Threshold │     │ - MorphologyTool    │   │ - DirectML/CUDA   │
             │ - YOLO/Contour   │     │ - ColorAnalyzer     │   └──────────────────┘
             │ - Shape/Line/... │     │ - OpenCV DNN YOLO   │
             └─────────┬────────┘     └──────────┬──────────┘
                       └───────────────┬──────────┘
                                       ▼
                             ┌──────────────────────────────┐
                             │ ToolResult / ResultPublisher │
                             │ → gContext.unifiedResults    │
                             └──────────────┬───────────────┘
                                         ▼
                             ┌──────────────────────────────┐
                             │ ResultOverlayState /         │
                             │ ImageViewer                  │
                             │ - 画图像 / ROI / 检测结果    │
                             └──────────────┬───────────────┘
                                        ▼
                       当前图形后端 + ImGui 渲染
```

---

## 2）运行时数据流图

这个视角更适合理解“数据到底怎么流动”。

```text
[用户操作]
    │
    ├─ 选择图片 / 文件夹
    │      │
    │      ▼
    │  OpenFileDialog
    │      ▼
    │  ImageImportService / AsyncImageLoader
    │      ▼
    │  ImageState / GraphicsBackend 纹理上传
    │
    ├─ 打开视频 / 摄像头
    │      │
    │      ▼
    │  VideoCapture::Update
    │      ▼
    │  当前帧 -> ImageState
    │
    └─ 在 UI 中设置 ROI / 工具参数 / 执行模式
           │
           ▼
      ToolsWindow / ROIManager
           │
           ▼
      ToolController / ToolExecutor
           │
           ▼
      VisionContext
           │
           ▼
   ┌───────────────────────────────┐
   │ 算法层                         │
   │ - OpenCV 传统视觉              │
   │ - TemplateMatchingTool        │
   │ - Contour / Shape / Line      │
   │ - Morphology / Color          │
   │ - YOLODetector (ONNX Runtime) │
   └───────────────────────────────┘
           │
           ▼
      ToolResult → ResultPublisher / ToolExecutor::PublishDetached
           │
           ▼
      gContext.unifiedResults → ResultOverlayState
           │
           ▼
      ImageViewer::DrawUnifiedResults()
           │
           ▼
      ImGui + 当前图形后端 Present
```

PLC 触发在进入 `ToolController` 前还经过硬件握手调度：

```text
PLC / 模拟器
  -> ModbusTcpAdapter（FC01 输入、FC05 输出）
  -> HardwareRuntimeService 后台轮询
  -> 单槽 Trigger 接收（Busy/等待 ACK 时忽略新 Trigger）
  -> UI 主线程 Tick 请求指定任务
  -> 在线相机 → 任务文件夹 → 任务单图 → 公共图片
  -> ToolController / ToolExecutor
  -> Busy OFF + Done + OK/NG/Error
  -> PLC ACK 上升沿
  -> 清除结果并允许下一轮
```

这里的“单槽”表示一个握手周期最多只有一个已接收请求；它与“执行全部”内部最多 4 个任务的并行调度是两个独立概念。

---

## 3）项目当前最关键的中轴线

如果把这个项目压缩成一条主线，可以理解为：

```text
输入源（图片/视频/摄像头）
        ↓
图像状态（ImageState，并同步到 VisionContext）
        ↓
工具执行（ToolExecutor / ITool）
        ↓
结果发布（ResultPublisher / ToolExecutor::PublishDetached）
        ↓
结果状态（gContext.unifiedResults / ResultOverlayState）
        ↓
界面显示（ImageViewer）
        ↓
GraphicsBackend 渲染输出（DX12 或 DX11）
```

这条中轴线就是项目最核心的运行骨架。

---

## 4）结果通路（已统一）

type 0-11、13-17 工具统一走一条结果通路；type 12 原图由 `ToolController` 直接恢复本轮原图：

```text
Tool -> ToolResult -> ResultPublisher / ToolExecutor::PublishDetached
     -> gContext.unifiedResults -> ResultOverlayState
     -> ImageViewer::DrawUnifiedResults()
```

实时 YOLO 也发布到 `gContext.unifiedResults`，视频偏移和实时状态由 `RealtimeDetectionState` 管理。

旧容器 `g_ContourOverlays` / `g_ShapeContourOverlays` / `g_LineOverlays` 及 `g_UnifiedResults` 影子状态已删除。

---

## 4.1）执行耗时口径

`ToolController` 可按帧调度工具，也可并行等待多个任务。当前分别记录三个层次的耗时：

```text
工具行 / 上步耗时 = 单个工具 ExecuteToolAt() 的执行成本
任务详情耗时      = 该任务内工具执行耗时的汇总
本轮总耗时        = 从整轮开始到完成的墙钟时间（含逐帧调度和并行等待）
```

这三个口径分别用于观察单工具成本、单任务成本和用户实际等待时间，不能互相替代。

---

## 5）按职责再拆一层的理解方式

### A. 壳层（Application Shell）
负责把程序跑起来。

- `Windows_imgui.cpp`
- `Core/RenderBackend.h`
- `Core/GraphicsBackend.*`
- `Core/DX12Backend.*`
- `Core/DX12Context.*`
- `Core/DX11Context.*`
- `Renderer/FontManager.*`
- `Renderer/PreviewTextureCache.*`

### B. 交互层（UI Interaction）
负责用户怎么操作、怎么看结果。

- `UI/ImageViewer.*`
- `UI/ToolsWindow.*`
- `UI/ROIManager.*`
- `UI/DockSpaceHost.*`
- `UI/Sidebar.*`

### C. 调度层（Execution Control）
负责“什么时候执行哪个工具”。

- `Core/ToolController.*`
- `Core/ToolExecutor.*`

### D. 领域层（Vision Algorithms）
负责“视觉处理本身”。

- `Algorithm/*`

### E. 状态与持久化层（State / Persistence）
负责上下文、参数、配方、日志。

- `Core/VisionContext.*`
- `Core/RecipeManager.*`
- `Log/LogSystem.*`
- `Core/ThemeManager.*`

---

## 6）关键文件索引

| 模块 | 关键文件 | 说明 |
|------|----------|------|
| 程序入口 | `Windows_imgui.cpp` | 主循环、窗口、统一图形后端、ImGui 初始化与逐帧驱动 |
| 后端契约 | `Core/RenderBackend.h` | DX11/DX12 一致的初始化、渲染、Resize、纹理和释放接口 |
| 后端选择 | `Core/GraphicsBackend.*` | DX12 优先、DX11 自动回退、统一主纹理和诊断信息 |
| DX12 实现 | `Core/DX12Backend.*` / `Core/DX12Context.*` | 公共契约适配与完整保留的 DX12 实现 |
| DX11 实现 | `Core/DX11Context.*` | 独立 D3D11 设备、交换链、ImGui 和纹理实现 |
| 公共头聚合 | `Windows_imgui.h` | 汇总主要模块头文件 |
| 统一上下文 | `Core/VisionContext.h` | 图像、ROI、模板和结果的统一容器 |
| 图像状态 | `Core/ImageState.*` | 当前图、原图、版本和 GPU 上传请求 |
| 任务状态 | `Core/ToolChainState.*` | 工具实例、任务定义、顺序、启用和独立输入 |
| 图像视图状态 | `Core/ImageViewState.h` | 缩放、平移、画布位置和网格设置 |
| 工具调度 | `Core/ToolController.h/.cpp` | 全部/当前任务/单步/循环、任务输入和最多 4 任务并行 |
| 工具执行 | `Core/ToolExecutor.h/.cpp` | type 0-11、13-17 分发到 ITool，type 12 原图由 ToolController 特殊处理 |
| 图像显示 | `UI/ImageViewer.h/.cpp` | 图片/视频显示、缩放平移、叠加绘制 |
| 工具界面 | `UI/ToolsWindow.h/.cpp` | 参数编辑、任务管理、输入配置、按宽度自动换行的独立流程图窗口和执行入口 |
| 工具图标 | `Renderer/FontManager.*` / `assets/icons/` | 把工具 PNG 合并进 ImGui 图集，缺失时由 ToolsWindow 回退绘制 |
| 结果总览 | `UI/RunResultWindow.*` | 任务卡片、详情、结果图、缩放/最大化和整轮耗时 |
| 结果布局 | `UI/RunResultLayout.*` | 无状态窗口尺寸、任务网格与标签避让策略 |
| ROI 交互 | `UI/ROIManager.h/.cpp` | ROI 创建、选中、拖动、坐标转换 |
| 模板匹配 | `Algorithm/TemplateMatchingTool.*` | 模板预处理、旋转搜索、NMS 和结果发布 |
| YOLO 推理 | `Algorithm/YOLODetector.*` | ORT 模型加载、预处理、推理、后处理 |
| 统一工具接口 | `Algorithm/ITool.h/.cpp` | `Execute(ctx)`、`DrawUI()`、`Save/Load()` |
| 统一输出 | `Algorithm/ToolResult.h` | 检测框、线、点、文本、区域等统一结构 |
| 输出能力表 | `Core/ToolResultCapabilities.h` | 定义 type 0-17 可发布的结果通道，供 UI 来源过滤和工具链校验共用 |
| 配方系统 | `Core/RecipeManager.*` | version 5 JSON 保存/恢复任务、工具实例、参数和资产，并兼容旧版本 |
| 视频与音频 | `Core/VideoCapture.*` / `Core/AudioPlayer.*` | 视频帧更新、播放控制、音视频同步 |
| 图片异步加载 | `Core/AsyncImageLoader.*` | 后台解码 + 主线程回调上传 |
| 通用纹理缓存 | `Renderer/PreviewTextureCache.*` | 使用 `RenderTextureHandle` 上传、查询和释放主图/预览/结果纹理 |
| 结果发布 | `Core/ResultPublisher.h` / `Core/ResultOverlayState.*` | 统一结果清理、发布和显示查询 |
| 下游空间结果 | `Core/ResultROIResolver.*` / `Core/FixtureTransform.*` | 把区域、检测框、文本框和线段统一转换为结果 ROI 或定位位姿；支持同源/跨源 A、B 结果组合，点点测量可请求中心点几何 |
| 日志系统 | `Log/LogSystem.*` | 线程安全日志、颜色分级、数量控制 |

---

## 7）总结

这个项目当前可以概括为：

```text
Windows GUI 壳
    ↓
ImGui 交互层
    ↓
Core 调度与状态管理
    ↓
OpenCV / ONNX Runtime 算法层
    ↓
ResultPublisher / ToolExecutor::PublishDetached
    ↓
gContext.unifiedResults → ResultOverlayState
    ↓
ImageViewer::DrawUnifiedResults
    ↓
ImageViewer 可视化 + GraphicsBackend 呈现
```

它已经不是单一算法 demo，而是一个正在逐步平台化的视觉工具台：

- 上层是交互式桌面 UI
- 中间是上下文、执行器、配方、日志、视频等系统能力
- 下层是 OpenCV 视觉算法与深度学习推理
- type 0-11、13-17 工具统一走 `VisionContext → ITool → ToolResult → ResultPublisher → ResultOverlayState → ImageViewer` 结果通路

这也是理解整个项目结构最重要的一点。
