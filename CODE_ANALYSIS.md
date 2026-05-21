# Windows_imgui 代码解析

## 📖 项目概述

这是一个基于 **Dear ImGui + DirectX 12 + OpenCV** 构建的 Windows 桌面视觉工具应用。用户可以通过图形界面加载图片、进行图像处理（灰度/模糊/二值化/Canny）、通过 ROI（感兴趣区域）进行交互式标注，并在一个可停靠的多窗口 UI 中实时查看结果。

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
              │  └─ ShowThresholdWin │
              └──────────────────────┘
                          │
                          ▼
              ┌──────────────────────┐
              │ 图片加载处理（调度）   │
              │ ├─ pendingPath→      │
              │ │  TestReadImage()   │
              │ └─ gNeedUpload→      │
              │    UploadToDX12()   │
              └──────────────────────┘
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
| `Core/OpenFileDialog.h/cpp` | **文件选择对话框** | `OpenFileDialog()` → 调用 Win32 `GetOpenFileName` |
| `UI/DockSpaceHost.h/cpp` | **UI主框架 + 图像交互 + ROI** | DockSpace, 6个子窗口, ROI绘制/选择/拖动/缩放 |
| `Renderer/FontManager.h/cpp` | **字体管理** | 加载 simhei.ttf / msyh.ttc 中文字体 |
| `log/LogSystem.h/cpp` | **线程安全日志系统** | 3级日志(INFO/WARN/ERROR), 颜色, 时间戳, 2000条上限 |
| `OpenCV/ThresholdTool.h/cpp` | **图像处理管线** | 灰度→模糊→二值化/Canny→RGBA上传, 性能计时 |

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
    UI::ShowOpenCV()            // 图像显示
    UI::ShowToolsWindow()       // 工具入口
    ThresholdTool::ShowThresholdWindow()

    // ── ⑤ ImGui 渲染 ──
    ImGui::Render()

    // ── ⑥ 获取帧上下文 ──
    WaitForNextFrameContext()
    backBufferIdx = pSwapChain->GetCurrentBackBufferIndex()
    Reset CommandAllocator + CommandList

    // ── ⑦ 图片加载调度 ──
    if pendingPath 不为空:
        uploadRequest = pendingPath; pendingPath.clear()
        OpenCVTest::TestReadImage(...)
    if gNeedUpload:
        UploadToDX12(...)  // 处理阈值结果回传

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
| `pendingPath` | `std::string` | 待加载的图片路径（UI写入→主循环读取） |
| `gImageWidth/Height` | `int` | 当前图片尺寸 |

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

### 3.3 图片加载管线 — `Core/OpenCVTest.h/cpp`

#### 3.3.1 加载流程

```
用户点击"选择图片"
  → OpenFileDialog() 返回路径
  → pendingPath = 路径           (UI线程写入)
  → 主循环检测 pendingPath
    → uploadRequest = pendingPath  (主线程读取)
    → OpenCVTest::TestReadImage()
       ├── ① 参数检查
       ├── ② 旧纹理 → 延迟释放队列 (gPendingReleaseTextures)
       ├── ③ cv::imread(path, IMREAD_UNCHANGED)
       ├── ④ gImage = img; gOriginalImage = img;
       ├── ⑤ gImageWidth/Height = img.cols/rows
       ├── ⑥ 通道转换 (BGR/BGRA/GRAY → RGBA)
       └── ⑦ UploadToDX12() 上传 GPU
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
按钮: "日志测试按钮" → 打印 pendingPath 内容
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
按钮: [打印ROI] [清理图片] [边缘检测] [模板匹配] [Blob分析]
勾选: [阈值调试] → 打开 ThresholdTool 窗口
```

---

### 3.5 坐标系统与 ROI 交互

#### 3.5.1 核心变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `gZoom` | `float` | 缩放倍数 (0.05~50.0) |
| `gPan` | `ImVec2` | 平移偏移量（像素） |
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
    ImVec2 start;  // 标准化坐标 0~1（相对于图像宽高）
    ImVec2 end;
};

struct ROIBox {
    ImVec2 lt, rt, lb, rb;  // 四角
    ImVec2 t, b, l, r;       // 四边中点
};
```

#### 3.5.5 ROI 交互状态机

```
右击开始绘制 → gDrawingROI = true, gROIStart = imageMousePos
右击结束绘制 → 创建 ROI，加入 gROIs 列表

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
  gDraggingROI = false, gActiveHandle = HANDLE_NONE
```

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

### 3.6 日志系统 — `log/LogSystem.h/cpp`

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

### 3.7 字体管理 — `Renderer/FontManager.h/cpp`

#### 3.7.1 流程

```
InitFonts(dpi_scale):
  ① io.Fonts->Clear() 清空旧字体
  ② 尝试加载 simhei.ttf:
     io.Fonts->AddFontFromFileTTF("simhei.ttf", 18.0f,
       nullptr, io.Fonts->GetGlyphRangesChineseFull())
  ③ 失败 → MessageBox 提示，加载 msyh.ttc
  ④ 全部失败 → MessageBox 报错
  ⑤ 设置 io.FontDefault = font
  ⑥ io.Fonts->Build()
  ⑦ 刷新 DX12 纹理:
     ImGui_ImplDX12_InvalidateDeviceObjects()
     ImGui_ImplDX12_CreateDeviceObjects()
