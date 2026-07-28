# Windows_imgui 代码解析

> 文档同步日期：2026-07-27。入口、任务管理、图像/ROI 状态、PLC 硬件调度、结果发布和 type 0-17 已与当前实现同步。


## 📖 项目概述

这是一个基于 **Dear ImGui + DirectX 12 + OpenCV** 构建的 Windows 桌面视觉工具应用。用户可以导入单图、递归图片文件夹、视频或相机帧，创建最多 16 个独立任务，配置 type 0-17 工具链、ROI、Fixture 和判定条件，并在可停靠窗口中查看任务结果、耗时和叠加图。

---

## 一、系统架构图

```
┌─────────────────────────────────────────────────────────────────────┐
│                      wWinMain (程序入口)                              │
│  Windows_imgui.cpp                                                   │
└──────────────────────────┬──────────────────────────────────────────┘
                           │
           ┌───────────────┴───────────────┐
           ▼                               ▼
   ┌───────────────┐              ┌───────────────────┐
   │ DX12 初始化    │              │  ImGui 初始化      │
   │ CreateDeviceD3D│              │  ImGui_ImplDX12   │
   └───────┬───────┘              │  ImGui_ImplWin32  │
           │                      └─────────┬─────────┘
           ▼                                ▼
   ┌───────────────┐              ┌───────────────────┐
   │InitDX12Context│              │FontManager初始化   │
   │ (辅助上下文)   │              │ (加载中文字体)     │
   └───────┬───────┘              └─────────┬─────────┘
           │                                │
           └───────────────┬────────────────┘
                           ▼
              ┌──────────────────────┐
              │     主循环            │
              │  while (!done) { }   │
              └──────────────────────┘
                           │
            ┌──────────────┼──────────────┐
            ▼              ▼              ▼
    ┌──────────────┐ ┌──────────┐ ┌──────────────┐
    │ Windows消息   │ │ ImGui帧  │ │ DX12渲染     │
    │ PeekMessage  │ │ 开始     │ │ Present      │
    └──────────────┘ └────┬─────┘ └──────────────┘
                          ▼
              ┌──────────────────────┐
              │    UI 各窗口绘制      │
              │  ├─ DrawDockSpaceHost│
              │  ├─ ShowSidebar      │
              │  ├─ ShowLogWindow    │
              │  ├─ ShowStatsWindow  │
              │  ├─ ShowOpenCV       │
              │  ├─ ShowToolsWindow  │
              │  ├─ ShowHardwareWindow│
              │  ├─ ShowRunResultWindow│
              │  └─ 任务列表/工具分配窗口│
              └──────────────────────┘
                          │
                          ▼
              ┌──────────────────────────┐
              │ Core 图片导入与上传调度    │
              │ ├─ ImageImportService   │
              │ ├─ AsyncImageLoader     │
              │ ├─ ImageState           │
              │ └─ ImageLoadController  │
              └──────────────────────────┘
```

---

## 二、文件结构与职责

