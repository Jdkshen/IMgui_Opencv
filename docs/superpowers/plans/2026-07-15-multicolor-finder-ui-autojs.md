# 多点找色紧凑 UI 与 Auto.js 兼容模式实施计划

> 历史实施计划：记录 2026-07-15 多点找色 UI/Auto.js 工作拆分；清单和代理提示仅属于当时流程，不应重新执行，当前说明见 `../../ALGORITHMS.md`。


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 优化多点找色工具在大量颜色点下的预览和参数编辑体验，并增加行为明确、配方可持久化的 Auto.js 兼容模式。

**Architecture:** `MultiColorFinder` 继续拥有算法参数和执行逻辑，通过一个模式字段在现有增强扫描与 Auto.js 兼容扫描之间切换。`ToolInstance` 和 `RecipeToolInstance` 只负责 UI/配方状态同步；`ToolsWindow.cpp` 保存临时选中点，不把选择状态写入配方。

**Tech Stack:** C++17、OpenCV 5.0、Dear ImGui、nlohmann/json、MSBuild、现有 C++ 回归测试程序。

---

## 文件职责

- `Algorithm/MultiColorFinder.h`：声明匹配模式及 Auto.js 统一容差。
- `Algorithm/MultiColorFinder.cpp`：实现兼容模式的扫描边界、完全匹配和结果发布；保存/加载算法参数。
- `Core/ToolInstance.h`：保存工具卡片当前模式和统一容差。
- `Core/ToolExecutor.cpp`：执行前把工具卡片参数同步到 `MultiColorFinder`。
- `Core/RecipeManager.h`、`Core/RecipeManager.cpp`：保存、加载、捕获和应用新增配方字段。
- `UI/ToolsWindow.cpp`：实现模式选择、紧凑预览、点位联动和可滚动表格。
- `Test/regression_tests.cpp`：覆盖兼容模式执行语义及配方往返。

### Task 1: 用回归测试锁定 Auto.js 兼容语义

**Files:**
- Modify: `Test/regression_tests.cpp:7235-7375`
- Modify: `Test/regression_tests.cpp:9761-9771`

- [ ] **Step 1: 添加“完全匹配、统一容差、首个整数结果”测试**

在现有 MultiColorFinder 测试旁新增：

```cpp
void TestMultiColorFinderAutoJsModeRequiresCompleteFirstIntegerMatch()
{
    MultiColorFinder tool;
    tool.matchMode = MultiColorMatchMode::AutoJsCompatible;
    tool.autoJsTolerance = 4;
    tool.maxResults = 20; // 兼容模式必须忽略该增强参数，只返回首个结果

    ColorPoint anchor;
    anchor.b = 10; anchor.g = 20; anchor.r = 30;
    anchor.tolerance = 0; // 证明兼容模式使用统一容差
    ColorPoint relative;
    relative.x = 2;
    relative.b = 40; relative.g = 50; relative.r = 60;
    relative.tolerance = 0;
    tool.points = {anchor, relative};

    VisionContext ctx;
    ctx.image = cv::Mat::zeros(8, 12, CV_8UC3);
    ctx.image.at<cv::Vec3b>(3, 1) = {12, 22, 32}; // 只有锚点，属于部分匹配
    ctx.image.at<cv::Vec3b>(3, 5) = {13, 23, 33};
    ctx.image.at<cv::Vec3b>(3, 7) = {43, 53, 63};
    ctx.image.at<cv::Vec3b>(5, 5) = {10, 20, 30}; // 第二个完整结果，不应发布
    ctx.image.at<cv::Vec3b>(5, 7) = {40, 50, 60};

    const ToolResult result = tool.Execute(ctx);
    Require(result.success && result.regions.size() == 1,
        "Auto.js multi-color mode did not publish exactly the first complete match");
    Require(result.regions.front().center == cv::Point2f(5.0f, 3.0f),
        "Auto.js multi-color mode did not preserve row-major first-match ordering");
    Require(result.precision == ResultPrecision::Pixel,
        "Auto.js multi-color mode incorrectly published subpixel precision");

    ctx.image.at<cv::Vec3b>(3, 7) = {0, 0, 0};
    ctx.image.at<cv::Vec3b>(5, 7) = {0, 0, 0};
    const ToolResult partialOnly = tool.Execute(ctx);
    Require(partialOnly.success && partialOnly.regions.empty(),
        "Auto.js multi-color mode published a partial fallback result");
}
```

- [ ] **Step 2: 添加“ROI 只限制锚点”测试**

