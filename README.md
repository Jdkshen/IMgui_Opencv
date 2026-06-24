# IMgui_Opencv

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具应用。

---

## 📁 项目结构

```
IMgui_Opencv/
├── assets/                     ← 资源文件（编译时自动复制到输出目录）
│   ├── fonts/simsun.ttc        ←   宋体
│   ├── icons/                  ←   添加工具弹窗 PNG 图标（24x24，启动时合并进 ImGui 图集）
│   └── images/                 ←   测试图片
│
├── Core/                       ← 核心模块
│   ├── DX12Context.cpp/h       ←   DX12 设备管理 + 全局状态
│   ├── OpenCVTest.cpp/h        ←   图片读取 + GPU 纹理上传
│   ├── AsyncImageLoader.cpp/h  ←   异步图片加载（后台线程 + 回调）
│   ├── ImageLoadController.cpp/h ← 图片加载调度控制器
│   ├── OpenFileDialog.cpp/h    ←   文件/文件夹选择对话框
│   ├── VideoCapture.cpp/h      ←   视频/摄像头播放（cv::VideoCapture）
│   ├── AudioPlayer.cpp/h       ←   XAudio2 + Media Foundation 音频播放
│   ├── ThemeManager.cpp/h      ←   主题切换（夜间/白天）+ theme.cfg 持久化
│   ├── RecipeManager.cpp/h     ←   配方保存/加载（JSON，支持工具实例序列化）
│   ├── ImageState.cpp/h        ←   当前图/原图状态收口
│   ├── FrameSourceState.cpp/h  ←   单图/图片序列/视频/摄像头帧状态
│   ├── FrameNavigation.cpp/h   ←   图片序列与帧切换
│   ├── LiveYoloRunner.cpp/h    ←   实时 YOLO 推理调度
│   ├── VisionContext.h/cpp     ←   统一视觉上下文（image/ROI/模板/结果/视图状态）
│   ├── ROI.h / ROIState.cpp/h  ←   ROI 数据结构与当前 ROI 状态
│   ├── ToolInstance.h          ←   工具实例参数聚合
│   ├── ToolTypes.h             ←   工具类型常量/名称
│   ├── ToolChainState.cpp/h    ←   工具链状态管理
│   ├── ToolExecutor.h/cpp      ←   统一工具执行器（参数同步、ROI 注入、执行、发布）
│   ├── ToolController.h/cpp    ←   工具调度器（全部/单步/循环/运行模式）
│   ├── FrameRenderer.cpp/h     ←   每帧渲染提交
│   ├── ResultPublisher.h       ←   ToolResult 统一发布入口
│   └── LegacyAppState.h / UIStateBridge.h ← 旧全局状态过渡桥
│
├── UI/                         ← 界面模块
│   ├── AppTitleBar.cpp/h       ←   原生标题栏颜色同步/标题区辅助
│   ├── DockSpaceHost.cpp/h     ←   主停靠空间 + 菜单栏
│   ├── ImageViewer.cpp/h       ←   图片预览 + 缩放平移 + 文件夹浏览 + 视频控制                                                                                                 
│   ├── LogWindow.cpp/h         ←   日志窗口（ImGuiListClipper 虚拟滚动）
│   ├── Sidebar.cpp/h           ←   侧边栏控制面板
│   ├── StatsWindow.cpp/h       ←   性能统计窗口
│   ├── ToolsWindow.cpp/h       ←   工具窗口（枚举状态机：全部执行/单步/循环）
│   └── ROIManager.cpp/h        ←   ROI 数据结构 + 交互 + 坐标转换
│
├── Algorithm/                  ← 图像算法
│   ├── ITool.h / ITool.cpp     ←   工具接口（抽象基类 + ToolRegistry 工厂）
│   ├── ToolResult.h            ←   统一输出结构（measurements/regions/detections/lines/texts/debugImage）
│   ├── ToolImageUtils.cpp/h    ←   工具输入图像、ROI 裁剪辅助
│   ├── FrameSource.h           ←   单图/序列/视频/摄像头统一帧包
│   ├── EdgeTool.cpp/h          ←   边缘检测工具（实现 ITool）
│   ├── ThresholdTool.cpp/h     ←   图像处理管线（灰度/模糊/Canny/二值化）
│   ├── ThresholdITool.cpp      ←   阈值工具 ITool 适配
│   ├── BlobTool.cpp/h          ←   Blob 分析工具（实现 ITool）
│   ├── YOLOTool.h/cpp          ←   YOLO 工具（实现 ITool）
│   ├── OpenCVYoloDetector.cpp/h ←   OpenCV DNN YOLO 推理后端
│   ├── MultiColorFinder.h/cpp  ←   多点找色工具（实现 ITool）
│   ├── YOLODetector.cpp/h      ←   YOLO 目标检测（ONNX Runtime 推理）
│   ├── TemplateMatch.cpp/h     ←   模板匹配（多方法/旋转/NMS）
│   ├── ContourDetector.cpp/h   ←   轮廓分析（凸包/圆度/近似）
│   ├── ShapeMatcher.cpp/h      ←   形状匹配（matchTemplate+轮廓比对）
│   ├── ShapeTools.cpp/h        ←   形状匹配工具适配（实现 ITool）
│   ├── LineDetector.cpp/h      ←   直线检测（Canny+HoughLinesP）
│   ├── MorphologyTool.cpp/h    ←   形态学工具（7 种运算）
│   └── ColorAnalyzer.cpp/h     ←   颜色分析（多色域 + 直方图）
│
├── Renderer/                   ← 渲染模块
│   └── FontManager.cpp/h       ←   中文字体加载 + 工具 PNG 图标合并到 ImGui 图集
│
├── Log/                        ← 日志系统
│   └── LogSystem.cpp/h         ←   线程安全日志（shared_ptr COW + displayText 预格式化）
│
├── include/                    ← 第三方库
│   ├── imgui/                  ←   Dear ImGui 1.92.8（含 .cpp 源文件）
│   ├── directx/                ←   DX12 辅助头文件（d3dx12.h 等）
│   ├── opencv/                 ←   OpenCV 头文件
│   ├── onnxruntime/            ←   ONNX Runtime C++ API
│   ├── nlohmann/               ←   JSON 库
│   └── dxguids/                ←   DX GUID 定义
│
├── docs/                       ← 项目文档
│   ├── ALGORITHMS.md           ←   OpenCV 算法详解 + 添加新算法指南
│   ├── BUILD.md                ←   编译构建说明
│   ├── CODE_ANALYSIS.md        ←   代码架构分析
│   ├── IMGUI_API.md            ←   Dear ImGui C++ API 完整参考
│   ├── CODE_STRUCTURE.md       ←   当前代码结构梳理
│   ├── TASK_EXECUTION.md       ←   任务执行模板和验收清单
│   ├── PROJECT_UPDATE_GUIDE.md ←   后续项目更新同步指南
│   ├── PERFORMANCE_REVIEW.md   ←   架构性能审查
│   ├── OPENCV5_EXPERIMENT.md   ←   OpenCV 5.0 YOLO 实验后端说明
│   ├── ROADMAP.md              ←   后续开发方向
│   └── VIDEO_AUDIO.md          ←   视频/音频模块说明
│
├── redist/                     ← 运行时 DLL
├── models/                     ← 预训练模型
│   └── yolo11n.onnx            ←   YOLO11 Nano ONNX 模型
│
├── Windows_imgui.cpp           ← 程序入口 + 主循环
├── Windows_imgui.h             ← 公共头文件汇总
├── framework.h                 ← 系统头文件
├── README.md                   ← 本文件
└── Windows_imgui.slnx          ← VS2022 解决方案
```