| 文件 | 用途 | 核心内容 |
|------|------|----------|
| `Windows_imgui.cpp` | **程序入口 + DX12管理 + 主循环** | `wWinMain()`, `CreateDeviceD3D()`, `WndProc()` |
| `Windows_imgui.h` | **公共头文件** | 包含所有模块的头文件 |
| `framework.h` | **系统包含文件** | Win32 / DX12 / OpenCV 基础库引用 |
| `Core/DX12Context.h/cpp` | **DX12 全局变量 + 初始化** | `gDevice`, `gTexture`, `gSrvCpuHandle`, `InitDX12Context()` |
| `Core/OpenCVTest.h/cpp` | **OpenCV图片读取 + GPU上传** | `TestReadImage()`, `UploadToDX12()`, 延迟释放队列 |
| `Core/AsyncImageLoader.h/cpp` | **异步图片加载** | `RequestLoad()`, `CheckAndProcess()` 后台解码+主线程回调 |
| `Core/VideoCapture.h/cpp` | **视频/摄像头播放** | OpenCV cv::VideoCapture，播放/暂停/跳帧/FPS |
| `Core/AudioPlayer.h/cpp` | **音频播放** | XAudio2 + Media Foundation，与视频同步 |
| `Core/OpenFileDialog.h/cpp` | **文件选择对话框** | `OpenFileDialog()` `OpenFolderDialog()` `ScanImageFiles()` |
| `Core/ThemeManager.h/cpp` | **主题管理** | 夜间/白天切换, theme.cfg 持久化 |
| `Core/RecipeManager.h/cpp` | **配方系统** | version 4 JSON 保存/加载任务、工具参数、ROI 和资产，兼容旧版本 |
| `Core/ImageState.h/cpp` | **图像状态** | 当前图、原图和图像版本状态收口 |
| `Core/ROI.h` / `Core/ROIState.h/cpp` | **ROI 状态** | ROI 数据结构和当前选区状态 |
| `Core/FrameSourceState.h/cpp` | **帧来源状态** | 单图、图片序列、视频、摄像头来源状态 |
| `Core/FrameNavigation.h/cpp` | **图片导航** | 图片序列上一张/下一张切换 |
| `Core/ImageLoadController.h/cpp` | **加载控制器** | 图片加载请求、异步回调和上传调度 |
| `Core/LiveYoloRunner.h/cpp` | **实时 YOLO 调度** | 视频/摄像头实时推理和耗时统计 |
| `Core/VisionContext.h/cpp` | **统一视觉上下文** | `struct VisionContext { image, rois, frozenTemplate, unifiedResults }` 替代散落全局变量；视图变换由 `ImageViewState` 管理 |
| `Core/ToolInstance.h` / `Core/ToolTypes.h` | **工具元数据** | 工具实例参数、类型常量和工具名称 |
| `Core/ToolChainState.h/cpp` | **工具链状态** | 工具实例、任务定义、归属、顺序、启用和独立输入 |
| `Core/ToolExecutor.h/cpp` | **统一工具执行器** | type 0-11、13-17 统一分发到 `RunViaITool()`，type 12 原图由 `ToolController` 特殊处理 |
| `Core/ToolController.h/cpp` | **工具调度器** | queue 驱动 + Tick() 替代旧 ExecState 状态机 |
| `Core/FrameRenderer.h/cpp` | **帧渲染提交** | 每帧渲染提交和渲染资源收尾 |
| `UI/DockSpaceHost.h/cpp` | **主框架** | DockSpace 容器 + 菜单栏 + 配方菜单 |
| `UI/LogWindow.h/cpp` | **日志窗口** | 带颜色分级和时间戳的日志列表 |
| `UI/Sidebar.h/cpp` | **侧边栏** | ROI 类型切换、快捷操作、自定义日志 |
| `UI/StatsWindow.h/cpp` | **性能统计窗口** | FPS、帧耗时、渲染信息 |
| `UI/ToolsWindow.h/cpp` | **功能窗口** | 工具参数、任务管理、输入配置、全部/当前任务/单步/循环/并行入口 |
| `UI/RunResultWindow.h/cpp` | **结果总览** | 任务卡片、详情、结果图、缩放/最大化和整轮耗时 |
| `UI/ImageViewer.h/cpp` | **图像/视频显示** | 缩放/平移/图片列表导航/视频控制栏 |
| `UI/ROIManager.h/cpp` | **ROI 交互管理** | 画框/拖拽/控制点/坐标转换 |
| `Renderer/FontManager.h/cpp` | **字体管理** | 加载 simsun.ttc 中文字体 |
| `Renderer/PreviewTextureCache.h/cpp` | **预览纹理缓存** | 缓存工具预览图和任务结果图对应的 DX12 纹理 |
| `Log/LogSystem.h/cpp` | **线程安全日志系统** | 3级日志(INFO/WARN/ERROR), 颜色, 时间戳, 2000条上限 |
| `Algorithm/ThresholdTool.h/cpp` | **图像处理管线** | 灰度→模糊→二值化/Canny→RGBA上传, 性能计时 |
| `Algorithm/ThresholdITool.cpp` | **阈值工具适配** | 把阈值调试接入 ITool |
| `Algorithm/EdgeTool.h/cpp` | **边缘检测工具** | Canny 边缘检测 ITool |
| `Algorithm/BlobTool.h/cpp` | **Blob 分析工具** | Blob 分析 ITool |
| `Algorithm/ToolImageUtils.h/cpp` | **工具图像辅助** | 输入图像准备和 ROI 裁剪 |
| `Algorithm/FrameSource.h` | **统一帧包** | 单图/序列/视频/摄像头帧包定义 |
| `Algorithm/TemplateMatch.h/cpp` | **模板匹配** | 多方法模板匹配、旋转匹配、结果绘制 |
| `Algorithm/YOLODetector.h/cpp` | **YOLO 目标检测** | ONNX Runtime 推理 YOLO11，ROI 限定区域，NMS 后处理 |
| `Algorithm/OpenCVYoloDetector.h/cpp` | **OpenCV DNN YOLO** | OpenCV 5.0 DNN 实验后端 |
| `Algorithm/ITool.h` / `Algorithm/ITool.cpp` | **工具接口** | 抽象基类 `ITool` + `ToolRegistry` 工厂 + 自动注册 |
| `Algorithm/ToolResult.h` | **统一输出** | 来源 ID、Pass/Fail/Error、分段耗时、测量、区域、检测、线、文本和调试图 |
| `Algorithm/YOLOTool.h/cpp` | **YOLO 工具** | 实现 ITool 接口的 YOLO 封装 |
| `Algorithm/ShapeTools.h/cpp` | **形状工具组** | ContourTool/ShapeTool/LineTool 实现 ITool 接口 |

---

## 三、核心模块详解

### 3.1 程序入口 — `Windows_imgui.cpp`

