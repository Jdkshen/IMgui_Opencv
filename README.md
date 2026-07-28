# IMgui_Opencv

> 文档同步日期：2026-07-28。任务分组、独立输入、PLC 单槽握手、16 任务触发、并行执行和结果总览均已按当前代码复核。全部文档入口见 `docs/README.md`。

## 2026-07-28 当前能力摘要

| 模块 | 当前能力 |
| --- | --- |
| 任务管理 | 最多 16 个任务，支持新建、重命名、排序、启用、禁用和删除；工具可直接加入当前任务 |
| 任务输入 | 每个任务可独立选择单图或递归图片文件夹；文件夹每轮推进一张图片，同一任务内的工具共用本轮输入 |
| 手动执行输入 | 任务可选择已连接的工业相机作为首选输入；相机不可用时回退到任务文件夹、任务单图或公共图片 |
| PLC 触发输入 | 在线相机 → 任务文件夹 → 任务单图 → 公共图片；在线相机本轮抓帧失败会明确输出 Error |
| 执行方式 | 支持执行全部、执行当前任务、全部单步、当前任务单步、循环和最多 4 个任务并行 |
| 结果总览 | 按任务顺序显示结果卡片和结果图；禁用任务不执行，也不显示结果卡片；支持详情和最大化 |
| 配方持久化 | 保存任务顺序、启用状态、独立图片、图片文件夹进度和相机优先设置 |
| PLC 握手 | 任务01 使用 Trigger 0，任务02～任务16使用 8～22；Busy/Done/OK/NG/Error/Heartbeat/ACK 使用 1～7；任务重命名保留自定义地址，删除和新增任务自动同步 Trigger |

