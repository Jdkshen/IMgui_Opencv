// =============================================================================
// ToolsWindow.cpp — 工具窗口 UI 实现
// 负责：工具目录、工具链列表、任务分组管理和复杂工具面板编排
// =============================================================================

// ---- UI 框架头文件 ----
#include "ToolsWindow.h"
#include "DockSpaceHost.h"
#include "../Core/ThemeManager.h"
#include "../Renderer/FontManager.h"
#include "../Renderer/PreviewTextureCache.h"

// ---- 算法工具头文件 ----
#include "../Algorithm/ThresholdTool.h"

// ---- ImGui 渲染引擎 ----
#include "../include/imgui/imgui.h"
#include "../include/imgui/imgui_internal.h"

// ---- UI 组件 ----
#include "ImageViewer.h"
#include "GeometryDrawEditor.h"
#include "ROIManager.h"
#include "Tools/BasicToolPanels.h"
#include "Tools/AdvancedDetectionToolPanels.h"
#include "Tools/DetectionToolPanels.h"
#include "Tools/MeasurementToolPanel.h"
#include "TaskGroupWindow.h"
#include "WorkflowWindow.h"

// ---- 核心服务层 ----
#include "../Core/VideoCapture.h"
#include "../Core/VisionContext.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolController.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolTypes.h"
#include "../Core/ToolResultCapabilities.h"
#include "../Core/ResultROIResolver.h"
#include "../Core/ToolChainPreflight.h"
#include "../Core/ToolChainValidator.h"
#include "../Core/ToolAssetService.h"
#include "../Core/ToolROIService.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/ImageState.h"
#include "../Core/ImageLoadController.h"
#include "../Core/ImageImportService.h"
#include "../Core/ROIState.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/TemplateState.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Core/RecipeAutosaveService.h"

// ---- 日志系统 ----
#include "../Log/LogSystem.h"

// ---- 算法库（YOLO/轮廓/形状/直线/形态学/颜色）----
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/OpenCVYoloDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/MultiColorFinder.h"

// ---- C++ 标准库 ----
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <vector>

// UI 命名空间 —— 工具窗口所有 UI 渲染逻辑
// =============================================================================
namespace UI
{
    // ---- 全局工具注册表：定义所有可用工具的类型、名称、分类、图标和描述 ----
    const std::vector<ToolMeta> g_ToolRegistry = {
        // 输入与预处理 (Base)
        {12, "原图",          ToolCategory::Base,      "▣", "恢复本轮输入原图"},
        {3,  "阈值调试",      ToolCategory::Base,      "◐", "灰度或颜色阈值分割"},
        {8,  "形态学",        ToolCategory::Base,      "▦", "腐蚀、膨胀与开闭运算"},
        {0,  "边缘检测",      ToolCategory::Base,      "◧", "提取灰度图像边缘"},

        // 定位与识别 (Detection)
        {1,  "模板匹配",      ToolCategory::Detection, "□", "在搜索区域定位模板"},
        {4,  "YOLO检测",      ToolCategory::Detection, "◎", "ONNX 通用目标检测"},
        {6,  "形状匹配",      ToolCategory::Detection, "△", "按轮廓形状定位目标"},
        {13, "文字识别",      ToolCategory::Detection, "T",  "识别图片中的文字"},
        {14, "二维码/条码识别", ToolCategory::Detection, "▣", "识别二维码及常用条码"},

        // 区域与几何 (Geometry)
        {2,  "Blob分析",      ToolCategory::Geometry,  "●", "提取连通区域及面积"},
        {5,  "轮廓分析",      ToolCategory::Geometry,  "◇", "分析轮廓、凸包与形状"},
        {7,  "直线检测",      ToolCategory::Geometry,  "▬", "检测直线与线段"},
        {17, "几何绘制",      ToolCategory::Geometry,  "G",  "绘制辅助几何标记"},

        // 分析与测量 (Analysis)
        {9,  "颜色分析",      ToolCategory::Analysis,  "◆", "统计颜色范围与占比"},
        {10, "多点找色",      ToolCategory::Analysis,  "◉", "按多个参考颜色点定位"},
        {16, "图像差分",      ToolCategory::Analysis,  "Δ", "比较参考图与当前图"},
        {15, "工业测量",      ToolCategory::Analysis,  "M",  "距离、角度、直径与公差"},

        // 实验工具 (Experimental)
        {11, "YOLO OpenCV 5.0", ToolCategory::Experimental, "✦", "OpenCV DNN 实验后端"},
    };

    // ---- 工具类型 → UI 渲染函数的映射表 ----
    static std::unordered_map<int, ToolUIFn> g_ToolUIMap;

    // ---- 将"原图"工具移到工具链最前面 ----
    void MoveOriginalToolToFront()
    {
        ToolChainState::MoveOriginalToolToFront();
    }

    // =========================================================================
    // 匿名命名空间 —— UI 状态变量和辅助函数（仅本文件可见）
    // =========================================================================
    namespace
    {
        // ---- 工具目录状态（任务组状态由 TaskGroupWindow 独立持有）----
        char s_toolCatalogFilter[96]{};                  // 工具目录搜索过滤文本

        // ---- 将字符串中 ASCII 字符转为小写（用于大小写不敏感搜索）----
        std::string AsciiLower(std::string value)
        {
            for (char& ch : value)
            {
                const unsigned char byte = static_cast<unsigned char>(ch);
                if (byte < 0x80)
                    ch = static_cast<char>(std::tolower(byte));
            }
            return value;
        }

        // ---- 检查工具是否匹配目录搜索过滤条件 ----
        bool ToolMatchesCatalogFilter(const ToolMeta& meta)
        {
            if (s_toolCatalogFilter[0] == '\0')
                return true;                            // 无过滤条件，全部通过
            const std::string needle = AsciiLower(s_toolCatalogFilter);
            const std::string haystack = AsciiLower(
                std::string(meta.name) + " " + meta.description);
            return haystack.find(needle) != std::string::npos;  // 在名称+描述中搜索
        }

    }

    bool BindSelectedTaskImagePath(const std::string& imagePath)
    {
        return TaskGroupWindow::BindSelectedTaskImagePath(imagePath);
    }

