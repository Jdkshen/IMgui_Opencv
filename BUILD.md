# Migui_Opencv 构建指南

基于 **Dear ImGui + DirectX 12 + OpenCV** 的 Windows 桌面视觉工具。

---

## 环境要求

| 工具 | 版本 |
|------|------|
| Windows | 10/11 |
| Visual Studio | 2022 (Community) |
| OpenCV | 4.12.0 |
| DirectX 12 | Windows SDK 自带 |

---

## 安装步骤

### 1. 安装 Visual Studio 2022
下载：https://visualstudio.microsoft.com/zh-hans/downloads/

安装时勾选 **"使用 C++ 的桌面开发"** 工作负载。

### 2. 安装 OpenCV 4.12.0
下载 Windows 包：https://opencv.org/releases/

解压到 `D:\opencv`，目录结构应为：
```
D:\opencv\
├── build\
│   ├── include\
│   └── x64\vc16\lib\
└── sources\
```

### 3. 修改路径（如果不同）
打开 `Windows_imgui.vcxproj`，搜索并替换：
- `D:\opencv` → 你的 OpenCV 路径
- DirectX 头文件已在项目中 (`DirectX-Headers-main/`)，无需额外修改

### 4. 拷贝 DLL
将 `D:\opencv\build\x64\vc16\bin\opencv_world4120.dll` 拷贝到 `x64\Debug\` 目录下（与 exe 同目录）。

---

## 编译运行

### VS2022
1. 打开 `Windows_imgui.slnx`
2. 选择 **x64 Debug** 配置
3. `Ctrl+Shift+B` 生成
4. `F5` 运行

### VS Code
1. 安装 C/C++ 扩展
2. `Ctrl+Shift+B` → 生成 (Debug)
3. `F5` → 运行调试

首次运行前确保 `x64\Debug\` 目录下有 `opencv_world4120.dll`。

---

## 运行时文件

exe 同目录需要：
- `opencv_world4120.dll`
- `simhei.ttf`（中文字体，项目中已包含）
- `theme.cfg`（自动生成）

---

## 项目结构

```
Windows_imgui/
├── Windows_imgui.cpp          # 程序入口
├── Windows_imgui.h            # 公共头文件
├── Core/                      # DX12 上下文、文件选择、图像加载
├── UI/                        # DockSpace、侧边栏、日志、图像预览
├── OpenCV/                    # 阈值处理、模板匹配
├── Renderer/                  # 字体管理
├── log/                       # 日志系统
├── imgui/                     # Dear ImGui 库
├── DirectX-Headers-main/      # DirectX 12 头文件
└── .vscode/                   # VS Code 配置
```
