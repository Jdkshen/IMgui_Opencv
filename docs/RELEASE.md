# 发布与持续集成

> 文档同步日期：2026-08-16。本文说明当前双图形后端构建、字体/图标/模型资源打包、CI、PLC 联调和唯一正式发布包规则。

## 1. 当前发布基线

2026-07-25 的 `codex/p0-p4-release` 已合并到默认分支 `master`，合并提交为 `64ddcba`。该基线包含：

- UI/Core/Algorithm 边界收敛；
- 旋转 ROI、Fixture、几何绘制和工业测量；
- 运行结果总览、预览纹理缓存和结果导出；
- 配方自动保存、硬件设置持久化、帧归档和 SPC；
- 通用相机、TCP、Modbus、PLC 和 OPC UA；
- 工具依赖、判定、失败停止和回归基础设施。

2026-07-26 工作树进一步加入最多 16 个任务、独立单图/文件夹、相机优先、全部/当前任务单步、最多 4 任务并行和任务结果图。2026-07-27 又加入 Modbus TCP IO 映射、16 任务 Trigger、单槽 Busy/Done/ACK 握手、自动重连和独立 PLC 模拟器。2026-07-28 完成待执行槽、ACK 超时复位、Trigger 映射同步及标准主程序干净重建。2026-07-30 增加完整保留 DX12 的独立 DX11 后端、统一渲染契约与自动回退。2026-08-02 完成独立流程图窗口、任务/工具面板对齐，并把工具 PNG 纳入 PostBuild 和运行包。2026-08-05 完成工具输入、ROI、上下游结果和统一输出的自动回归覆盖。2026-08-16 统一视频 YOLO、多相机、高 DPI 和发布目录。当前本地验证状态见 [STATUS_2026-08-16.md](STATUS_2026-08-16.md)；这些修改正式提交和发布前，不应把状态快照等同于新的 GitHub Release。

## 2. 本地运行包

先构建 Release x64，再执行：

```powershell
pwsh -File .\scripts\package_runtime.ps1
```

脚本生成唯一正式目录 `dist/IMgui_Opencv-Release-x64/` 和唯一正式压缩包 `dist/IMgui_Opencv-Release-x64.zip`，并在压缩后校验必要文件。默认打包成功后会删除同名前缀的日期版、`hidpi` 版等历史变体。运行包包含：

- `Windows_imgui.exe`；
- `Start_Diagnostics.cmd`：先检查必须 DLL，再启动主程序；Win10 双击无反应时优先运行它，并查看 `%LOCALAPPDATA%\IMgui_Opencv\startup.log` 与同目录崩溃转储；
- PostBuild 复制的 OpenCV、ONNX Runtime、DirectML、NCNN 等运行时；
- 中文字体、`assets/icons/` 工具 PNG 和运行配置；
- 生产配方、案例配方及其必要图片；其中包含 2、4、6、8、10、12、16 任务的中文案例和一份共用测试图片；
- OCR 资产和存在于本机的可选模型；
- 默认 YOLO 模型 `models/yolo11n.onnx` 与 `models/coco_classes.txt`；
- 多相机、海康、华睿和硬件接入说明；
- 硬件说明与 open62541 许可证材料。

当前标准 ZIP 的大小和 SHA256 见 [STATUS_2026-08-16.md](STATUS_2026-08-16.md)。若主程序仍在运行，Windows 会锁定 EXE 或运行时文件；应关闭程序后重新构建和打包，不再通过日期后缀、`hidpi` 或 `_updated` 文件名规避文件锁。

默认不包含约 333 MB 的可选 CUDA provider，因为当前 GPU 路径使用 DirectML。确有需要时执行：

```powershell
pwsh -File .\scripts\package_runtime.ps1 -IncludeCudaProvider
```

缺少必要 DLL、必要案例资源或运行包 ZIP 时，打包/CI 必须失败；缺少明确标记为可选的 YOLO/OCR 模型时，运行包可以跳过该模型，但程序应给出清晰提示。

## 3. 持续集成

`.github/workflows/windows-build.yml` 在 Windows clean runner 上：

1. 拉取源码和 Git LFS 依赖；
2. 构建 `Windows_imgui.vcxproj`；
3. 构建 `Test/RegressionTests.vcxproj`；
4. 运行完整 `RegressionTests.exe`，不是只跑专项子集；
5. 生成并检查运行包；
6. 上传 artifact。

Winsock 和 IP Helper 通过系统库 `Ws2_32.lib`、`Iphlpapi.lib` 链接，不额外带 DLL。open62541 v1.4.17 静态链接，运行包保留其版本和 MPL-2.0 声明。厂商相机/PLC SDK DLL 仍属于可选部署依赖。

## 4. 发布验收

- `Windows_imgui.vcxproj` Release x64 零错误构建。
- `Test/RegressionTests.vcxproj` Release x64 零错误构建。
- Debug x64 和 Release x64 均需编译通过，并分别运行完整回归。
- 完整回归全部通过，包括任务输入、相机回退、任务并行和 PLC 连续触发丢弃策略。
- 在脱离源码目录的位置启动，中文标签完整，添加工具弹窗显示 12 个工具 PNG 图标；不得依赖 `..\..\assets\icons` 回退路径。
- 右侧工具区在 1280×720 下仍能看到模式按钮和“`N 个工具`”状态，任务设置中的“绑定相机”文字不裁切。
- 默认启动日志确认选择 DirectX 12；设置 `IMGUI_OPENCV_RENDER_BACKEND=dx11` 后确认 DirectX 11 路径可进入主循环。
- DX12 初始化失败日志必须包含失败原因、自动回退动作和 DX11 回退结果。
- DX11 与 DX12 的布局、字体、图片显示、窗口缩放、多视口和最大化行为使用同一套 UI 回归清单。
- `RegressionTests.exe --plc-handshake-only` 与 `python tools\plc_simulator\plc_simulator.py --self-test` 通过。
- 用模拟器至少完成一次“任务01 Trigger → Busy → Done/结果 → ACK → 清除”和一次非任务01 Trigger 联调。
- `git diff --check` 通过，Markdown 围栏和文档链接无明显错误。
- 运行包至少包含主程序、OpenCV 5、DirectML、NCNN、ONNX Runtime，以及与构建工具集匹配的 x64 MSVC CRT/OpenMP DLL。打包脚本会从 `VCToolsRedistDir` 或 Visual Studio 官方 Redist 目录自动定位，并在 ZIP 校验阶段拒绝缺少 `msvcp140_atomic_wait.dll`、`vcruntime140_1.dll`、`vcomp140.dll` 等运行库的包。
- 在干净目录解压运行包后，程序能启动，并通过「文件(F) → 打开配方...」加载一个无需可选模型的案例配方；多任务案例的同目录图片路径必须正常解析。
- README、Roadmap、构建说明、第三方声明和本次状态快照已同步。

## 5. Release 命名建议

- 标签：`vMAJOR.MINOR.PATCH`。
- 附件：`IMgui_Opencv-Release-x64.zip`。
- 发布说明列出用户可见变化、配方兼容版本、需要的可选模型/设备 SDK、完整回归结果和已知限制。
- 不上传 `x64/`、`.vs/`、PDB、日志或用户本机配置。