## 📐 系统架构

```
┌─────────────────────────────────┐
│           UI Layer              │
│   ImGui / DockSpace / 交互      │
└───────────────┬─────────────────┘
                │
┌───────────────▼─────────────────┐
│         Core Layer              │
│   ROI / Recipe / 图片加载       │
└───────────────┬─────────────────┘
                │
┌───────────────▼─────────────────┐
│       Tool Framework (ITool)     │
│   YOLOTool / ContourTool /       │
│   ShapeTool / LineTool / MCF     │
└───────────────┬─────────────────┘
                │
┌───────────────▼─────────────────┐
│       Algorithm Layer           │
│   OpenCV / ONNX Runtime         │
└───────────────┬─────────────────┘
                │
┌───────────────▼─────────────────┐
│        DX12 Renderer            │
└─────────────────────────────────┘
```

## 🏗️ 架构设计

本项目采用「**UI → Core → Algorithm**」三层架构：

```
UI                          Core                        Algorithm
─────────────────────────────────────────────────────────────────
AppTitleBar                 DX12Context                 ITool / ToolResult
DockSpace                   OpenCVTest                  TemplateMatch
Sidebar                     AsyncImageLoader            YOLODetector
ImageViewer                 ImageLoadController         OpenCVYoloDetector
ToolsWindow                 RecipeManager               ContourDetector
ROIManager                  ImageState                  ShapeMatcher
LogWindow                   VisionContext               LineDetector
StatsWindow                 ROI / ROIState              ThresholdTool
                            ToolInstance / ToolTypes    BlobTool
                            ToolChainState              MorphologyTool
                            ToolExecutor                ColorAnalyzer
                            ToolController              MultiColorFinder
                            FrameSourceState
                            FrameNavigation
                            LiveYoloRunner
                            FrameRenderer
                            VideoCapture / AudioPlayer
                            ThemeManager
```