#### 3.1.1 `wWinMain()` 执行顺序

```
① DPI感知启用
   └─ ImGui_ImplWin32_EnableDpiAwareness()
   └─ 获取主显示器缩放比例 main_scale

② 创建Win32窗口
   └─ RegisterClassExW → CreateWindowW
   └─ 标题: "Dear ImGui DirectX12 Example"

③ DirectX 12 初始化
   └─ CreateDeviceD3D(hwnd)
       ├─ 创建 D3D12 Device (Feature Level 11.0)
       ├─ 创建 RTV 描述符堆 (2个后备缓冲区)
       ├─ 创建 SRV 描述符堆 (64个槽位) + 分配器
       ├─ 创建 CommandQueue / CommandAllocator / CommandList
       ├─ 创建 Fence（同步机制）
       ├─ 创建 SwapChain (FLIP_DISCARD模式)
       └─ CreateRenderTarget() 创建后备缓冲区

④ 辅助 DX12 上下文
   └─ InitDX12Context() → 另创一套Device/Queue/List（用于图片上传）

⑤ 初始化 Dear ImGui
   ├─ ImGui::CreateContext()
   ├─ ConfigFlags: NavKeyboard + DockingEnable + ViewportsEnable
   ├─ StyleColorsDark + DPI缩放
   ├─ ImGui_ImplWin32_Init(hwnd)
   └─ ImGui_ImplDX12_Init(&init_info)  ← 自定义SRV分配器

⑥ 加载中文字体
   └─ FontManager::InitFonts(main_scale)
       ├─ 优先 simhei.ttf (黑体)
       ├─ 后备 msyh.ttc (微软雅黑)
       └─ ImGui_ImplDX12_InvalidateDeviceObjects + CreateDeviceObjects

⑦ 主循环
   └─ while (!done) { ... }
```

#### 3.1.2 主循环内部流程

```
while (!done)
{
    // ── ① 消息处理 ──
    PeekMessage → TranslateMessage → DispatchMessage
    if WM_QUIT → done = true

    // ── ② 窗口遮挡处理 ──
    if 窗口被遮挡或最小化 → Sleep(10); continue;

    // ── ③ ImGui 新帧 ──
    ImGui_ImplDX12_NewFrame()
    ImGui_ImplWin32_NewFrame()
    ImGui::NewFrame()

    // ── ④ 绘制 UI 窗口 ──
    UI::DrawDockSpaceHost()     // DockSpace + 菜单栏
    UI::ShowSidebar()           // 侧边栏
    UI::ShowLogWindow()         // 日志
    UI::ShowStatsWindow()       // 性能
    UI::ShowHardwareWindow()    // 设备连接页签
    UI::ShowOpenCV()            // 图像显示
    UI::ShowToolsWindow()       // 工具/任务入口，内部调用 ToolController::Tick()
    UI::ShowRunResultWindow()   // 任务结果总览与详情

    HardwareRuntimeService::Tick() // 发布异步抓帧结果和硬件运行状态

    // ── ⑤ ImGui 渲染 ──
    ImGui::Render()

    // ── ⑥ 获取帧上下文 ──
    WaitForNextFrameContext()
    backBufferIdx = pSwapChain->GetCurrentBackBufferIndex()
    Reset CommandAllocator + CommandList

    // ── ⑦ 图片纹理上传调度 ──
    ImageLoadController::ProcessPendingUpload()
        → 从 ImageState 读取待显示图像
        → UploadToDX12(...)

    // ── ⑧ DX12 渲染管线 ──
    Barrier: PRESENT → RENDER_TARGET
    ClearRenderTargetView
    SetDescriptorHeaps
    ImGui_ImplDX12_RenderDrawData()
    Barrier: RENDER_TARGET → PRESENT
    Close CommandList
    ExecuteCommandLists
    UpdatePlatformWindows
    Signal Fence
    SwapChain->Present(1, 0)  // 垂直同步
}
```

#### 3.1.3 关键数据结构

```cpp
// 帧上下文（双缓冲）
struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;   // 围栏值（同步用）
};

// SRV描述符分配器（基于空闲列表）
struct ExampleDescriptorHeapAllocator {
    ID3D12DescriptorHeap* Heap;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT HeapHandleIncrement;
    ImVector<int> FreeIndices;  // 空闲索引栈
};
```

#### 3.1.4 窗口消息处理 `WndProc()`

```cpp
WM_SIZE → CleanupRenderTarget() → ResizeBuffers() → CreateRenderTarget()
WM_SYSCOMMAND → 屏蔽 ALT 菜单
WM_DESTROY → PostQuitMessage(0)
```

---

### 3.2 DX12 上下文 — `Core/DX12Context.h/cpp`

