# 视频播放 & 音频模块开发记录

> 日期：2026-05-28 ~ 2026-05-29

---

## 新增模块

### 1. `Core/VideoCapture` — 视频/摄像头播放

| 文件 | 说明 |
|------|------|
| `Core/VideoCapture.h` | 视频播放接口（打开/关闭/播放/暂停/跳帧等） |
| `Core/VideoCapture.cpp` | 基于 OpenCV `cv::VideoCapture` 实现 |

**功能：**
- 打开视频文件 / 摄像头
- 播放、暂停、停止、循环
- 帧滑动条跳转、FPS 显示
- 中文路径支持（UTF-8 → 8.3 短路径名）

**状态机：**

```
OpenVideo(path) → 显示第一帧，等待用户点击"播放"
    ↓ 点击播放
Play() → s_Playing=true → Update() 按 FPS 速率读取帧
    ↓ 点击暂停
Pause() → Update() 停止读取，画面定格
    ↓ 点击停止
Stop() → 回到第 0 帧
    ↓ 点击关闭
Close() → 释放 cv::VideoCapture + 清空画面
```

### 2. `Core/AudioPlayer` — 音频播放

| 文件 | 说明 |
|------|------|
| `Core/AudioPlayer.h` | 音频播放接口 |
| `Core/AudioPlayer.cpp` | XAudio2 + Media Foundation 实现 |

**架构：**

```
视频文件
  ├─→ cv::VideoCapture → OpenCV → 视频帧 → DX12 纹理 → ImGui 显示
  └─→ IMFSourceReader → PCM 采样 → XAudio2 → 扬声器
       (Media Foundation)            (DirectX)
```

**与 VideoCapture 同步：**

| VideoCapture 操作 | AudioPlayer 同步 |
|-------------------|-----------------|
| `OpenVideo()` | `AudioPlayer::Open()` |
| `Play()` | `AudioPlayer::Play()` |
| `Pause()` | `AudioPlayer::Pause()` |
| `Stop()` | `AudioPlayer::Stop()` |
| `Close()` | `AudioPlayer::Close()` |
| `SeekFrame()` | `AudioPlayer::Seek()` |

无音频流的文件不会报错，静默跳过。

---

## UI 改动

### `UI/ImageViewer.cpp` — 视频控制栏

打开视频后，工具栏下方出现控制栏：

| 按钮 | 颜色 | 功能 |
|------|------|------|
| 播放 / 暂停 | 绿 🟢 / 橙 🟠 | `TogglePlay()` |
| 停止 | 红 🔴 | `Stop()` 回到开头 |
| 关闭 | 灰 ⬜ | `Close()` 释放资源 |
| 帧滑动条 | — | 拖拽跳转到指定帧 |
| 循环 | — | 循环播放开关 |
| FPS 指示 | 绿/灰 | 实时帧率 + 视频/摄像头标识 |

使用 ImGui `PushStyleColor` 原生 API 实现彩色按钮，不依赖 Unicode 符号或特殊字体。

---

## 修复的问题

| # | 问题 | 根因 | 修复 |
|---|------|------|------|
| 1 | `ImVec2`/`ImU32` 找不到 | `DockSpaceHost.h`/`ImageViewer.h`/`ROIManager.h` 使用 ImGui 类型但未 include | 添加 `#include "../imgui/imgui.h"` |
| 2 | `SliderInt` 编译错误 | ImGui 1.92.8 不接受额外格式参数 | 改为 `"%d"` |
| 3 | 按钮显示 `?` | ImGui 默认字体无 Unicode 字形 | 改用 ImGui `PushStyleColor` 彩色按钮 |
| 4 | 打开视频直接播放 | `Update()` 暂停时仍每帧 `read()` | 拆分为 `ReadFrame()` + `Update()`，暂停时 `Update()` 直接返回 |
| 5 | 关闭视频画面不清 | `Close()` 只释放 capture 不清图片 | 添加 `UI::ClearImage()` |
| 6 | 中文路径 `?` | `cv::VideoCapture::open()` 不接受 `wchar_t*` | 用 `GetShortPathNameW` 获取 8.3 短路径 |
| 7 | 关闭软件弹窗崩溃 | 后台线程未 join 触发 `std::terminate()` | 主清理流程添加 `VideoCapture::Close()` |
| 8 | 播放无声音 | OpenCV 不处理音频 | 新增 `AudioPlayer` 模块 |

---

## 新增依赖

| 库 | 用途 |
|----|------|
| `xaudio2.lib` | DirectX 音频播放 |
| `mfplat.lib` | Media Foundation 平台 |
| `mfreadwrite.lib` | Media Foundation 流读取 |
| `mfuuid.lib` | Media Foundation UUID |

---

## 项目文件更新

- `Windows_imgui.vcxproj` — 添加 `AudioPlayer.h/cpp`
- `Windows_imgui.vcxproj.filters` — 同步筛选器
- `Windows_imgui.cpp` — 主清理流程添加 `VideoCapture::Close()`
