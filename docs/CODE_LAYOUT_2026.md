# 代码目录与存储规则

> 文档同步日期：2026-08-09。当前边界已包含任务分组、独立输入、并行执行、PLC IO 映射、工具 UI 资源和握手回归。


仓库按职责归属组织。生产代码应放入现有模块目录，不要把新的业务源码堆到项目根目录。

## 源码模块

| 目录 | 职责 | 典型内容 |
| --- | --- | --- |
| `Core/` | 运行状态、图像/ROI、任务调度、配方、结果发布、标定和 Fixture | `VisionContext`、`ImageState`、`ToolController`、`RecipeManager` |
| `Algorithm/` | 无 UI 的图像处理与检测算子，通过 `ITool` 接收 `VisionContext` 并返回 `ToolResult` | 边缘、阈值、Blob、模板、YOLO、OCR、条码、卡尺等 |
| `UI/` | Dear ImGui 窗口、参数收集、ROI 编辑和结果展示 | `ToolsWindow`、`ImageViewer`、`ROIManager`、`RunResultWindow` |
| `Renderer/` | 仅渲染相关的辅助 | 字体、图标和 `PreviewTextureCache` |
| `Log/` | 线程安全日志实现 | 日志存储、格式化和日志窗口 |
| `Test/` | C++ 回归工程和测试夹具 | 配方往返、判定、ROI、卡尺、任务输入、硬件回退与 PLC 握手 |
| `tools/plc_simulator/` | 独立 Python/Tkinter 联调工具 | Modbus TCP Server、任务01～16 Trigger 和 ACK 模拟 |
| `docs/` | 当前说明、API 参考、构建说明和历史设计资料 | 架构、算法、ImGui、性能和 Roadmap |
| `include/` | 第三方头文件和 vendored 库 | OpenCV、ImGui、nlohmann/json、ZXing-cpp |
| `assets/` | 小型测试图片和字体 | QR/OCR 测试图片、运行字体 |
| `models/` | 可选本地模型 | PP-OCRv6 资产和本地测试模型 |
| `redist/` | 运行时 DLL 和链接库 | OpenCV、ONNX Runtime、DirectML、NCNN、open62541 |

## 分层规则

1. `UI` 收集参数和显示状态，通过 `Core/ToolController` 请求执行，不实现算法细节。
2. `Core` 持有执行上下文与调度，组合 `VisionContext`、`ITool`、`ToolInstance` 和 `ToolResult`。
3. `Algorithm` 不读取 UI 或已删除的历史图像/ROI 全局状态。工具通过 `VisionContext` 接收图像与 ROI。
4. `ToolInstance` 保存实例参数、标签、ROI 绑定和判定设置；`lastResult`、`measureRuntimeROIIds` 等运行时字段不写入配方。
5. `Renderer` 和 `Log` 不得成为另一套应用状态存储。
6. PLC/相机协议在 Core 后台线程轮询，但工具执行、图像发布和 ImGui 状态更新必须回到 UI 主线程；窗口代码不直接操作协议套接字。

## 配方存储

- 受版本控制的源案例：`docs/recipe_examples/all_tools_test.recipe`
- 普通 Release 可执行文件的运行副本：
  `x64/Release/recipes/全工具测试.recipe`
- 验证可执行文件的运行副本：
  `x64/ReleaseVerify/recipes/全工具测试.recipe`
- 测试配方的模板图放在运行配方旁边。
- `x64/`、`Debug/`、`Release/` 下的构建输出会被忽略，不能当作源码。

当前工具范围是 type `0` 到 `17`，type `12` 是原图链路节点。`all_tools_test.recipe` 仍保留 version 2，用于验证当前 version 5 加载器的向后兼容；YOLO 条目需要有效的本地模型路径才能通过推理。另提供 2、4、6、8、10、12、16 任务的 version 5 中文完整案例。

任务定义位于配方 `taskGroups`，工具通过 `groupName` 关联任务。正式任务最多 16 个，任务输入路径、文件夹进度和相机优先设置都属于配方数据。

## 新增代码

- 新状态或编排：放入 `Core/`。
- 新检测算子：放入 `Algorithm/`；进入工具链时实现 `ITool`。
- 新控件或显示：放入 `UI/`。
- 新回归：放入 `Test/` 并更新测试工程文件。
- 新设计或操作说明：放入 `docs/` 并更新文档索引。
- 不要把 `.obj`、`.exe`、`.dll`、`.pdb`、运行配方副本或用户设置放到源码根目录。

## 构建验证

```powershell
msbuild Windows_imgui.slnx /p:Configuration=Release /p:Platform=x64 /m
.\Test\x64\Release\RegressionTests.exe
.\Test\x64\Release\RegressionTests.exe --plc-handshake-only
python .\tools\plc_simulator\plc_simulator.py --self-test
```

若 `msbuild` 不在 `PATH`，请从“Developer PowerShell for VS 2022”执行，或使用本机 `vswhere` 查找 MSBuild；不要把某个开发者的 Visual Studio 绝对路径写入工程文件。