#### 3.2.1 全局变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `g_pd3dDevice` | `ID3D12Device*` | 主 DX12 设备（ImGui 使用） |
| `gDevice` | `ID3D12Device*` | 辅助 DX12 设备（图片上传用） |
| `g_pd3dCommandList` | `ID3D12GraphicsCommandList*` | 主命令列表 |
| `gCmdList` | `ID3D12GraphicsCommandList*` | 辅助命令列表 |
| `gTexture` | `ID3D12Resource*` | 当前显示的图片纹理 |
| `g_pd3dSrvDescHeap` | `ID3D12DescriptorHeap*` | SRV 描述符堆 |
| `gSrvCpuHandle` | `D3D12_CPU_DESCRIPTOR_HANDLE` | SRV CPU 句柄 |
| `gSrvGpuHandle` | `D3D12_GPU_DESCRIPTOR_HANDLE` | SRV GPU 句柄（传给 ImGui::Image） |
| `ImageState::Width/Height()` | `int` | 当前图片尺寸 |

#### 3.2.2 `InitDX12Context()` 函数

```
① 枚举 GPU 适配器（跳过软件适配器）
② D3D12CreateDevice(Feature Level 11.0)
③ 创建 CommandQueue
④ 创建 CommandAllocator
⑤ 创建 CommandList → Close()
```

> ⚠️ 注意：这里创建的是**独立于 ImGui 之外的**一套 DX12 设备/命令列表，专门用于图片纹理的上传操作。

---

### 3.3 图片加载管线 — `Core/ImageImportService.*`

#### 3.3.1 加载流程

```
用户点击"选择图片"
  → OpenFileDialog() 返回路径
  → ImageImportService::ImportImage(path)
     ├── ① 校验路径并清理旧执行/叠加状态
     ├── ② FrameSourceState 切换到单图模式
     ├── ③ AsyncImageLoader::Request(path)
     └── ④ ImageLoadController 接收解码结果
          ├── ImageState::SetImage(img)
          ├── 更新宽高、版本号和原图
          └── 请求 DX12 纹理上传

选择文件夹时由 `ImportFolder()` 递归收集受支持图片，并通过
`FrameNavigation` 切换当前帧。空目录、路径不存在和解码失败都会返回
可展示的错误信息。
```

#### 3.3.2 `UploadToDX12()` 核心逻辑

```
① 参数检查 (device, cmdList)
② 检查纹理连续性 → 不连续则 clone
③ 初始化纹理资源（仅第一次）:
   ├── CreateCommittedResource(DEFAULT堆, COPY_DEST状态)
   └── CreateShaderResourceView → 绑定到 srvHandle
④ 初始化上传堆（仅第一次）:
   └── CreateCommittedResource(UPLOAD堆, GENERIC_READ)
⑤ UpdateSubresources() 将 CPU 数据拷贝到 GPU
⑥ ResourceBarrier: COPY_DEST → PIXEL_SHADER_RESOURCE
```

#### 3.3.3 延迟释放队列

```cpp
std::vector<ID3D12Resource*> gPendingReleaseTextures;

// 添加：加载新图时，旧纹理不立即释放（避免UI还在读取）
gPendingReleaseTextures.push_back(oldTexture);

// 冲刷：在安全的时机统一释放
void FlushPendingRelease() {
    for (auto* res : gPendingReleaseTextures)
        res->Release();
    gPendingReleaseTextures.clear();
}
```

> 这种设计避免了一边显示旧图片、一边释放资源的竞态问题。

---

### 3.4 UI 层 — `UI/DockSpaceHost.h/cpp`

#### 3.4.1 窗口管理

**状态变量（全局）：**

| 变量 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `g_ShowLog` | `bool` | `true` | 日志窗口 |
| `g_ShowSidebar` | `bool` | `true` | 侧边栏 |
| `g_ShowStats` | `bool` | `true` | 性能窗口 |
| `g_ShowOpenCV` | `bool` | `true` | 图像窗口 |
| `g_ShowTools` | `bool` | `true` | 工具窗口 |
| `show_demo_window` | `bool` | `false` | ImGui Demo |

#### 3.4.2 各窗口详解

**① `DrawDockSpaceHost()` — 主框架**

```
ImGui::SetNextWindowPos/MainViewport → 全屏无边框
ImGuiWindowFlags:
  NoDocking | NoTitleBar | NoCollapse | NoResize
  | NoMove | NoBringToFrontOnFocus | NoNavFocus
  | NoBackground | MenuBar

┌── 菜单栏 ──────────────────────────┐
│ 文件: 新建 | 打开 | 保存 | 退出      │
│ 视图: Log | 侧边栏 | 性能 | 预览 | 功能 │
│ 工具: OpenCV | 检测                  │
│ 帮助: ImGui Demo | 关于              │
└─────────────────────────────────────┘

DockSpace → 子窗口可随意停靠/浮动
```

**② `ShowSidebar()` — 侧边栏**

```
按钮: "并发日志测试" → 创建10个线程写日志（展示线程安全）
按钮: "日志测试按钮" → 输出当前图像状态摘要
输入框: 自定义日志输入 → "发送到日志" 按钮
```

