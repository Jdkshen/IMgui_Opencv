# Windows_imgui

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具应用。

---

## 📁 项目结构

```
Windows_imgui/
│
├── Windows_imgui.cpp          ⭐ 程序入口 + DX12 设备管理 + 主循环
├── Windows_imgui.h            ⭐ 公共头文件（包含所有模块）
├── framework.h                ⭐ 系统头文件（Win32/DX12/OpenCV）
│
├── Core/                      ⭐ 核心模块
│   ├── OpenFileDialog.h/cpp     打开文件对话框
│   ├── OpenCVTest.h/cpp        OpenCV 图片读取 + DX12 纹理上传
│   └── DX12Context.h/cpp       DX12 全局变量 + 上下文初始化
│
├── UI/                        ⭐ UI 层
│   ├── DockSpaceHost.h/cpp      框架层：DockSpace 主窗口、菜单栏、
│   │                              侧边栏、日志、性能、工具入口
│   ├── ImageViewer.h/cpp        图像层：图片预览、缩放平移、ROI绘制
│   └── ROIManager.h/cpp         ROI 数据结构、坐标转换、交互状态
│
├── Renderer/                  ⭐ 渲染模块
│   └── FontManager.h/cpp        字体管理（中文字体加载）
│
├── log/                       ⭐ 日志系统
│   └── LogSystem.h/cpp          线程安全日志（颜色、时间戳）
│
├── OpenCV/                    ⭐ 图像算法模块
│   └── ThresholdTool.h/cpp      阈值/模糊/Canny 图像处理
│   └── TemplateMatch.h/cpp      模板匹配
│
├── imgui/                     ⭐ Dear ImGui 库（第三方）
│
├── simhei.ttf / simsun.ttc    ⭐ 中文字体
├── test.jpg                   ⭐ 测试图片
├── Windows_imgui.slnx         ⭐ VS2022 解决方案
└── Windows_imgui.vcxproj      ⭐ 项目文件
```

---

## 🏗️ 整体流程框架

### 1️⃣ 程序入口 — wWinMain（Windows_imgui.cpp）

```
wWinMain()
  │
  ├── 1. DPI 感知设置（EnableDpiAwareness）
  ├── 2. 创建 Win32 窗口（RegisterClass + CreateWindow）
  ├── 3. 初始化 DirectX 12（CreateDeviceD3D）
  │     ├── Device / SwapChain
  │     ├── RTV 堆 / SRV 堆
  │     ├── CommandQueue / Allocator / CommandList
  │     └── Fence
  ├── 4. InitDX12Context()  ← 辅助 DX12 上下文
  ├── 5. 显示窗口
  ├── 6. 初始化 Dear ImGui
  │     ├── ImGui::CreateContext()
  │     ├── ConfigFlags（Docking / Viewports）
  │     ├── StyleColorsDark + DPI 缩放
  │     ├── ImGui_ImplWin32_Init()
  │     └── ImGui_ImplDX12_Init()
  ├── 7. FontManager::InitFonts() ← 加载中文字体
  │
  └── 8. 主循环（while !done）
        │
        ├── ① PeekMessage 处理消息
        ├── ② 窗口最小化/遮挡处理
        ├── ③ ImGui 新帧
        │     ├── ImGui_ImplDX12_NewFrame()
        │     ├── ImGui_ImplWin32_NewFrame()
        │     └── ImGui::NewFrame()
        │
        ├── ④ 绘制 UI 窗口
        │     ├── UI::DrawDockSpaceHost()    ← 菜单栏 + DockSpace
        │     ├── UI::ShowSidebar()          ← 侧边栏
        │     ├── UI::ShowLogWindow()        ← 日志
        │     ├── UI::ShowStatsWindow()      ← FPS
        │     ├── UI::ShowOpenCV()           ← 图像预览（→ ImageViewer）
        │     ├── UI::ShowToolsWindow()      ← 工具入口
        │     ├── TemplateMatch::ShowWindow()
        │     ├── TemplateMatch::ShowTemplateEditor()
        │     └── ThresholdTool::ShowThresholdWindow()
        │
        ├── ⑤ 图片加载（主循环调度）
        │     ├── pendingPath → uploadRequest
        │     ├── OpenCVTest::TestReadImage()
        │     │     ├── cv::imread()
        │     │     ├── → RGBA 转换
        │     │     └── UploadToDX12()
        │     └── 阈值结果上传（gNeedUpload）
        │
        ├── ⑥ DX12 渲染
        │     ├── WaitForNextFrameContext()
        │     ├── Barrier: PRESENT → RENDER_TARGET
        │     ├── ClearRenderTargetView
        │     ├── SetDescriptorHeaps
        │     ├── ImGui_ImplDX12_RenderDrawData()
        │     ├── Barrier: RENDER_TARGET → PRESENT
        │     ├── Close + ExecuteCommandLists
        │     ├── UpdatePlatformWindows()
        │     ├── Signal Fence
        │     └── SwapChain->Present()
        │
        └── ⑦ 清理
              ├── ImGui_ImplDX12_Shutdown()
              ├── ImGui_ImplWin32_Shutdown()
              ├── ImGui::DestroyContext()
              ├── CleanupDeviceD3D()
              └── DestroyWindow / UnregisterClass
```

---

## 🧩 各模块详解

### 🔹 Core 核心模块

| 文件 | 功能 |
|------|------|
| `DX12Context.h/cpp` | 全局变量定义（g_pd3dDevice, gTexture, gSrvCpuHandle 等）+ InitDX12Context() |
| `OpenFileDialog.h/cpp` | Win32 GetOpenFileName 对话框 |
| `OpenCVTest.h/cpp` | OpenCV 读取图片 + UploadToDX12() 上传 GPU 纹理 |

