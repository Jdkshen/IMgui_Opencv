# 任务执行说明

> 文档同步日期：2026-07-27。本文中的“任务”主要指开发工作项；产品内任务分组见 [TASK_GROUPS.md](TASK_GROUPS.md)，PLC 触发时序见 [HARDWARE_INTEGRATION.md](HARDWARE_INTEGRATION.md)。


本文档用于给开发者或 AI 助手派发项目任务。目标是让每个任务都有明确输入、修改范围、验收标准和文档同步要求。

## 当前开发工作项

> 注意：本节是项目开发清单，不是 PLC Trigger 运行队列。PLC 工业握手采用单槽模型，Busy 或等待 ACK 期间的新 Trigger 会被忽略，不会排队补跑。

后续开发按以下顺序推进，除非用户明确指定插队任务：

| 优先级 | 任务 | 状态与验收目标 | 主要文件 |
| --- | --- | --- | --- |
| P0-1 | 原图删除与输入源一致性 | ✅ 已完成：原图可删除；单工具、单步、批量执行统一输入回退；删除/移动清理执行缓存和结果 | `Core/ToolController.*`、`Core/ToolChainState.*`、`UI/ToolsWindow.*` |
| P0-1a | 图片/文件夹导入稳定性 | ✅ Core 导入服务统一单图、递归文件夹、空目录、缺失路径和导航状态清理；异步有效/失败解码均有回归 | `Core/ImageImportService.*`、`Core/AsyncImageLoader.*`、`UI/ImageViewer.*` |
| P0-2 | 案例配方自动加载执行 | ✅ 已完成回归：案例路径可迁移，OCR 模型相对路径解析，`case_pipeline` 自动执行验证 | `Core/RecipeManager.*`、`Test/regression_tests.cpp`、`docs/recipe_examples/` |
| P0-3 | 完整回归基线 | ✅ 已完成：完整 `RegressionTests.exe` 通过，不只运行专项参数 | `Test/RegressionTests.vcxproj`、`Test/regression_tests.cpp` |
| P1-1 | 稳定工具身份 | ✅ `ToolInstance.toolId`、`ToolResult.sourceToolId`、配方字段和上游 ID 优先解析；UI 的活动工具/实时检测引用别名已删除 | `Core/ToolInstance.h`、`Core/ToolChainState.*`、`Core/ToolExecutor.cpp` |
| P1-2 | ImageViewer/Core 边界 | ✅ ImageViewer 通过 `ImageState`、`FrameNavigation`、`ImageImportService`、`ImageViewState`、`ResultOverlayState` 获取图像、播放和叠加状态，不再直接调用视频/实时检测/工具链全局状态 | `UI/ImageViewer.*`、`Core/FrameNavigation.*`、`Core/ResultOverlayState.*` |
| P1-3 | TemplateMatch 去全局状态 | ✅ `TemplateMatch` 已收缩为无 UI 的模板辅助，参数和模板资产归实例/Core 服务 | `Algorithm/TemplateMatch.*`、`Algorithm/TemplateMatchingTool.*` |
| P1-4 | RecipeManager 轻量化 | ✅ `RecipeToolInstance` 改为 ToolInstance JSON + 资产快照组合 DTO；Save 不再反查实时工具链，Load 先解析资产再 Apply | `Core/RecipeManager.*`、`Core/ToolInstance.*` |
| P1-5 | ROIEditorState | ✅ 绘制、拖拽、Handle、Hover、连续 ROI、runtimeId、工具绑定和测量 ROI 恢复/同步/回滚/删除均已迁入 Core | `Core/ROIEditorState.*`、`Core/ToolROIService.*`、`UI/ROIManager.*` |
| P2-1 | Blob 与图像差分 | ✅ Blob 特征/筛选参数、聚合质量指标、统一命名测量项公差判定；图像差分参考图/差异区域/差异面积已完成 | `Algorithm/BlobTool.*`、`Algorithm/DifferenceTool.*`、`Core/ToolJudgement.*` |
| P2-2 | 标定与 Fixture 产品化 | ✅ 多点 X/Y 比例拟合、透视拟合、逐点残差、RMS/最大残差、标定文件导入导出和 Fixture 坐标轴已接入；后续补镜头畸变标定 | `Core/CalibrationFitter.*`、`Core/CalibrationModel.*`、`Core/FixtureTransform.*`, `UI/ImageViewer.*` |
| P2-3 | SPC 与报表 | ✅ 均值、标准差、Cp/Cpk、测量项选择、统计窗口、趋势图和 CSV 导出 | `Core/ResultExporter.*`、`Core/InspectionHistory.*`、`UI/StatsWindow.*` |
| P3-1 | 工具链体验 | ✅ 启用/禁用、Core 工具剪贴板复制粘贴、稳定 ID、运行前检查、依赖显示/循环校验、分组筛选及批量启用/标签/失败策略 | `Core/ToolChainPreflight.*`、`Core/ToolChainValidator.*`、`Core/ToolChainState.*`、`UI/ToolsWindow.*` |
| P3-2 | 多任务配方 | ✅ 最多 16 个任务；任务级单图/文件夹、相机优先、全部/当前任务/单步、最多 4 任务并行及任务结果总览均已接入并回归 | `Core/ToolChainState.*`、`Core/ToolController.*`、`UI/ToolsWindow.cpp`、`UI/RunResultWindow.cpp` |
| P4-1 | 工程发布 | ✅ GitHub clean runner 已完成主程序、回归工程、完整测试、运行包校验和 artifact 上传；Git LFS 与可迁移案例资源已验证 | `.github/workflows/`、`scripts/`、`docs/RELEASE.md` |
| P4-2 | 设备平台化 | ✅ 通用途径已完成并接入主程序：OpenCV/UVC/RTSP 相机、TCP、Modbus TCP、Modbus PLC、OPC UA；Modbus IO 表支持 Trigger/Busy/Done/OK/NG/Error/Heartbeat/ACK、单任务拍照触发、单点/整套握手测试、超时报警和指数退避重连；厂商专用 SDK 由目标设备适配 | `Core/HardwareRuntimeService.*`、`Core/HardwareSettingsService.*`、`Core/ModbusTcpAdapter.*`、`UI/HardwareWindow.*` |