**③ `ShowLogWindow()` — 日志窗口**

```
"Clear" 按钮 → LogSystem::Clear()

每条日志显示格式:
  [时间戳] 日志内容
  颜色: INFO=灰色, WARN=黄色, ERROR=红色
  自定义颜色: 按线程ID着色

交互:
  - 右键 → "Copy" 复制到剪贴板
  - Ctrl+C (悬停时) → 复制
  - 自动滚动到底部
```

**④ `ShowStatsWindow()` — 性能窗口**

```
显示: FPS, Frame Time (ms)
预留: GPU状态, Draw Calls, 三角形数
```

**⑤ `ShowOpenCV()` — 核心图像窗口**

```
工具栏:
  [放大] [缩小] [1:1] [清ROI] [打印ROI] [清理图片] [选择图片]

图像区域:
  - 带水平滚动条的 Child 区域
  - 支持鼠标滚轮缩放（以鼠标位置为锚点）
  - 支持鼠标左键拖拽平移

当有图片时:
  ImGui::Image(textureGPUHandle, drawSize)
当无图片时:
  显示 "暂无图片"
```

**⑥ `ShowToolsWindow()` — 工具窗口**

```
顶部: 任务分组管理、任务列表、工具分配、添加工具
执行: 执行全部、执行当前任务、全部单步、当前任务单步、循环、再次运行
任务: 最多16个，支持重命名/排序/启用/删除/独立图片或文件夹/相机优先
工具卡片: 独立参数、任务下拉、移动/复制/删除、输入来源、ROI/Fixture/判定
每帧末尾: ToolController::Tick() 推进当前队列或收集并行任务结果
```

**⑦ `ShowRunResultWindow()` — 结果窗口**

按启用任务顺序显示卡片；详情展示任务内工具状态、耗时和结果图，支持缩放、平移和最大化。预览纹理由 `PreviewTextureCache` 复用。

---

### 3.5 坐标系统与 ROI 交互

#### 3.5.1 核心变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `gZoom` | `float&` | `ImageViewState` 中缩放倍数的兼容引用 (0.05~50.0) |
| `gPan` | `ImVec2&` | `ImageViewState` 中平移偏移量的兼容引用（像素） |
| `imageScreenPos` | `ImVec2` | 图像左上角在屏幕上的位置 |
| `gCanvasSize` | `ImVec2` | 画布区域大小 |

#### 3.5.2 坐标转换函数

```cpp
// 图像坐标 → 屏幕坐标
ImVec2 ImageToScreenPos(const ImVec2& p) {
    return ImVec2(
        imageScreenPos.x + gPan.x + p.x * gZoom,
        imageScreenPos.y + gPan.y + p.y * gZoom
    );
}

// 屏幕坐标 → 图像坐标
ImVec2 ScreenToImagePos(const ImVec2& p) {
    return ImVec2(
        (p.x - imageScreenPos.x - gPan.x) / gZoom,
        (p.y - imageScreenPos.y - gPan.y) / gZoom
    );
}
```

#### 3.5.3 缩放逻辑（锚点缩放）

```
ZoomAtCenter(delta):
  1. 记录鼠标在屏幕上的位置
  2. 反向计算鼠标对应的图像坐标 (ScreenToImagePos)
  3. 应用缩放: gZoom *= (1.0 + delta)
  4. 将图像坐标重新投影回屏幕 (ImageToScreenPos)
  5. 计算偏差并修正 gPan，保证鼠标锚点不动
```

#### 3.5.4 ROI 数据结构

```cpp
struct ROI {
    std::uint64_t runtimeId = 0; // UI 运行时关联标识，不写入配方
    int type = ROI_TYPE_RECT;
    ImVec2 start = {0, 0};       // 图像像素坐标
    ImVec2 end = {0, 0};         // 图像像素坐标
    float angle = 0.0f;          // 矩形顺时针旋转角度（度）
    std::vector<ImVec2> points;  // 多边形顶点
};

struct ROIBox {
    ImVec2 lt, rt, lb, rb;  // 四角
    ImVec2 t, b, l, r;       // 四边中点
};
```

#### 3.5.5 ROI 交互状态机

```
右击开始绘制 → ROIEditorState 记录绘制状态和起点
右击结束绘制 → 创建 ROI，调用 ROIState::Add(...)

左击检测优先级:
  ① 检测是否点在 Handle 上（8方向控制点）
  ② 检测是否点在 ROI 内部（选中+启动拖动）
  ③ 都不中 → 取消选中

Handle 拖动:
  LT/RT/LB/RB → 对角缩放
  T/B/L/R     → 单边缩放

ROI 移动:
  记录鼠标在图像中的位移 → 平移 start/end

释放左键:
  ROIEditorState 清除拖动状态和活动控制点
```

`gDrawingROI`、`gROIStart` 仍作为兼容引用存在，实际状态由 `ROIEditorState` 持有；ROI 集合和当前选择由 `ROIState` 管理。`gZoom`、`gPan` 同理，是 `ImageViewState` 状态的兼容引用。