**设计原则**：UI 不直接调算法，算法不依赖 ImGui，Core 调度资源，日志全局独立。

### 执行架构（重构后）

```
ShowToolsWindow (工具实例 UI，执行逻辑已拆到 Core)
    │
    ├─ ToolController::Tick()     ← 每帧消费执行队列
    │
    └─ ToolExecutor::Execute()    ← 统一入口 (switch type 0-12)
         ├─ RunViaITool()         types 0-11 (边缘/模板/Blob/阈值/YOLO/轮廓/形状/直线/形态学/颜色/多点找色/OpenCV5 YOLO)
         └─ ToolController        type 12 (原图工具，恢复本轮原图)
```

### 核心数据流

```
图片/视频 → ImageSource → VisionContext → ITool::Execute(ctx) → ToolResult → DrawUnifiedResults()
                                │                    │
                    ┌───────────┼──────────┐  ┌──────┼──────┐
                    │ image     │ rois     │  │ detections │ lines │
                    │ template  │ zoom/pan │  │ regions    │ texts │
                    └───────────┴──────────┘  └──────┴───────┘
```

---

## 🏗️ 主流程

```
wWinMain()
  ├── DPI 感知设置
  ├── 创建 Win32 窗口
  ├── DX12 设备初始化（Device/SwapChain/CommandQueue/Fence）
  ├── 初始化 Dear ImGui（Docking + Viewports + DX12 后端）
  ├── 加载中文字体（simhei.ttf → 系统微软雅黑 → 默认）
  │
  └── 主循环
        ├── PeekMessage 消息处理
        ├── 窗口遮挡检测
        ├── 视频帧更新（VideoCapture::Update + AudioPlayer 同步）
        ├── ImGui 新帧
        ├── UI 绘制（DockSpace/侧边栏/日志/图像/工具/阈值/模板匹配）
        ├── 图片加载调度（OpenCV → GPU 纹理上传）
        ├── DX12 渲染管线
        │     ├── Barrier: PRESENT → RENDER_TARGET
        │     ├── Clear + RenderDrawData
        │     ├── Barrier: RENDER_TARGET → PRESENT
        │     └── Present
        └── 清理 → 退出
```

## 🎯 功能特性