历史合并说明见 `docs/STATUS_2026-07-25.md`，任务分组收尾见 `docs/STATUS_2026-07-26.md`，PLC 工业握手初版见 `docs/STATUS_2026-07-27.md`，最新整合与验证见 `docs/STATUS_2026-07-28.md`。
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
│   ├── VisionContext.h/cpp     ←   执行图像快照、帧源、ROI、模板、取消令牌和结果
│   ├── ROI.h / ROIState.cpp/h  ←   ROI 数据结构与当前 ROI 状态
│   ├── ROIEditorState.cpp/h    ←   ROI 绘制、拖动和控制点编辑状态
│   ├── ImageViewState.cpp/h    ←   图像缩放、平移和画布状态
│   ├── ToolInstance.h          ←   工具实例参数聚合
│   ├── ToolTypes.h             ←   工具类型常量/名称
│   ├── ToolChainState.cpp/h    ←   工具链状态管理
│   ├── ToolExecutor.h/cpp      ←   统一工具执行器（参数同步、ROI 注入、执行、发布）
│   ├── ToolController.h/cpp    ←   工具调度器（全部/单步/循环/运行模式）
│   ├── HardwareRuntimeService.cpp/h ← 相机抓帧与检测结果硬件发布
│   ├── FrameRenderer.cpp/h     ←   每帧渲染提交
│   ├── ResultPublisher.h       ←   ToolResult 统一发布入口
│   ├── ResultOverlayState.cpp/h ←   结果、实时检测和 Fixture 叠加查询
│   ├── ToolJudgement.cpp/h   ←   Pass/Fail/Error 判定与失败停止
│   └── ResultROIResolver.cpp/h ← 上游工具结果转 ROI
│
├── UI/                         ← 界面模块
│   ├── DockSpaceHost.cpp/h     ←   主停靠空间 + 菜单栏
│   ├── ImageViewer.cpp/h       ←   图片预览 + 缩放平移 + 文件夹浏览 + 视频控制                                                                                                 
│   ├── LogWindow.cpp/h         ←   日志窗口（ImGuiListClipper 虚拟滚动）
│   ├── Sidebar.cpp/h           ←   侧边栏控制面板
│   ├── StatsWindow.cpp/h       ←   性能统计窗口
│   ├── ToolsWindow.cpp/h       ←   工具参数、任务管理和执行入口
│   └── ROIManager.cpp/h        ←   ROI 绘制、交互转发 + 坐标转换（数据定义见 Core/ROI.h）
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
│   ├── TemplateMatchingTool.cpp/h ← 工具链模板匹配（实例参数/旋转/NMS）
│   ├── TemplateMatch.cpp/h     ←   旧模板调试窗口兼容层
│   ├── QRCodeTool.cpp/h        ←   QR/Code128/EAN/Data Matrix/PDF417
│   ├── MeasurementTool.cpp/h   ←   距离/线宽/角度/圆直径与公差
│   ├── OCRTool.cpp/h            ←   PP-OCRv6 tiny + NCNN 文字识别
│   ├── DifferenceTool.cpp/h     ←   参考图差分与差异区域
│   ├── GeometryDrawTool.cpp/h   ←   配方化几何标注
│   ├── ContourDetector.cpp/h   ←   轮廓分析（凸包/圆度/近似）
│   ├── ShapeMatcher.cpp/h      ←   形状匹配（matchTemplate+轮廓比对）
│   ├── ShapeTools.cpp/h        ←   形状匹配工具适配（实现 ITool）
│   ├── LineDetector.cpp/h      ←   直线检测（Canny+HoughLinesP）
│   ├── MorphologyTool.cpp/h    ←   形态学工具（7 种运算）
│   └── ColorAnalyzer.cpp/h     ←   颜色分析（多色域 + 直方图）
│
├── Renderer/                   ← 渲染模块
│   ├── FontManager.cpp/h       ←   中文字体加载 + 工具 PNG 图标合并到 ImGui 图集
│   └── PreviewTextureCache.cpp/h ← 工具预览图与任务结果图的 DX12 纹理缓存
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
│   ├── README.md              ←   文档索引与历史/当前说明边界
│   ├── ALGORITHMS.md           ←   OpenCV 算法详解 + 添加新算法指南
│   ├── BUILD.md                ←   编译构建说明
│   ├── CODE_ANALYSIS.md        ←   代码架构分析
│   ├── IMGUI_API.md            ←   Dear ImGui C++ API 完整参考
│   ├── CODE_STRUCTURE.md       ←   当前代码结构梳理
│   ├── TASK_EXECUTION.md       ←   任务执行模板和验收清单
│   ├── TASK_GROUPS.md          ←   任务分组、独立输入、执行方式和结果总览说明
│   ├── PROJECT_UPDATE_GUIDE.md ←   后续项目更新同步指南
│   ├── PERFORMANCE_REVIEW.md   ←   架构性能审查
│   ├── OPENCV5_EXPERIMENT.md   ←   OpenCV 5.0 YOLO 实验后端说明
│   ├── INSPECTION_PIPELINE_2026.md ← 导入、判定、条码、测量、结果 ROI 更新
│   ├── ROADMAP.md              ←   后续开发方向
│   └── VIDEO_AUDIO.md          ←   视频/音频模块说明
│
├── redist/                     ← 本地/Release 运行时 DLL 和 lib
├── models/                     ← 预训练模型与测试素材
│   └── ppocrv6/                ←   OCR 默认 NCNN 模型
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
DockSpaceHost               DX12Context                 ITool / ToolResult
Sidebar                     OpenCVTest                  TemplateMatch
ImageViewer                 AsyncImageLoader            YOLODetector
ToolsWindow                 ImageLoadController         OpenCVYoloDetector
ROIManager                  RecipeManager               ContourDetector
LogWindow                   ImageState                  ShapeMatcher
StatsWindow                 VisionContext               LineDetector
RunResultWindow             ROI / ROIState              ThresholdTool
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
    └─ ToolExecutor::Execute()    ← 统一入口
         ├─ RunViaITool()         types 0-11、13-17
         ├─ PrepareDetached()/PublishDetached() 任务并行快照与主线程发布
         └─ ToolController        type 12（原图工具，恢复本轮原图）
```

### 核心数据流

```text
ImageImportService / FrameNavigation / HardwareRuntimeService
    → ImageState
    → ToolController / ToolExecutor
    → VisionContext / ITool
    → ToolResult
    → ResultPublisher（并行任务通过 ToolExecutor::PublishDetached 发布）
    → gContext.unifiedResults
    → ResultOverlayState
    → ImageViewer::DrawUnifiedResults()
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
| 图片加载 | 支持 JPG/JPEG/PNG/BMP/TIF/TIFF/WebP，文件对话框 + 递归文件夹浏览 + 异步加载 |
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
| 工具实例 | 手风琴式工具面板，每实例独立参数（模板/ROI/角度/预处理），支持 18 种工具类型 |
| 添加工具图标 | 添加工具弹窗支持 PNG 图片图标，资源位于 `assets/icons/`，加载失败时回退到内置绘制图标 |
| 批量执行 | 全部执行（逐帧高亮）+ 单步执行（点击推进）+ 循环模式 |
| 任务分组 | 最多 16 个任务；支持任务排序、独立工具链、独立图片/文件夹、相机优先和任务级结果图 |
| 任务并行 | “全部执行”可并行处理最多 4 个互相独立的任务；跨任务依赖时自动回退顺序执行 |
| 结果总览 | 按启用任务顺序显示结果卡片、状态、耗时和结果图，禁用任务自动隐藏 |
| 工具链输入 | 每个工具可选择“上一步原图”“上一步处理图”或“原图工具输出”，也可插入“原图”工具重置链路 |
| 运行模式 | 批量执行时可关闭逐工具调度日志和工具行高亮，降低 UI/日志干扰；当前仍保留算法日志和图像上传 |
| ROI 类型 | 矩形(绿) / 点(金黄) / 线段(青) / 圆(橙) / 多边形(紫)，按类型着色+可视化 |
| 配方系统 | 保存/加载全部工具实例参数、模板图片、搜索ROI（JSON） |
| 主题切换 | 夜间/白天模式，自动持久化 |
| 日志系统 | 三级日志（INFO/WARN/ERROR），颜色分级，2000 条上限 |