#### 3.5.6 8 方向 Handle 枚举

```cpp
enum HandleType {
    HANDLE_NONE,
    HANDLE_LT,   // 左上角
    HANDLE_RT,   // 右上角
    HANDLE_LB,   // 左下角
    HANDLE_RB,   // 右下角
    HANDLE_T,    // 上边中点
    HANDLE_B,    // 下边中点
    HANDLE_L,    // 左边中点
    HANDLE_R     // 右边中点
};
```

#### 3.5.7 图像自适应函数

```cpp
FitImageToWindow():
  scaleX = canvasW / imageW
  scaleY = canvasH / imageH
  gZoom = min(scaleX, scaleY)
  if gZoom > 1.0: gZoom = 1.0   // 不放大超过原图
  gPan = (canvasSize - drawSize) / 2  // 居中
```

---

### 3.6 日志系统 — `Log/LogSystem.h/cpp`

#### 3.6.1 数据结构

```cpp
enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };

struct LogItem {
    LogLevel level;         // 日志级别
    std::string time;       // 时间戳 (精确到毫秒)
    std::string text;       // 日志内容
    ImVec4 color;           // 自定义颜色
    bool useCustomColor;    // 是否使用自定义颜色
};
```

#### 3.6.2 核心函数

| 函数 | 说明 |
|------|------|
| `Add(level, fmt, ...)` | 添加日志（默认颜色） |
| `Add(level, color, fmt, ...)` | 添加日志（自定义颜色） |
| `Clear()` | 清空所有日志 |
| `GetLogs()` | 获取日志副本（线程安全） |
| `GetThreadColor()` | 根据线程 ID 生成固定颜色 |

#### 3.6.3 线程安全机制

```
每个写操作 (Add/Clear/GetLogs) 都通过:
  std::lock_guard<std::mutex> lock(g_logMutex)

读取时返回副本:
  std::vector<LogItem> GetLogs() {
      std::lock_guard<std::mutex> lock(g_logMutex);
      return s_logs;  // 返回拷贝
  }

自动裁剪:
  if (s_logs.size() > 2000)
      s_logs.erase(s_logs.begin());
```

#### 3.6.4 时间戳格式

```
2024-01-01 12:00:00.123
```

---

### 3.7 渲染辅助 — `Renderer/`

#### 3.7.1 流程

```
InitFonts(dpi_scale):
  ① 通过 GetModuleFileName 获取 exe 所在目录
  ② 尝试加载 exe目录/simsun.ttc:
     io.Fonts->AddFontFromFileTTF(exeDir+"simsun.ttc", 18.0f,
       nullptr, io.Fonts->GetGlyphRangesChineseFull())
  ③ 失败 → 尝试 C:/Windows/Fonts/msyh.ttc（系统微软雅黑）
  ④ 全部失败 → 使用默认字体
  ⑤ 设置 io.FontDefault = font
  ⑥ io.Fonts->Build()
```

> `GetGlyphRangesChineseFull()` 加载完整的 CJK 统一表意文字（超过 2 万个汉字）。

### 3.7.2 预览纹理缓存 — `Renderer/PreviewTextureCache.h/cpp`

工具预览图和任务结果图通过 `PreviewTextureCache` 复用 DX12 纹理；当源图变化或缓存被清理时再更新或释放资源，减少结果总览和工具卡片重复创建纹理的开销。

---

### 3.8 图像处理管线 — `Algorithm/ThresholdTool.h/cpp`

#### 3.8.1 管线数据流

```
VisionContext.image（只读输入）
  │
  ├── [可选] 灰度化 ToolInstance::threshold.useGray
  │     BGRA/BGR/GRAY → GRAY
  │
  ├── [可选] 高斯模糊 threshold.enableBlur
  │     GaussianBlur(kernel = blurSize*2+1)
  │
  ├── [可选] 二值化 threshold.enableThreshold
  │     threshold(value, THRESH_BINARY)
  │
  ├── [可选] Canny 边缘检测 threshold.enableCanny
  │     Canny(lowThreshold, highThreshold)
  │
  └── ToolResult.debugImage（新图像输出）
        → ResultPublisher / ImageState
        → ImageLoadController 请求纹理更新
```

#### 3.8.2 ThresholdSettings 参数

```cpp
struct ThresholdSettings {
    bool useGray         = false;
    bool enableBlur       = false;  // 启用模糊
    bool enableThreshold  = false;  // 启用二值化
    bool enableCanny      = false;  // 启用边缘检测
    int  blurSize         = 5;      // 模糊核大小
    int  threshold        = 128;    // 二值化阈值
    int  cannyLow         = 50;     // Canny 低阈值
    int  cannyHigh        = 150;    // Canny 高阈值
};
```

#### 3.8.3 性能计时

