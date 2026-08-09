# 项目后续更新指南

> 文档同步日期：2026-08-09。工具数量、任务分组、UI 布局/资源、统一结果能力、计时口径、PLC IO 映射和握手维护规则已同步。


本文档用于项目后续迭代时保持代码、工程文件、资源和文档同步。

## 更新原则

1. 新功能优先走 `ITool + VisionContext + ToolResult`。
2. UI 只负责参数和交互，执行逻辑放到 `Core/ToolExecutor.cpp` 或独立 Core 模块。
3. 算法代码放在 `Algorithm/`，尽量不要依赖 ImGui。
4. 结果输出走 `ToolResult → ResultPublisher/PublishDetached → ResultOverlayState`，不要从 UI 直接修改结果容器。
5. 配方字段新增后必须同时支持保存和加载。
6. 文档状态必须跟代码一致，避免 Roadmap 写已完成但代码没有入口。

## 每次更新必须同步的文件

| 变更类型 | 必查文件 |
| --- | --- |
| 新增工具 | `Algorithm/`、`UI/ToolsWindow.*`、`Core/ToolExecutor.cpp`、`Core/RecipeManager.*`、`Windows_imgui.vcxproj`、`Windows_imgui.vcxproj.filters` |
| 修改工具参数 | `UI/ToolsWindow.h`、`UI/ToolsWindow.cpp`、`Core/ToolExecutor.cpp`、`Core/RecipeManager.*` |
| 修改结果结构 | `Algorithm/ToolResult.h`、`UI/ImageViewer.cpp`、`Core/ResultPublisher.h`、相关 docs |
| 修改 ROI | `Core/ROI.h`、`Core/ROIState.*`、`Core/ROIEditorState.*`、`UI/ROIManager.*`、`Core/VisionContext.h`、配方相关代码 |
| 修改工具 ROI 解析 | `Core/ToolExecutor.cpp`、`Core/LiveYoloRunner.cpp`、`Core/ToolChainPreflight.cpp`、`Core/ToolROIService.*`、普通/并行执行回归；保持“未绑定＝整图” |
| 修改任务分组/输入 | `Core/ToolChainState.*`、`Core/ToolController.*`、`Core/RecipeManager.*`、`UI/ToolsWindow.cpp`、`UI/RunResultWindow.cpp`、`docs/TASK_GROUPS.md` |
| 修改 PLC/相机触发 | `Core/HardwareRuntimeService.*`、`Core/HardwareSettingsService.*`、`UI/HardwareWindow.cpp`、`Test/regression_tests.cpp`、`docs/HARDWARE_INTEGRATION.md`、`tools/plc_simulator/README.md` |
| 修改构建依赖 | `Windows_imgui.vcxproj`、`redist/`、`include/`、`docs/BUILD.md` |
| 修改模型资源 | `models/`、`Windows_imgui.vcxproj` 后构建复制命令、`README.md` |
| 修改字体/工具图标 | `assets/fonts/`、`assets/icons/`、`Renderer/FontManager.*`、`Windows_imgui.vcxproj` PostBuild、`scripts/package_runtime.ps1`、`docs/BUILD.md`、`docs/RELEASE.md` |
| 修改任务/工具布局 | `UI/ToolsWindow.cpp`、`UI/Tools/BasicToolPanels.cpp`、`Test/UI_SMOKE_CHECKLIST.md`、`README.md` |
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

7. `docs/README.md`
   - 新增、删除或改名文档时更新索引。
   - 历史快照和当前说明必须分开标识。

8. 硬件相关文档
   - IO 地址、方向、反相和任务绑定是否与默认映射一致。
   - 是否明确区分“相机未连接回退图片”和“相机在线但抓帧失败报 Error”。
   - 是否仍保持单槽握手：Busy/等待 ACK 期间忽略 Trigger，不积压、不补跑。
   - 模拟器任务01～16地址、主程序示例和回归测试是否同步。

9. UI 和运行资源
   - 1280×720 下任务标签、下拉框、工具卡片和底部状态是否完整可见。
   - 添加工具弹窗的图标与文字是否按固定列对齐。
   - 流程图是否只在独立窗口显示，关闭后可再次打开。
   - 在脱离源码目录的输出/发布包内，中文字体和 `assets/icons/` 是否仍可加载。

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
  -> 核对 PostBuild / package_runtime 资源复制
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

新增或修改工具输出字段时，必须同步更新 `Core/ToolResultCapabilities.h`。该能力表同时驱动工具卡片输出说明、结果 ROI/Fixture 来源过滤和 `ToolChainValidator` 运行前校验；不要在 UI 或校验器中另写工具 type 白名单。

可作为结果 ROI/Fixture 来源的空间通道只有 `regions`、`detections`、`lines` 和带矩形框的 `texts`。纯 `measurements` 或 `debugImage` 不能直接生成空间 ROI；若确有空间含义，应先发布对应的统一空间结果。