---

## 🔧 工具实例系统

功能窗口（手风琴布局）支持 18 种工具类型；type 0-11、13-17 已接入 ITool 统一接口，type 12 为原图重置特殊工具：

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
| YOLO OpenCV 5.0 | OpenCV DNN 实验推理 | 模型、类别、置信度、NMS、ROI | ✅ OpenCVYoloITool |
| OCR文字识别 | PP-OCRv6 tiny + NCNN 推理 | 检测/识别模型、字典、置信度、最大文本数、ROI | ✅ OCRTool |
| 二维码/条码 | QR、Code128、EAN、Data Matrix、PDF417 | 格式、ROI、多码、增强和去重 | ✅ QRCodeTool |
| 工业测量 | 距离、线宽、角度、圆直径和公差 | 卡尺、拟合、标定、公差 | ✅ MeasurementTool |
| 图像差分 | 参考图差异检测 | 阈值、面积、模糊、形态学 | ✅ DifferenceTool |
| 几何绘制 | 结果几何标注 | 线、矩形、圆等几何元素 | ✅ GeometryDrawTool |

> **ITool 接口**：统一 `Execute(VisionContext& ctx) → ToolResult`，结果经 `ResultPublisher` 发布、由 `ResultOverlayState` 查询，最后通过 `DrawUnifiedResults()` 在图像上叠加绘制。

每个实例的参数完全独立，互不影响。模板图像和搜索 ROI 按实例保存。
每个实例还可以独立选择输入来源：`上一步原图` 使用上一个工具执行前的图，`上一步处理图` 使用上一个工具执行后的图，`原图工具输出` 使用最近一次 `原图` 工具恢复出的图片（未执行原图工具时回退到本轮原图）。新添加工具默认使用 `原图工具输出`。`原图` 工具可作为链路中的重置步骤，把当前图恢复成本轮原图，避免多个处理工具在同一张图上持续累加。边缘、阈值、形态学、颜色分析等处理类工具会把结果写回工具链，后续模板匹配、YOLO、多点找色等识别类工具可以直接使用处理后的图片，也可以随时切回原图工具输出。

### 执行模式（ToolController 调度）

```cpp
enum class Mode { Idle, Running, Waiting };
```

- **单个执行**：`RequestRun(index)` 提交指定工具的执行请求，由后续帧中的 `Tick()` 调度执行。
- **全部执行**：`RequestRunAll(loop)` 按任务列表顺序执行全部启用任务，再执行未分组工具；禁用任务不进入执行顺序。
- **当前任务**：`RequestRunTaskGroup(name)` 只执行选中任务，任务内保持工具顺序。
- **单步执行**：`RequestStepNext()` 推进整条配方，`RequestStepNextTaskGroup(name)` 只推进当前任务；高亮使用真实的全局工具索引。
- **任务并行**：启用“任务并行”后，“全部执行”最多同时运行 4 个启用任务；任务内部仍顺序执行，跨任务结果依赖会自动回退顺序模式。
- **计时口径**：工具行显示单工具执行耗时；结果总览标题的“本轮总耗时”使用整轮墙钟时间，包含任务调度与并行等待，更接近用户实际等待时间。
- **工具链传图**：批量/单步开始时缓存当前图片；每个工具执行前按实例输入来源选择图片，执行后把当前图片记录为下一步可用输出。

任务输入按以下规则解析：任务勾选“使用工业相机（优先）”且相机已连接时，先请求一帧；相机不可用或抓帧失败时使用该任务绑定的文件夹或单图；任务未绑定图片时使用当前公共图片。文件夹在每轮开始时推进一张，同一任务内所有工具使用同一张本轮图片。完整说明见 `docs/TASK_GROUPS.md`。

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
- **任务分组**：任务名称、顺序、启用状态、单图路径、图片文件夹、当前文件夹索引和相机优先设置

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