```
ToolExecutor 统一记录：
prepareMs → 准备图像、ROI、参数和依赖
executeMs → ITool::Execute 算法执行
publishMs → 判定、缓存和结果发布
wallMs    → 工具墙钟耗时
```

#### 3.8.4 UI 界面

```
┌─────────────────────────────────┐
│ 图像处理窗口                      │
│                                 │
│ [重置]  [✓] 使用灰度              │
│ ────────────────                 │
│ 图像模糊处理                      │
│ [✓] Enable Blur                  │
│ Kernel Size: [====●=====] 5      │
│ ────────────────                 │
│ 图像二值化处理                    │
│ [✓] Enable Threshold             │
│ Threshold: [====●=====] 128      │
│ ────────────────                 │
│ Canny边缘检测                     │
│ [✓] Enable Canny                 │
│ Low Threshold: [===●======] 50   │
│ High Threshold: [===●======] 150 │
│ ────────────────                 │
│ 性能分析 (ms)                    │
│ Gray   : 0.123 ms                │
│ Blur   : 0.456 ms                │
│ Filter : 0.789 ms                │
│ RGBA   : 0.012 ms                │
│ ────────────────                 │
│ Total  : 1.380 ms                │
└─────────────────────────────────┘
```

---

### 3.9 YOLO 目标检测 — `Algorithm/YOLODetector.h/cpp`

#### 3.9.1 架构

基于 **ONNX Runtime** 进行 YOLO11 模型推理，支持手动 DLL 加载避免静态初始化问题。

```
YOLODetector 命名空间
  ├── LoadModel(onnxPath, classesPath)   ← 加载 ONNX + 类别
  ├── IsLoaded()                         ← 检查就绪状态
  ├── Detect(image, conf, nms, roi)      ← 执行推理
  ├── DrawDetections(image, objs)        ← 绘制结果
  └── Unload()                           ← 释放资源

全局状态（延迟初始化指针）:
  s_OrtDll      → LoadLibrary("onnxruntime.dll")
  s_Env         → Ort::Env（ORT 环境）
  s_Session     → Ort::Session（模型会话）
  s_Allocator   → Ort::AllocatorWithDefaultOptions
  s_MemInfo     → Ort::MemoryInfo（CPU 内存信息）
```

#### 3.9.2 推理流程

```
Detect(image, confThreshold, nmsThreshold, roi)
  │
  ├── ① ROI 裁剪 + 预处理
  │     Preprocess(image, roi)
  │       → cv::dnn::blobFromImage(1/255, 640×640, RGB)
  │
  ├── ② ONNX Runtime 推理
  │     Ort::Value::CreateTensor<float>(blob)
  │     → s_Session->Run(inNames, &input, 1, outNames, 1)
  │
  ├── ③ 后处理
  │     Postprocess(data, shape, conf, nms, roi)
  │       → 解析 cx/cy/w/h + 类别置信度
  │       → cv::dnn::NMSBoxes() 去重
  │       → 坐标映射回原始图像
  │
  └── ④ 返回 std::vector<DetectedObject>
```

#### 3.9.3 DetectedObject 结构

```cpp
struct DetectedObject {
    cv::Rect box;           // 检测框（图像坐标）
    int   classId   = -1;   // 类别 ID
    float confidence = 0.0f; // 置信度
    std::string className;   // 类别名称（80类 COCO）
};
```

#### 3.9.4 类别系统

未提供类别文件时自动使用 COCO 80 类默认名称（person, bicycle, car, ... toothbrush）。

#### 3.9.5 绘制

`DrawDetections()` 使用 12 色调色板按类别着色，标签显示 `类别名 置信度 (cx,cy)`，字体大小和线宽随图像尺寸自适应缩放。

---

```
┌─────────────┐    选择图片    ┌─────────────┐
│  用户交互    │ ───────────→  │ OpenFileDialog│
│ (鼠标/键盘)  │              │ .cpp         │
└──────┬──────┘              └──────┬──────┘
       │                            │ 返回路径
       ▼                            ▼
┌──────────────┐           ┌───────────────┐
│UI::ShowOpenCV│ ────────→ │ImageImportSvc │
│ 显示图片      │           │ (Core 服务)    │
└──────┬──────┘           └──────┬───────┘
       │                         │ 异步解码
       ▼                         ▼
┌───────────────────────────────────────────┐
│AsyncImageLoader / ImageLoadController     │
│  ├── cv::imread() 从硬盘读取               │
│  ├── ImageState::SetImage(img)            │
│  ├── 通道转换 BGR→RGBA                     │
│  └── UploadToDX12() 上传 GPU               │
│         ├── 创建 D3D12 Texture (DEFAULT)   │
│         ├── Create SRV                     │
│         └── UpdateSubresources()           │
└────────────────────┬──────────────────────┘
                     │
                     ▼
              ┌──────────────┐
              │ gTexture     │
              │ (GPU 显存)   │
              └──────┬───────┘
                     │
          ┌──────────┴──────────┐
          ▼                     ▼
   ┌───────────────┐    ┌───────────────┐
   │ImGui::Image() │    │ToolController │
   │ (显示当前图)  │    │→ ToolExecutor │
   └───────────────┘    │   ├─ 灰度化    │
                        │   ├─ 模糊      │
                        │   ├─ 二值化    │
                        │   └─ Canny    │
                        └──────┬────────┘
                               │ ToolResult.debugImage
                               ▼
                        ┌──────────────┐
                        │ImageState::  │
                        │SetDebugImage │
                        └──────┬───────┘
                               │
                               ▼
                        ┌──────────────┐
                        │ gTexture     │
                        │ (处理结果)    │
                        └──────────────┘
```