```cpp
void TestMultiColorFinderAutoJsRoiRestrictsAnchorOnly()
{
    MultiColorFinder tool;
    tool.matchMode = MultiColorMatchMode::AutoJsCompatible;
    tool.autoJsTolerance = 0;
    tool.useROI = true;

    ColorPoint anchor;
    anchor.b = 15; anchor.g = 25; anchor.r = 35;
    ColorPoint outsideRoi;
    outsideRoi.x = -2;
    outsideRoi.b = 45; outsideRoi.g = 55; outsideRoi.r = 65;
    tool.points = {anchor, outsideRoi};

    VisionContext ctx;
    ctx.image = cv::Mat::zeros(8, 10, CV_8UC3);
    ctx.image.at<cv::Vec3b>(2, 3) = {15, 25, 35};
    ctx.image.at<cv::Vec3b>(2, 1) = {45, 55, 65};
    ROI roi;
    roi.start = ImVec2(3.0f, 2.0f);
    roi.end = ImVec2(5.0f, 4.0f);
    ctx.rois = {roi};
    ctx.selectedROI = 0;

    const ToolResult result = tool.Execute(ctx);
    Require(result.success && result.regions.size() == 1 &&
            result.regions.front().center == cv::Point2f(3.0f, 2.0f),
        "Auto.js ROI did not allow a relative sample outside the anchor search region");
}
```

- [ ] **Step 3: 添加算法参数新旧 JSON 兼容测试**

```cpp
void TestMultiColorFinderAutoJsModeSerializationDefaultsAndRoundTrip()
{
    MultiColorFinder legacy;
    legacy.Load(nlohmann::json::object());
    Require(legacy.matchMode == MultiColorMatchMode::Enhanced,
        "legacy multi-color JSON did not default to enhanced mode");
    Require(legacy.autoJsTolerance == 4,
        "legacy multi-color JSON did not restore the Auto.js tolerance default");

    MultiColorFinder configured;
    configured.matchMode = MultiColorMatchMode::AutoJsCompatible;
    configured.autoJsTolerance = 19;
    const nlohmann::json saved = configured.Save();

    MultiColorFinder restored;
    restored.Load(saved);
    Require(restored.matchMode == MultiColorMatchMode::AutoJsCompatible &&
            restored.autoJsTolerance == 19,
        "multi-color Auto.js settings did not survive JSON round-trip");
}
```

- [ ] **Step 4: 注册测试并运行，确认当前实现失败**

在 `main()` 的 MultiColorFinder 测试组中调用两个新测试，然后执行：

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /v:minimal
```

Expected: 编译失败，提示 `MultiColorMatchMode`、`matchMode` 或 `autoJsTolerance` 尚未定义。

### Task 2: 实现 Auto.js 兼容扫描和算法参数序列化

**Files:**
- Modify: `Algorithm/MultiColorFinder.h:11-50`
- Modify: `Algorithm/MultiColorFinder.cpp:179-505`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: 声明模式和默认参数**

在 `ColorPoint` 之前增加：

```cpp
enum class MultiColorMatchMode
{
    Enhanced = 0,
    AutoJsCompatible = 1
};
```

在 `MultiColorFinder` 的搜索参数中增加：

```cpp
MultiColorMatchMode matchMode = MultiColorMatchMode::Enhanced;
int autoJsTolerance = 4;
```

- [ ] **Step 2: 让扫描参数按模式选择**

在 `Execute()` 中准备点位时统一裁剪容差，并根据模式覆盖每点容差：

```cpp
const bool autoJsMode = matchMode == MultiColorMatchMode::AutoJsCompatible;
const int compatibleTolerance = std::clamp(autoJsTolerance, 0, 255);
for (const auto& pt : ptsProc)
{
    prepared.push_back({
        pt.x, pt.y, pt.b, pt.g, pt.r,
        autoJsMode ? compatibleTolerance : std::clamp(pt.tolerance, 0, 255)});
}
const int maxKeep = autoJsMode ? 1 : std::max(1, maxResults);
```

兼容模式只限制锚点落入 `searchRect`，增强模式继续要求所有采样点位于 ROI 内：

```cpp
const int startY = autoJsMode
    ? std::max(searchRect.y, -minY)
    : std::max(searchRect.y - minY, -minY);
const int endY = autoJsMode
    ? std::min(searchRect.y + searchRect.height, src.rows - maxY)
    : std::min(searchRect.y + searchRect.height - maxY, src.rows - maxY);
const int startX = autoJsMode
    ? std::max(searchRect.x, -minX)
    : std::max(searchRect.x - minX, -minX);
const int endX = autoJsMode
    ? std::min(searchRect.x + searchRect.width, src.cols - maxX)
    : std::min(searchRect.x + searchRect.width - maxX, src.cols - maxX);
