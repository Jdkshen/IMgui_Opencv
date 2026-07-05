# 任务执行说明

本文档用于给开发者或 AI 助手派发项目任务。目标是让每个任务都有明确输入、修改范围、验收标准和文档同步要求。

## 当前任务队列

后续开发按以下顺序推进，除非用户明确指定插队任务：

| 优先级 | 任务 | 目标 | 主要文件 |
| --- | --- | --- | --- |
| P0-1 | 结果导出 | ✅ 已完成：`Core/ResultExporter` 可导出 JSON 结果和结果截图 | `Core/ResultExporter.*`、`UI/DockSpaceHost.cpp` |
| P0-2 | 配方加载后自动执行 | 加载配方、图片/模板后可自动执行全部工具，让检测流程可复现 | `Core/RecipeManager.*`、`Core/ToolController.*`、`UI/DockSpaceHost.*`、`UI/ToolsWindow.*` |
| P0-3 | 运行报告 | ✅ 已完成：全部执行后可生成 Markdown 报告，包含工具名、耗时、结果数量、OK/FAIL | `Core/ResultExporter.*`、`UI/DockSpaceHost.cpp` |
| P1-1 | 尺寸测量 | 点到点距离、线段长度、圆直径、角度、ROI 宽高 | `Algorithm/`、`Core/ToolTypes.h`、`UI/ToolsWindow.*`、`Core/RecipeManager.*` |
| P1-2 | Blob 分析增强 | 面积、中心点、外接矩形、圆度、长宽比、筛选条件、OK/NG 阈值 | `Algorithm/BlobTool.*`、`Algorithm/ToolResult.h`、`UI/ToolsWindow.*` |
| P1-3 | 二维码/条码识别 | 先用 OpenCV `QRCodeDetector` 做二维码，条码后续评估 ZXing-cpp | `Algorithm/`、`Windows_imgui.vcxproj`、`UI/ToolsWindow.*` |
| P1-4 | 图像差分 | 参考图与当前图差异检测，高亮差异区域 | `Algorithm/`、`Core/RecipeManager.*`、`UI/ToolsWindow.*` |
| P1-5 | OCR | ✅ 已接入：type 13，PP-OCRv6 tiny + NCNN，支持 ROI、文本框、置信度和配方保存加载 | `Algorithm/OCRTool.*`、`Algorithm/WindowsPPOCREngine.*`、`Core/ToolExecutor.cpp`、`UI/ToolsWindow.cpp` |
| P2-1 | OK/NG 状态 | 每个工具输出统一 OK/NG 和失败原因 | `Algorithm/ToolResult.h`、`Core/ToolExecutor.*`、`UI/ImageViewer.*` |
| P2-2 | 失败停止 | 全部执行时工具 NG 可停止后续工具 | `Core/ToolController.*`、`UI/ToolsWindow.*` |
| P2-3 | 工具启用/禁用 | 工具实例可跳过执行但保留参数 | `Core/ToolInstance.h`、`Core/ToolController.*`、`Core/RecipeManager.*` |
| P2-4 | 复制/粘贴参数 | 同类工具之间复制参数 | `Core/ToolInstance.h`、`UI/ToolsWindow.*` |
| P2-5 | 工具分组/折叠 | 大量工具时管理流程 | `UI/ToolsWindow.*`、`Core/RecipeManager.*` |
| P3-1 | runtime.zip | 打包 OpenCV、ONNX Runtime、DirectML、NCNN 等本地运行时 | `redist/`、`docs/BUILD.md` |
| P3-2 | GitHub Release | 源码进仓库，运行时包放 Release 附件 | GitHub Release、`docs/BUILD.md` |
| P4 | 平台化 | 节点编辑器、插件系统、工业相机、工业通讯 | 后续单独拆分 |

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
1. 分配新的 type，避免占用当前 0-13（新增工具从 14 开始）。
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
ToolRegistry::Register(14, []() -> std::unique_ptr<ITool> {
    return std::make_unique<XXXXTool>();
});
ToolRegistry::RegisterName(14, "XXXX");
```

4. 在 `UI/ToolsWindow.h` 的 `ToolInstance` 中添加参数。
5. 在 `UI/ToolsWindow.cpp` 的 `g_ToolRegistry` 添加工具名称和分类。
6. 在 `UI/ToolsWindow.cpp` 添加参数面板。
7. 在 `Core/ToolExecutor.cpp` 同步 `ToolInstance` 参数到工具对象。
8. 在 `ToolExecutor::Execute()` 中把新 type 分发到 `RunViaITool()`。
9. 在 `Core/RecipeManager.h/.cpp` 增加序列化和反序列化字段。
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
- 空图保护：gImage.empty()
- ROI 越界：selectedROI 是否有效
- cv::Rect 是否越界
- ImGui Begin/End 是否成对
- GPU 上传：gPendingUpload / gNeedUpload
- 工具结果：gContext.unifiedResults
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

检查重点：
1. 工具数量是否正确。
2. ITool 接入数量是否正确。
3. ToolResult 字段是否与 Algorithm/ToolResult.h 一致。
4. Roadmap 中已完成/未完成状态是否与代码一致。
5. 新增文件是否加入项目结构说明。
```

## 当前工具 type 分配

| type | 工具 | 当前状态 |
| --- | --- | --- |
| 0 | 边缘检测 | 已接入 ITool |
| 1 | 模板匹配 | 已接入 ITool |
| 2 | Blob 分析 | 已接入 ITool，当前主要为占位/待增强 |
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

新增工具从 `14` 开始分配。

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
rg -n "TODO|未完成|待实现|5 个工具|5 种已接入|传统执行|专用执行|OpenCV 4.12" README.md docs -g *.md
rg -n "ToolRegistry::Register|g_ToolRegistry|case 12|case 13|case 14|RunViaITool" Algorithm UI Core
rg -n "ClCompile Include=.*XXXX|ClInclude Include=.*XXXX" Windows_imgui.vcxproj
```
