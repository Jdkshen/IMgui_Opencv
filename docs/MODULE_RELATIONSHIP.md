# 模块关系图

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
│ - DX12 初始化                                                         │
│ - ImGui 初始化                                                        │
│ - 每帧驱动 UI / 视频 / 推理 / 上传 / 渲染                             │
└───────────────┬──────────────────────────────┬─────────────────────────┘
                │                              │
                │ 驱动界面                     │ 驱动系统服务
                ▼                              ▼
┌──────────────────────────┐      ┌─────────────────────────────────────┐
│ UI 层                    │      │ Core / Service 层                  │
│ - DockSpaceHost          │      │ - DX12Context                      │
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
            │ - 队列/状态控制   │      │ - type 0-11、13 统一通路 │
            └──────────────────┘      └──────────┬───────────┘
                                                  │
                        ┌─────────────────────────┼────────────────────────┐
                        ▼                         ▼                        ▼
             ┌──────────────────┐     ┌─────────────────────┐   ┌──────────────────┐
             │ ITool 统一接口    │     │ OpenCV/工具算法      │   │ 深度学习推理      │
             │ - type 0-11、13  │     │ - ThresholdTool     │   │ - YOLODetector    │
             │ - Edge/Template  │     │ - TemplateMatch     │   │ - ONNX Runtime    │
             │ - Blob/Threshold │     │ - MorphologyTool    │   │ - DirectML/CUDA   │
             │ - YOLO/Contour   │     │ - ColorAnalyzer     │   └──────────────────┘
             │ - Shape/Line/... │     │ - OpenCV DNN YOLO   │
             └─────────┬────────┘     └──────────┬──────────┘
                       └───────────────┬──────────┘
                                       ▼
                             ┌────────────────────┐
                             │ ToolResult / 叠加层 │
                             │ → gContext.unified  │
                             └──────────┬─────────┘
                                        ▼
                             ┌────────────────────┐
                             │ ImageViewer        │
                             │ - 画图像           │
                             │ - 画 ROI           │
                             │ - 画检测结果       │
                             └──────────┬─────────┘
                                        ▼
                                 DX12 + ImGui 渲染
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
    │  AsyncImageLoader
    │      ▼
    │  gImage / DX12纹理上传
    │
    ├─ 打开视频 / 摄像头
    │      │
    │      ▼
    │  VideoCapture::Update
    │      ▼
    │  当前帧 -> gImage
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
   │ - TemplateMatch               │
   │ - Contour / Shape / Line      │
   │ - Morphology / Color          │
   │ - YOLODetector (ONNX Runtime) │
   └───────────────────────────────┘
           │
           ▼
      ToolResult → gContext.unifiedResults
           │
           ▼
      ImageViewer::Draw...
           │
           ▼
      ImGui + DX12 Present
```

---

## 3）项目当前最关键的中轴线

如果把这个项目压缩成一条主线，可以理解为：

```text
输入源（图片/视频/摄像头）
        ↓
图像状态（gImage / VisionContext）
        ↓
工具执行（ToolExecutor / ITool）
        ↓
结果表达（gContext.unifiedResults）
        ↓
界面显示（ImageViewer）
        ↓
DX12 渲染输出
```

这条中轴线就是项目最核心的运行骨架。

---

## 4）结果通路（已统一）

type 0-11、13 工具统一走一条结果通路；type 12 原图由 `ToolController` 直接恢复本轮原图：

```text
Tool -> ToolResult -> gContext.unifiedResults -> DrawUnifiedResults()
```

实时 YOLO 也发布到 `gContext.unifiedResults`，视频偏移和实时状态由 `RealtimeDetectionState` 管理。

旧容器 `g_ContourOverlays` / `g_ShapeContourOverlays` / `g_LineOverlays` 及 `g_UnifiedResults` 影子状态已删除。

---

## 4.1）执行耗时口径

`ToolController` 每帧调度一个工具，但批量执行的“总耗时”不再使用跨帧墙钟时间。当前口径为：

```text
全部执行总耗时 = 每个工具 ExecuteToolAt() 的执行耗时累加
上步耗时       = 最近一个工具的 ExecuteToolAt() 执行耗时
```

这意味着底部“总耗时”和工具行右侧耗时属于同一口径，只统计工具真实执行时间，不包含逐帧调度等待、UI 刷新或帧间隔。

---

## 5）按职责再拆一层的理解方式

### A. 壳层（Application Shell）
负责把程序跑起来。

- `Windows_imgui.cpp`
- `Core/DX12Context.*`
- `Renderer/FontManager.*`

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
| 程序入口 | `Windows_imgui.cpp` | 主循环、窗口、DX12、ImGui 初始化与逐帧驱动 |
| 公共头聚合 | `Windows_imgui.h` | 汇总主要模块头文件 |
| 统一上下文 | `Core/VisionContext.h` | 图像、ROI、模板和结果的统一容器 |
| 图像视图状态 | `Core/ImageViewState.h` | 缩放、平移、画布位置和网格设置 |
| 工具调度 | `Core/ToolController.h/.cpp` | 执行模式、队列、单步/批量控制 |
| 工具执行 | `Core/ToolExecutor.h/.cpp` | type 0-11、13 分发到 ITool，type 12 原图由 ToolController 特殊处理 |
| 图像显示 | `UI/ImageViewer.h/.cpp` | 图片/视频显示、缩放平移、叠加绘制 |
| 工具界面 | `UI/ToolsWindow.h/.cpp` | 参数编辑、工具实例、执行入口 |
| ROI 交互 | `UI/ROIManager.h/.cpp` | ROI 创建、选中、拖动、坐标转换 |
| 模板匹配 | `Algorithm/TemplateMatch.*` | 模板匹配、旋转、NMS、模板编辑 |
| YOLO 推理 | `Algorithm/YOLODetector.*` | ORT 模型加载、预处理、推理、后处理 |
| 统一工具接口 | `Algorithm/ITool.h/.cpp` | `Execute(ctx)`、`DrawUI()`、`Save/Load()` |
| 统一输出 | `Algorithm/ToolResult.h` | 检测框、线、点、文本、区域等统一结构 |
| 配方系统 | `Core/RecipeManager.*` | JSON 保存/恢复工具实例、参数和模板 |
| 视频与音频 | `Core/VideoCapture.*` / `Core/AudioPlayer.*` | 视频帧更新、播放控制、音视频同步 |
| 图片异步加载 | `Core/AsyncImageLoader.*` | 后台解码 + 主线程回调上传 |
| DX12 纹理上传 | `Core/OpenCVTest.*` | UploadToDX12 / FlushPendingRelease（独立函数） |
| 结果发布 | `Core/ResultPublisher.h` | 统一结果清理 |
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
gContext.unifiedResults → DrawUnifiedResults
    ↓
ImageViewer 可视化 + DX12 呈现
```

它已经不是单一算法 demo，而是一个正在逐步平台化的视觉工具台：

- 上层是交互式桌面 UI
- 中间是上下文、执行器、配方、日志、视频等系统能力
- 下层是 OpenCV 视觉算法与深度学习推理
- type 0-11、13 工具统一走 `VisionContext → ITool → ToolResult → DrawUnifiedResults` 结果通路

这也是理解整个项目结构最重要的一点。
