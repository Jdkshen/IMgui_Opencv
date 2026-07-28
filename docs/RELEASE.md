# 发布与持续集成

> 文档同步日期：2026-07-28。本文说明当前构建、打包、CI、PLC 联调和发布验收规则。

## 1. 当前发布基线

2026-07-25 的 `codex/p0-p4-release` 已合并到默认分支 `master`，合并提交为 `64ddcba`。该基线包含：

- UI/Core/Algorithm 边界收敛；
- 旋转 ROI、Fixture、几何绘制和工业测量；
- 运行结果总览、预览纹理缓存和结果导出；
- 配方自动保存、硬件设置持久化、帧归档和 SPC；
- 通用相机、TCP、Modbus、PLC 和 OPC UA；
- 工具依赖、判定、失败停止和回归基础设施。

2026-07-26 工作树进一步加入最多 16 个任务、独立单图/文件夹、相机优先、全部/当前任务单步、最多 4 任务并行和任务结果图。2026-07-27 又加入 Modbus TCP IO 映射、16 任务 Trigger、单槽 Busy/Done/ACK 握手、自动重连和独立 PLC 模拟器。2026-07-28 完成待执行槽、ACK 超时复位、Trigger 映射同步及标准主程序干净重建。当前本地验证状态见 [STATUS_2026-07-28.md](STATUS_2026-07-28.md)；这些修改正式提交和发布前，不应把状态快照等同于新的 GitHub Release。

## 2. 本地运行包

先构建 Release x64，再执行：

```powershell
pwsh -File .\scripts\package_runtime.ps1
```

脚本生成 `dist/IMgui_Opencv-Release-x64.zip`，并在压缩后校验必要文件。运行包包含：

- `Windows_imgui.exe`；
- PostBuild 复制的 OpenCV、ONNX Runtime、DirectML、NCNN 等运行时；
- 字体和运行配置；
- 生产配方、案例配方及其必要图片；
- OCR 资产和存在于本机的可选模型；
- 硬件说明与 open62541 许可证材料。

当前本地发布基线是 2026-07-28 Clean 后全量构建生成的标准文件 `x64\Release\Windows_imgui.exe`，SHA256 为 `33A5F696EF4C628C5634F678B25D662EE20321158CF743A616A349B8F526FFA2`。2026-07-27 的 `Windows_imgui_updated.exe` 仅是避开旧进程文件锁的历史临时成品，不应装入当前正式运行包。

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
- 完整回归全部通过，包括任务输入、相机回退、任务并行和 PLC 连续触发丢弃策略。
- `RegressionTests.exe --plc-handshake-only` 与 `python tools\plc_simulator\plc_simulator.py --self-test` 通过。
- 用模拟器至少完成一次“任务01 Trigger → Busy → Done/结果 → ACK → 清除”和一次非任务01 Trigger 联调。
- `git diff --check` 通过，Markdown 围栏和文档链接无明显错误。
- 运行包至少包含主程序、OpenCV 5、DirectML、NCNN 和 ONNX Runtime 必要 DLL。
- 在干净目录解压运行包后，程序能启动并加载一个无需可选模型的案例配方。
- README、Roadmap、构建说明、第三方声明和本次状态快照已同步。

## 5. Release 命名建议

- 标签：`vMAJOR.MINOR.PATCH`。
- 附件：`IMgui_Opencv-Release-x64.zip`。
- 发布说明列出用户可见变化、配方兼容版本、需要的可选模型/设备 SDK、完整回归结果和已知限制。
- 不上传 `x64/`、`.vs/`、PDB、日志或用户本机配置。