| 模块 | 功能 |
|------|------|
| 图片加载 | 支持 JPG/PNG/BMP，文件对话框 + 文件夹浏览（上/下张切换）+ 异步加载 |
| 视频播放 | 打开视频文件 / 摄像头，播放/暂停/停止/循环，帧滑动条跳转，FPS 显示 |
| 音频播放 | XAudio2 + Media Foundation，与视频同步播放/暂停/跳转 |
| 图像处理 | 灰度化、高斯模糊、二值化、Canny 边缘检测 |
| ROI 管理 | 交互式创建/选中/拖动/缩放/删除，5 种几何类型（矩形/点/线段/圆/多边形）按类型着色 |
| 模板匹配 | 多实例模板匹配，旋转/NMS/阈值，结果可视化 |
| YOLO 检测 | ONNX Runtime 推理 YOLO11 ONNX 模型，支持 ROI 限定区域，NMS 后处理 |
| 轮廓分析 | findContours + 凸包/圆度/多边形近似，可选 ROI matchShapes 轮廓比对 |
| 形状匹配 | matchTemplate 定位 + Hu矩/ShapeContext/Hausdorff 轮廓比对，红绿着色 |
| 直线检测 | Canny + HoughLinesP + 角度/长度过滤 + ROI 限定 |
| 形态学 | 腐蚀/膨胀/开/闭/梯度/顶帽/黑帽 7种操作 |
| 颜色分析 | BGR/HSV/Lab/YCbCr 色域切换 + 直方图统计 |
| 多点找色 | 参考图 ROI 捕获 + 点击取色 + 容差匹配 + 部分匹配反馈 |
| 工具实例 | 手风琴式工具面板，每实例独立参数（模板/ROI/角度/预处理），支持 12 种工具 |
| 添加工具图标 | 添加工具弹窗支持 PNG 图片图标，资源位于 `assets/icons/`，加载失败时回退到内置绘制图标 |
| 批量执行 | 全部执行（逐帧高亮）+ 单步执行（点击推进）+ 循环模式 |
| 工具链输入 | 每个工具可选择“上一步原图”“上一步处理图”或“原图工具输出”，也可插入“原图”工具重置链路 |
| 运行模式 | 批量执行时可关闭逐工具调度日志和工具行高亮，降低 UI/日志干扰；当前仍保留算法日志和图像上传 |
| ROI 类型 | 矩形(绿) / 点(金黄) / 线段(青) / 圆(橙) / 多边形(紫)，按类型着色+可视化 |
| 配方系统 | 保存/加载全部工具实例参数、模板图片、搜索ROI（JSON） |
| 主题切换 | 夜间/白天模式，自动持久化 |
| 日志系统 | 三级日志（INFO/WARN/ERROR），颜色分级，2000 条上限 |

---

## 🔧 工具实例系统

功能窗口（手风琴布局）支持 12 种工具类型，其中 type 0-11 已接入 ITool 统一接口；type 12 为原图重置特殊工具：

| 类型 | 功能 | 独立参数 | ITool |
|------|------|---------|-------|
| 原图 | 恢复本轮开始时缓存的原图，阻断前面处理结果继续累加 | — | 特殊工具 |
| 边缘检测 | Canny 边缘检测 | 低/高阈值、灰度开关 | ✅ EdgeTool |
| 模板匹配 | 多实例模板匹配 | 模板图、搜索ROI、旋转角度、匹配阈值/NMS、模板预处理、图像预处理，结果发布到统一叠加层 | ✅ TemplateMatchITool |
| Blob分析 | 斑点分析 | 面积范围 | ✅ BlobTool |
| 阈值调试 | 图像处理管线 | 灰度/模糊/二值化/Canny 全套 | ✅ ThresholdITool |
| YOLO检测 | ONNX 目标检测 | 模型文件、类别文件、置信度/NMS阈值、ROI限定 | ✅ YOLOTool |
| 轮廓分析 | findContours + 凸包/圆度 | 阈值模式、面积过滤、多边形近似 | ✅ ContourTool |
| 形状匹配 | Hu矩/ShapeContext | 模板图、匹配阈值、模板预处理 | ✅ ShapeTool |
| 直线检测 | Canny+HoughLinesP | 低/高Canny阈值、长度/角度过滤 | ✅ LineTool |
| 形态学 | 腐蚀/膨胀/开/闭等 7 种 | 核大小、形状、迭代次数 | ✅ MorphologyITool |
| 颜色分析 | BGR/HSV/Lab/YCbCr | 色域切换、直方图 | ✅ ColorAnalyzerITool |
| 多点找色 | 多颜色点同时匹配 | 参考图、锚点、ROI、容差、最大结果数 | ✅ MultiColorFinder |