```

- [ ] **Step 3: 禁用兼容模式的部分匹配和亚像素发布**

只在增强模式统计和发布最佳部分匹配。把现有部分匹配统计分支的条件从 `matched > bestPartialCount` 改成：

```cpp
else if (!autoJsMode && matched > bestPartialCount)
```

把现有回退条件改成：

```cpp
if (!autoJsMode && matches.empty() && bestPartialCount > 0 &&
    bestPartialCount < nPts)
```

把亚像素开关改成：

```cpp
const bool allowSubpixelRefinement = !autoJsMode && !imgUseBinary;
```

- [ ] **Step 4: 保存和加载模式参数**

在 `Save()` 中增加：

```cpp
j["matchMode"] = static_cast<int>(matchMode);
j["autoJsTolerance"] = std::clamp(autoJsTolerance, 0, 255);
```

在 `Load()` 中使用兼容旧配方的默认值：

```cpp
matchMode = static_cast<MultiColorMatchMode>(
    std::clamp(j.value("matchMode", 0), 0, 1));
autoJsTolerance = std::clamp(j.value("autoJsTolerance", 4), 0, 255);
```

- [ ] **Step 5: 构建并运行新增算法测试**

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /v:minimal
& '.\Test\x64\Release\RegressionTests.exe'
```

Expected: `regression_tests: all tests passed`，包括原有增强模式 ROI、部分匹配和亚像素测试。

### Task 3: 接通工具状态和配方往返

**Files:**
- Modify: `Core/ToolInstance.h:126-136`
- Modify: `Core/ToolExecutor.cpp:664-677`
- Modify: `Core/RecipeManager.h:163-173`
- Modify: `Core/RecipeManager.cpp:380-390,697-710,1057-1085,1279-1315`
- Modify: `Test/regression_tests.cpp:4836-4927`

- [ ] **Step 1: 先扩展配方往返测试**

在现有 `RecipeToolInstance mcf` 测试数据中设置：

```cpp
mcf.mcfMatchMode = 1;
mcf.mcfAutoJsTolerance = 17;
```

加载后增加：

```cpp
Require(loaded.tools[1].mcfMatchMode == 1,
    "multi-color match mode round-trip regressed");
Require(loaded.tools[1].mcfAutoJsTolerance == 17,
    "multi-color Auto.js tolerance round-trip regressed");
```

重新构建测试，Expected: 新字段尚未定义导致编译失败。

- [ ] **Step 2: 增加工具实例和配方数据字段**

在两个结构体的多点找色区增加：

```cpp
int mcfMatchMode = 0;
int mcfAutoJsTolerance = 4;
```

- [ ] **Step 3: 更新执行前参数同步**

在 `SyncIToolParams()` 的 type 10 分支增加：

```cpp
mf->matchMode = static_cast<MultiColorMatchMode>(
    std::clamp(it.mcfMatchMode, 0, 1));
mf->autoJsTolerance = std::clamp(it.mcfAutoJsTolerance, 0, 255);
```

- [ ] **Step 4: 更新配方所有状态路径**

在 `RecipeManager.cpp` 的 JSON 保存、JSON 加载、`ToolInstance -> RecipeToolInstance` 捕获、`RecipeToolInstance -> ToolInstance` 应用四条路径中同步：

```cpp
mcfMatchMode
mcfAutoJsTolerance
```

加载旧配方时分别使用默认值 `0` 和 `4`。恢复 `toolImpl` 时也把字段写回 `MultiColorFinder`，避免 UI 和算法对象分裂。

- [ ] **Step 5: 运行配方和算法回归测试**

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /v:minimal
& '.\Test\x64\Release\RegressionTests.exe'
```

Expected: `regression_tests: all tests passed`。

### Task 4: 实现多点找色紧凑预览和滚动参数表

**Files:**
- Modify: `UI/ToolsWindow.cpp:2127-2533`

- [ ] **Step 1: 扩展每个工具实例的临时 UI 状态**

扩展现有 `McfState`，不要增加全局配方字段：

```cpp
struct McfState
{
    ROI lastROI;
    bool hasLastROI = false;
    int roiIdx = -1;
    int selectedPoint = -1;
    bool previewOpen = true;
    bool scrollToSelected = false;
};
```

重置、清除参考图和清空点位时将 `selectedPoint = -1`；每帧按点位数量修正越界索引。

- [ ] **Step 2: 增加匹配模式控件**

在“搜索”区域顶部增加固定宽度组合框：

```cpp
static const char* kMcfModes[] = {"增强模式", "Auto.js 兼容"};
it.mcfMatchMode = std::clamp(it.mcfMatchMode, 0, 1);
ImGui::SetNextItemWidth(160.0f);
if (ImGui::Combo("匹配模式", &it.mcfMatchMode, kMcfModes, IM_ARRAYSIZE(kMcfModes)))
    ToolController::RequestRun(inst);