## 任务描述模板

```text
任务名称：
在 IMgui_Opencv 项目中实现/修复/优化：XXXX

目标：
- 说明用户最终能做什么，或者修复什么错误。

范围：
- 允许修改的模块：
- 不希望改动的模块：

参考文件：
- UI/ToolsWindow.h
- UI/ToolsWindow.cpp
- Core/ToolExecutor.cpp
- Core/ToolController.cpp
- Algorithm/ITool.h
- Algorithm/ToolResult.h
- docs/CODE_STRUCTURE.md

验收标准：
1. 功能能在界面上触发。
2. 空图、空 ROI、空模型路径等异常输入不会崩溃。
3. 执行结果能显示或写入日志。
4. 配方保存/加载不破坏旧数据。
5. README 和 docs 中对应状态已更新。
```

## 新增视觉工具任务模板

```text
在 IMgui_Opencv 项目里添加新工具：XXXX

要求：
1. 分配新的 type，避免占用当前 0-17（新增工具从 18 开始）。
2. 优先实现 ITool 接口。
3. 输出优先使用 ToolResult。
4. UI 参数放入 ToolInstance。
5. 支持配方保存和加载。
6. 更新 VS 工程文件。
7. 更新 README.md、docs/ROADMAP.md、docs/ALGORITHMS.md。

参考文件：
- Algorithm/ITool.h
- Algorithm/ToolResult.h
- Algorithm/ShapeTools.h/.cpp
- Algorithm/MultiColorFinder.h/.cpp
- UI/ToolsWindow.h
- UI/ToolsWindow.cpp
- Core/ToolExecutor.cpp
- Core/RecipeManager.h/.cpp
- Windows_imgui.vcxproj
- Windows_imgui.vcxproj.filters
```

## 新增工具执行步骤

1. 在 `Algorithm/` 下新增 `XXXX.h/.cpp`。
2. 实现 `ITool`：
   - `GetName() const`
   - `GetType() const`
   - `Execute(VisionContext& ctx)`
   - `DrawUI()`
   - `Save()`
   - `Load()`
3. 在 `Algorithm/ITool.cpp` 注册：

```cpp
ToolRegistry::Register(18, []() -> std::unique_ptr<ITool> {
    return std::make_unique<XXXXTool>();
});
ToolRegistry::RegisterName(14, "XXXX");
```