> **ITool 接口**：统一 `Execute(VisionContext& ctx) → ToolResult`，结果通过 `DrawUnifiedResults()` 在图像上叠加绘制。

每个实例的参数完全独立，互不影响。模板图像和搜索 ROI 按实例保存。
每个实例还可以独立选择输入来源：`上一步原图` 使用上一个工具执行前的图，`上一步处理图` 使用上一个工具执行后的图，`原图工具输出` 使用最近一次 `原图` 工具恢复出的图片（未执行原图工具时回退到本轮原图）。新添加工具默认使用 `原图工具输出`。`原图` 工具可作为链路中的重置步骤，把当前图恢复成本轮原图，避免多个处理工具在同一张图上持续累加。边缘、阈值、形态学、颜色分析等处理类工具会把结果写回工具链，后续模板匹配、YOLO、多点找色等识别类工具可以直接使用处理后的图片，也可以随时切回原图工具输出。

### 执行模式（ToolController 调度）

```cpp
enum class Mode { Idle, Running, Waiting };
```

- **单个执行**：`RequestRun(index)` 将指定工具加入队列，每帧由 `Tick()` 消费。
- **全部执行**：`RequestRunAll(loop)` 顺序执行所有工具，记录单步耗时和总耗时；总耗时按各工具真实执行耗时累加，不包含逐帧调度等待、UI 刷新或帧间隔。
- **单步执行**：`RequestStepNext()` 每次推进一个工具，执行后回到空闲状态。
- **工具链传图**：批量/单步开始时缓存当前图片；每个工具执行前按实例输入来源选择图片，执行后把当前图片记录为下一步可用输出。

### 图像预处理（模板匹配）

模板匹配支持对源图做预处理后再匹配：
- ☑ 转为灰度 → 图片变灰，在灰度图上匹配
- ☑ 二值化 → 图片二值化后匹配
- ☐ 取消 → 自动恢复原始彩图（持久备份，不丢失）

---

## 📋 配方系统

配方保存为 JSON 文件（`.recipe`），存储在 `recipes/` 目录，包含：

- 全局参数（阈值/模板匹配参数）
- 全局 ROI 列表
- **全部工具实例**：类型、输入来源、模板图片（PNG）、搜索 ROI、旋转参数、匹配参数、模板预处理、图像预处理、原图重置、边缘检测参数、阈值调试参数、YOLO/轮廓/形状/直线/形态学/颜色分析/多点找色参数

### 配方操作

| 操作 | 说明 |
|------|------|
| 💾 保存 | 菜单 → 配方 → 输入名称 → 保存当前配方 |
| 📂 加载 | 菜单 → 配方 → 输入名称 → 加载，或点击已有配方列表 |
| 🖼 模板 | 每实例模板自动保存为 `配方名_tplN.png` |

---

## 🚀 搬到其他电脑

1. 复制整个项目文件夹
2. 安装 VS2022（勾选"使用 C++ 的桌面开发"）
3. 打开 `Windows_imgui.slnx`，直接编译运行

**不需要额外安装 OpenCV 或配置任何路径**，默认工程依赖已包含在项目中。当前 `Windows_imgui.vcxproj` 统一链接仓库内置 OpenCV 5.0 runtime（`redist/opencv_world500*.dll/lib`），include 和 runtime 都从 `include/`、`redist/` 取得。

## 🔧 技术债务 & 重构路线

当前最大的架构债不是算法，而是 **全局变量驱动** 和 **if/else 工具调度**。

