# IMgui_Opencv 构建基线

本项目是基于 Dear ImGui、DirectX 12、OpenCV 5.0 和 ONNX Runtime 的 Windows 桌面视觉工具。

## 基线目标

当前基线要求：

- 能构建：`Windows_imgui.slnx` Release x64 构建通过。
- 能启动：`x64\Release\Windows_imgui.exe` 能正常创建进程。
- 能跑样例图：`RegressionTests.exe` 会加载 `assets\images\test.jpg`，设置 ROI，执行一个 `ITool` 工具，绘制结果 overlay，并验证配方保存/加载。

## 依赖规则

构建只依赖仓库内依赖，不使用本机硬编码路径：

| 目录 | 用途 |
| --- | --- |
| `include/opencv/` | OpenCV 5.0 头文件 |
| `include/onnxruntime/` | ONNX Runtime C/C++ API 头文件 |
| `include/ncnn/` | NCNN OCR 推理头文件 |
| `include/directx/` | DX12 辅助头文件 |
| `include/nlohmann/` | JSON 头文件 |
| `redist/` | 本地或 Release 包恢复的 OpenCV、ONNX Runtime、DirectML、NCNN `.lib/.dll` |
| `assets/fonts/` | 运行时字体资源 |
| `assets/images/` | 回归测试样例图，不复制到主程序输出目录 |
| `models/ppocrv6/` | OCR 默认 PP-OCRv6 tiny NCNN 模型和字典 |

不要把本机 OpenCV 目录、个人用户目录、VS 本机安装目录写进项目属性。工程属性应使用 `$(ProjectDir)`、`$(OutDir)` 这类相对宏。

## 构建命令

Release x64：

```bat
msbuild Windows_imgui.slnx /p:Configuration=Release /p:Platform=x64 /m
```

构建产物：

| 产物 | 路径 |
| --- | --- |
| 主程序 | `x64\Release\Windows_imgui.exe` |
| 回归测试 | `x64\Release\RegressionTests.exe` |

`Windows_imgui.slnx` 已包含：

- `Windows_imgui.vcxproj`
- `Test\RegressionTests.vcxproj`

所以解决方案级构建会同时验证主程序和最小回归测试是否还能编译。

## 运行测试

先构建 Release，再运行：

```bat
x64\Release\RegressionTests.exe
```

也可以运行：

```bat
run_tasks.bat
```

当前测试覆盖：

- 模板匹配基础定位。
- ROI 坐标转换。
- YOLO 无模型时的失败路径。
- 配方保存/加载 round-trip。
- 样例图核心链路：加载图片、设置 ROI、执行 `ITool`、生成 `ToolResult`、绘制 overlay。

## PostBuild 规则

主程序 PostBuild 会复制运行所需文件：

- `assets\fonts\*`
- `redist\onnxruntime*.dll`
- `redist\ncnn.dll`
- `redist\opencv_world500.dll`
- `redist\opencv_videoio_ffmpeg500_64.dll`
- `redist\opencv_videoio_msmf500_64.dll`
- `redist\DirectML.dll`
- `models\*.onnx` 和 `models\*.txt`，如果本机存在
- `models\ppocrv6\*`，如果本机存在

PostBuild 会清理输出目录里的旧 OpenCV 4.x DLL，避免混用 OpenCV 版本。

## 仓库清理规则

不应入库：

- `x64/`、`Debug/`、`Release/`
- `.vs/`
- `*.obj`、`*.pdb`、`*.ilk`、`*.tlog`
- `imgui.ini`、`theme.cfg`
- 日志和本机运行状态

大模型和视频建议走 Git LFS；`Test/` 下遗留的 Python 下载/生成脚本只作为可选辅助，不属于当前构建和回归测试必需链路：

- `*.onnx`
- `*.pt`
- `*.mp4`
- `*.avi`
- `*.mov`
- `*.mkv`

仓库内 `include/` 是构建基线的一部分。`redist/` 需要保留目录和说明文件，体积较大的 `.dll/.lib` 建议通过 Git LFS、下载脚本或 GitHub Release 的 `runtime.zip` 恢复；本地构建前需确保 `redist/` 至少包含 OpenCV 5.0、ONNX Runtime、DirectML、NCNN 对应运行时。

## 运行时包建议

后续发布时建议制作 `runtime.zip`，解压后目录结构保持：

```text
redist/
  opencv_world500.dll
  opencv_videoio_ffmpeg500_64.dll
  opencv_videoio_msmf500_64.dll
  onnxruntime*.dll
  DirectML.dll
  ncnn.dll
  ncnn.lib
```
