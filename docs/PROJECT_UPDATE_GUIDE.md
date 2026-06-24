# 项目后续更新指南

本文档用于项目后续迭代时保持代码、工程文件、资源和文档同步。

## 更新原则

1. 新功能优先走 `ITool + VisionContext + ToolResult`。
2. UI 只负责参数和交互，执行逻辑放到 `Core/ToolExecutor.cpp` 或独立 Core 模块。
3. 算法代码放在 `Algorithm/`，尽量不要依赖 ImGui。
4. 结果输出优先走 `gContext.unifiedResults`。
5. 配方字段新增后必须同时支持保存和加载。
6. 文档状态必须跟代码一致，避免 Roadmap 写已完成但代码没有入口。

## 每次更新必须同步的文件

| 变更类型 | 必查文件 |
| --- | --- |
| 新增工具 | `Algorithm/`、`UI/ToolsWindow.*`、`Core/ToolExecutor.cpp`、`Core/RecipeManager.*`、`Windows_imgui.vcxproj`、`Windows_imgui.vcxproj.filters` |
| 修改工具参数 | `UI/ToolsWindow.h`、`UI/ToolsWindow.cpp`、`Core/ToolExecutor.cpp`、`Core/RecipeManager.*` |
| 修改结果结构 | `Algorithm/ToolResult.h`、`UI/ImageViewer.cpp`、`Core/ResultPublisher.h`、相关 docs |
| 修改 ROI | `UI/DockSpaceHost.h`、`UI/ROIManager.*`、`Core/VisionContext.h`、`Core/RecipeManager.*` |
| 修改构建依赖 | `Windows_imgui.vcxproj`、`redist/`、`include/`、`docs/BUILD.md` |
| 修改模型资源 | `models/`、`Windows_imgui.vcxproj` 后构建复制命令、`README.md` |
| 修改文档状态 | `README.md`、`docs/ROADMAP.md`、`docs/CODE_STRUCTURE.md`、`docs/ALGORITHMS.md` |

## 文档同步规则

每次完成一个功能后，按顺序检查：

1. `README.md`
   - 工具数量是否更新。
   - 功能特性表是否更新。
   - 工具实例表是否更新。
   - ITool 接入列表是否更新。

2. `docs/ROADMAP.md`
   - 已完成任务改成 `✅ 已完成`。
   - 进行中的阶段改成 `进行中`。
   - 规划项不要写成已完成。

3. `docs/ALGORITHMS.md`
   - 新算法的用途、参数、流程和 OpenCV API 是否记录。
   - 新 ITool 是否加入已接入工具表。

4. `docs/CODE_STRUCTURE.md`
   - 新目录、新模块、新执行路径是否加入。
   - type 分发表是否更新。

5. `docs/CODE_ANALYSIS.md`
   - 核心结构变化是否同步。

6. `docs/MODULE_RELATIONSHIP.md`
   - 如果调用关系变了，需要更新模块关系图和结果通路。

## 新增工具更新流程

```text
需求确认
  -> 分配 type
  -> 新增 Algorithm 工具
  -> 注册 ITool
  -> 添加 ToolInstance 参数
  -> 添加 ToolsWindow UI
  -> 接入 ToolExecutor
  -> 接入 RecipeManager
  -> 更新 vcxproj / filters
  -> 运行或编译验证
  -> 更新文档
```

## 结果输出规范

优先使用 `ToolResult`：

| 结果类型 | 使用字段 |
| --- | --- |
| YOLO/分类框 | `detections` |
| 轮廓/Blob/形状区域 | `regions` |
| 直线 | `lines` |
| 面积/长度/角度 | `measurements` |
| OCR/文本 | `texts` |
| 调试图 | `debugImage` |

只有在工具本身会生成一张新的处理图时，才使用：

```cpp
gPendingUpload = rgba;
gNeedUpload = true;
```

## 配方兼容规则

新增字段时：

1. 给字段设置默认值。
2. `Load()` 时判断 JSON 字段是否存在。
3. 不要要求旧配方必须包含新字段。
4. 模板图、参考图等二进制数据要明确保存方式：
   - 文件路径
   - 配方同名 PNG
   - Base64
5. 更新 `RecipeToolInstance`。

## 工程文件同步

新增 C++ 文件后必须同步：

```xml
<ClInclude Include="Algorithm\XXXX.h" />
<ClCompile Include="Algorithm\XXXX.cpp" />
```

同时在 `Windows_imgui.vcxproj.filters` 中加入对应筛选器，否则 Visual Studio 里文件结构会不完整。

## 回归检查

每次较大更新后建议检查：

```powershell
rg -n "ToolRegistry::Register|g_ToolRegistry|ToolExecutor::Execute" Algorithm UI Core
rg -n "RecipeToolInstance|Capture\\(|Apply\\(|Save\\(|Load\\(" Core\\RecipeManager.*
rg -n "ClInclude Include=|ClCompile Include=" Windows_imgui.vcxproj
rg -n "5 个工具|5 种已接入|传统执行|专用执行|OpenCV 4.12|新增工具从 `11`" README.md docs -g *.md
```

## 发布前检查

- Debug / Release 配置是否都能找到 OpenCV 和运行时 DLL。
- `redist/` 中新增 DLL 是否被后构建事件复制。
- `models/` 中新增模型是否被复制到输出目录。
- 根目录是否出现不该提交的大文件日志，例如 `debug.log`。
- `imgui.ini`、`theme.cfg`、测试输出是否需要忽略。
- README 的功能列表和 Roadmap 状态是否准确。

## 当前项目状态快照

截至 2026-06-24：

| 项 | 状态 |
| --- | --- |
| 工具数量 | 12 |
| ITool 工具 | 12 个：type 0-11（边缘、模板、Blob、阈值、YOLO、轮廓、形状、直线、形态学、颜色、多点找色、YOLO OpenCV 5.0） |
| 特殊工具 | type 12 原图：由 `ToolController` 恢复本轮原图，作为工具链重置节点 |
| 统一结果 | `ToolResult { measurements, regions, detections, lines, texts, debugImage }` |
| 结果显示 | `gContext.unifiedResults -> DrawUnifiedResults()` |
| 执行调度 | `ToolController + ToolExecutor`；批量总耗时按各工具执行耗时累加，不包含跨帧等待/UI刷新 |
| 工具链输入 | 每个实例支持上一步原图、上一步处理图、原图工具输出 |
| 配方 | 支持工具实例序列化，包含 `inputSourceMode` |
| 最近编译 | 2026-06-23，`x64\Debug\Windows_imgui.exe` 与 `x64\Release\Windows_imgui.exe` 均已生成新产物 |
| 下一批规划 | OCR、二维码/条码、Blob 增强、尺寸测量、工业通讯 |