| 优先级 | 任务 | 收益 |
|:---:|------|------|
| ① | **ITool 接口** — ✅ YOLO/轮廓/形状/直线/多点找色已接入 | 新工具优先走统一接口 |
| ② | **ToolResult 统一** — ✅ ITool 返回 `measurements/regions/detections/lines/texts/debugImage` 结构，模板匹配等结果统一叠加 | 减少 `DetectedObject`/`ContourResult` 分裂；`gMatchROIs` 仅保留给模板匹配结果列表 |
| ③ | **ROI 升级** — ✅ Point/Line/Circle/Polygon 已实现 | 工业场景必备 |
| ④ | **Recipe 版本化** — ✅ 配方加 `version` 字段 | v1→v2→v3 升级不炸 |
| ⑤ | **VisionContext** — ✅ `struct { image, rois, template, results, zoom/pan }` | 多线程/批量执行安全 |
| ⑥ | **节点流程编辑器** — 可视化拖拽式工具链 | 平台化关键一步 |

---

## ⚠️ 免责声明

本项目仅供**学习、研究和教育**目的使用。

- 不得用于任何违法用途
- 使用本软件产生的任何后果由使用者自行承担
- YOLO 模型检测结果仅供参考，不构成任何形式的判断依据

## 📄 开源协议

本项目采用 [MIT License](LICENSE)。

---

## 🔧 统一工具接口

所有视觉工具统一继承 `ITool`（`Algorithm/ITool.h`），通过 `VisionContext` 传递上下文：

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual const char* GetName() const = 0;
    virtual int GetType() = 0;
    virtual ToolResult Execute(VisionContext& ctx) = 0;  // 核心
    virtual void DrawUI() = 0;
    virtual nlohmann::json Save() const = 0;
    virtual void Load(const nlohmann::json&) = 0;
    static std::unique_ptr<ITool> Create(int type);       // 工厂
};
```

已接入：EdgeTool(0) / TemplateMatchITool(1) / BlobTool(2) / ThresholdITool(3) / YOLOTool(4) / ContourTool(5) / ShapeTool(6) / LineTool(7) / MorphologyITool(8) / ColorAnalyzerITool(9) / MultiColorFinder(10) / OpenCVYoloITool(11)。原图工具(type 12)由 `ToolController` 直接恢复本轮原图。

执行结果统一封装为 `ToolResult { measurements, regions, detections, lines, texts, debugImage }`，显示层通过统一结果结构叠加绘制。

## 🎯 ROI 类型

```
ROI → 矩形(RECT) / 点(POINT) / 线段(LINE) / 圆(CIRCLE) / 多边形(POLYGON)
```

5 种几何类型已实现，按类型着色区分（绿/金黄/青/橙/紫），支持交互式创建/选中/拖动/删除。

## 🚀 开发路线

| 阶段 | 状态 | 内容 |
|------|------|------|
| 一 | ✅ 完成 | 图片浏览、ROI、图像处理、模板匹配、YOLO、视频、配方、日志 |
| 二 | ✅ 完成 | ITool 接口、ToolResult、VisionContext、ROI 类型升级 |
| 三 | 进行中 | ✅ 多点找色；规划 OCR、二维码/条码、Blob分析增强、尺寸测量 |
| 四 | 🔲 规划 | 工业相机SDK、Modbus TCP、PLC通讯、OPC UA、MQTT |
| 五 | 🔲 远景 | 节点式流程编辑器、可选脚本/插件系统；当前主线不依赖 Python |

---

## 🌟 项目目标

基于 **Dear ImGui + DirectX 12 + OpenCV + ONNX Runtime** 构建轻量级工业视觉平台。

参考：Cognex VisionPro · Hikrobot VisionMaster · HALCON

实现图像处理、视觉检测、深度学习推理、工业通讯、自动化集成的完整解决方案。

第三方依赖协议：
| 依赖 | 协议 |
|------|------|
| Dear ImGui | MIT |
| OpenCV | Apache 2.0 |
| ONNX Runtime | MIT |
| nlohmann/json | MIT |

