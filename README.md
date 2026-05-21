# IMgui_Opencv

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具应用。

---

## 📁 项目结构

```
IMgui_Opencv/
├── assets/                     ← 资源文件（编译时自动复制到输出目录）
│   ├── fonts/simhei.ttf        ←   黑体
│   ├── fonts/simsun.ttc        ←   宋体
│   └── images/test.jpg         ←   测试图片
│
├── Core/                       ← 核心模块
│   ├── DX12Context.cpp/h       ←   DX12 辅助上下文、全局变量
│   ├── OpenCVTest.cpp/h        ←   图片读取 + GPU 纹理上传
│   └── OpenFileDialog.cpp/h    ←   文件选择对话框
│
├── UI/                         ← 界面模块
│   ├── DockSpaceHost.cpp/h     ←   主停靠空间 + 菜单栏 + 侧边栏
│   ├── ImageViewer.cpp/h       ←   图片预览、缩放平移、ROI 交互
│   └── ROIManager.cpp/h        ←   ROI 数据结构、坐标转换
│
├── Algorithm/                  ← 图像算法
│   ├── TemplateMatch.cpp/h     ←   模板匹配
│   └── ThresholdTool.cpp/h     ←   阈值/模糊/Canny 处理
│
├── Renderer/                   ← 渲染模块
│   └── FontManager.cpp/h       ←   中文字体加载（自动定位 exe 目录）
│
├── Log/                        ← 日志系统
│   └── LogSystem.cpp/h         ←   线程安全日志（颜色/时间戳）
│
├── imgui/                      ← 第三方：Dear ImGui 库
├── DirectX-Headers-main/       ← 第三方：DX12 辅助头文件（d3dx12.h）
├── include/opencv/             ← 第三方：OpenCV 头文件
├── redist/                     ← 第三方：OpenCV + VC++ 库与运行时 DLL
│
├── Windows_imgui.cpp           ← 程序入口 + DX12 设备管理 + 主循环
├── Windows_imgui.h             ← 公共头文件
├── framework.h                 ← 系统头文件
├── theme.cfg                   ← 主题配置（自动复制到输出）
├── Windows_imgui.slnx          ← VS2022 解决方案（全部相对路径）
└── Windows_imgui.vcxproj       ← 项目文件
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
| 图片加载 | 支持 JPG/PNG/BMP，通过文件对话框选择 |
| 图像处理 | 灰度化、高斯模糊、二值化、Canny 边缘检测 |
| ROI 管理 | 交互式创建/选中/拖动/缩放/删除感兴趣区域 |
| 模板匹配 | 图像模板匹配及结果可视化 |
| 主题切换 | 夜间/白天模式，自动持久化 |
| 日志系统 | 三级日志（INFO/WARN/ERROR），颜色分级，2000 条上限 |

## 🚀 搬到其他电脑

1. 复制整个项目文件夹
2. 安装 VS2022（勾选"使用 C++ 的桌面开发"）
3. 打开 `Windows_imgui.slnx`，直接编译运行

**不需要额外安装 OpenCV 或配置任何路径**，所有第三方依赖已包含在项目中。