---

## 四、任务与 PLC 运行调度

任务配置由 `ToolChainState` 持有，执行批次由 `ToolController` 持有，硬件连接和握手状态由 `HardwareRuntimeService` 持有。三者职责不能互换：任务重命名/排序不会直接操作 PLC；PLC 线程也不会直接修改工具卡片或结果纹理。

```text
HardwareWindow 配置
  -> HardwareSettingsService 保存并校验 IO 表
  -> HardwareRuntimeService 后台轮询 Modbus
  -> Trigger 上升沿且握手空闲
  -> UI 主线程 Tick 取出指定任务请求
  -> ToolController 只运行该任务
  -> 结果聚合为 Pass / Fail / Error
  -> HardwareRuntimeService 写 Busy/Done/OK/NG/Error
  -> ACK 上升沿清除结果并释放下一轮
```

标准映射中任务01 Trigger 为地址0，任务02～16为地址8～22，地址1～7保留给 Busy、Done、OK、NG、Error、Heartbeat、ACK。任务列表变更只补齐缺失 Trigger，不覆盖用户自定义地址；“恢复标准映射”才会按当前任务顺序重建。

握手是单槽模型：请求已接收、Busy 或等待 ACK 时的新 Trigger 会被忽略并计数，不会在 ACK 后补跑。PLC 输入图像优先级是“在线相机 → 任务文件夹 → 任务单图 → 公共图片”；相机在线但本轮抓帧失败发布 Error。实现与操作细节见 [硬件接入说明](HARDWARE_INTEGRATION.md)。

---

## 五、构建与运行

### 5.1 环境要求

| 组件 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 |
| 开发工具 | Visual Studio 2022 |
| C++ 标准 | C++20 |
| 图形 API | DirectX 12 |
| 第三方依赖 | 全部包含在项目中（OpenCV / ONNX Runtime / ImGui / DX12 辅助头文件） |

### 5.2 构建步骤

```
1. 打开 Windows_imgui.slnx
2. 选择 x64 Debug 或 Release 配置
3. 生成解决方案 (Ctrl+Shift+B)
4. 按 F5 运行
```

编译时 PostBuild 事件自动将 `assets/`、`imgui.ini`、`theme.cfg` 复制到输出目录。

### 5.3 运行时文件

以下文件自动与 exe 同目录（无需手动拷贝）：

- `simsun.ttc` — 中文字体（PostBuild 自动复制）
- `opencv_world500*.dll`、`opencv_videoio_ffmpeg500_64.dll`、`opencv_videoio_msmf500_64.dll` — OpenCV 5.0 运行时
- `onnxruntime*.dll` — ONNX Runtime 运行时
- `yolo11n.onnx` — YOLO11 模型（PostBuild 自动复制到 models/）
- `ncnn.dll` / `ncnn.lib` — OCR 的 NCNN 运行时和链接库
- `models/ppocrv6/*` — PP-OCRv6 tiny 检测/识别模型和字典

---

## 六、扩展指南

### 6.1 添加新的图像处理算法

```
① 从 type 18 开始分配编号，在 Algorithm/ 创建 YourTool.h/.cpp 并实现 ITool
② 在 ToolRegistry 注册工厂，在 ToolInstance/ToolSettings 保存实例参数
③ 在 ToolsWindow 的 g_ToolRegistry 和参数面板中添加入口
④ 在 ToolExecutor::RunViaITool() 同步参数
⑤ 在 ToolInstance::ToRecipeJson()/LoadRecipeJson() 保存与加载参数
⑥ 更新 vcxproj/filters、回归测试和文档
```

### 6.2 添加新的菜单项

```
在 UI::DrawDockSpaceHost() 的 BeginMenuBar() 中添加：
  if (ImGui::BeginMenu("新菜单")) {
      if (ImGui::MenuItem("功能项")) { ... }
      ImGui::EndMenu();
  }
```

### 6.3 添加新的窗口

```
① 在 DockSpaceHost.h 中声明状态变量 (如 g_ShowMyWindow)
② 实现 ShowMyWindow() 函数
③ 在 DockSpaceHost.cpp 中增加 #include
④ 在 wWinMain 主循环中添加调用
⑤ 在视图菜单中添加开关
```