4. 在 `Core/ToolInstance.h` 的 `ToolInstance` 中添加参数。
5. 在 `UI/ToolsWindow.cpp` 的 `g_ToolRegistry` 添加工具名称和分类。
6. 在 `UI/ToolsWindow.cpp` 添加参数面板。
7. 在 `Core/ToolExecutor.cpp` 同步 `ToolInstance` 参数到工具对象。
8. 在 `ToolExecutor::Execute()` 中把新 type 分发到 `RunViaITool()`。
9. 优先在 `ToolInstance::ToRecipeJson()` / `LoadRecipeJson()` 增加参数字段；模板、参考图等资产兼容信息再更新 `RecipeToolInstance`。
10. 在 `Windows_imgui.vcxproj` 和 `Windows_imgui.vcxproj.filters` 加入新源码文件。
11. 更新相关文档。

## 修复 Bug 任务模板

```text
修复问题：
描述现象、复现步骤和预期结果。

复现步骤：
1.
2.
3.

期望结果：

实际结果：

优先检查：
- 空图保护：`ImageState::HasImage()` 或 `ctx.image.empty()`
- ROI 越界：`ROIState`/执行快照中的 `selectedROI` 是否有效
- cv::Rect 是否越界
- ImGui Begin/End 是否成对
- GPU 上传：`ImageState::SetDebugImage()`、`ImageLoadController::ProcessPendingUpload()`
- 工具结果：`ResultPublisher → gContext.unifiedResults → ResultOverlayState`
```

## 修改文档任务模板

```text
更新文档：
根据当前代码检查并更新以下文档：
- README.md
- docs/ROADMAP.md
- docs/ALGORITHMS.md
- docs/CODE_STRUCTURE.md
- docs/CODE_ANALYSIS.md
- docs/MODULE_RELATIONSHIP.md
- docs/TASK_GROUPS.md（任务、输入或执行方式变化时）
- docs/README.md（新增、改名或废弃文档时）

检查重点：
1. 工具数量是否正确。
2. ITool 接入数量是否正确。
3. ToolResult 的来源、状态、耗时和结果字段是否与 `Algorithm/ToolResult.h` 一致。
4. Roadmap 中已完成/未完成状态是否与代码一致。
5. 新增文件是否加入项目结构说明。
```

## 当前工具 type 分配

| type | 工具 | 当前状态 |
| --- | --- | --- |
| 0 | 边缘检测 | 已接入 ITool |
| 1 | 模板匹配 | 已接入 ITool |
| 2 | Blob 分析 | 已接入 ITool，支持面积、中心、圆度、长宽比、方向、轮廓、筛选与统一测量公差判定 |
| 3 | 阈值调试 | 已接入 ITool |
| 4 | YOLO 检测 | 已接入 ITool |
| 5 | 轮廓分析 | 已接入 ITool |
| 6 | 形状匹配 | 已接入 ITool |
| 7 | 直线检测 | 已接入 ITool |
| 8 | 形态学 | 已接入 ITool |
| 9 | 颜色分析 | 已接入 ITool |
| 10 | 多点找色 | 已接入 ITool |
| 11 | YOLO OpenCV 5.0 | 已接入 ITool，OpenCV DNN 实验工具 |
| 12 | 原图 | 特殊工具，由 ToolController 恢复本轮原图 |
| 13 | OCR 文字识别 | 已接入 ITool，PP-OCRv6 tiny + NCNN |
| 14 | 二维码/条码 | 已接入 ITool，支持 QR、Code128、EAN、Data Matrix、PDF417 |
| 15 | 工业测量 | 已接入 ITool，支持距离、线宽、角度、圆直径、标定与公差 |
| 16 | 图像差分 | 已接入 ITool，支持参考图、阈值、面积和形态学过滤 |
| 17 | 几何绘制 | 已接入 ITool，支持结果几何标注 |

新增工具从 `18` 开始分配。

## 验收清单

任务完成前至少检查：

- 代码能编译，或说明未编译的原因。
- 新增 `.cpp/.h` 已加入 `.vcxproj` 和 `.vcxproj.filters`。
- 所有 OpenCV 执行入口有空图保护。
- ROI 使用前检查范围和尺寸。
- 工具执行会写日志或产生可见结果。
- ITool 工具返回 `ToolResult`，不要新增不必要的全局结果容器。
- 配方保存/加载字段完整。
- 文档状态同步。

## 常用验证命令

```powershell
rg -n "TODO|未完成|待实现|5 个工具|5 种已接入|传统执行|专用执行|OpenCV 4.12|type 0-16|13 个 ITool" README.md docs -g *.md
rg -n "ToolRegistry::Register|g_ToolRegistry|case 12|case 13|case 14|case 15|case 16|case 17|RunViaITool" Algorithm UI Core
rg -n "ClCompile Include=.*XXXX|ClInclude Include=.*XXXX" Windows_imgui.vcxproj
git diff --check
```