```

> `GetGlyphRangesChineseFull()` 加载完整的 CJK 统一表意文字（超过 2 万个汉字）。

---

### 3.8 图像处理管线 — `OpenCV/ThresholdTool.h/cpp`

#### 3.8.1 管线数据流

```
gImage (原图)
  │
  ├── [可选] 灰度化 gUseGray
  │     BGRA/BGR/GRAY → GRAY
  │
  ├── [可选] 高斯模糊 gPipe.enableBlur
  │     GaussianBlur(kernel = blurSize*2+1)
  │
  ├── [可选] 二值化 gPipe.enableThreshold
  │     threshold(value, THRESH_BINARY)
  │
  ├── [可选] Canny 边缘检测 gPipe.enableCanny
  │     Canny(lowThreshold, highThreshold)
  │
  ├── → RGBA 转换
  │     GRAY/BGR/BGRA → RGBA
  │
  ├── gPendingUpload = rgba
  ├── gNeedUpload = true
  │
  └── 主循环检测 gNeedUpload
        → UploadToDX12() 更新 GPU 纹理
```

#### 3.8.2 PipelineState 参数

```cpp
struct PipelineState {
    bool enableBlur       = false;  // 启用模糊
    bool enableThreshold  = false;  // 启用二值化
    bool enableCanny      = false;  // 启用边缘检测
    int  blurSize         = 5;      // 模糊核大小
    int  threshold        = 128;    // 二值化阈值
    int  cannyLow         = 50;     // Canny 低阈值
    int  cannyHigh        = 150;    // Canny 高阈值
};
```

#### 3.8.3 性能计时系统

```
ApplyProcess() 内部使用 std::chrono::high_resolution_clock 计时：

t0 ── 开始
  │
t1 ── 灰度前
  │
t2 ── 灰度后        → gTimeGray = t2 - t0
  │
t3 ── 模糊后        → gTimeBlur = t3 - t2
  │
t4 ── 阈值/Canny后  → gTimeFilter = t4 - t3
  │
t5 ── RGBA 转换后   → gTimeRGBA = t5 - t4
  │
t6 ── 完成          → gTimeTotal = t6 - t0
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

## 四、数据流全景图

```
┌─────────────┐    选择图片    ┌─────────────┐
│  用户交互    │ ───────────→  │ OpenFileDialog│
│ (鼠标/键盘)  │              │ .cpp         │
└──────┬──────┘              └──────┬──────┘
       │                            │ 返回路径
       ▼                            ▼
┌──────────────┐           ┌───────────────┐
│UI::ShowOpenCV│ ←──────── │ pendingPath   │
│ 显示图片      │           │ (全局变量)     │
└──────┬──────┘           └──────┬───────┘
       │                         │ 主循环检测
       ▼                         ▼
┌───────────────────────────────────────────┐
│OpenCVTest::TestReadImage()                │
│  ├── cv::imread() 从硬盘读取               │
│  ├── gImage = img (OpenCV Mat)            │
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
   │ImGui::Image() │    │ThresholdTool  │
   │ (显示原图)    │    │ ApplyProcess  │
   └───────────────┘    │   ├─ 灰度化    │
                        │   ├─ 模糊      │
                        │   ├─ 二值化    │
                        │   └─ Canny    │
                        └──────┬────────┘
                               │ gPendingUpload
                               ▼
                        ┌──────────────┐
                        │UploadToDX12()│
                        │ (覆盖纹理)    │
                        └──────┬───────┘
                               │
                               ▼
                        ┌──────────────┐
                        │ gTexture     │
                        │ (处理结果)    │
                        └──────────────┘
```

---

## 五、构建与运行

### 5.1 环境要求

| 组件 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 |
| 开发工具 | Visual Studio 2022 |
| C++ 标准 | C++20 |
| 图形 API | DirectX 12 |
| OpenCV | 4.12.0（需要 opencv_world4120.dll） |
| Dear ImGui | 内置（1.91.6+） |

### 5.2 构建步骤

```
1. 打开 Windows_imgui.slnx
2. 选择 x64 Debug 或 Release 配置
3. 生成解决方案 (Ctrl+Shift+B)
4. 按 F5 运行
```

### 5.3 运行时文件

以下文件需要放在可执行文件同目录下：

- `simhei.ttf` — 中文字体（必须）
- `opencv_world4120.dll` — OpenCV 运行时（必须）
- `test.jpg` — 测试图片（可选）

---

## 六、扩展指南

### 6.1 添加新的图像处理算法

```
① 在 OpenCV/ 目录下创建 YourTool.h 和 YourTool.cpp
② 在 YourTool.h 中声明 UI 函数和参数变量
③ 在 Windows_imgui.h 中添加 #include "OpenCV/YourTool.h"
④ 在 UI::ShowToolsWindow() 中添加按钮入口
⑤ 在主循环中添加 YourTool::ShowWindow() 调用
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