结果 ROI 默认转换为包围矩形。若消费工具要求点或线，不要在算法内部猜测矩形语义；应通过 `ResultROIRequest::outputGeometry` 声明转换，并确保普通执行、分离执行和任务并行快照都使用相同请求。工业测量模式 0 的现有规则是区域类结果取中心点、线段结果保留原始端点；双结果模式需要同时维护第二来源的稳定 ID、旧索引、结果序号、配方序列化、增删/移动重映射、依赖校验和执行图边。

修改任务列表启动/恢复逻辑时，应保留“空配方不自动创建任务”的规则；正式任务只能由用户操作或配方数据产生，删除最后一个任务后也必须保持为空。修改线段测量输出时，应同步复核 `BuildToolResultLineOverlayLabel()` 在主图和任务结果总览两处的调用，并优先使用结构化 `value` 测量项。

工具生成新处理图时写入：

```cpp
result.debugImage = processed.clone();
```

由执行链决定是否把 `debugImage` 作为后续处理图，并通过 `ImageState::SetDebugImage()` 请求 GPU 上传；算法层不要直接访问上传标志。

## 配方兼容规则

新增字段时：

1. 给字段设置默认值。
2. `Load()` 时判断 JSON 字段是否存在。
3. 不要要求旧配方必须包含新字段。
4. 模板图、参考图等二进制数据要明确保存方式：
   - 文件路径
   - 配方同名 PNG
   - Base64
5. 优先更新 `ToolInstance::ToRecipeJson()` / `LoadRecipeJson()`；只有配方资源文件兼容字段才更新 `RecipeToolInstance`。

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
rg -n "ToolInstance::ToRecipeJson|LoadRecipeJson|RecipeToolInstance|Capture\\(|Apply\\(|Save\\(|Load\\(" Core\\RecipeManager.* Core\\ToolInstance.*
rg -n "ClInclude Include=|ClCompile Include=" Windows_imgui.vcxproj
rg -n "5 个工具|5 种已接入|传统执行|专用执行|OpenCV 4.12|type 0-16|13 个 ITool|截至 2026-06-28" README.md docs -g *.md
rg -n "任务队列|排队补跑|直接发布 Error|相机未连接|任务01|任务16|Windows_imgui_updated" README.md docs tools -g *.md
```

## 发布前检查

- Debug / Release 配置是否都能找到 OpenCV 和运行时 DLL。
- `redist/` 中新增 DLL/lib 是否被后构建事件复制，体积大的运行时是否走 Git LFS、下载脚本或 Release 包。
- `models/` 中新增模型是否被复制到输出目录，OCR 的 `models/ppocrv6/` 是否完整。
- 根目录是否出现不该提交的大文件日志，例如 `debug.log`。
- `imgui.ini`、`theme.cfg`、测试输出是否需要忽略。
- README 的功能列表和 Roadmap 状态是否准确。
- 完整回归、`--plc-handshake-only` 和 PLC 模拟器 `--self-test` 是否通过。
- 标准 `Windows_imgui.exe` 是否来自当前干净构建；若临时验证使用 `_updated.exe`，发布前必须恢复标准文件名并复测。

## 当前项目状态快照

截至 2026-08-09：

| 项 | 状态 |
| --- | --- |
| 工具数量 | 17 个 ITool 工具 + 1 个原图特殊工具，共 18 种 type 0-17 |
| ITool 工具 | 17 个：type 0-11、13-17；type 12 原图由 ToolController 特殊处理，新增类型从 18 开始 |
| 特殊工具 | type 12 原图：由 `ToolController` 恢复本轮原图，作为工具链重置节点 |
| 统一结果 | `ToolResult` 包含来源、Pass/Fail/Error、跳过状态、原因、分段耗时、测量、区域、检测、线、文本和调试图 |
| 结果显示 | `ResultPublisher/PublishDetached → gContext.unifiedResults → ResultOverlayState → ImageViewer` |
| 执行调度 | `ToolController + ToolExecutor`；工具行记录单工具耗时，结果总览记录整轮墙钟时间；支持最多 16 个任务和 4 任务并行 |
| PLC 调度 | 16 个任务独立 Trigger；单槽 Busy/Done/ACK 握手；Busy 或等待 ACK 时新 Trigger 忽略且不补跑 |
| PLC 输入 | 在线相机 → 任务文件夹 → 任务单图 → 公共图片；在线相机抓帧失败输出 Error |
| 工具链输入 | 每个实例支持上一步原图、上一步处理图、原图工具输出 |
| 配方 | 当前写出 version 5；保存工具稳定 ID、任务组、独立输入、相机优先、工具依赖和全部实例参数，并兼容旧版本 |
| 结果导出 | `Core/ResultExporter` 支持 JSON 结果、PNG 结果截图、Markdown 运行报告 |
| OCR | type 13，PP-OCRv6 tiny + NCNN，默认模型路径 `models\ppocrv6\*` |
| 最近验证 | Release x64 主程序和回归工程已生成，完整回归通过，`git diff --check` 通过；当前总状态见 `STATUS_2026-08-09.md`。正式发布前仍需关闭运行中的 EXE 后重建并重新打包 |
| 下一批方向 | 在现有只读流程图窗口上评估节点编辑、现场标定向导、厂商相机 SDK、加密 OPC UA/MQTT 和模型性能优化 |