```

兼容模式隐藏“最大结果数”和“去重距离”，显示“完全匹配 / 首个整数坐标”；增强模式保留原控件。

- [ ] **Step 3: 为预览计算悬停点和选中点**

在绘制标记前，根据鼠标到标记的屏幕距离选取最近点：

```cpp
int hoveredPoint = -1;
float hoveredDistance2 = 36.0f;
if (ImGui::IsItemHovered())
{
    const ImVec2 mouse = ImGui::GetMousePos();
    for (int pi = 0; pi < static_cast<int>(mf->points.size()); ++pi)
    {
        const float sx = base.x + (it.mcfAnchorX + mf->points[pi].x) * rs * step;
        const float sy = base.y + (it.mcfAnchorY + mf->points[pi].y) * rs * step;
        const float dx = mouse.x - sx;
        const float dy = mouse.y - sy;
        const float distance2 = dx * dx + dy * dy;
        if (distance2 <= hoveredDistance2)
        {
            hoveredDistance2 = distance2;
            hoveredPoint = pi;
        }
    }
}
```

所有点只画十字；仅 `selectedPoint` 或 `hoveredPoint` 画编号。选中点增加外圈，并用主题强调色区分。

- [ ] **Step 4: 修改点击逻辑，优先选择已有点**

预览被点击时：

```cpp
if (hoveredPoint >= 0)
{
    mfs.selectedPoint = hoveredPoint;
    mfs.scrollToSelected = true;
}
else
{
    const ImVec2 mouse = ImGui::GetMousePos();
    const int px = static_cast<int>((mouse.x - base.x) / step / rs);
    const int py = static_cast<int>((mouse.y - base.y) / step / rs);
    if (px >= 0 && px < ref.cols && py >= 0 && py < ref.rows)
    {
        uchar b = 0, g = 0, r = 0;
        if (ReadBgrAt(ref, py, px, b, g, r))
        {
            ColorPoint cp;
            cp.b = b;
            cp.g = g;
            cp.r = r;
            cp.tolerance = 10;
            if (mf->points.empty())
            {
                it.mcfAnchorX = px;
                it.mcfAnchorY = py;
            }
            else
            {
                cp.x = px - it.mcfAnchorX;
                cp.y = py - it.mcfAnchorY;
            }
            mf->points.push_back(cp);
            mfs.selectedPoint = static_cast<int>(mf->points.size()) - 1;
            mfs.scrollToSelected = true;
            LogSystem::Add(LOG_INFO, color,
                "取色: (%d,%d) BGR(%d,%d,%d)",
                cp.x, cp.y, cp.b, cp.g, cp.r);
        }
    }
}
```

悬停标记时显示编号、偏移、BGR 和实际生效容差；悬停空白区域时显示现有取色说明。

- [ ] **Step 5: 用固定高度表格替换逐行滑块**

使用以下表格框架：

```cpp
const float rowHeight = ImGui::GetFrameHeightWithSpacing();
const float tableHeight = std::min(220.0f,
    rowHeight * static_cast<float>(std::min<size_t>(mf->points.size(), 6)) +
    ImGui::GetTextLineHeightWithSpacing() + 8.0f);