    // =========================================================================
    // ShowToolsWindow —— 工具窗口主渲染入口（每帧调用）
    // 包含：工具目录弹窗、工具卡片列表、批量操作、分组筛选、配方状态等
    // =========================================================================
    void ShowToolsWindow()
    {
        static cv::Mat g_PersistOriginal;               // 持久保存原始图（跨帧）
        if (GeometryDrawEditor::ConsumeChanged())
            SaveCurrentRecipe();                        // 几何编辑变更后自动保存配方

        // 清理不再使用的预览纹理缓存
        std::vector<std::uint64_t> activeToolIds;
        activeToolIds.reserve(ToolChainState::Count());
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
            activeToolIds.push_back(tool.toolId);
        PreviewTextureCache::Prune(activeToolIds);

        // 渲染任务分组管理窗口和工作流图
        TaskGroupWindow::DrawTaskGroupManagerWindows();
        WorkflowWindow::Draw(TaskGroupWindow::CollectVisibleWorkflowToolIndices(),
            TaskGroupWindow::WorkflowChainTitle());

        if (!g_ShowTools)
            return;

        if (TaskGroupWindow::ConsumeToolsWindowFocusRequest())
            ImGui::SetNextWindowFocus();

        ImGui::Begin("功能窗口", &g_ShowTools,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // 工具分类名称（中文）
        const char *kCatNames[] = {
            "输入与预处理", "定位与识别", "区域与几何",
            "分析与测量", "实验工具"
        };
        bool isDark = (g_CurrentTheme == 0);            // 当前是否为暗色主题

        // ---- 根据工具类型返回对应的强调色 ----
        auto ToolAccentColor = [](int type) -> ImU32
        {
            switch (type)
            {
            case 12: return IM_COL32(96, 125, 139, 255); // original
            case 0:  return IM_COL32(74, 144, 226, 255);  // edge
            case 3:  return IM_COL32(245, 166, 35, 255);  // threshold
            case 8:  return IM_COL32(126, 211, 33, 255);  // morphology
            case 2:  return IM_COL32(80, 227, 194, 255);  // blob
            case 1:  return IM_COL32(189, 16, 224, 255);  // template
            case 4:  return IM_COL32(255, 82, 82, 255);   // yolo
            case 5:  return IM_COL32(248, 231, 28, 255);  // contour
            case 6:  return IM_COL32(144, 19, 254, 255);  // shape
            case 7:  return IM_COL32(91, 192, 222, 255);  // line
            case 9:  return IM_COL32(255, 112, 67, 255);  // color
            case 10: return IM_COL32(0, 188, 212, 255);   // multi color
            case 11: return IM_COL32(102, 187, 106, 255); // experiment
            case 13: return IM_COL32(67, 160, 255, 255);  // OCR
            case 14: return IM_COL32(38, 198, 218, 255);  // QR code
            case 15: return IM_COL32(255, 193, 7, 255);   // measurement
            case 17: return IM_COL32(0, 172, 193, 255);   // geometry draw
            default: return IM_COL32(120, 140, 160, 255);
            }
        };

        // ---- 用 ImDrawList 直接绘制工具图标（程序化绘制，不依赖字体图标）----
        auto DrawToolIcon = [](ImDrawList *drawList, int type, ImVec2 p, float size, ImU32 accent)
        {
            const float r = size * 0.5f;
            const ImU32 white = IM_COL32(255, 255, 255, 235);
            const ImU32 stroke = IM_COL32(20, 24, 30, 160);
            ImVec2 center(p.x + r, p.y + r);

            drawList->AddRectFilled(p, ImVec2(p.x + size, p.y + size), accent, 3.0f);

            switch (type)
            {
            case 12:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), white, 1.5f, 0, 1.4f);
                drawList->AddLine(ImVec2(p.x + 5, p.y + r), ImVec2(p.x + size - 5, p.y + r), white, 1.3f);
                break;
            case 0:
            case 7:
                drawList->AddLine(ImVec2(p.x + 3, p.y + size - 4), ImVec2(p.x + size - 3, p.y + 4), white, 1.8f);
                break;
            case 3:
                drawList->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + size - 2, p.y + r), IM_COL32(255, 255, 255, 210), 2.0f);
                drawList->AddRect(ImVec2(p.x + 2, p.y + r), ImVec2(p.x + size - 2, p.y + size - 2), white, 2.0f, 0, 1.2f);
                break;
            case 1:
            case 5:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), white, 1.5f, 0, 1.4f);
                break;
            case 4:
            case 10:
                drawList->AddCircle(center, size * 0.31f, white, 18, 1.5f);
                drawList->AddCircleFilled(center, size * 0.10f, white, 12);
                break;
            case 13:
            {
                const ImVec2 glyphSize = ImGui::CalcTextSize("T");
                drawList->AddText(ImVec2(
                    p.x + (size - glyphSize.x) * 0.5f,
                    p.y + (size - glyphSize.y) * 0.5f), white, "T");
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 1.2f);
                break;
            }
            case 14:
                drawList->AddRect(ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 0.0f, 0, 1.3f);
                drawList->AddRectFilled(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + 9, p.y + 9), white);
                drawList->AddRectFilled(ImVec2(p.x + size - 9, p.y + 6), ImVec2(p.x + size - 6, p.y + 9), white);
                drawList->AddRectFilled(ImVec2(p.x + 6, p.y + size - 9), ImVec2(p.x + 9, p.y + size - 6), white);
                break;
            case 15:
            {
                const ImVec2 glyphSize = ImGui::CalcTextSize("M");
                drawList->AddText(ImVec2(
                    p.x + (size - glyphSize.x) * 0.5f,
                    p.y + (size - glyphSize.y) * 0.5f), white, "M");
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 5), ImVec2(p.x + size - 4, p.y + size - 5), white, 1.2f);
                break;
            }
            case 17:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 0.0f, 0, 1.3f);
                drawList->AddLine(ImVec2(p.x + 3, p.y + size - 3), ImVec2(p.x + size - 3, p.y + 3), white, 1.3f);
                break;
            case 6:
                drawList->AddTriangleFilled(ImVec2(center.x, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), ImVec2(p.x + 3, p.y + size - 3), white);
                break;
            case 8:
                drawList->AddLine(ImVec2(p.x + r, p.y + 3), ImVec2(p.x + r, p.y + size - 3), white, 1.3f);
                drawList->AddLine(ImVec2(p.x + 3, p.y + r), ImVec2(p.x + size - 3, p.y + r), white, 1.3f);
                break;
            case 9:
                drawList->AddCircleFilled(center, size * 0.31f, white, 18);
                break;
            case 11:
                drawList->AddCircle(center, size * 0.34f, white, 18, 1.3f);
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 3), ImVec2(p.x + size - 4, p.y + size - 3), white, 1.2f);
                break;
            default:
                drawList->AddCircleFilled(center, size * 0.28f, white, 18);
                break;
            }

            drawList->AddRect(p, ImVec2(p.x + size, p.y + size), stroke, 3.0f);
        };

        // ===== 调度器：每帧消费执行队列（替代旧 ExecState 状态机）=====
        ToolController::Tick();
        // Tick 可能在本帧完成整批执行。立即覆盖待上传的中间图，保证
        // 下一帧只上传当前选中任务结果，不闪出任务01或最后完成任务。
        TaskGroupWindow::RefreshSelectedTaskPreviewAfterRun();

        // 收集所有任务组名称（用于筛选下拉框）
        std::vector<std::string> toolGroups;
        toolGroups.reserve(ToolChainState::ReadOnlyTaskGroups().size());
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            toolGroups.push_back(group.name);

        // 根据当前筛选条件获取可见工具索引
        const std::vector<int> visibleToolIndices =
            TaskGroupWindow::CollectVisibleWorkflowToolIndices();
        const std::string chainTitle = TaskGroupWindow::WorkflowChainTitle();
        // 工具链标题（主题色高亮）
        ImGui::TextColored(isDark ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f) : ImVec4(0.05f, 0.39f, 0.46f, 1.0f),
            "%s · 工具链", chainTitle.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%zu 个", visibleToolIndices.size());

        // 工作流图窗口切换按钮
        const char* workflowButtonLabel = WorkflowWindow::IsOpen()
            ? "切换到工具流程图##workflow_graph_window"
            : "打开工具流程图##workflow_graph_window";
        if (ImGui::Button(workflowButtonLabel, ImVec2(-1.0f, 0.0f)))
            WorkflowWindow::Open();

        // 任务分组管理 + 批量操作（并排两个等宽按钮）
        const bool hasTools = !ToolChainState::Empty();
        const float topActionGap = ImGui::GetStyle().ItemSpacing.x;
        const float topActionWidth = (std::max)(90.0f,
            (ImGui::GetContentRegionAvail().x - topActionGap) * 0.5f);
        if (ImGui::Button("任务分组管理", ImVec2(topActionWidth, 0.0f)))
            TaskGroupWindow::OpenManagerWindows();
        ImGui::SameLine(0.0f, topActionGap);
        ImGui::BeginDisabled(!hasTools);                // 无工具时禁用批量操作
        if (ImGui::Button("批量操作", ImVec2(topActionWidth, 0.0f)))
            ImGui::OpenPopup("ToolBatchActions");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("ToolBatchActions"))
        {
            bool changed = false;
            if (ImGui::MenuItem("全部启用"))
            {
                ToolChainState::SetAllEnabled(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部禁用"))
            {
                ToolChainState::SetAllEnabled(false);
                changed = true;
            }
            if (ImGui::MenuItem("全部显示结果标签"))
            {
                ToolChainState::SetAllResultLabelsVisible(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部隐藏结果标签"))
            {
                ToolChainState::SetAllResultLabelsVisible(false);
                changed = true;
            }
            if (ImGui::MenuItem("全部失败后停止"))
            {
                ToolChainState::SetAllStopOnFailure(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部失败后继续"))
            {
                ToolChainState::SetAllStopOnFailure(false);
                changed = true;
            }
            ImGui::Separator();
            for (const std::string& group : toolGroups)
            {
                if (ImGui::BeginMenu(group.c_str()))
                {
                    if (ImGui::MenuItem("启用"))
                    {
                        ToolChainState::SetGroupEnabled(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("禁用"))
                    {
                        ToolChainState::SetGroupEnabled(group, false);
                        changed = true;
                    }
                    if (ImGui::MenuItem("显示结果标签"))
                    {
                        ToolChainState::SetGroupResultLabelsVisible(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("隐藏结果标签"))
                    {
                        ToolChainState::SetGroupResultLabelsVisible(group, false);
                        changed = true;
                    }
                    if (ImGui::MenuItem("失败后停止"))
                    {
                        ToolChainState::SetGroupStopOnFailure(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("失败后继续"))
                    {
                        ToolChainState::SetGroupStopOnFailure(group, false);
                        changed = true;
                    }
                    ImGui::EndMenu();
                }
            }
            if (changed)
            {
                ToolController::OnToolChainChanged();
                SaveCurrentRecipe();
            }
            ImGui::EndPopup();
        }
        // ---- 分组筛选下拉框 ----
        if (toolGroups.empty())
        {
            ImGui::TextDisabled("暂无分组");
        }
        else
        {
            ImGui::SetNextItemWidth(-1.0f);
            const std::string& currentTaskGroup = TaskGroupWindow::CurrentTaskGroupName();
            const char* groupPreview = TaskGroupWindow::IsUngroupedFilter()
                ? "未分组"
                : (currentTaskGroup.empty() ? "全部分组" : currentTaskGroup.c_str());
            if (ImGui::BeginCombo("##group_filter", groupPreview))
            {
                if (ImGui::Selectable("全部分组",
                    TaskGroupWindow::IsAllGroupsFilter()))
                    TaskGroupWindow::SelectAllGroups();
                if (ImGui::Selectable("未分组", TaskGroupWindow::IsUngroupedFilter()))
                    TaskGroupWindow::SelectUngroupedTools();
                for (const std::string& group : toolGroups)
                {
                    if (ImGui::Selectable(group.c_str(),
                        !TaskGroupWindow::IsUngroupedFilter() &&
                        TaskGroupWindow::CurrentTaskGroupName() == group))
                    {
                        const int groupIndex = ToolChainState::TaskGroupIndexByName(group);
                        if (groupIndex >= 0)
                        {
                            TaskGroupWindow::SelectTaskGroupInTools(
                                ToolChainState::ReadOnlyTaskGroups()[groupIndex].id,
                                group);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("筛选工具分组");
        }

        // ---- 配方保存状态显示 ----
        const RecipeAutosaveSnapshot recipeSave = RecipeAutosaveService::Snapshot();
        ImGui::TextDisabled("当前配方: %s", CurrentRecipeName());
        ImGui::SetItemTooltip("%s%s%s%s%s", CurrentRecipePath().c_str(),
            recipeSave.lastSavedAt.empty() ? "" : "\n最后保存: ",
            recipeSave.lastSavedAt.c_str(),
            recipeSave.lastError.empty() ? "" : "\n错误: ",
            recipeSave.lastError.c_str());
        ImGui::SameLine();
        const bool saveFailed = !recipeSave.lastError.empty();
        const bool saveBusy = recipeSave.dirty || recipeSave.pending || recipeSave.saving;
        ImGui::TextColored(saveFailed
            ? ImVec4(0.95f, 0.38f, 0.32f, 1.0f)
            : saveBusy ? ImVec4(0.95f, 0.72f, 0.22f, 1.0f)
            : ImVec4(0.35f, 0.78f, 0.48f, 1.0f),
            "%s", saveFailed ? "保存失败" : saveBusy ? "保存中" : "已保存");

        // ---- 添加工具按钮（打开工具目录弹窗）----
        if (ImGui::Button("+ 添加工具", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddToolPopup");

        // 限制工具目录弹窗高度在显示器工作区内，避免在 768p/900p 工控屏上
        // 底部工具被任务栏遮挡。使用 ImGui 内部垂直滚动代替。
        const ImGuiViewport* toolPopupViewport = ImGui::GetWindowViewport();
        const float workTop = toolPopupViewport
            ? toolPopupViewport->WorkPos.y : 0.0f;
        const float workBottom = toolPopupViewport
            ? toolPopupViewport->WorkPos.y + toolPopupViewport->WorkSize.y
            : ImGui::GetIO().DisplaySize.y;
        const float availableAbove = ImGui::GetItemRectMin().y - workTop;
        const float availableBelow = workBottom - ImGui::GetItemRectMax().y;
        const float toolPopupMaxHeight = std::clamp(
            (std::max)(availableAbove, availableBelow) - 12.0f,
            220.0f, 720.0f);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(330.0f, 0.0f), ImVec2(430.0f, toolPopupMaxHeight));
        if (ImGui::BeginPopup("AddToolPopup"))
        {
            // 搜索框
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##tool_catalog_filter",
                "搜索工具名称或用途...", s_toolCatalogFilter,
                IM_ARRAYSIZE(s_toolCatalogFilter));

            // 统计每个分类下可见的工具数量
            std::array<int, static_cast<int>(ToolCategory::COUNT)> visibleCounts{};
            int visibleToolCount = 0;
            int visibleCategoryCount = 0;
            for (const ToolMeta& meta : g_ToolRegistry)
            {
                if (!ToolMatchesCatalogFilter(meta))
                    continue;
                ++visibleCounts[static_cast<int>(meta.category)];
                ++visibleToolCount;
            }
            for (int count : visibleCounts)
                visibleCategoryCount += count > 0 ? 1 : 0;

            ImGui::TextDisabled("%d 个工具 · %d 个分类",
                visibleToolCount, visibleCategoryCount);
            ImGui::Spacing();

            if (visibleToolCount == 0)
            {
                ImGui::TextDisabled("没有匹配的工具");
            }

            // 按分类折叠面板渲染工具条目
            for (int c = 0; c < (int)ToolCategory::COUNT; c++)
            {
                if (visibleCounts[c] == 0)
                    continue;

                const bool filterActive = s_toolCatalogFilter[0] != '\0';
                if (filterActive)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);  // 搜索时强制展开所有分类

                char categoryLabel[96]{};
                std::snprintf(categoryLabel, sizeof(categoryLabel),
                    "%s  %d 个###tool_category_%d",
                    kCatNames[c], visibleCounts[c], c);
                // 前两个分类（预处理/检测）默认展开
                const ImGuiTreeNodeFlags categoryFlags = c <= static_cast<int>(ToolCategory::Detection)
                    ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
                if (ImGui::CollapsingHeader(categoryLabel, categoryFlags))
                {
                    // 遍历当前分类下的所有工具条目
                    for (const auto &meta : g_ToolRegistry)
                    {
                        if (meta.category != (ToolCategory)c ||
                            !ToolMatchesCatalogFilter(meta))
                            continue;
                        char itemId[32];
                        snprintf(itemId, sizeof(itemId), "##tool_%d", meta.type);
                        ImVec2 rowPos = ImGui::GetCursorScreenPos();
                        const float rowH = (std::max)(
                            42.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f);
                        // 点击工具条目：创建工具实例并添加到工具链
                        if (ImGui::Selectable(itemId, false, 0, ImVec2(0.0f, rowH)))
                        {
                            ToolInstance tool{};
                            tool.type = meta.type;
                            // 从具体任务视图添加工具时，直接归入该任务；
                            // “全部分组/未分组”视图仍保持新增为未分组。
                            TaskGroupWindow::AssignNewToolToCurrentGroup(tool);
                            const int addedIndex = ToolChainState::AddTool(std::move(tool));
                            const ToolInstance* addedTool = ToolChainState::AtReadOnly(addedIndex);
                            const std::uint64_t addedToolId = addedTool ? addedTool->toolId : 0;
                            MoveOriginalToolToFront();
                            ToolController::OnToolChainChanged();
                            ToolChainState::SetActiveIndex(
                                ToolChainState::IndexOfToolId(addedToolId));
                            SaveCurrentRecipe();
                            s_toolCatalogFilter[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }

                        ImDrawList *drawList = ImGui::GetWindowDrawList();
                        const float iconSize = 20.0f;
                        const float iconX = rowPos.x + ImGui::GetStyle().FramePadding.x;
                        const float iconY = rowPos.y + (rowH - iconSize) * 0.5f;
                        ImFontAtlasRect iconRect;
                        if (FontManager::GetToolIconRect(meta.type, &iconRect))
                        {
                            drawList->AddImageRounded(ImGui::GetIO().Fonts->TexRef,
                                ImVec2(iconX, iconY),
                                ImVec2(iconX + iconSize, iconY + iconSize),
                                iconRect.uv0, iconRect.uv1,
                                IM_COL32_WHITE, 3.0f);
                        }
                        else
                        {
                            DrawToolIcon(drawList, meta.type, ImVec2(iconX, iconY), iconSize, ToolAccentColor(meta.type));
                        }

                        const float textX = iconX + iconSize + 9.0f;
                        drawList->AddText(
                            ImVec2(textX, rowPos.y + 5.0f),
                            ImGui::GetColorU32(ImGuiCol_Text), meta.name);
                        drawList->AddText(
                            ImVec2(textX, rowPos.y + 22.0f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            meta.description);
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // ---- 从完整路径提取文件名 ----
        auto FileName = [](const std::string &path) -> std::string
        {
            size_t p = path.find_last_of("\\/");
            return (p != std::string::npos) ? path.substr(p + 1) : path;
        };

        // ---- 根据工具类型 ID 查找注册表中的显示名称 ----
        auto ToolName = [](int type) -> const char *
        {
            for (const auto &m : g_ToolRegistry)
                if (m.type == type)
                    return m.name;
            return "?";
        };

        // ---- 捕获工具持久化状态（用于检测 UI 变更后自动保存）----
        auto CaptureToolPersistentState = [](const ToolInstance& tool)
        {
            nlohmann::json state = tool.ToRecipeJson();
            if (tool.type == 10 && tool.toolImpl)
            {
                if (const auto* finder = dynamic_cast<const MultiColorFinder*>(tool.toolImpl.get()))
                {
                    const nlohmann::json finderState = finder->Save();
                    state["mcfPoints"] = finderState.value(
                        "points", nlohmann::json::array());
                }
            }
            return state;
        };

        // ---- UI 辅助 lambda：统一视觉风格 ----

        // 分区标题（主题色文字 + 分隔线）
        auto SectionHeader = [isDark](const char *label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, isDark
                ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f)
                : ImVec4(0.05f, 0.39f, 0.46f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Separator, isDark
                ? ImVec4(0.18f, 0.36f, 0.40f, 1.0f)
                : ImVec4(0.48f, 0.67f, 0.70f, 1.0f));
            ImGui::SeparatorText(label);
            ImGui::PopStyleColor(2);
        };
        // 主要操作按钮（青色调，占满宽度）
        auto PrimaryButton = [isDark](const char *label) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, isDark
                ? ImVec4(0.10f, 0.40f, 0.48f, 1.0f)
                : ImVec4(0.12f, 0.49f, 0.57f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isDark
                ? ImVec4(0.13f, 0.50f, 0.59f, 1.0f)
                : ImVec4(0.08f, 0.42f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, isDark
                ? ImVec4(0.08f, 0.33f, 0.40f, 1.0f)
                : ImVec4(0.05f, 0.35f, 0.42f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 0.99f, 1.0f));
            bool clicked = ImGui::Button(label, ImVec2(-1, 28));
            ImGui::PopStyleColor(4);
            return clicked;
        };
        // 从工具卡片触发单步执行
        auto RunToolFromCard = [](int inst) -> bool
        {
            if (ImageState::Current().empty())
            {
                LogSystem::Add(LOG_WARN, "请先加载图片");
                return false;
            }
            ToolController::RequestRun(inst);
            return true;
        };
        // 次要按钮（无特殊样式）
        auto SecondaryButton = [](const char *label, float w = 0) -> bool
        {
            return ImGui::Button(label, ImVec2(w, 0));
        };
        // 参数标签（自动对齐到固定宽度，空间不足时换行）
        auto ParamLabel = [](const char* label, float labelW = 0.0f)
        {
            const float rowStartX = ImGui::GetCursorPosX();
            const float rowAvailableWidth = ImGui::GetContentRegionAvail().x;
            const float measuredLabelWidth = ImGui::CalcTextSize(label).x;
            const float minimumLabelWidth = labelW > 0.0f
                ? labelW : ImGui::GetFontSize() * 4.3f;
            const float resolvedLabelWidth = (std::max)(minimumLabelWidth,
                measuredLabelWidth);
            const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float minimumControlWidth = ImGui::GetFontSize() * 5.5f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (rowAvailableWidth >= resolvedLabelWidth + innerSpacing + minimumControlWidth)
            {
                ImGui::SameLine();
                ImGui::SetCursorPosX(rowStartX + resolvedLabelWidth + innerSpacing);
            }
            ImGui::SetNextItemWidth((std::max)(1.0f,
                ImGui::GetContentRegionAvail().x));
        };

        auto DrawSearchROIControls = [&](ToolInstance& it, int)
        {
            const char* resultPolicies[] = {
                "中心点在 ROI 内", "与 ROI 相交", "完全在 ROI 内", "覆盖率达到阈值"
            };
            int resultPolicy = std::clamp(it.roiResultPolicy, 0, 3);
            ParamLabel("查找 ROI 筛选");
            if (ImGui::Combo("##roi_result_policy", &resultPolicy,
                             resultPolicies, IM_ARRAYSIZE(resultPolicies)))
            {
                it.roiResultPolicy = resultPolicy;
                it.MarkParametersChanged();
            }
            if (it.roiResultPolicy == 3)
            {
                ParamLabel("最小覆盖");
                if (ImGui::SliderFloat("##roi_minimum_coverage",
                                       &it.roiMinimumCoverage, 0.0f, 1.0f, "%.2f"))
                {
                    it.roiMinimumCoverage = std::clamp(
                        it.roiMinimumCoverage, 0.0f, 1.0f);
                    it.MarkParametersChanged();
                }
            }
            SectionHeader("查找区域");
            const bool hasToolROI = !it.searchROIs.empty();
            ImGui::TextDisabled(hasToolROI
                ? "本工具ROI: 已绑定 %zu 个"
                : "本工具ROI: 未绑定（执行整图）", it.searchROIs.size());

            if (ToolROIService::IsSearchROIEditActive(it.toolId))
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "拖拽ROI后确认");
                if (PrimaryButton("确认绑定##search_roi_confirm"))
                {
                    const ToolROIEditResult result = ToolROIService::ConfirmSearchROIEdit(it);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "查找区域: 已绑定ROI到当前工具");
                        SaveCurrentRecipe();
                    }
                    else
                    {
                        LogSystem::Add(LOG_WARN, "查找区域: ROI 区域无效");
                    }
                }
                if (SecondaryButton("取消##search_roi_cancel", -1.0f))
                    ToolROIService::CancelSearchROIEdit(it.toolId);
                return;
            }

            if (ImGui::BeginTable("##search_roi_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (SecondaryButton(it.searchROIs.empty()
                    ? "添加ROI##search_roi_add" : "修改ROI##search_roi_edit", -1.0f))
                {
                    ToolROIService::BeginSearchROIEdit(it);
                }
                ImGui::TableNextColumn();
                if (SecondaryButton("清除##search_roi_clear", -1.0f))
                {
                    ToolROIService::ClearSearchROIs(it);
                    LogSystem::Add(LOG_INFO, "查找区域: 已清除当前工具ROI");
                    SaveCurrentRecipe();
                }
                ImGui::EndTable();
            }
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);

        ImVec4 themeCard      = isDark ? ImVec4(0.145f, 0.145f, 0.149f, 1.0f) : ImVec4(0.965f, 0.975f, 0.980f, 1.0f);
        ImVec4 themeCardHover = isDark ? ImVec4(0.176f, 0.176f, 0.188f, 1.0f) : ImVec4(0.895f, 0.925f, 0.935f, 1.0f);
        ImVec4 themeActive    = isDark ? ImVec4(0.12f, 0.34f, 0.39f, 0.72f) : ImVec4(0.66f, 0.83f, 0.86f, 1.0f);
        const int toolsColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCard);

        auto ResetCardColor = [isDark]() {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                ? ImVec4(0.145f, 0.145f, 0.149f, 1.0f)
                : ImVec4(0.965f, 0.975f, 0.980f, 1.0f));
        };

        // ---- Card 面板辅助 ----
        int currentCardType = -1;
        int currentCardInst = -1;
        int duplicateToolIndex = -1;
        int pasteToolAfterIndex = -1;

        auto DrawUnifiedToolResult = [isDark, &SectionHeader](const ToolInstance& tool)
        {
            if (!tool.hasLastResult)
                return;

            const ToolResult& result = tool.lastResult;
            const std::uint64_t debugImageBytes = result.timing.debugImageBytes > 0
                ? result.timing.debugImageBytes
                : static_cast<std::uint64_t>(result.debugImage.total() *
                    result.debugImage.elemSize());
            SectionHeader("结果输出");
            const char* statusText = result.skipped ? "跳过" :
                (result.status == ToolResultStatus::Pass ? "通过" :
                    (result.status == ToolResultStatus::Fail ? "不合格" : "异常"));
            const ImVec4 statusColor = result.skipped
                ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                : result.status == ToolResultStatus::Pass
                    ? (isDark ? ImVec4(0.25f, 0.90f, 0.40f, 1.0f)
                              : ImVec4(0.05f, 0.48f, 0.20f, 1.0f))
                    : result.status == ToolResultStatus::Fail
                        ? (isDark ? ImVec4(1.0f, 0.65f, 0.20f, 1.0f)
                                  : ImVec4(0.78f, 0.28f, 0.08f, 1.0f))
                        : (isDark ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                                  : ImVec4(0.78f, 0.16f, 0.12f, 1.0f));
            ImGui::TextColored(statusColor, "状态: %s", statusText);

            std::string channelSummary;
            auto AppendCount = [&channelSummary](const char* name, std::size_t count)
            {
                if (count == 0)
                    return;
                if (!channelSummary.empty())
                    channelSummary += "  ·  ";
                channelSummary += name;
                channelSummary += " ";
                channelSummary += std::to_string(count);
            };
            AppendCount("区域", result.regions.size());
            AppendCount("检测", result.detections.size());
            AppendCount("线段", result.lines.size());
            AppendCount("文本", result.texts.size());
            AppendCount("测量", result.measurements.size());
            if (!result.debugImage.empty() || result.timing.debugImageBytes > 0)
                AppendCount("处理图", 1);
            else if (tool.type == 12 && result.success)
                AppendCount("图像", 1);
            if (channelSummary.empty())
                channelSummary = "无结构化输出";
            ImGui::TextWrapped("%s", channelSummary.c_str());

            const float wallMs = result.timing.wallMs > 0.0f
                ? result.timing.wallMs
                : result.timing.prepareMs + result.timing.executeMs +
                    result.timing.publishMs;
            if (result.skipped)
                ImGui::TextDisabled("耗时: --");
            else
                ImGui::TextDisabled("耗时: 总 %.3f ms｜准备 %.3f｜执行 %.3f｜发布 %.3f",
                    wallMs, result.timing.prepareMs, result.timing.executeMs,
                    result.timing.publishMs);

            if (!result.statusReason.empty())
                ImGui::TextWrapped("原因: %s", result.statusReason.c_str());
            if (!result.message.empty() && result.message != result.statusReason)
                ImGui::TextWrapped("说明: %s", result.message.c_str());

            const bool hasDetails = !result.regions.empty() ||
                !result.detections.empty() || !result.lines.empty() ||
                !result.texts.empty() || !result.measurements.empty() ||
                !result.debugImage.empty();
            if (!hasDetails || !ImGui::CollapsingHeader("输出详情##unified_result_details"))
                return;

            constexpr std::size_t kMaximumDisplayedItems = 12;
            auto DrawMore = [](std::size_t size)
            {
                if (size > kMaximumDisplayedItems)
                    ImGui::TextDisabled("其余 %zu 项已省略", size - kMaximumDisplayedItems);
            };

            for (std::size_t index = 0;
                index < (std::min)(result.detections.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.detections[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("检测 %zu: %s  类别 %d  分数 %.3f  框 [%d,%d,%d,%d]",
                    index + 1, item.label.empty() ? "未分类" : item.label.c_str(),
                    item.classId, item.score, item.box.x, item.box.y,
                    item.box.width, item.box.height);
            }
            DrawMore(result.detections.size());

            for (std::size_t index = 0;
                index < (std::min)(result.regions.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.regions[index];
                const cv::Point2f center = item.center != cv::Point2f()
                    ? item.center
                    : cv::Point2f(item.bbox.x + item.bbox.width * 0.5f,
                        item.bbox.y + item.bbox.height * 0.5f);
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("区域 %zu: %s  中心 (%.1f, %.1f)  框 [%d,%d,%d,%d]  面积 %.1f  分数 %.3f  角度 %.2f°",
                    index + 1, item.label.empty() ? "未命名" : item.label.c_str(),
                    center.x, center.y, item.bbox.x, item.bbox.y,
                    item.bbox.width, item.bbox.height, item.area, item.score,
                    item.angle);
            }
            DrawMore(result.regions.size());

            for (std::size_t index = 0;
                index < (std::min)(result.lines.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.lines[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("线段 %zu: (%d,%d) -> (%d,%d)  长度 %.2f  角度 %.2f°",
                    index + 1, item.p1.x, item.p1.y, item.p2.x, item.p2.y,
                    item.length, item.angle);
            }
            DrawMore(result.lines.size());

            for (std::size_t index = 0;
                index < (std::min)(result.texts.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.texts[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("文本 %zu: %s  置信度 %.3f  框 [%d,%d,%d,%d]",
                    index + 1, item.text.c_str(), item.confidence,
                    item.box.x, item.box.y, item.box.width, item.box.height);
            }
            DrawMore(result.texts.size());

            for (std::size_t index = 0;
                index < (std::min)(result.measurements.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.measurements[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("测量 %zu: %s = %.6g %s",
                    index + 1, item.name.c_str(), item.value, item.unit.c_str());
            }
            DrawMore(result.measurements.size());

            if (!result.debugImage.empty())
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("处理图: %d x %d  通道 %d  %.2f MB",
                    result.debugImage.cols, result.debugImage.rows,
                    result.debugImage.channels(),
                    debugImageBytes / 1048576.0);
            }
        };

        auto BeginCard = [isDark, &currentCardType, &currentCardInst, &duplicateToolIndex,
            &SecondaryButton, &SectionHeader, &ParamLabel,
            &DrawUnifiedToolResult](const char *title, const char *icon = "")
        {
            ImGui::PushID(currentCardInst * 100 + currentCardType);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
            char childId[96];
            snprintf(childId, sizeof(childId), "##tool_card_%d_%d", currentCardInst, currentCardType);
            ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const float parameterRegionWidth = ImGui::GetContentRegionAvail().x;
            const float minimumParameterWidth = ImGui::GetFontSize() * 5.5f;
            const float desiredLabelReserve = ImGui::GetFontSize() * 7.0f;
            const float labelReserve = (std::min)(desiredLabelReserve,
                (std::max)(0.0f, parameterRegionWidth - minimumParameterWidth));
            const float parameterWidth = (std::max)(1.0f,
                parameterRegionWidth - labelReserve -
                    ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushItemWidth(parameterWidth);
            ToolInstance* cardTool = ToolChainState::At(currentCardInst);
            const std::string titleText = cardTool && !cardTool->label.empty()
                ? cardTool->label
                : std::string(title);
            ImGui::TextColored(isDark
                ? ImVec4(0.48f, 0.80f, 0.85f, 1.0f)
                : ImVec4(0.05f, 0.39f, 0.46f, 1.0f),
                "%s%s", icon, titleText.c_str());
            const float titleRightX = ImGui::GetItemRectMax().x -
                ImGui::GetWindowPos().x;
            const float toolMs = ToolController::GetToolTimeMs(currentCardInst);
            if (toolMs > 0.0f)
            {
                char timeText[32];
                snprintf(timeText, sizeof(timeText), "%.3fms", toolMs);
                const float textW = ImGui::CalcTextSize(timeText).x;
                const float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textW;
                const bool fitsBesideTitle = titleRightX +
                    ImGui::GetStyle().ItemSpacing.x <= rightX;
                if (fitsBesideTitle)
                    ImGui::SameLine();
                if (rightX > ImGui::GetCursorPosX())
                    ImGui::SetCursorPosX(rightX);
                ImGui::TextColored(isDark ? ImVec4(0.30f, 0.95f, 0.46f, 1.0f) : ImVec4(0.02f, 0.42f, 0.18f, 1.0f), "%s", timeText);
                if (cardTool && cardTool->hasLastResult && ImGui::IsItemHovered())
                {
                    const ToolResultTiming& timing = cardTool->lastResult.timing;
                    ImGui::SetTooltip(
                        "准备 %.3f ms\n执行 %.3f ms\n发布 %.3f ms\n"
                        "输入 %.2f MB\n调试图 %.2f MB\n结果数据 %.2f KB\n"
                        "后端：预处理 %.3f / 推理 %.3f / 后处理 %.3f ms",
                        timing.prepareMs, timing.executeMs, timing.publishMs,
                        timing.inputBytes / 1048576.0,
                        timing.debugImageBytes / 1048576.0,
                        timing.resultDataBytes / 1024.0,
                        timing.backendPreprocessMs, timing.backendInferenceMs,
                        timing.backendPostprocessMs);
                }
            }
            ImGui::Separator();
            if (cardTool)
            {
                if (cardTool->parametersDirty)
                {
                    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
                        "参数已修改，重新执行后更新结果");
                }
                SectionHeader("实例设置");
                bool labelEnabled = !cardTool->label.empty();
                bool showResultLabels = cardTool->showResultLabels;
                bool enabled = cardTool->enabled;
                bool collapsed = cardTool->collapsed;
                const float instanceLabelWidth = ImGui::CalcTextSize("显示结果").x +
                    ImGui::GetStyle().ItemInnerSpacing.x;
                if (ImGui::BeginTable("##tool_instance_settings", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##setting_label",
                        ImGuiTableColumnFlags_WidthFixed, instanceLabelWidth);
                    ImGui::TableSetupColumn("##setting_value",
                        ImGuiTableColumnFlags_WidthStretch);
                    auto NextInstanceSetting = [](const char* label)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                    };

                    NextInstanceSetting("工具标签");
                    if (ImGui::Checkbox("启用##tool_label_enabled", &labelEnabled))
                    {
                        if (labelEnabled && cardTool->label.empty())
                            cardTool->label = title ? title : "";
                        if (!labelEnabled)
                            cardTool->label.clear();
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("标签名称");
                    ImGui::BeginDisabled(!labelEnabled);
                    if (labelEnabled && cardTool->label.empty())
                        cardTool->label = title ? title : "";
                    char labelBuf[128];
                    snprintf(labelBuf, sizeof(labelBuf), "%s", cardTool->label.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##tool_label", labelBuf, IM_ARRAYSIZE(labelBuf)))
                    {
                        cardTool->label = labelBuf;
                        MarkCurrentRecipeDirty();
                    }
                    ImGui::EndDisabled();

                    NextInstanceSetting("工具状态");
                    if (ImGui::Checkbox("启用##tool_enabled", &enabled))
                    {
                        cardTool->enabled = enabled;
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("结果显示");
                    if (ImGui::Checkbox("显示标签##tool_result_labels", &showResultLabels))
                    {
                        cardTool->showResultLabels = showResultLabels;
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("任务分组");
                    ImGui::SetNextItemWidth(-1.0f);
                    const char* groupPreview = cardTool->groupName.empty()
                        ? "未分组" : cardTool->groupName.c_str();
                    if (ImGui::BeginCombo("##tool_group_select", groupPreview))
                    {
                        if (ImGui::Selectable("未分组", cardTool->groupName.empty()) &&
                            ToolChainState::AssignToolToTaskGroup(currentCardInst, -1))
                        {
                            TaskGroupWindow::CommitTaskGroupChange();
                        }
                        const auto& taskGroups = ToolChainState::ReadOnlyTaskGroups();
                        for (int groupIndex = 0;
                            groupIndex < static_cast<int>(taskGroups.size()); ++groupIndex)
                        {
                            const bool selected =
                                cardTool->groupName == taskGroups[groupIndex].name;
                            if (ImGui::Selectable(taskGroups[groupIndex].name.c_str(), selected) &&
                                ToolChainState::AssignToolToTaskGroup(currentCardInst, groupIndex))
                            {
                                TaskGroupWindow::CommitTaskGroupChange();
                            }
                        }
                        ImGui::Separator();
                        ImGui::BeginDisabled(
                            taskGroups.size() >= ToolChainState::MaximumTaskGroups());
                        if (ImGui::Selectable("+ 新建任务并加入"))
                        {
                            const int createdIndex = ToolChainState::CreateTaskGroup();
                            if (createdIndex >= 0 &&
                                ToolChainState::AssignToolToTaskGroup(
                                    currentCardInst, createdIndex))
                            {
                                TaskGroupWindow::CommitTaskGroupChange();
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::EndCombo();
                    }

                    NextInstanceSetting("卡片显示");
                    if (ImGui::Checkbox("折叠##tool_collapsed", &collapsed))
                    {
                        cardTool->collapsed = collapsed;
                        if (collapsed && ToolChainState::ActiveIndex() == currentCardInst)
                            ToolChainState::SetActiveIndex(-1);
                        MarkCurrentRecipeDirty();
                    }

                    if (cardTool->type == 4 || cardTool->type == 11 || cardTool->type == 13)
                    {
                        NextInstanceSetting("模型缺失");
                        if (ImGui::Checkbox("跳过工具##skip_missing_model",
                            &cardTool->skipIfModelMissing))
                        {
                            MarkCurrentRecipeDirty();
                        }
                    }

                    NextInstanceSetting("操作");
                    if (SecondaryButton("复制工具", -1.0f))
                        duplicateToolIndex = currentCardInst;
                    ImGui::EndTable();
                }

                const std::vector<ToolChainDependency> dependencies =
                    ToolChainValidator::DescribeDependencies(ToolChainState::ReadOnlyTools());
                bool hasDependencyDisplay = false;
                for (const ToolChainDependency& dependency : dependencies)
                {
                    if (dependency.consumerIndex == currentCardInst ||
                        dependency.sourceIndex == currentCardInst)
                    {
                        hasDependencyDisplay = true;
                        break;
                    }
                }
                if (hasDependencyDisplay)
                {
                    SectionHeader("依赖关系");
                    for (const ToolChainDependency& dependency : dependencies)
                    {
                        const char* kindName = dependency.kind == ToolDependencyKind::ResultROI
                            ? "结果ROI" : "Fixture";
                        if (dependency.consumerIndex == currentCardInst)
                        {
                            if (dependency.valid)
                            {
                                const ToolInstance& source = *ToolChainState::AtReadOnly(dependency.sourceIndex);
                                const char* sourceName = source.type == 12
                                    ? "原图" : ToolRegistry::GetName(source.type);
                                const std::string name = ToolInstanceTitle(sourceName, source.label);
                                ImGui::TextDisabled("%s <- %d. %s", kindName,
                                    dependency.sourceIndex + 1, name.c_str());
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                                    "%s: %s", kindName, dependency.issue.c_str());
                            }
                        }
                        if (dependency.valid && dependency.sourceIndex == currentCardInst)
                        {
                            const ToolInstance& consumer = *ToolChainState::AtReadOnly(dependency.consumerIndex);
                            const char* consumerName = consumer.type == 12
                                ? "原图" : ToolRegistry::GetName(consumer.type);
                            const std::string name = ToolInstanceTitle(consumerName, consumer.label);
                            ImGui::TextDisabled("%s -> %d. %s", kindName,
                                dependency.consumerIndex + 1, name.c_str());
                        }
                    }
                }

                bool resultRoiChanged = false;
                const char* resultRoiModes[] = {
                    "固定/手工 ROI", "上游第 N 个结果", "上游全部结果", "选择两个结果"
                };
                const bool supportsSelectedPair = cardTool->type == 15 &&
                    (cardTool->measureMode == 0 || cardTool->measureMode == 2 ||
                     cardTool->measureMode == 6 || cardTool->measureMode == 7);
                const int resultRoiModeCount = supportsSelectedPair ? 4 : 3;
                const int configuredResultRoiMode = cardTool->resultRoiMode;
                cardTool->resultRoiMode = std::clamp(cardTool->resultRoiMode,
                    0, resultRoiModeCount - 1);
                resultRoiChanged |= cardTool->resultRoiMode != configuredResultRoiMode;
                resultRoiChanged |= ImGui::Combo("输入 ROI", &cardTool->resultRoiMode,
                    resultRoiModes, resultRoiModeCount);
                if (cardTool->resultRoiMode != 0)
                {
                    const bool selectedPair = cardTool->resultRoiMode == 3;
                    auto sourceIsCompatible = [&](int sourceType, bool secondInput)
                    {
                        const ToolResultCapabilities capabilities =
                            ToolCapabilitiesForType(sourceType);
                        if (!capabilities.SupportsSpatialResult())
                            return false;
                        if (!selectedPair || cardTool->type != 15)
                            return true;
                        const bool requiresLine = cardTool->measureMode == 2 ||
                            cardTool->measureMode == 7 ||
                            (cardTool->measureMode == 6 && secondInput);
                        return !requiresLine || capabilities.lines;
                    };
                    auto drawResultSource = [&](const char* label, int& configuredIndex,
                                                std::uint64_t& configuredId,
                                                bool secondInput)
                    {
                        std::string sourcePreview = "未选择";
                        int resolvedIndex = ToolChainState::IndexOfToolId(configuredId);
                        if (resolvedIndex < 0)
                            resolvedIndex = configuredIndex;
                        if (resolvedIndex >= 0 && resolvedIndex < currentCardInst &&
                            resolvedIndex < static_cast<int>(ToolChainState::Count()))
                        {
                            const auto& source = *ToolChainState::AtReadOnly(resolvedIndex);
                            const char* sourceName = source.type == 12 ? "原图" : ToolRegistry::GetName(source.type);
                            sourcePreview = std::to_string(resolvedIndex + 1) + ". " +
                                ToolInstanceTitle(sourceName, source.label) + "  [" +
                                ToolResultKindsLabel(source.type) + "]";
                            if (!sourceIsCompatible(source.type, secondInput))
                                sourcePreview = "不兼容: " + sourcePreview;
                        }
                        if (ImGui::BeginCombo(label, sourcePreview.c_str()))
                        {
                            bool hasCompatibleSource = false;
                            for (int sourceIndex = 0; sourceIndex < currentCardInst; ++sourceIndex)
                            {
                                const auto& source = *ToolChainState::AtReadOnly(sourceIndex);
                                if (!sourceIsCompatible(source.type, secondInput))
                                    continue;
                                hasCompatibleSource = true;
                                const char* sourceName = source.type == 12
                                    ? "原图" : ToolRegistry::GetName(source.type);
                                const std::string option = std::to_string(sourceIndex + 1) + ". " +
                                    ToolInstanceTitle(sourceName, source.label) + "  [" +
                                    ToolResultKindsLabel(source.type) + "]";
                                const bool selected = configuredId != 0
                                    ? configuredId == source.toolId
                                    : configuredIndex == sourceIndex;
                                if (ImGui::Selectable(option.c_str(), selected))
                                {
                                    configuredIndex = sourceIndex;
                                    configuredId = source.toolId;
                                    resultRoiChanged = true;
                                }
                            }
                            if (!hasCompatibleSource)
                                ImGui::TextDisabled("前面没有可输出空间结果的工具");
                            ImGui::EndCombo();
                        }
                    };

                    auto drawResultChoice = [&](const char* label,
                                                int configuredSourceIndex,
                                                std::uint64_t configuredSourceId,
                                                int& configuredResultIndex,
                                                bool secondInput)
                    {
                        int resolvedIndex = ToolChainState::IndexOfToolId(configuredSourceId);
                        if (resolvedIndex < 0)
                            resolvedIndex = configuredSourceIndex;

                        const ToolInstance* source = nullptr;
                        if (resolvedIndex >= 0 && resolvedIndex < currentCardInst &&
                            resolvedIndex < static_cast<int>(ToolChainState::Count()))
                        {
                            source = ToolChainState::AtReadOnly(resolvedIndex);
                        }

                        std::vector<ResultROIChoice> choices;
                        if (source && source->hasLastResult)
                        {
                            ResultROIRequest request;
                            request.mode = ResultROIMode::NthResult;
                            request.category = cardTool->resultRoiCategory;
                            request.classId = cardTool->resultRoiClassId;
                            request.minScore = cardTool->resultRoiMinScore;
                            request.minArea = cardTool->resultRoiMinArea;
                            request.sortMode = cardTool->resultRoiSortMode;
                            request.sortDescending = cardTool->resultRoiSortDescending;
                            request.requireLineResults = selectedPair &&
                                cardTool->type == 15 &&
                                (cardTool->measureMode == 2 ||
                                 cardTool->measureMode == 7 ||
                                 (cardTool->measureMode == 6 && secondInput));
                            choices = ResultROIResolver::ListChoices(
                                source->lastResult, request);
                        }

                        std::string preview;
                        if (!source)
                            preview = "请先选择上游工具";
                        else if (!source->hasLastResult)
                            preview = "等待上游执行";
                        else if (choices.empty())
                            preview = "当前筛选条件下没有结果";
                        else if (configuredResultIndex >= 0 &&
                            configuredResultIndex < static_cast<int>(choices.size()))
                        {
                            preview = choices[configuredResultIndex].label;
                        }
                        else
                            preview = "原选择已超出当前结果范围";

                        if (ImGui::BeginCombo(label, preview.c_str()))
                        {
                            if (choices.empty())
                            {
                                ImGui::TextDisabled("请先执行上游工具，或调整结果筛选条件");
                            }
                            else
                            {
                                for (const ResultROIChoice& choice : choices)
                                {
                                    const bool selected =
                                        configuredResultIndex == choice.resultIndex;
                                    if (ImGui::Selectable(choice.label.c_str(), selected))
                                    {
                                        configuredResultIndex = choice.resultIndex;
                                        resultRoiChanged = true;
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    };

                    drawResultSource(selectedPair ? "上游工具 A" : "上游工具",
                        cardTool->resultRoiSourceTool, cardTool->resultRoiSourceToolId, false);
                    if (cardTool->resultRoiMode == 1 || selectedPair)
                    {
                        drawResultChoice(selectedPair ? "选择结果 A" : "选择结果",
                            cardTool->resultRoiSourceTool,
                            cardTool->resultRoiSourceToolId,
                            cardTool->resultRoiIndex, false);
                    }
                    if (selectedPair)
                    {
                        drawResultSource("上游工具 B",
                            cardTool->resultRoiSecondSourceTool,
                            cardTool->resultRoiSecondSourceToolId, true);
                        drawResultChoice("选择结果 B",
                            cardTool->resultRoiSecondSourceTool,
                            cardTool->resultRoiSecondSourceToolId,
                            cardTool->resultRoiSecondIndex, true);
                    }
                    if (cardTool->resultRoiMode != 0)
                    {
                        char resultCategory[128];
                        snprintf(resultCategory, sizeof(resultCategory), "%s", cardTool->resultRoiCategory.c_str());
                        ParamLabel("结果类别");
                        if (ImGui::InputText("##result_roi_category", resultCategory,
                            IM_ARRAYSIZE(resultCategory)))
                        {
                            cardTool->resultRoiCategory = resultCategory;
                            resultRoiChanged = true;
                        }
                        resultRoiChanged |= ImGui::DragInt("类别ID##result_roi_class", &cardTool->resultRoiClassId,
                            1.0f, -1, 100000);
                        resultRoiChanged |= ImGui::DragFloat("最低分数##result_roi_score", &cardTool->resultRoiMinScore,
                            0.01f, -1.0f, 1.0f, "%.3f");
                        resultRoiChanged |= ImGui::DragFloat("最小面积##result_roi_area", &cardTool->resultRoiMinArea,
                            1.0f, -1.0f, 100000000.0f, "%.1f");
                        const char* resultSortModes[] = {"原始顺序", "分数优先", "面积优先"};
                        cardTool->resultRoiSortMode = std::clamp(cardTool->resultRoiSortMode, 0, 2);
                        resultRoiChanged |= ImGui::Combo("结果排序##result_roi_sort", &cardTool->resultRoiSortMode,
                            resultSortModes, IM_ARRAYSIZE(resultSortModes));
                        resultRoiChanged |= ImGui::Checkbox("降序##result_roi_desc", &cardTool->resultRoiSortDescending);
                        const char* missingPolicies[] = {"结果不存在时跳过", "结果不存在时判定失败"};
                        cardTool->resultRoiMissingPolicy = std::clamp(cardTool->resultRoiMissingPolicy, 0, 1);
                        resultRoiChanged |= ImGui::Combo("缺失处理", &cardTool->resultRoiMissingPolicy,
                            missingPolicies, IM_ARRAYSIZE(missingPolicies));
                        if (cardTool->type == 15 && cardTool->measureMode == 0)
                        {
                            if (selectedPair)
                            {
                                ImGui::TextWrapped("点点距离：结果 A、B 分别转换为中心点；"
                                    "可以选择同一上游的不同序号，也可以选择两个不同上游。");
                            }
                            else
                            {
                                ImGui::TextWrapped("点点距离：区域结果自动取中心点，线段结果保留端点；"
                                    "使用当前排序后的前两个点。至少需要 2 个区域结果或 1 条线段。");
                            }
                        }
                        else if (selectedPair && cardTool->type == 15 &&
                            (cardTool->measureMode == 2 || cardTool->measureMode == 7))
                        {
                            ImGui::TextWrapped("线测量：结果 A、B 必须分别选择可输出线段的上游工具。");
                        }
                        else if (selectedPair && cardTool->type == 15 &&
                            cardTool->measureMode == 6)
                        {
                            ImGui::TextWrapped("点线距离：结果 A 转换为中心点；"
                                "结果 B 必须选择可输出线段的上游工具。");
                        }
                    }
                }
                if (resultRoiChanged)
                    SaveCurrentRecipe();

                bool fixtureChanged = ImGui::Checkbox("启用定位坐标系", &cardTool->fixture.enabled);
                if (cardTool->fixture.enabled)
                {
                    std::string fixturePreview = "未选择";
                    int fixtureSourceIndex = ToolChainState::IndexOfToolId(cardTool->fixture.sourceToolId);
                    if (fixtureSourceIndex < 0)
                        fixtureSourceIndex = cardTool->fixture.sourceToolIndex;
                    if (fixtureSourceIndex >= 0 && fixtureSourceIndex < currentCardInst &&
                        fixtureSourceIndex < static_cast<int>(ToolChainState::Count()))
                    {
                        const auto& source = *ToolChainState::AtReadOnly(fixtureSourceIndex);
                        fixturePreview = std::to_string(fixtureSourceIndex + 1) + ". " +
                            ToolInstanceTitle(ToolRegistry::GetName(source.type), source.label) +
                            "  [" + ToolResultKindsLabel(source.type) + "]";
                        if (!ToolCapabilitiesForType(source.type).SupportsSpatialResult())
                            fixturePreview = "不兼容: " + fixturePreview;
                    }
                    if (ImGui::BeginCombo("定位上游", fixturePreview.c_str()))
                    {
                        bool hasCompatibleSource = false;
                        for (int sourceIndex = 0; sourceIndex < currentCardInst; ++sourceIndex)
                        {
                            const auto& source = *ToolChainState::AtReadOnly(sourceIndex);
                            if (!ToolCapabilitiesForType(source.type).SupportsSpatialResult())
                                continue;
                            hasCompatibleSource = true;
                            const std::string option = std::to_string(sourceIndex + 1) + ". " +
                                ToolInstanceTitle(ToolRegistry::GetName(source.type), source.label) +
                                "  [" + ToolResultKindsLabel(source.type) + "]";
                            const bool selected = cardTool->fixture.sourceToolId != 0
                                ? cardTool->fixture.sourceToolId == source.toolId
                                : cardTool->fixture.sourceToolIndex == sourceIndex;
                            if (ImGui::Selectable(option.c_str(), selected))
                            {
                                cardTool->fixture.sourceToolIndex = sourceIndex;
                                cardTool->fixture.sourceToolId = source.toolId;
                                fixtureChanged = true;
                            }
                        }
                        if (!hasCompatibleSource)
                            ImGui::TextDisabled("前面没有可输出定位结果的工具");
                        ImGui::EndCombo();
                    }
                    const ToolInstance* fixtureSource =
                        ToolChainState::AtReadOnly(fixtureSourceIndex);
                    std::vector<std::pair<int, std::string>> fixtureChoices;
                    if (fixtureSource && fixtureSource->hasLastResult)
                    {
                        const ToolResult& sourceResult = fixtureSource->lastResult;
                        const std::size_t candidateCount = (std::max)({
                            sourceResult.regions.size(), sourceResult.detections.size(),
                            sourceResult.lines.size(), sourceResult.texts.size()});
                        fixtureChoices.reserve(candidateCount);
                        for (int resultIndex = 0;
                            resultIndex < static_cast<int>(candidateCount); ++resultIndex)
                        {
                            FixturePose pose;
                            if (!FixtureTransform::TryExtractPose(
                                sourceResult, resultIndex, pose))
                            {
                                continue;
                            }

                            const char* kind = "结果";
                            std::string itemLabel;
                            float score = 0.0f;
                            if (resultIndex < static_cast<int>(sourceResult.regions.size()))
                            {
                                kind = "区域";
                                itemLabel = sourceResult.regions[resultIndex].label;
                                score = sourceResult.regions[resultIndex].score;
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.detections.size()))
                            {
                                kind = "检测框";
                                itemLabel = sourceResult.detections[resultIndex].label;
                                score = sourceResult.detections[resultIndex].score;
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.lines.size()))
                            {
                                kind = "线段";
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.texts.size()))
                            {
                                kind = "文本";
                                itemLabel = sourceResult.texts[resultIndex].text;
                                score = sourceResult.texts[resultIndex].confidence;
                            }

                            std::ostringstream label;
                            label << resultIndex + 1 << ". " << kind;
                            if (!itemLabel.empty())
                                label << "｜" << itemLabel;
                            label << "｜中心(" << std::fixed << std::setprecision(1)
                                << pose.origin.x << ',' << pose.origin.y << ')';
                            if (pose.angleDegrees != 0.0f)
                                label << "｜角度 " << pose.angleDegrees << "°";
                            if (score > 0.0f)
                                label << "｜分数 " << std::setprecision(3) << score;
                            fixtureChoices.emplace_back(resultIndex, label.str());
                        }
                    }

                    std::string fixtureResultPreview;
                    if (!fixtureSource)
                        fixtureResultPreview = "请先选择定位上游";
                    else if (!fixtureSource->hasLastResult)
                        fixtureResultPreview = "等待上游执行";
                    else if (fixtureChoices.empty())
                        fixtureResultPreview = "上游当前没有可用定位结果";
                    else
                    {
                        const auto selected = std::find_if(
                            fixtureChoices.begin(), fixtureChoices.end(),
                            [cardTool](const auto& choice)
                            {
                                return choice.first == cardTool->fixture.resultIndex;
                            });
                        fixtureResultPreview = selected != fixtureChoices.end()
                            ? selected->second
                            : "原选择已超出当前结果范围";
                    }
                    if (ImGui::BeginCombo("定位结果", fixtureResultPreview.c_str()))
                    {
                        if (fixtureChoices.empty())
                        {
                            ImGui::TextDisabled("请先执行定位上游工具");
                        }
                        else
                        {
                            for (const auto& choice : fixtureChoices)
                            {
                                const bool selected =
                                    cardTool->fixture.resultIndex == choice.first;
                                if (ImGui::Selectable(choice.second.c_str(), selected))
                                {
                                    cardTool->fixture.resultIndex = choice.first;
                                    fixtureChanged = true;
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (SecondaryButton("从当前定位结果记录参考位姿"))
                    {
                        int sourceIndex = ToolChainState::IndexOfToolId(cardTool->fixture.sourceToolId);
                        if (sourceIndex < 0)
                            sourceIndex = cardTool->fixture.sourceToolIndex;
                        const ToolInstance* sourceTool = ToolChainState::AtReadOnly(sourceIndex);
                        if (sourceTool && sourceTool->hasLastResult)
                        {
                            FixturePose pose;
                            if (FixtureTransform::TryExtractPose(
                                sourceTool->lastResult,
                                cardTool->fixture.resultIndex,
                                pose))
                            {
                                cardTool->fixture.referenceOrigin = pose.origin;
                                cardTool->fixture.referenceAngleDegrees = pose.angleDegrees;
                                fixtureChanged = true;
                            }
                        }
                    }
                    ImGui::TextDisabled("参考: (%.2f, %.2f), %.2f deg",
                        cardTool->fixture.referenceOrigin.x,
                        cardTool->fixture.referenceOrigin.y,
                        cardTool->fixture.referenceAngleDegrees);
                    fixtureChanged |= ImGui::Checkbox("定位缺失时判定失败", &cardTool->fixture.failOnMissing);
                }
                if (fixtureChanged)
                    SaveCurrentRecipe();

                bool judgementChanged = false;
                if (ImGui::BeginTable("##judgement_flags", 2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableNextColumn();
                    judgementChanged |= ImGui::Checkbox("启用判定", &cardTool->judgement.enabled);
                    ImGui::TableNextColumn();
                    judgementChanged |= ImGui::Checkbox("失败停止", &cardTool->judgement.stopOnFailure);
                    ImGui::EndTable();
                }
                if (cardTool->judgement.enabled)
                {
                    judgementChanged |= ImGui::DragInt("最少结果", &cardTool->judgement.minResultCount, 1.0f, 0, 100000);
                    judgementChanged |= ImGui::DragInt("最多结果", &cardTool->judgement.maxResultCount, 1.0f, -1, 100000);
                    ImGui::TextDisabled("最多结果 -1 表示不限制");

                    judgementChanged |= ImGui::DragFloat("最低分数", &cardTool->judgement.minScore, 0.01f, -1.0f, 1.0f, "%.3f");
                    judgementChanged |= ImGui::DragFloat("最小面积", &cardTool->judgement.minArea, 1.0f, -1.0f, 1000000000.0f, "%.1f");
                    judgementChanged |= ImGui::DragFloat("最大面积", &cardTool->judgement.maxArea, 1.0f, -1.0f, 1000000000.0f, "%.1f");

                    judgementChanged |= ImGui::Checkbox("测量项范围", &cardTool->judgement.measurementRangeEnabled);
                    if (cardTool->judgement.measurementRangeEnabled)
                    {
                        char measurementName[128];
                        snprintf(measurementName, sizeof(measurementName), "%s", cardTool->judgement.measurementName.c_str());
                        ParamLabel("测量项名称");
                        if (ImGui::InputText("##judgement_measurement_name", measurementName,
                            IM_ARRAYSIZE(measurementName)))
                        {
                            cardTool->judgement.measurementName = measurementName;
                            judgementChanged = true;
                        }
                        if (cardTool->hasLastResult && !cardTool->lastResult.measurements.empty())
                        {
                            const char* preview = cardTool->judgement.measurementName.empty()
                                ? "从上次结果选择"
                                : cardTool->judgement.measurementName.c_str();
                            if (ImGui::BeginCombo("可用测量项", preview))
                            {
                                for (const ToolResult::Measurement& measurement : cardTool->lastResult.measurements)
                                {
                                    const bool selected = cardTool->judgement.measurementName == measurement.name;
                                    if (ImGui::Selectable(measurement.name.c_str(), selected))
                                    {
                                        cardTool->judgement.measurementName = measurement.name;
                                        judgementChanged = true;
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                        judgementChanged |= ImGui::DragScalar("测量下限", ImGuiDataType_Double,
                            &cardTool->judgement.minMeasurement, 0.01f, nullptr, nullptr, "%.6f");
                        judgementChanged |= ImGui::DragScalar("测量上限", ImGuiDataType_Double,
                            &cardTool->judgement.maxMeasurement, 0.01f, nullptr, nullptr, "%.6f");
                    }

                    char requiredText[256];
                    snprintf(requiredText, sizeof(requiredText), "%s", cardTool->judgement.requiredText.c_str());
                    ParamLabel("文本条件");
                    if (ImGui::InputText("##judgement_required_text", requiredText,
                        IM_ARRAYSIZE(requiredText)))
                    {
                        cardTool->judgement.requiredText = requiredText;
                        judgementChanged = true;
                    }
                    const char* textModes[] = {"包含", "完全相等"};
                    cardTool->judgement.textMatchMode = std::clamp(cardTool->judgement.textMatchMode, 0, 1);
                    judgementChanged |= ImGui::Combo("文本匹配", &cardTool->judgement.textMatchMode, textModes, IM_ARRAYSIZE(textModes));
                    judgementChanged |= ImGui::Checkbox("区分大小写", &cardTool->judgement.textCaseSensitive);
                    ImGui::TextDisabled("分数/面积为 -1 时不参与判定");
                }
                if (judgementChanged)
                    SaveCurrentRecipe();

                DrawUnifiedToolResult(*cardTool);
                ImGui::Separator();
            }
            return true;
        };
        auto EndCard = []()
        {
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopID();
            ImGui::Spacing();
        };

        // ---- 工具 UI 函数注册 ----
        g_ToolUIMap.clear();
        ToolPanelContext basicPanelContext;
        basicPanelContext.beginCard = [&](const char* title) { BeginCard(title); };
        basicPanelContext.beginCardWithIcon = [&](const char* title, const char* icon)
        {
            BeginCard(title, icon);
        };
        basicPanelContext.endCard = EndCard;
        basicPanelContext.sectionHeader = SectionHeader;
        basicPanelContext.primaryButton = PrimaryButton;
        basicPanelContext.secondaryButton = [&](const char* label) { return SecondaryButton(label); };
        basicPanelContext.secondaryButtonSized = SecondaryButton;
        basicPanelContext.parameterLabel = ParamLabel;
        basicPanelContext.runTool = RunToolFromCard;
        basicPanelContext.drawSearchROI = DrawSearchROIControls;
        basicPanelContext.markRecipeAssetsDirty = MarkCurrentRecipeAssetsDirty;
        basicPanelContext.saveRecipe = SaveCurrentRecipe;
        RegisterBasicToolPanels(g_ToolUIMap, basicPanelContext);
        RegisterDetectionToolPanels(g_ToolUIMap, basicPanelContext);
        RegisterMeasurementToolPanel(g_ToolUIMap, basicPanelContext);
        RegisterAdvancedDetectionToolPanels(g_ToolUIMap, basicPanelContext);


        // ---- 手风琴工具列表（点击展开/收起，底部固定执行区预留空间） ----
        const ImGuiStyle& style = ImGui::GetStyle();
        const float actionButtonH = ImGui::GetFrameHeight() + 4.0f;
        const float bottomModeH = ImGui::GetFrameHeight() + 2.0f;
        const float bottomTimeH = ImGui::GetTextLineHeight();
        const float bottomLoopSettingsH = ToolController::IsLoopEnabled()
            ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
        const float bottomParallelStatusH = ImGui::GetFrameHeightWithSpacing();
        const ToolChainPreflightResult preflight = ToolChainState::Empty()
            ? ToolChainPreflightResult{}
            : ToolChainPreflight::Check(
                  ToolChainState::ReadOnlyTools(), ImageState::HasImage(),
                  ROIState::ReadOnlyItems().size());
        float preflightBlockH = 0.0f;
        if (!ToolChainState::Empty())
        {
            if (preflight.valid())
            {
                preflightBlockH = ImGui::GetTextLineHeightWithSpacing();
            }
            else
            {
                const float wrapWidth = std::max(
                    80.0f, ImGui::GetContentRegionAvail().x - style.ScrollbarSize);
                preflightBlockH = ImGui::GetFrameHeightWithSpacing();

                char summary[128]{};
                std::snprintf(summary, sizeof(summary),
                    "发现 %zu 个问题，执行前请处理：", preflight.issues.size());
                preflightBlockH += ImGui::CalcTextSize(
                    summary, nullptr, false, wrapWidth).y + style.ItemSpacing.y;

                for (const ToolChainPreflightIssue& issue : preflight.issues)
                {
                    const std::string line = issue.toolIndex >= 0
                        ? "工具 " + std::to_string(issue.toolIndex + 1) + "：" + issue.message
                        : "全局：" + issue.message;
                    preflightBlockH += ImGui::CalcTextSize(
                        line.c_str(), nullptr, false, wrapWidth).y + style.ItemSpacing.y;
                }
            }
        }
        const float bottomSeparatorH = style.ItemSpacing.y + 1.0f;
        const float bottomPaddingH = style.WindowPadding.y + 4.0f;
        const float bottomActionRowsH = actionButtonH * 2.0f + style.ItemSpacing.y;
        const float bottomH = ToolChainState::Empty()
            ? 0.0f
            : bottomActionRowsH + bottomLoopSettingsH + bottomParallelStatusH +
              bottomModeH + bottomTimeH + preflightBlockH +
              bottomSeparatorH + style.ItemSpacing.y * 5.0f + bottomPaddingH;
        // 工具列表可滚动区域（底部预留执行按钮空间）
        ImGui::BeginChild("##ToolList", ImVec2(0, -bottomH), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // ---- 空状态：无工具时显示引导提示 ----
        if (visibleToolIndices.empty())
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 start = ImGui::GetCursorScreenPos();
            const float lineH = ImGui::GetTextLineHeight();
            const float lineGap = (std::max)(4.0f, style.ItemSpacing.y);
            const float buttonH = ImGui::GetFrameHeight();
            const float buttonGap = (std::max)(10.0f, style.ItemSpacing.y * 2.0f);
            const float verticalPadding = (std::max)(12.0f, style.WindowPadding.y);
            const float textBlockH = lineH * 3.0f + lineGap * 2.0f;
            // Reserve independent text and action areas.  The previous fixed
            // button offset overlapped the third line, especially with DPI scaling.
            const float cardH = (std::max)(120.0f,
                verticalPadding * 2.0f + textBlockH + buttonGap + buttonH);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            // 空状态卡片背景
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.118f, 0.118f, 0.118f, 1.0f) : ImVec4(0.82f, 0.86f, 0.91f, 1.0f));
            ImU32 border = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.235f, 0.235f, 0.235f, 1.0f) : ImVec4(0.62f, 0.67f, 0.74f, 1.0f));
            drawList->AddRectFilled(start, ImVec2(start.x + avail.x, start.y + cardH), bg, 6.0f);
            drawList->AddRect(start, ImVec2(start.x + avail.x, start.y + cardH), border, 6.0f);

            // 三行空状态提示文字（居中）
            const char* emptyLines[] = {
                "暂无工具",
                "点击上方 [+ 添加工具] 组成处理链。",
                "每个工具默认读取原图工具输出。"
            };
            const float blockH = lineH * IM_ARRAYSIZE(emptyLines) +
                lineGap * (IM_ARRAYSIZE(emptyLines) - 1);
            const float buttonY = start.y + cardH - verticalPadding - buttonH;
            const float textAreaTop = start.y + verticalPadding;
            const float textAreaH = (std::max)(0.0f,
                buttonY - buttonGap - textAreaTop);
            float lineY = textAreaTop +
                (std::max)(0.0f, (textAreaH - blockH) * 0.5f);
            for (int line = 0; line < IM_ARRAYSIZE(emptyLines); ++line)
            {
                const float textW = ImGui::CalcTextSize(emptyLines[line]).x;
                const float lineX = start.x + (std::max)(12.0f, (avail.x - textW) * 0.5f);
                ImGui::SetCursorScreenPos(ImVec2(lineX, lineY));
                if (line == 0)
                    ImGui::TextDisabled("%s", emptyLines[line]);    // 第一行灰色
                else
                    ImGui::TextUnformatted(emptyLines[line]);
                lineY += lineH + lineGap;
            }

            const float addButtonW = (std::min)(220.0f,
                (std::max)(1.0f, avail.x - 32.0f));
            ImGui::SetCursorScreenPos(ImVec2(
                start.x + (avail.x - addButtonW) * 0.5f, buttonY));
            if (ImGui::Button("+ 添加第一个工具", ImVec2(addButtonW, buttonH)))
                ImGui::OpenPopup("AddToolPopup");
        }
        else
        {
            // 紧凑间距模式
            const float compactSpacingY = (std::max)(1.0f, style.ItemSpacing.y * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                ImVec2(style.ItemSpacing.x, compactSpacingY));
            int selectedForRemove = -1;                  // 待删除的工具索引
            int moveFrom = -1;                           // 拖拽排序：来源索引
            int moveTo = -1;                             // 拖拽排序：目标索引
            static int s_scrollOpenedToolToTop = -1;     // 滚动到展开工具的顶部
            static int s_lastFollowedExecutionIndex = -1; // 上次跟随执行的索引

            // 第一遍：计算序号和类型标签的最大宽度（用于对齐）
            float maxIndexWidth = 0.0f;
            float maxTypeWidth = 0.0f;
            for (int visiblePosition = 0;
                visiblePosition < static_cast<int>(visibleToolIndices.size());
                ++visiblePosition)
            {
                char indexText[32];
                std::snprintf(indexText, sizeof(indexText), "%d", visiblePosition + 1);
                maxIndexWidth = (std::max)(maxIndexWidth,
                    ImGui::CalcTextSize(indexText).x);

                const ToolInstance* visibleTool =
                    ToolChainState::AtReadOnly(visibleToolIndices[visiblePosition]);
                if (!visibleTool)
                    continue;
                char typeText[32];
                std::snprintf(typeText, sizeof(typeText), "#%d", visibleTool->type);
                maxTypeWidth = (std::max)(maxTypeWidth,
                    ImGui::CalcTextSize(typeText).x);
            }

            // 卡片头部布局常量
            const float headerControlSize = ImGui::GetFrameHeight();
            const float headerHeight = headerControlSize + compactSpacingY;
            const float headerControlPad = style.FramePadding.x;
            const float headerLeftSlotWidth =
                headerControlSize + headerControlPad * 2.0f;
            const float headerRightSlotWidth = headerLeftSlotWidth;
            const float headerIconSize = ImGui::GetTextLineHeight();
            const float headerIconGap = style.ItemInnerSpacing.x;
            const float headerColumnGap = style.ItemInnerSpacing.x;

            // 执行状态追踪（用于高亮当前执行的工具卡片）
            const int stepToolIndex = ToolController::GetStepToolIndex();
            const bool batchExecutionActive = !ToolController::IsRuntimeMode() &&
                ToolController::GetMode() != ToolController::Mode::Idle;
            const int executionFollowIndex = batchExecutionActive
                ? ToolController::GetCurrentIndex()       // 批量执行：跟随当前索引
                : stepToolIndex;                          // 分步执行：跟随步骤索引
            if (executionFollowIndex < 0)
                s_lastFollowedExecutionIndex = -1;
            const bool executionTargetChanged = executionFollowIndex >= 0 &&
                executionFollowIndex != s_lastFollowedExecutionIndex;

            for (int visiblePosition = 0;
                visiblePosition < static_cast<int>(visibleToolIndices.size());
                ++visiblePosition)
            {
                const int inst = visibleToolIndices[visiblePosition];
                ToolInstance* listToolPtr = ToolChainState::At(inst);
                if (!listToolPtr)
                    continue;
                ToolInstance& listTool = *listToolPtr;
                int type = listTool.type;
                bool expanded = (ToolChainState::ActiveIndex() == inst && !listTool.collapsed);
                const nlohmann::json persistentStateBefore = expanded
                    ? CaptureToolPersistentState(listTool)
                    : nlohmann::json();
                float toolHeaderY = ImGui::GetCursorPosY();
                if (s_scrollOpenedToolToTop == inst && expanded)
                {
                    ImGui::SetScrollY(toolHeaderY);
                    s_scrollOpenedToolToTop = -1;
                }

                const bool batchHl = inst == executionFollowIndex;  // 批量执行高亮

                // ========== 卡片头部（始终可见）==========
                char cardId[32];
                snprintf(cardId, sizeof(cardId), "##toolhdr%d", inst);

                const int headerColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
                if (expanded)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeActive);
                else if (batchHl)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                        ? ImVec4(0.10f, 0.27f, 0.20f, 1.0f)
                        : ImVec4(0.72f, 0.86f, 0.77f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCardHover);

                ImGui::BeginChild(cardId, ImVec2(0.0f, headerHeight), 0,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImVec2 headerMin = ImGui::GetCursorScreenPos();

                const char *name = ToolName(type);
                for (const auto &m : g_ToolRegistry)
                    if (m.type == type) { name = m.name; break; }
                // 工具列表标题直接同步标签输入框；未设置标签时才显示原工具名。
                const std::string displayName = listTool.label.empty()
                    ? std::string(name)
                    : listTool.label;
                const std::string fullDisplayName = ToolInstanceTitle(name, listTool.label);

                char indexLabel[32];
                snprintf(indexLabel, sizeof(indexLabel), "%d", visiblePosition + 1);
                char typeLabel[32];
                snprintf(typeLabel, sizeof(typeLabel), "#%d", type);

                float childW = ImGui::GetContentRegionAvail().x;
                float childH = ImGui::GetWindowHeight();
                const float controlY = (childH - headerControlSize) * 0.5f;

                // 透明点击区（避开右侧删除按钮），点击展开/折叠工具卡片
                ImGui::InvisibleButton(cardId,
                    ImVec2((std::max)(0.0f, childW - headerRightSlotWidth), childH));
                const bool headerHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    listTool.collapsed = false;
                    ToolChainState::SetActiveIndex(expanded ? -1 : inst);
                    if (!expanded)
                        s_scrollOpenedToolToTop = inst;
                }
                // 右键上下文菜单
                if (ImGui::BeginPopupContextItem(cardId)) {
                    const int firstMovable = ToolChainState::FirstMovableIndex();
                    const bool canMove = inst >= firstMovable;    // 原图工具不可移动
                    const bool canMoveUp = canMove && visiblePosition > 0 &&
                        visibleToolIndices[visiblePosition - 1] >= firstMovable;
                    const bool canMoveDown = canMove &&
                        visiblePosition + 1 < static_cast<int>(visibleToolIndices.size());
                    if (ImGui::MenuItem("上移", nullptr, false, canMoveUp)) {
                        moveFrom = inst;
                        moveTo = -1;
                    }
                    if (ImGui::MenuItem("下移", nullptr, false, canMoveDown)) {
                        moveFrom = inst;
                        moveTo = 1;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("复制", "Ctrl+C"))
                        ToolChainState::CopyToolToClipboard(inst);
                    if (ImGui::MenuItem("粘贴", "Ctrl+V", false, ToolChainState::HasToolClipboard()))
                        pasteToolAfterIndex = inst;
                    ImGui::Separator();
                    const bool canRemove = inst >= 0 && inst < static_cast<int>(ToolChainState::Count());
                    if (ImGui::MenuItem("删除", nullptr, false, canRemove))
                        selectedForRemove = inst;
                    ImGui::EndPopup();
                }

                ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
                ImVec2 indexSize = ImGui::CalcTextSize(indexLabel);
                ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);

                ImDrawList* headerDraw = ImGui::GetWindowDrawList();
                if (batchHl)
                {
                    const ImU32 stepAccent = ImGui::ColorConvertFloat4ToU32(isDark
                        ? ImVec4(0.22f, 0.92f, 0.48f, 1.0f)
                        : ImVec4(0.04f, 0.58f, 0.24f, 1.0f));
                    headerDraw->AddRectFilled(
                        headerMin,
                        ImVec2(headerMin.x + 3.0f, headerMin.y + childH),
                        stepAccent,
                        2.0f);
                    headerDraw->AddRect(
                        headerMin,
                        ImVec2(headerMin.x + childW, headerMin.y + childH),
                        stepAccent,
                        4.0f,
                        0,
                        1.5f);
                }
                ImU32 controlBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
                ImU32 controlText = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.70f, 0.76f, 0.84f, 1.0f) : ImVec4(0.28f, 0.30f, 0.34f, 1.0f));

                ImVec2 arrowBoxMin(headerMin.x + headerControlPad, headerMin.y + controlY);
                ImVec2 arrowBoxMax(arrowBoxMin.x + headerControlSize,
                    arrowBoxMin.y + headerControlSize);
                headerDraw->AddRect(arrowBoxMin, arrowBoxMax, controlBorder,
                    style.FrameRounding);
                ImVec2 arrowCenter((arrowBoxMin.x + arrowBoxMax.x) * 0.5f, (arrowBoxMin.y + arrowBoxMax.y) * 0.5f);
                const float triHalf = headerControlSize * 0.20f;
                if (expanded)
                {
                    headerDraw->AddTriangleFilled(
                        ImVec2(arrowCenter.x - triHalf, arrowCenter.y - triHalf * 0.45f),
                        ImVec2(arrowCenter.x + triHalf, arrowCenter.y - triHalf * 0.45f),
                        ImVec2(arrowCenter.x, arrowCenter.y + triHalf * 0.65f),
                        controlText);
                }
                else
                {
                    headerDraw->AddTriangleFilled(
                        ImVec2(arrowCenter.x - triHalf * 0.45f, arrowCenter.y - triHalf),
                        ImVec2(arrowCenter.x - triHalf * 0.45f, arrowCenter.y + triHalf),
                        ImVec2(arrowCenter.x + triHalf * 0.65f, arrowCenter.y),
                        controlText);
                }

                // 展开按钮 | 图标 | 名称 | 序号 | 类型 | 删除按钮。
                // 序号和类型使用全列表的固定列宽，避免 #0/#12 或两位序号造成跳动。
                const float headerGroupX = headerMin.x + headerLeftSlotWidth +
                    style.FramePadding.x;
                const float headerIconY = headerMin.y + (childH - headerIconSize) * 0.5f;
                const float nameX = headerGroupX + headerIconSize + headerIconGap;
                const float metadataRightX = headerMin.x + childW -
                    headerRightSlotWidth - style.FramePadding.x;
                const float minimumNameWidth = ImGui::GetFontSize() * 3.0f;
                const float availableAfterNameStart =
                    (std::max)(0.0f, metadataRightX - nameX);
                const bool showIndexColumn = availableAfterNameStart >=
                    minimumNameWidth + headerColumnGap + maxIndexWidth;
                const bool showTypeColumn = showIndexColumn &&
                    availableAfterNameStart >= minimumNameWidth + headerColumnGap +
                        maxIndexWidth + headerColumnGap + maxTypeWidth;

                float metadataCursorX = metadataRightX;
                float typeColumnX = metadataRightX;
                if (showTypeColumn)
                {
                    typeColumnX = metadataCursorX - maxTypeWidth;
                    metadataCursorX = typeColumnX - headerColumnGap;
                }
                float indexColumnX = metadataCursorX;
                if (showIndexColumn)
                {
                    indexColumnX = metadataCursorX - maxIndexWidth;
                    metadataCursorX = indexColumnX - headerColumnGap;
                }
                const float nameMaxX = (std::max)(nameX, metadataCursorX);
                const float nameAvailableW = (std::max)(0.0f, nameMaxX - nameX);
                const bool headerTitleClipped = nameSize.x > nameAvailableW;
                std::string headerDisplayName = displayName;
                ImFontAtlasRect headerIconRect;
                if (FontManager::GetToolIconRect(type, &headerIconRect))
                {
                    headerDraw->AddImageRounded(ImGui::GetIO().Fonts->TexRef,
                        ImVec2(headerGroupX, headerIconY),
                        ImVec2(headerGroupX + headerIconSize, headerIconY + headerIconSize),
                        headerIconRect.uv0, headerIconRect.uv1,
                        IM_COL32_WHITE, 3.0f);
                }
                else
                {
                    DrawToolIcon(headerDraw, type, ImVec2(headerGroupX, headerIconY), headerIconSize, ToolAccentColor(type));
                }

                ImU32 headerTextColor = ImGui::ColorConvertFloat4ToU32(batchHl
                    ? (isDark ? ImVec4(0.25f, 0.95f, 0.45f, 1) : ImVec4(0.05f, 0.55f, 0.20f, 1))   // 绿色高亮
                    : (isDark ? ImVec4(1, 1, 1, 0.85f) : ImVec4(0.1f, 0.1f, 0.1f, 0.85f)));
                const float rowCenterY = headerMin.y + childH * 0.5f;
                const float nameY = rowCenterY - nameSize.y * 0.5f;
                headerDraw->PushClipRect(ImVec2(nameX, headerMin.y), ImVec2(nameMaxX, headerMin.y + childH), true);
                headerDraw->AddText(ImVec2(nameX, nameY), headerTextColor,
                    headerDisplayName.c_str());
                headerDraw->PopClipRect();
                if (showIndexColumn)
                {
                    const float indexX = indexColumnX + maxIndexWidth - indexSize.x;
                    headerDraw->AddText(ImVec2(indexX, rowCenterY - indexSize.y * 0.5f),
                        headerTextColor, indexLabel);
                }
                if (showTypeColumn)
                {
                    const float typeX = typeColumnX + maxTypeWidth - typeSize.x;
                    headerDraw->AddText(ImVec2(typeX, rowCenterY - typeSize.y * 0.5f),
                        headerTextColor, typeLabel);
                }

                bool removeHovered = false;
                char removeId[32];
                snprintf(removeId, sizeof(removeId), "X##remove_tool_%d", inst);
                ImVec2 removeMin(headerMin.x + childW - headerControlPad -
                    headerControlSize, headerMin.y + controlY);
                ImVec2 removeMax(removeMin.x + headerControlSize,
                    removeMin.y + headerControlSize);
                ImGui::SetCursorScreenPos(removeMin);
                ImGui::InvisibleButton(removeId,
                    ImVec2(headerControlSize, headerControlSize));
                removeHovered = ImGui::IsItemHovered();
                const bool removeClicked = ImGui::IsItemClicked();
                ImU32 removeBg = ImGui::ColorConvertFloat4ToU32(removeHovered
                    ? (isDark ? ImVec4(0.55f, 0.16f, 0.16f, 1.0f) : ImVec4(0.95f, 0.20f, 0.20f, 1.0f))
                    : (isDark ? ImVec4(0.18f, 0.20f, 0.24f, 1.0f) : ImVec4(0.90f, 0.92f, 0.95f, 1.0f)));
                ImU32 removeBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
                ImU32 removeText = ImGui::ColorConvertFloat4ToU32(removeHovered ? ImVec4(1, 1, 1, 1) : (isDark ? ImVec4(0.76f, 0.80f, 0.86f, 1) : ImVec4(0.36f, 0.38f, 0.42f, 1)));
                headerDraw->AddRectFilled(removeMin, removeMax, removeBg,
                    style.FrameRounding);
                headerDraw->AddRect(removeMin, removeMax, removeBorder,
                    style.FrameRounding);
                ImVec2 xCenter((removeMin.x + removeMax.x) * 0.5f, (removeMin.y + removeMax.y) * 0.5f);
                const float xHalf = headerControlSize * 0.20f;
                const float xThickness = (std::max)(1.3f, headerControlSize * 0.07f);
                headerDraw->AddLine(ImVec2(xCenter.x - xHalf, xCenter.y - xHalf),
                    ImVec2(xCenter.x + xHalf, xCenter.y + xHalf), removeText, xThickness);
                headerDraw->AddLine(ImVec2(xCenter.x + xHalf, xCenter.y - xHalf),
                    ImVec2(xCenter.x - xHalf, xCenter.y + xHalf), removeText, xThickness);
                if (removeClicked)
                    selectedForRemove = inst;
                if (headerHovered && !removeHovered)
                {
                    if (headerTitleClipped)
                        ImGui::SetTooltip("%s", fullDisplayName.c_str());
                }

                ImGui::EndChild();
                if (executionTargetChanged && inst == executionFollowIndex)
                {
                    ImGuiWindow* toolListWindow = ImGui::GetCurrentWindow();
                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();
                    if (itemMin.y < toolListWindow->ClipRect.Min.y)
                        ImGui::SetScrollHereY(0.0f);
                    else if (itemMax.y > toolListWindow->ClipRect.Max.y)
                        ImGui::SetScrollHereY(1.0f);
                    s_lastFollowedExecutionIndex = executionFollowIndex;
                }
                int headerColorStackNow = ImGui::GetCurrentContext()->ColorStack.Size;
                if (headerColorStackNow > headerColorStackBase)
                    ImGui::PopStyleColor(headerColorStackNow - headerColorStackBase);

                // ---- 展开的工具 UI（手风琴内容） ----
                if (expanded)
                {
                    if (type == 12)
                    {
                        ImGui::TextDisabled("执行时恢复本轮原图");
                    }
                    else
                    {
                        const char* inputModes[] = { "上一步原图", "上一步处理图", "原图工具输出" };
                        ParamLabel("输入");
                        char inputId[32];
                        snprintf(inputId, sizeof(inputId), "##input_%d", inst);
                        ImGui::Combo(inputId, &listTool.inputSourceMode, inputModes, IM_ARRAYSIZE(inputModes));
                    }

                    auto uiFn = g_ToolUIMap.find(type);
                    if (uiFn != g_ToolUIMap.end() && uiFn->second)
                    {
                        currentCardType = type;
                        currentCardInst = inst;
                        uiFn->second(listTool, inst);
                        currentCardType = -1;
                        currentCardInst = -1;
                    }

                    const nlohmann::json persistentStateAfter =
                        CaptureToolPersistentState(listTool);
                    if (persistentStateAfter != persistentStateBefore)
                    {
                        listTool.MarkParametersChanged();
                        MarkCurrentRecipeDirty();
                    }
                }
            }

            if (ToolChainState::ActiveIndex() >= 0)
            {
                float trailingSpace = ImGui::GetWindowHeight() - 44.0f;
                if (trailingSpace < 120.0f)
                    trailingSpace = 120.0f;
                ImGui::Dummy(ImVec2(1.0f, trailingSpace));
            }

            ImGui::PopStyleVar();

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && ToolChainState::ActiveIndex() >= 0)
            {
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
                    ToolChainState::CopyToolToClipboard(ToolChainState::ActiveIndex());
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && ToolChainState::HasToolClipboard())
                    pasteToolAfterIndex = ToolChainState::ActiveIndex();
            }

            if (moveFrom >= 0 && moveTo != 0) {
                GeometryDrawEditor::Cancel();
                if (ToolChainState::MoveToolWithinTaskGroup(moveFrom, moveTo)) {
                    ToolController::OnToolChainChanged();
                    SaveCurrentRecipe();
                }
            }

            if (selectedForRemove >= 0) {
                GeometryDrawEditor::Cancel();
                if (ToolChainState::RemoveTool(selectedForRemove)) {
                    ToolController::OnToolChainChanged();
                    SaveCurrentRecipe();
                }
            }

            if (duplicateToolIndex >= 0) {
                GeometryDrawEditor::Cancel();
                int insertedIndex = -1;
                if (ToolChainState::DuplicateTool(duplicateToolIndex, &insertedIndex)) {
                    ToolController::OnToolChainChanged();
                    ToolChainState::SetActiveIndex(insertedIndex);
                    SaveCurrentRecipe();
                }
            }

            if (pasteToolAfterIndex >= 0) {
                GeometryDrawEditor::Cancel();
                int insertedIndex = -1;
                if (ToolChainState::PasteToolAfter(pasteToolAfterIndex, &insertedIndex)) {
                    ToolController::OnToolChainChanged();
                    ToolChainState::SetActiveIndex(insertedIndex);
                    SaveCurrentRecipe();
                }
            }
        }

        ImGui::EndChild();

        // ---- 底部：全部执行 / 单步 / 循环 按钮 ----
        if (!ToolChainState::Empty())
        {
            ImGui::Separator();

            if (!preflight.valid())
            {
                if (ImGui::CollapsingHeader("运行前检查", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                        "发现 %zu 个问题，执行前请处理：", preflight.issues.size());
                    for (const ToolChainPreflightIssue& issue : preflight.issues)
                    {
                        if (issue.toolIndex >= 0)
                            ImGui::TextWrapped("工具 %d：%s", issue.toolIndex + 1, issue.message.c_str());
                        else
                            ImGui::TextWrapped("全局：%s", issue.message.c_str());
                    }
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.20f, 0.70f, 0.34f, 1.0f), "运行前检查通过");
            }

            auto mode = ToolController::GetMode();
            bool running = (mode != ToolController::Mode::Idle);
            const bool loopEnabled = ToolController::IsLoopEnabled();

            auto RunActionButton = [](const char* label, const ImVec2& size, const ImVec4& base, const ImVec4& hover, const ImVec4& active) -> bool
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
                bool clicked = ImGui::Button(label, size);
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                return clicked;
            };
            auto ModeButton = [isDark](const char* label, const ImVec2& size, bool selected, const ImVec4& selectedBase, const ImVec4& selectedHover, const ImVec4& idleBase, const ImVec4& idleHover) -> bool
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, selected ? selectedBase : idleBase);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? selectedHover : idleHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedBase);
                ImGui::PushStyleColor(ImGuiCol_Text, selected
                    ? ImVec4(1.0f, 1.0f, 1.0f, 0.95f)
                    : (isDark ? ImVec4(0.78f, 0.84f, 0.90f, 0.92f) : ImVec4(0.18f, 0.25f, 0.32f, 0.90f)));
                bool clicked = ImGui::Button(label, size);
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                return clicked;
            };

            const float bottomAvailW = ImGui::GetContentRegionAvail().x;
            const float actionGap = style.ItemSpacing.x;
            const float runW = (std::max)(0.0f, (bottomAvailW - actionGap) * 0.5f);
            const float secondaryW = (std::max)(0.0f,
                (bottomAvailW - actionGap * 2.0f) / 3.0f);

            const ImVec4 runBase = isDark ? ImVec4(0.10f, 0.40f, 0.48f, 1.0f) : ImVec4(0.12f, 0.49f, 0.57f, 1.0f);
            const ImVec4 runHover = isDark ? ImVec4(0.13f, 0.50f, 0.59f, 1.0f) : ImVec4(0.08f, 0.42f, 0.50f, 1.0f);
            const ImVec4 runActive = isDark ? ImVec4(0.08f, 0.33f, 0.40f, 1.0f) : ImVec4(0.05f, 0.35f, 0.42f, 1.0f);
            const ImVec4 subBase = isDark ? ImVec4(0.176f, 0.176f, 0.188f, 1.0f) : ImVec4(0.84f, 0.87f, 0.89f, 1.0f);
            const ImVec4 subHover = isDark ? ImVec4(0.235f, 0.235f, 0.235f, 1.0f) : ImVec4(0.76f, 0.84f, 0.86f, 1.0f);
            const ImVec4 subActive = isDark ? ImVec4(0.12f, 0.23f, 0.27f, 1.0f) : ImVec4(0.65f, 0.78f, 0.81f, 1.0f);
            const ImVec4 loopBase = loopEnabled ? (isDark ? ImVec4(0.12f, 0.42f, 0.25f, 1.0f) : ImVec4(0.28f, 0.62f, 0.38f, 1.0f)) : subBase;
            const ImVec4 loopHover = loopEnabled ? (isDark ? ImVec4(0.16f, 0.52f, 0.31f, 1.0f) : ImVec4(0.22f, 0.54f, 0.32f, 1.0f)) : subHover;
            const ImVec4 loopActive = loopEnabled ? (isDark ? ImVec4(0.09f, 0.34f, 0.20f, 1.0f) : ImVec4(0.18f, 0.47f, 0.27f, 1.0f)) : subActive;
            const std::string& currentTaskGroup =
                TaskGroupWindow::CurrentTaskGroupName();
            const bool hasCurrentTask = !currentTaskGroup.empty() &&
                ToolChainState::TaskGroupIndexByName(currentTaskGroup) >= 0;

            if (RunActionButton("全部执行", ImVec2(runW, actionButtonH),
                runBase, runHover, runActive))
            {
                // 全部执行也优先请求绑定相机取帧，取帧完成后再执行完整工具链。
                // 无相机或取帧失败时由控制器回退到已有任务图片并执行前置检查。
                ToolController::RequestRunAll(loopEnabled, true);
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(!hasCurrentTask);
            if (RunActionButton("执行当前任务", ImVec2(runW, actionButtonH),
                runBase, runHover, runActive))
            {
                // 只有当前任务显式绑定相机时才请求取帧；未绑定任务始终使用
                // 任务图片或公共图片。强制抓帧仅保留给 PLC 生产触发入口。
                ToolController::RequestRunTaskGroup(
                    currentTaskGroup, loopEnabled, true, false);
            }
            ImGui::EndDisabled();

            // 单步执行：整条配方与当前任务使用两个明确入口。
            int stepCur = ToolController::GetStepCursor();
            const int allStepTotal = static_cast<int>(ToolChainState::Count());
            const int currentTaskStepTotal = hasCurrentTask
                ? static_cast<int>(std::count_if(
                    ToolChainState::ReadOnlyTools().begin(),
                    ToolChainState::ReadOnlyTools().end(),
                    [&currentTaskGroup](const ToolInstance& tool)
                    {
                        return tool.groupName == currentTaskGroup;
                    })) : 0;
            const bool allStepping = stepCur > 0 && !ToolController::IsStepTaskGroup();
            const bool currentTaskStepping = stepCur > 0 &&
                ToolController::IsStepTaskGroup() &&
                ToolController::GetStepTaskGroupName() == currentTaskGroup;

            if (RunActionButton(allStepping ? "全部单步中" : "全部单步",
                ImVec2(secondaryW, actionButtonH),
                allStepping ? runBase : subBase,
                allStepping ? runHover : subHover,
                allStepping ? runActive : subActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (allStepping && stepCur >= allStepTotal)
                    ToolController::RequestStepReset();
                else
                    ToolController::RequestStepNext();
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(!hasCurrentTask || currentTaskStepTotal == 0);
            if (RunActionButton(currentTaskStepping ? "任务单步中" : "当前任务单步",
                ImVec2(secondaryW, actionButtonH),
                currentTaskStepping ? runBase : subBase,
                currentTaskStepping ? runHover : subHover,
                currentTaskStepping ? runActive : subActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (currentTaskStepping && stepCur >= currentTaskStepTotal)
                    ToolController::RequestStepReset();
                else
                    ToolController::RequestStepNextTaskGroup(currentTaskGroup);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();

            // 循环
            if (RunActionButton(loopEnabled ? "循环开" : "循环",
                ImVec2(secondaryW, actionButtonH), loopBase, loopHover, loopActive))
            {
                ToolController::SetLoopEnabled(!loopEnabled);
            }

            if (loopEnabled)
            {
                int loopIntervalMs = ToolController::GetLoopIntervalMs();
                ParamLabel("循环等待(ms)");
                if (ImGui::InputInt("##ToolChainLoopInterval", &loopIntervalMs, 10, 100))
                {
                    ToolController::SetLoopIntervalMs(loopIntervalMs);
                    MarkCurrentRecipeDirty();
                }
                ImGui::SetItemTooltip("每轮完成后的等待时间；0 表示立即继续");
            }

            bool taskParallel = ToolController::IsTaskParallelEnabled();
            const int enabledTaskCount = static_cast<int>(std::count_if(
                ToolChainState::ReadOnlyTaskGroups().begin(),
                ToolChainState::ReadOnlyTaskGroups().end(),
                [](const TaskGroupDefinition& group)
                {
                    return group.enabled;
                }));
            if (ImGui::BeginTable("##task_parallel_status", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float parallelToggleWidth = ImGui::CalcTextSize("任务并行").x +
                    ImGui::GetFrameHeight();
                ImGui::TableSetupColumn("##toggle", ImGuiTableColumnFlags_WidthFixed,
                    parallelToggleWidth);
                ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(running || loopEnabled || enabledTaskCount < 2);
                if (ImGui::Checkbox("任务并行", &taskParallel))
                    ToolController::SetTaskParallelEnabled(taskParallel);
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("最多 %d 个任务同时执行",
                    ToolController::GetTaskParallelLimit());
                ImGui::SetItemTooltip(
                    "仅用于“全部执行”；每个任务内部仍按工具顺序执行。\n"
                    "循环、单步、单任务执行保持原来的顺序模式。");
                ImGui::EndTable();
            }

            bool runtimeMode = ToolController::IsRuntimeMode();
            const float modeGap = 5.0f;
            const float modeW = (bottomAvailW - modeGap) * 0.5f;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(modeGap, ImGui::GetStyle().ItemSpacing.y));
            if (ModeButton("普通模式", ImVec2(modeW, bottomModeH), !runtimeMode, runBase, runHover, subBase, subHover))
                ToolController::SetRuntimeMode(false);
            ImGui::SameLine();
            if (ModeButton("运行模式", ImVec2(modeW, bottomModeH), runtimeMode, runBase, runHover, subBase, subHover))
                ToolController::SetRuntimeMode(true);
            ImGui::PopStyleVar();

            const float stepMs = ToolController::GetLastStepTimeMs();
            const float totalMs = ToolController::GetTotalTimeMs();
            const ImVec4 timeColor = isDark ? ImVec4(0.34f, 0.78f, 0.48f, 1.0f) : ImVec4(0.05f, 0.40f, 0.19f, 1.0f);
            const ImVec4 progressColor = isDark ? ImVec4(0.72f, 0.78f, 0.86f, 1.0f) : ImVec4(0.22f, 0.28f, 0.36f, 1.0f);
            if (running)
            {
                const float elapsedMs = ToolController::GetElapsedTimeMs();
                if (loopEnabled)
                {
                    const int waitRemainingMs = ToolController::GetLoopWaitRemainingMs();
                    if (waitRemainingMs > 0)
                    {
                        ImGui::TextColored(progressColor,
                            "第%llu轮  本轮%.1fms  等待%d/%dms",
                            static_cast<unsigned long long>(ToolController::GetLoopIteration()),
                            totalMs,
                            waitRemainingMs,
                            ToolController::GetLoopIntervalMs());
                    }
                    else
                    {
                        ImGui::TextColored(progressColor,
                            "第%llu轮  工具%d/%d  本轮%.1fms  上步%.1fms",
                            static_cast<unsigned long long>(ToolController::GetLoopIteration()),
                            ToolController::GetRunProgressCurrent(),
                            ToolController::GetRunProgressTotal(),
                            elapsedMs,
                            stepMs);
                    }
                }
                else
                {
                    ImGui::TextColored(progressColor,
                        "运行中 %d/%d | 已用 %.3fms | 上步 %.3fms",
                        ToolController::GetRunProgressCurrent(),
                        ToolController::GetRunProgressTotal(),
                        elapsedMs,
                        stepMs);
                }
            }
            else if (totalMs > 0.0f)
            {
                ImGui::TextColored(timeColor,
                    "%s: 总 %.3fms | 上步 %.3fms",
                    ToolController::WasLastRunTaskGroup()
                        ? "上次当前任务" : "上次全部执行",
                    totalMs,
                    stepMs);
            }
            else
            {
                ImGui::TextDisabled("%zu 个工具", ToolChainState::Count());
            }
        }

        ImGui::PopStyleVar(4);
        int toolsColorStackNow = ImGui::GetCurrentContext()->ColorStack.Size;
        if (toolsColorStackNow > toolsColorStackBase)
            ImGui::PopStyleColor(toolsColorStackNow - toolsColorStackBase);

        ImGui::End();
        UpdateCurrentRecipeAutoSave();
    }

} // namespace UI