**图片加载流程：**

```
选择图片 → OpenFileDialog() → pendingPath
  ↓（主循环检测）
TestReadImage(path, device, cmdList, texture, srvHandle)
  ├── 释放旧纹理（延迟释放队列）
  ├── cv::imread() 读取
  ├── BGR/BGRA/GRAY → RGBA
  └── UploadToDX12()
        ├── CreateCommittedResource（DEFAULT堆）
        ├── CreateShaderResourceView
        ├── UpdateSubresources()
        └── Barrier: COPY_DEST → PIXEL_SHADER_RESOURCE
```

---

### 🔹 UI 框架层（DockSpaceHost.h/cpp）

| 函数 | 说明 |
|------|------|
| `DrawDockSpaceHost()` | 全屏 DockSpace + 菜单栏（文件/视图/工具/帮助） |
| `ShowSidebar()` | 侧边栏（日志测试按钮、输入框） |
| `ShowLogWindow()` | 日志显示（颜色分级、右键复制、自动滚动） |
| `ShowStatsWindow()` | FPS / 帧时间显示 |
| `ShowToolsWindow()` | 工具入口（ROI管理、边缘检测、模板匹配、阈值调试开关） |

### 🔹 UI 图像层（ImageViewer.h/cpp）

| 函数 | 说明 |
|------|------|
| `ShowOpenCV()` | 图像预览窗口（工具栏：放大/缩小/适合/清空ROI/选择图片） |
| `FitImageToWindow()` | 图像自适应窗口缩放 |
| `ImageToScreen()` | ROI 绘制与交互核心（创建/选中/拖动/缩放/删除） |
| `ClearImage()` | 清理图像显示状态（保留 GPU 纹理） |

### 🔹 ROI 管理（ROIManager.h/cpp）

| 内容 | 说明 |
|------|------|
| `ROI` 结构体 | 存储图像坐标的 start/end |
| `HandleType` 枚举 | 8方向控制点 + 中心点（HANDLE_LT ~ HANDLE_CENTER） |
| `ImageToScreenPos()` / `ScreenToImagePos()` | 图像坐标 ↔ 屏幕坐标转换 |
| `NormalizeROI()` | 标准化 ROI（确保 start < end） |
| `ZoomAtCenter()` | 以鼠标为锚点缩放 |
| `PrintROIToLog()` | 打印所有 ROI 到日志 |
| `ClearROIState()` | 重置 ROI 交互状态 |

**图像交互：**

| 操作 | 功能 |
|------|------|
| 滚轮 | 锚点缩放 |
| 左键拖拽（空白） | 平移 |
| 右键拖拽 | 绘制 ROI |
| 左键点击 ROI | 选中（红色） |
| 左键拖拽 ROI | 移动 |
| 拖拽控制点 | 缩放（8方向） |

**坐标系：**
```
图像坐标(0~1) ←→ 屏幕坐标(像素)
  ScreenToImagePos()   ↔   ImageToScreenPos()
```

---

### 🔹 日志系统（LogSystem.h/cpp）

- 线程安全（mutex 保护）
- 三级日志：INFO(灰) / WARN(黄) / ERROR(红)
- 自定义颜色重载
- 毫秒级时间戳
- 最大 2000 条自动裁剪
- GetThreadColor() 按线程 ID 生成颜色

```cpp
LogSystem::Add(LOG_INFO, "消息 %d", 123);
LogSystem::Add(LOG_INFO, color, "带颜色: %s", str);
LogSystem::Clear();
auto logs = LogSystem::GetLogs();  // 线程安全副本
```

---

### 🔹 字体管理（FontManager.h/cpp）

```
InitFonts(dpi_scale)
  ├── 清空旧字体
  ├── 加载 simhei.ttf（黑体 18px 中文全字库）
  ├── 失败 → msyh.ttc（微软雅黑）
  ├── 设置 io.FontDefault + Build()
  └── DX12 纹理刷新
```

---

### 🔹 图像处理管线（ThresholdTool.h/cpp）

```
ApplyProcess()
  ├── ① 灰度化（可选）
  ├── ② 高斯模糊（可选）
  ├── ③ 二值化 / Canny（可选）
  ├── ④ → RGBA 转换
  ├── ⑤ gPendingUpload / gNeedUpload 标记
  └── ⑥ 主循环 → UploadToDX12()
```

**PipelineState 参数：**

```cpp
struct PipelineState {
    bool enableBlur;         // 高斯模糊
    bool enableThreshold;    // 二值化
    bool enableCanny;        // Canny 边缘
    int  blurSize;           // 模糊核
    int  threshold;          // 阈值
    int  cannyLow, cannyHigh; // Canny 阈值
};
```

**性能计时：** Gray / Blur / Filter / RGBA → Total（ms）

---

## 🚀 构建运行

| 环境 | 版本 |
|------|------|
| 系统 | Windows 10/11 |
| IDE | Visual Studio 2022 |
| C++ | C++20 |
| GPU | DirectX 12 |
| OpenCV | 4.12.0 |

```bash
1. 打开 Windows_imgui.slnx
2. 选择 x64 Debug/Release
3. Ctrl+Shift+B 生成
4. F5 运行
```

**运行时依赖（与 exe 同目录）：**
- `simhei.ttf`
- `opencv_world4120.dll`
- `test.jpg`（可选）

---

## 🔧 扩展指南

### 添加新视觉工具
1. `OpenCV/` 下创建 `YourTool.h/cpp`
2. `Windows_imgui.h` 添加 include
3. `UI::ShowToolsWindow()` 添加入口
4. 主循环添加 `YourTool::ShowWindow()`

### 添加菜单项
在 `DrawDockSpaceHost()` 的 `BeginMenuBar()` 内添加。
