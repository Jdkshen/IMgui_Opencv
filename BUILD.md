# IMgui_Opencv 构建指南

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具。

---

## 环境要求

| 工具 | 版本 |
|------|------|
| Windows | 10/11 |
| Visual Studio | 2022（勾选"使用 C++ 的桌面开发"） |
| GPU | 支持 DirectX 12 |

**不需要额外安装 OpenCV**，所有头文件、库和 DLL 已包含在项目中。

---

## 项目自带依赖

| 目录 | 内容 | 用途 |
|------|------|------|
| `include/opencv/` | OpenCV 头文件 | 编译时 |
| `redist/` | OpenCV .lib + .dll + VC++ 运行时 | 链接 + 运行时 |
| `imgui/` | Dear ImGui 源码 | 编译时 |
| `DirectX-Headers-main/` | DX12 辅助头文件（d3dx12.h） | 编译时 |
| `assets/fonts/` | 中文字体 | 运行时（自动复制） |
| `assets/images/` | 测试图片 | 运行时（自动复制） |

编译时 PostBuild 事件会自动把 `assets/` 和配置文件复制到输出目录。

---

## 编译运行

### VS2022

1. 打开 `Windows_imgui.slnx`
2. 选择 **x64 Debug**（或 Release）
3. `Ctrl+Shift+B` 生成
4. `F5` 调试运行

### VS Code

1. `Ctrl+Shift+B` → 选择 **生成 (Debug)**
2. `F5` → 调试运行

---

## 项目结构

```
IMgui_Opencv/
├── assets/                     # 资源文件（自动复制到输出）
├── Core/                       # DX12 上下文、文件选择、图像加载
├── UI/                         # DockSpace、图像预览、ROI 管理
├── Algorithm/                  # 阈值处理、模板匹配
├── Renderer/                   # 字体管理
├── Log/                        # 日志系统
├── imgui/                      # Dear ImGui 库（第三方）
├── DirectX-Headers-main/       # DX12 辅助头文件（第三方）
├── include/opencv/              # OpenCV 头文件（第三方）
├── redist/                     # OpenCV + VC++ lib/dll（第三方）
└── Windows_imgui.slnx          # VS2022 解决方案
```

## 搬到其他电脑

1. 复制整个 `IMgui_Opencv` 文件夹
2. 安装 VS2022（一次性）
3. 打开 `Windows_imgui.slnx`，直接编译运行

所有路径使用 `$(ProjectDir)` 相对路径，无需任何手动配置。
