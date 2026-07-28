# IMgui_Opencv 构建基线

> 文档同步日期：2026-07-28。构建、完整回归、PLC 握手专项和本轮成品状态见 [STATUS_2026-07-28.md](STATUS_2026-07-28.md)。


本项目是基于 Dear ImGui、DirectX 12、OpenCV 5.0 和 ONNX Runtime 的 Windows 桌面视觉工具。

## 基线目标

当前基线要求：

- 能构建：`Windows_imgui.slnx` Release x64 构建通过。
- 能启动：正常发布和当前验证均使用 `x64\Release\Windows_imgui.exe`；该标准文件已于 2026-07-28 完成 Clean 后的 Release x64 全量重建。
- 能跑完整回归：`RegressionTests.exe` 覆盖图像导入、ROI、全部工具基础路径、判定、配方兼容、任务独立输入、文件夹轮换、相机回退、任务并行、PLC 单槽握手和结果状态。

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
| 回归测试 | `Test\x64\Release\RegressionTests.exe` |

> 当前工作树说明：最新主程序是 `x64\Release\Windows_imgui.exe`，大小 4,406,784 字节，SHA256 为 `33A5F696EF4C628C5634F678B25D662EE20321158CF743A616A349B8F526FFA2`。2026-07-27 的 `Windows_imgui_updated.exe` 只是标准文件被旧进程占用时产生的历史临时成品，不再作为当前发布基线。

若构建被中断后出现 `LNK1103: 调试信息损坏`，应先对对应工程执行 Clean，再完整 Build；不要继续复用可能损坏的 OBJ/PDB 增量链接。

`Windows_imgui.slnx` 已包含：

- `Windows_imgui.vcxproj`
- `Test\RegressionTests.vcxproj`

所以解决方案级构建会同时验证主程序和最小回归测试是否还能编译。

## 运行测试

先构建 Release，再运行：

```bat
Test\x64\Release\RegressionTests.exe
```

也可以运行：

```bat
run_tasks.bat
```

当前完整测试覆盖：

- 图像导入、导航、图像状态和纹理上传请求边界。
- ROI、旋转 ROI、结果 ROI、Fixture、标定和卡尺测量。
- type 0-17 的主要算法、空输入和缺失模型路径。
- ToolResult 判定、失败停止、稳定工具 ID 和配方往返兼容。
- 任务顺序、禁用过滤、独立单图、递归文件夹轮换、相机失败回退和最多 4 任务并行。
- 结果导出、历史/SPC、设备协议适配和发布策略。

专项运行参数包括：

```bat
Test\x64\Release\RegressionTests.exe --policy-only
Test\x64\Release\RegressionTests.exe --caliper-only
Test\x64\Release\RegressionTests.exe --qr-only
Test\x64\Release\RegressionTests.exe --task-images-only
Test\x64\Release\RegressionTests.exe --hardware-camera-only
Test\x64\Release\RegressionTests.exe --plc-handshake-only
```

PLC 模拟器协议自测不需要启动主程序：

```bat
python tools\plc_simulator\plc_simulator.py --self-test
```

需要图形联调时双击 `tools\plc_simulator\run_plc_simulator.bat`，再按 [PLC 模拟器说明](../tools/plc_simulator/README.md) 配置主程序。

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
