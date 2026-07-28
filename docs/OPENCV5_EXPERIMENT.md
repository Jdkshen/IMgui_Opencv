# OpenCV 5.0 YOLO 实验功能

> 文档同步日期：2026-07-27。主进程 DNN 和独立 helper 的当前数据传递方式已核对；该实验工具仍走统一 `ToolResult` 与任务输入链。


当前工程已统一切到仓库内置 OpenCV 5.0；`YOLO OpenCV 5.0` 仍保留为 OpenCV DNN 后端，用于和 ONNX Runtime YOLO 工具对比。

## 当前状态

原工具：
- 名称：`YOLO检测`
- 分类：`检测与识别`
- 后端：ONNX Runtime
- type id：`4`

实验工具：
- 名称：`YOLO OpenCV 5.0`
- 分类：`实验功能`
- type id：`11`

当前仓库只保留主程序需要的 OpenCV 5.0 运行时：
- `Windows_imgui.vcxproj` 链接本地 `redist/opencv_world500.lib` / `redist/opencv_world500d.lib`
- 运行 DLL 来自本地或 Release 包恢复的 `redist/opencv_world500*.dll`
- videoio 插件来自 `redist/opencv_videoio_ffmpeg500_64.dll`、`redist/opencv_videoio_msmf500_64.dll`
- 不依赖任何本机 OpenCV 安装路径

实验页里的“执行内置 OpenCV DNN 测试”现在走主进程 OpenCV 5.0 DNN 路径，可用于和原来的 ONNX Runtime YOLO 工具对比。

独立 helper 的测速入口仍然保留：
- `运行 OpenCV 5.0 Helper 测速`

这个按钮会启动独立进程：
- `x64\Release\opencv5_helper\opencv5_yolo_helper.exe`

按钮会把当前图片或摄像头帧写入临时 BGR raw 文件，把路径、宽高、通道、阈值等参数传给 helper；helper 退出后主程序删除临时文件。日志输出后端、`forward_ms`、`wall_ms`、检测数量和退出码。该路径用于隔离测试 classic/new graph engine，不与主进程 DNN 会话共享状态。

helper 和主程序一样使用项目本地 `redist/` 的 OpenCV 5.0 world DLL，不再依赖本机安装目录。

## OpenCV 5.0 仓库依赖

构建信息：
- 配置：Debug/Release x64
- 头文件：`include/opencv`
- 主程序链接库：本地 `redist/opencv_world500.lib` / `redist/opencv_world500d.lib`
- 主程序运行 DLL：本地或 Release 包恢复的 `redist/opencv_world500.dll` / `redist/opencv_world500d.dll`
- helper 使用同一套 `redist/` OpenCV 5.0 runtime

旧的 `opencv_core500.dll`、`opencv_imgproc500.dll`、`opencv_dnn500.dll` 已从 helper 目录移到 `old-dnn-dlls-*` 备份目录，避免和新的 world DLL 混用。

## Helper 用法

直接运行 exe 不带参数会闪一下退出，这是正常的。需要传入模型参数：

```powershell
x64\Release\opencv5_helper\opencv5_yolo_helper.exe models\yolo11n.onnx 10 classic 320
x64\Release\opencv5_helper\opencv5_yolo_helper.exe models\yolo11n.onnx 10 new 320
```

实际图像检测模式：

```powershell
x64\Release\opencv5_helper\opencv5_yolo_helper.exe models\yolo11n.onnx 10 classic 320 --raw-bgr frame.raw 640 480 3 0.25 0.45
```

也可以双击：

```text
x64\Release\opencv5_helper\run_opencv5_yolo_benchmark.bat
```

`yolo11n.onnx` 当前输入尺寸按 `320` 测试。使用 `640` 会触发 reshape 相关错误。

## 已测结果参考

CPU：AMD 5800H，无独立 GPU。

用户实测：
- OpenCV 5.0 alpha new graph engine：稳定约 `27-30ms`
- OpenCV 5.0 alpha classic engine：稳定约 `24-26ms`

当前看起来 classic engine 比 new graph engine 更快。OpenCV 5.0 alpha 的 new graph engine 还会提示 target 暂不支持，因此这个实验结果不能直接理解为“5.0 一定更快”。

## OpenCV 5.0 迁移注意

不要在同一个 exe 里混用不同 OpenCV world 版本，也不要把零散 `opencv_core/imgproc/dnn` DLL 与 `opencv_world500*.dll` 混放。当前工程统一使用 OpenCV 5.0，PostBuild 会清理输出目录里的旧版本 OpenCV DLL，并只复制匹配的 `opencv_world500*.dll`。

OpenCV 5.0 把部分几何 API 拆到新头文件，主程序里使用 `contourArea`、`matchShapes`、`getRotationMatrix2D` 等函数的文件需要显式包含：

```cpp
#include <opencv2/geometry.hpp>
```
