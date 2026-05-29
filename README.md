# IMgui_Opencv

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具应用。

---

## 📁 项目结构

```
IMgui_Opencv/
├── assets/                     ← 资源文件（编译时自动复制到输出目录）
│   ├── fonts/simsun.ttc        ←   宋体
│   └── images/                 ←   测试图片
│
├── Core/                       ← 核心模块
│   ├── DX12Context.cpp/h       ←   DX12 设备管理 + 全局状态
│   ├── OpenCVTest.cpp/h        ←   图片读取 + GPU 纹理上传
│   ├── AsyncImageLoader.cpp/h  ←   异步图片加载（后台线程 + 回调）
│   ├── OpenFileDialog.cpp/h    ←   文件/文件夹选择对话框
│   ├── VideoCapture.cpp/h      ←   视频/摄像头播放（cv::VideoCapture）
│   ├── AudioPlayer.cpp/h       ←   XAudio2 + Media Foundation 音频播放
│   ├── ThemeManager.cpp/h      ←   主题切换（夜间/白天）+ theme.cfg 持久化
│   └── RecipeManager.cpp/h     ←   配方保存/加载（JSON，支持工具实例序列化）
│
├── UI/                         ← 界面模块
│   ├── DockSpaceHost.cpp/h     ←   主停靠空间 + 菜单栏
│   ├── ImageViewer.cpp/h       ←   图片预览 + 缩放平移 + 文件夹浏览 + 视频控制
│   ├── LogWindow.cpp/h         ←   日志窗口（ImGuiListClipper 虚拟滚动）
│   ├── Sidebar.cpp/h           ←   侧边栏控制面板
│   ├── StatsWindow.cpp/h       ←   性能统计窗口
│   ├── ToolsWindow.cpp/h       ←   工具窗口（枚举状态机：全部执行/单步/循环）
│   └── ROIManager.cpp/h        ←   ROI 数据结构 + 交互 + 坐标转换
│
├── Algorithm/                  ← 图像算法
│   ├── YOLODetector.cpp/h      ←   YOLO 目标检测（ONNX Runtime 推理）
│   ├── TemplateMatch.cpp/h     ←   模板匹配（多方法/旋转/NMS）
│   └── ThresholdTool.cpp/h     ←   图像处理管线（灰度/模糊/Canny/二值化）
│
├── Renderer/                   ← 渲染模块
│   └── FontManager.cpp/h       ←   中文字体加载
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
| ROI 管理 | 交互式创建/选中/拖动/缩放/删除感兴趣区域，多类型颜色区分 |
| 模板匹配 | 多实例模板匹配，旋转/NMS/阈值，结果可视化 |
| YOLO 检测 | ONNX Runtime 推理 YOLO11 ONNX 模型，支持 ROI 限定区域，NMS 后处理 |
| 工具实例 | 手风琴式工具面板，每实例独立参数（模板/ROI/角度/预处理） |
| 批量执行 | 全部执行（逐帧高亮）+ 单步执行（点击推进）+ 循环模式 |
| 配方系统 | 保存/加载全部工具实例参数、模板图片、搜索ROI（JSON） |
| 主题切换 | 夜间/白天模式，自动持久化 |
| 日志系统 | 三级日志（INFO/WARN/ERROR），颜色分级，2000 条上限 |

---

## 🔧 工具实例系统

功能窗口（手风琴布局）支持 5 种工具类型：

| 类型 | 功能 | 独立参数 |
|------|------|---------|
| 边缘检测 | Canny 边缘检测 | 低/高阈值、灰度开关 |
| 模板匹配 | 多实例模板匹配 | 模板图、搜索ROI、旋转角度、匹配阈值/NMS、模板预处理、图像预处理 |
| Blob分析 | 斑点分析 | 面积范围 |
| 阈值调试 | 图像处理管线 | 灰度/模糊/二值化/Canny 全套 |
| YOLO检测 | ONNX 目标检测 | 模型文件、类别文件、置信度/NMS阈值、ROI限定 |

每个实例的参数完全独立，互不影响。模板图像和搜索 ROI 按实例保存。

### 执行模式（枚举状态机）

```cpp
enum class ExecMode { Idle, BatchRunning, StepRunning, DelayForResult, DelayForLoad };
```

- **全部执行**：逐帧自动执行所有工具，当前实例蓝色高亮，完成后显示总耗时。支持多图片切换 + 循环模式
- **单步执行**：点一次执行一个工具，按钮变蓝"单步中..."，执行完保持高亮，显示单步耗时
- **循环**：自动重复"全部执行"，按钮变绿"循环中"

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
- **全部工具实例**：类型、模板图片（PNG）、搜索 ROI、旋转参数、匹配参数、模板预处理、图像预处理、边缘检测参数、阈值调试参数

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

**不需要额外安装 OpenCV 或配置任何路径**，所有第三方依赖已包含在项目中。