**不需要额外安装 OpenCV 或配置本机绝对路径**。当前 `Windows_imgui.vcxproj` 使用项目内 `include/` 头文件，并从本地 `redist/` 取 OpenCV 5.0、ONNX Runtime、DirectML、NCNN 的 `.lib/.dll`。大型运行时文件后续建议通过 GitHub Release 的 `runtime.zip` 恢复到 `redist/`。

## 🔧 技术债务 & 重构路线

当前主要架构债集中在 `ToolsWindow.cpp` 体积较大、部分兼容引用仍存在，以及工具参数同步仍需要在 `ToolExecutor` 中按 type 维护。

| 优先级 | 任务 | 收益 |
|:---:|------|------|
| ① | **ITool 接口** — ✅ YOLO/轮廓/形状/直线/多点找色已接入 | 新工具优先走统一接口 |
| ② | **ToolResult 统一** — ✅ ITool 返回状态、来源、耗时及 `measurements/regions/detections/lines/texts/debugImage` | 结果经统一发布与叠加层显示 |
| ③ | **ROI 升级** — ✅ Point/Line/Circle/Polygon 已实现 | 工业场景必备 |
| ④ | **Recipe 版本化** — ✅ 当前写出 version 4，并兼容旧配方字段 | 配方演进不破坏旧案例 |
| ⑤ | **VisionContext** — ✅ 图像快照、帧源、ROI、模板、取消令牌和统一结果 | 多线程/批量执行安全 |
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
    virtual int GetType() const = 0;
    virtual ToolResult Execute(VisionContext& ctx) = 0;  // 核心
    virtual void DrawUI() = 0;
    virtual nlohmann::json Save() const = 0;
    virtual void Load(const nlohmann::json&) = 0;
    static std::unique_ptr<ITool> Create(int type);       // 工厂
};
```

已接入：EdgeTool(0) / TemplateMatchITool(1) / BlobTool(2) / ThresholdITool(3) / YOLOTool(4) / ContourTool(5) / ShapeTool(6) / LineTool(7) / MorphologyITool(8) / ColorAnalyzerITool(9) / MultiColorFinder(10) / OpenCVYoloITool(11) / OCRTool(13) / QRCodeTool(14) / MeasurementTool(15) / DifferenceTool(16) / GeometryDrawTool(17)。原图工具(type 12)由 `ToolController` 直接恢复本轮原图。

执行结果统一封装为 `ToolResult`，除几何和测量数据外还包含来源工具 ID、Pass/Fail/Error、跳过状态、原因和 prepare/execute/publish/wall/backend 分段耗时。

## 🎯 ROI 类型

```
ROI → 矩形(RECT) / 点(POINT) / 线段(LINE) / 圆(CIRCLE) / 多边形(POLYGON)
```

5 种几何类型已实现，按类型着色区分（绿/金黄/青/橙/紫），支持交互式创建/选中/拖动/删除。

## 🧪 PLC 联调模拟器

没有真实 PLC 时，可双击 `tools\plc_simulator\run_plc_simulator.bat` 启动图形化 Modbus TCP Server。模拟器提供任务01～任务16的独立 Trigger、Busy/Done/OK/NG/Error/Heartbeat 状态显示，以及手动或自动 ACK。默认连接参数为 `127.0.0.1:1502`、Unit ID `1`，详细步骤见 [PLC 模拟器说明](tools/plc_simulator/README.md)。

## 🚀 开发路线

| 阶段 | 状态 | 内容 |
|------|------|------|
| 一 | ✅ 完成 | 图片浏览、ROI、图像处理、模板匹配、YOLO、视频、配方、日志 |
| 二 | ✅ 完成 | ITool 接口、ToolResult、VisionContext、ROI 类型升级 |
| 三 | ✅ 完成 | 多点找色、结果导出、运行报告、OCR、二维码/条码、Blob增强、测量、差分和几何绘制 |
| 四 | 进行中 | ✅ OpenCV/UVC/RTSP 相机、TCP、Modbus TCP、PLC 和 OPC UA；PLC IO 握手、单任务拍照触发、心跳、ACK、超时报警和自动重连已接入；厂商相机 SDK、加密 OPC UA 和 MQTT 按现场需求接入 |
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
| NCNN | BSD 3-Clause |
| ZXing-cpp | Apache 2.0 |
| open62541 | MPL 2.0 |
| nlohmann/json | MIT |