const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_SizingFixedFit;
int removeIdx = -1;
if (ImGui::BeginTable("##mcfPoints", 6, flags, ImVec2(-FLT_MIN, tableHeight)))
{
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("点", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("颜色", ImGuiTableColumnFlags_WidthFixed, 38.0f);
    ImGui::TableSetupColumn("偏移", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("BGR", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("容差", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableHeadersRow();

    if (mfs.scrollToSelected && mfs.selectedPoint >= 0)
    {
        ImGui::SetScrollY(std::max(
            0.0f, mfs.selectedPoint * rowHeight - rowHeight));
        mfs.scrollToSelected = false;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(mf->points.size()), rowHeight);
    while (clipper.Step())
    {
        for (int pi = clipper.DisplayStart; pi < clipper.DisplayEnd; ++pi)
        {
            ColorPoint& point = mf->points[pi];
            ImGui::PushID(pi);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

            ImGui::TableSetColumnIndex(0);
            char number[16];
            if (pi == 0)
                std::snprintf(number, sizeof(number), "A");
            else
                std::snprintf(number, sizeof(number), "#%d", pi + 1);
            if (ImGui::Selectable(number, mfs.selectedPoint == pi,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0.0f, rowHeight)))
            {
                mfs.selectedPoint = pi;
            }

            ImGui::TableSetColumnIndex(1);
            const ImVec4 swatch(point.r / 255.0f, point.g / 255.0f,
                point.b / 255.0f, 1.0f);
            ImGui::ColorButton("##swatch", swatch,
                ImGuiColorEditFlags_NoTooltip |
                ImGuiColorEditFlags_NoDragDrop,
                ImVec2(18.0f, 18.0f));

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(pi == 0
                ? "锚点 (0,0)"
                : cv::format("%+d,%+d", point.x, point.y).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d,%d,%d", point.b, point.g, point.r);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("BGR(%d,%d,%d)", point.b, point.g, point.r);

            ImGui::TableSetColumnIndex(4);
            if (it.mcfMatchMode == 1)
            {
                ImGui::Text("%d", it.mcfAutoJsTolerance);
            }
            else
            {
                int tolerance = point.tolerance;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##tolerance", &tolerance, 1, 10))
                {
                    point.tolerance = std::clamp(tolerance, 0, 255);
                    ToolController::RequestRun(inst);
                }
            }

            ImGui::TableSetColumnIndex(5);
            if (SecondaryButton("X", 20.0f))
                removeIdx = pi;
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}
```

增强模式的 `InputInt` 修改 `pt.tolerance` 并裁剪到 `0-255`；兼容模式显示 `it.mcfAutoJsTolerance`，只允许在表格上方统一修改。

- [ ] **Step 6: 增加统一容差和清空操作**

增强模式保留“统一容差”，变更时写入所有点；兼容模式编辑独立的 `it.mcfAutoJsTolerance`：

```cpp
if (it.mcfMatchMode == 1)
{
    int tolerance = it.mcfAutoJsTolerance;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("统一容差", &tolerance, 1, 10))
    {
        it.mcfAutoJsTolerance = std::clamp(tolerance, 0, 255);
        ToolController::RequestRun(inst);
    }
}
else
{
    int tolerance = mf->points.front().tolerance;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("统一容差", &tolerance, 1, 10))
    {
        tolerance = std::clamp(tolerance, 0, 255);
        for (ColorPoint& point : mf->points)
            point.tolerance = tolerance;
        ToolController::RequestRun(inst);
    }
}

ImGui::SameLine();
if (SecondaryButton("清空颜色点"))
{
    mf->points.clear();
    mfs.selectedPoint = -1;
    ToolController::RequestRun(inst);
}
```

- [ ] **Step 7: 正确处理删除后的选中索引**

删除完成后执行：

```cpp
mf->points.erase(mf->points.begin() + removeIdx);
if (mf->points.empty())
    mfs.selectedPoint = -1;
else if (mfs.selectedPoint > removeIdx)
    --mfs.selectedPoint;
else if (mfs.selectedPoint == removeIdx)
    mfs.selectedPoint = std::min(removeIdx, static_cast<int>(mf->points.size()) - 1);
```

Expected: 删除首点、中间点、末点和当前点均不会留下越界索引。

### Task 5: 完整验证和范围检查

**Files:**
- Verify: `Algorithm/MultiColorFinder.h`
- Verify: `Algorithm/MultiColorFinder.cpp`
- Verify: `Core/ToolInstance.h`
- Verify: `Core/ToolExecutor.cpp`
- Verify: `Core/RecipeManager.h`
- Verify: `Core/RecipeManager.cpp`
- Verify: `UI/ToolsWindow.cpp`
- Verify: `Test/regression_tests.cpp`

- [ ] **Step 1: 运行 Release x64 回归测试**

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /v:minimal
& '.\Test\x64\Release\RegressionTests.exe'
```

Expected: `regression_tests: all tests passed`。

- [ ] **Step 2: 构建临时 Release x64 主程序**

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Windows_imgui.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /p:TargetName=Windows_imgui_verify_temp /m:1 /nologo /v:minimal
```

Expected: 构建退出码为 `0`，不覆盖正在运行的正式程序。

- [ ] **Step 3: 检查差异和手工交互**

```powershell
git diff --check -- Algorithm/MultiColorFinder.h Algorithm/MultiColorFinder.cpp Core/ToolInstance.h Core/ToolExecutor.cpp Core/RecipeManager.h Core/RecipeManager.cpp UI/ToolsWindow.cpp Test/regression_tests.cpp
```

手工确认：大量点时列表固定高度；行与预览标记双向选中；已有标记不会重复添加；两种模式切换不会覆盖单点容差；旧配方加载为增强模式；兼容模式保存后可恢复。
