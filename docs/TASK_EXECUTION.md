# 任务执行说明

本文档用于给开发者或 AI 助手派发项目任务。目标是让每个任务都有明确输入、修改范围、验收标准和文档同步要求。

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
1. 分配新的 type，避免占用当前 0-12（新增工具从 13 开始）。
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
ToolRegistry::Register(13, []() -> std::unique_ptr<ITool> {
    return std::make_unique<XXXXTool>();
});
ToolRegistry::RegisterName(13, "XXXX");
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

新增工具从 `13` 开始分配。

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
rg -n "ToolRegistry::Register|g_ToolRegistry|case 12|case 13|RunViaITool" Algorithm UI Core
rg -n "ClCompile Include=.*XXXX|ClInclude Include=.*XXXX" Windows_imgui.vcxproj
```
