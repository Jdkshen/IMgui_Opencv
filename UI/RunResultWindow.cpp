#include "RunResultWindow.h"

#include "DockSpaceHost.h"
#include "../Core/DX12Context.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/ImageState.h"
#include "../Core/ThemeManager.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolController.h"
#include "../Renderer/PreviewTextureCache.h"
#include "../include/imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace UI
{
namespace
{
    struct RunResultRow
    {
        int index = 0;
        std::string name;
        ToolResultStatus status = ToolResultStatus::Error;
        bool skipped = false;
        bool executed = false;
        float timeMs = 0.0f;
        std::string summary;
    };

    struct RunResultSnapshot
    {
        bool valid = false;
        bool loopRound = false;
        std::uint64_t loopIteration = 0;
        std::string recipeName;
        ToolResultStatus overallStatus = ToolResultStatus::Error;
        float totalTimeMs = 0.0f;
        int passCount = 0;
        int failCount = 0;
        int errorCount = 0;
        int skippedCount = 0;
        int pendingCount = 0;
        std::vector<RunResultRow> rows;
        std::vector<ToolResult> overlayResults;
        cv::Mat resultImage;
        std::uint64_t textureKey = 0;
        std::uint64_t captureSerial = 0;
    };

    struct RunResultViewState
    {
        bool visible = false;
        bool maximized = false;
        bool showImagePreview = false;
        bool restorePlacement = false;
        ImVec2 restorePosition{};
        ImVec2 restoreSize{};
        float imageZoom = 1.0f;
        ImVec2 imagePan{};
        int imageWidth = 0;
        int imageHeight = 0;
    };

    struct GroupResultWindow
    {
        std::uint64_t id = 0;
        std::string groupName;
        RunResultSnapshot snapshot;
        RunResultViewState view;
    };

    RunResultSnapshot g_snapshot;
    RunResultViewState g_mainView;
    RunResultViewState g_groupDashboardView;
    std::vector<GroupResultWindow> g_groupWindows;
    std::uint64_t g_expandedGroupId = 0;
    std::uint64_t g_nextGroupWindowId = 1;
    bool g_autoShow = true;
    bool g_autoShowPreferenceLoaded = false;
    std::uint64_t g_seenBatchSerial = 0;
    constexpr std::size_t kMaximumGroupResultWindows =
        ToolChainState::MaximumTaskGroups();

    void LoadAutoShowPreference()
    {
        if (g_autoShowPreferenceLoaded)
            return;
        g_autoShowPreferenceLoaded = true;

        std::ifstream input("ui_preferences.cfg");
        std::string key;
        int value = 1;
        while (input >> key >> value)
        {
            if (key == "auto_show_result")
            {
                g_autoShow = value != 0;
                break;
            }
        }
    }

    void SaveAutoShowPreference()
    {
        std::ofstream output("ui_preferences.cfg", std::ios::trunc);
        if (output)
            output << "auto_show_result " << (g_autoShow ? 1 : 0) << "\n";
    }

    const char* StatusText(ToolResultStatus status)
    {
        switch (status)
        {
        case ToolResultStatus::Pass: return "OK";
        case ToolResultStatus::Fail: return "NG";
        case ToolResultStatus::Error: return "ERROR";
        default: return "ERROR";
        }
    }

    const char* StatusDescription(ToolResultStatus status)
    {
        switch (status)
        {
        case ToolResultStatus::Pass: return "检测通过";
        case ToolResultStatus::Fail: return "检测不合格";
        case ToolResultStatus::Error: return "执行异常";
        default: return "执行异常";
        }
    }

    ImVec4 StatusColor(ToolResultStatus status, bool dark)
    {
        switch (status)
        {
        case ToolResultStatus::Pass:
            return dark ? ImVec4(0.12f, 0.52f, 0.29f, 1.0f) : ImVec4(0.13f, 0.62f, 0.32f, 1.0f);
        case ToolResultStatus::Fail:
            return dark ? ImVec4(0.72f, 0.22f, 0.18f, 1.0f) : ImVec4(0.82f, 0.24f, 0.18f, 1.0f);
        case ToolResultStatus::Error:
        default:
            return dark ? ImVec4(0.78f, 0.48f, 0.12f, 1.0f) : ImVec4(0.88f, 0.50f, 0.10f, 1.0f);
        }
    }

    ImVec4 StatusTextColor(ToolResultStatus status, bool dark)
    {
        switch (status)
        {
        case ToolResultStatus::Pass:
            return dark ? ImVec4(0.32f, 0.86f, 0.48f, 1.0f) : ImVec4(0.05f, 0.48f, 0.20f, 1.0f);
        case ToolResultStatus::Fail:
            return dark ? ImVec4(1.00f, 0.42f, 0.36f, 1.0f) : ImVec4(0.78f, 0.16f, 0.12f, 1.0f);
        case ToolResultStatus::Error:
        default:
            return dark ? ImVec4(1.00f, 0.70f, 0.28f, 1.0f) : ImVec4(0.72f, 0.36f, 0.04f, 1.0f);
        }
    }

    std::string ToolDisplayName(const ToolInstance& tool)
    {
        const char* baseName = tool.type == 12 ? "原图" : ToolRegistry::GetName(tool.type);
        if (!baseName || !*baseName)
            baseName = "工具";
        if (tool.label.empty())
            return baseName;
        return tool.label;
    }

    std::string ResultSummary(const ToolResult& result)
    {
        if (result.skipped)
            return result.message.empty() ? "已跳过" : result.message;
        if (!result.statusReason.empty())
            return result.statusReason;
        if (!result.message.empty())
            return result.message;

        char text[128]{};
        if (!result.detections.empty())
            std::snprintf(text, sizeof(text), "%zu 个检测目标", result.detections.size());
        else if (!result.texts.empty())
            std::snprintf(text, sizeof(text), "%zu 条识别文本", result.texts.size());
        else if (!result.regions.empty())
            std::snprintf(text, sizeof(text), "%zu 个区域", result.regions.size());
        else if (!result.measurements.empty())
            std::snprintf(text, sizeof(text), "%zu 项测量", result.measurements.size());
        else if (!result.lines.empty())
            std::snprintf(text, sizeof(text), "%zu 条线段", result.lines.size());
        else
            std::snprintf(text, sizeof(text), "执行完成");
        return text;
    }

    RunResultSnapshot BuildSnapshot(const std::string* groupFilter = nullptr)
    {
        RunResultSnapshot next;
        next.valid = true;
        next.loopIteration = ToolController::GetLastCompletedLoopRound();
        next.loopRound = next.loopIteration > 0;
        next.captureSerial = ToolController::GetCompletedBatchSerial();
        next.recipeName = CurrentRecipeName();
        next.totalTimeMs = groupFilter ? 0.0f : ToolController::GetTotalTimeMs();
        if (groupFilter)
        {
            next.resultImage = ToolController::GetTaskResultImage(*groupFilter).clone();
            next.textureKey = 0x4000000000000000ULL;
            for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            {
                if (group.name == *groupFilter)
                {
                    next.textureKey |= group.id;
                    break;
                }
            }
        }
        else
        {
            next.resultImage = ImageState::Current().clone();
            next.textureKey = 0x8000000000000001ULL;
        }

        std::vector<ToolResult> aggregateResults;
        const auto& tools = ToolChainState::ReadOnlyTools();
        next.rows.reserve(tools.size());
        aggregateResults.reserve(tools.size());

        for (int index = 0; index < static_cast<int>(tools.size()); ++index)
        {
            const ToolInstance& tool = tools[index];
            if (!tool.enabled || (groupFilter && tool.groupName != *groupFilter))
                continue;

            RunResultRow row;
            row.index = static_cast<int>(next.rows.size()) + 1;
            row.name = ToolDisplayName(tool);
            row.timeMs = ToolController::GetToolTimeMs(index);
            row.executed = tool.hasLastResult;
            if (groupFilter)
                next.totalTimeMs += row.timeMs;

            if (!tool.hasLastResult)
            {
                row.status = ToolResultStatus::Error;
                row.summary = "未执行";
                ++next.pendingCount;
            }
            else
            {
                row.status = tool.lastResult.status;
                row.skipped = tool.lastResult.skipped;
                row.summary = ResultSummary(tool.lastResult);
                aggregateResults.push_back(tool.lastResult);
                ToolResult overlayResult = tool.lastResult;
                overlayResult.debugImage.release();
                next.overlayResults.push_back(std::move(overlayResult));

                if (row.skipped)
                    ++next.skippedCount;
                else if (row.status == ToolResultStatus::Pass)
                    ++next.passCount;
                else if (row.status == ToolResultStatus::Fail)
                    ++next.failCount;
                else
                    ++next.errorCount;
            }
            next.rows.push_back(std::move(row));
        }

        next.overallStatus = HardwareRuntimeService::AggregateInspectionStatus(aggregateResults);
        if (next.pendingCount > 0 && next.overallStatus == ToolResultStatus::Pass)
            next.overallStatus = ToolResultStatus::Error;
        return next;
    }

    std::vector<std::string> CollectTaskGroups()
    {
        std::vector<std::string> groups;
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            groups.push_back(group.name);

        bool hasUngroupedTools = false;
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
        {
            if (tool.groupName.empty())
            {
                hasUngroupedTools = true;
                continue;
            }
            if (std::find(groups.begin(), groups.end(), tool.groupName) == groups.end())
                groups.push_back(tool.groupName);
        }
        if (hasUngroupedTools)
            groups.push_back({});
        return groups;
    }

    void CaptureSnapshots()
    {
        if (ToolController::WasLastRunTaskGroup())
        {
            const std::string& groupName = ToolController::GetLastRunTaskGroupName();
            g_snapshot = BuildSnapshot(&groupName);
        }
        else
        {
            g_snapshot = BuildSnapshot();
        }
        const std::vector<std::string> groups = CollectTaskGroups();
        for (GroupResultWindow& window : g_groupWindows)
        {
            if (std::find(groups.begin(), groups.end(), window.groupName) == groups.end())
            {
                window.view.visible = false;
                continue;
            }
            window.snapshot = BuildSnapshot(&window.groupName);
        }
    }

    void OpenGroupResultWindows()
    {
        const std::vector<std::string> groups = CollectTaskGroups();
        g_mainView.visible = false;
        std::vector<GroupResultWindow> orderedWindows;
        orderedWindows.reserve((std::min)(groups.size(), kMaximumGroupResultWindows));
        for (const std::string& groupName : groups)
        {
            if (orderedWindows.size() >= kMaximumGroupResultWindows)
                break;
            auto existing = std::find_if(g_groupWindows.begin(), g_groupWindows.end(),
                [&groupName](const GroupResultWindow& window)
                {
                    return window.groupName == groupName;
                });
            if (existing != g_groupWindows.end())
            {
                existing->snapshot = BuildSnapshot(&existing->groupName);
                existing->view.visible = false;
                orderedWindows.push_back(std::move(*existing));
                continue;
            }

            GroupResultWindow window;
            window.id = g_nextGroupWindowId++;
            window.groupName = groupName;
            window.snapshot = BuildSnapshot(&window.groupName);
            window.view.visible = false;
            orderedWindows.push_back(std::move(window));
        }
        g_groupWindows = std::move(orderedWindows);
        g_expandedGroupId = 0;
        g_groupDashboardView.visible = !groups.empty();
    }

    void DrawCenteredText(const char* text, const ImVec4& color)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const float textWidth = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (width - textWidth) * 0.5f));
        ImGui::TextColored(color, "%s", text);
    }

    void DrawCenteredTableText(const char* text, const ImVec4& color)
    {
        ImGui::AlignTextToFramePadding();
        DrawCenteredText(text, color);
    }

    void DrawMetricCard(const char* id, const char* label, const char* value, const ImVec4& valueColor)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            g_CurrentTheme == 0 ? ImVec4(0.12f, 0.14f, 0.17f, 1.0f) : ImVec4(0.93f, 0.95f, 0.96f, 1.0f));
        ImGui::BeginChild(id, ImVec2(0.0f, 68.0f), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float textBlockHeight = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        const float topOffset = (std::max)(0.0f, (ImGui::GetContentRegionAvail().y - textBlockHeight) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topOffset);
        DrawCenteredText(label, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        DrawCenteredText(value, valueColor);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawCompactMetric(const char* label, const char* value, const ImVec4& valueColor)
    {
        ImGui::TableNextColumn();
        DrawCenteredText(label, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        DrawCenteredText(value, valueColor);
    }

    void DrawResultImageThumbnail(float height, const RunResultSnapshot& snapshot);

    PreviewTextureView GetSnapshotTexture(const RunResultSnapshot& snapshot,
        int maxDimension)
    {
        if (snapshot.textureKey == 0 || snapshot.resultImage.empty())
            return {};
        std::uint64_t signature =
            PreviewTextureCache::ImageSignature(snapshot.resultImage);
        signature = PreviewTextureCache::CombineSignature(
            signature, snapshot.captureSerial);
        if (PreviewTextureCache::NeedsUpdate(snapshot.textureKey,
            PreviewTextureKind::RunResult, signature))
        {
            PreviewTextureCache::Queue(snapshot.textureKey,
                PreviewTextureKind::RunResult, signature,
                snapshot.resultImage, maxDimension);
        }
        return PreviewTextureCache::Get(snapshot.textureKey,
            PreviewTextureKind::RunResult);
    }

    bool DrawGroupSummaryCard(GroupResultWindow& groupWindow, float cardHeight)
    {
        const bool isDark = g_CurrentTheme == 0;
        const RunResultSnapshot& snapshot = groupWindow.snapshot;
        bool openDetail = false;
        ImGui::PushID(static_cast<int>(groupWindow.id));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            isDark ? ImVec4(0.065f, 0.078f, 0.095f, 1.0f)
                   : ImVec4(0.975f, 0.980f, 0.985f, 1.0f));
        if (ImGui::BeginChild("##task_summary_card", ImVec2(0.0f, cardHeight),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const std::string taskName = groupWindow.groupName.empty()
                ? std::string("未分组") : groupWindow.groupName;
            if (ImGui::BeginTable("##task_card_header", 3, ImGuiTableFlags_SizingFixedFit))
            {
                constexpr float actionWidth = 56.0f;
                ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthFixed, actionWidth);
                ImGui::TableSetupColumn("##title", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed, actionWidth);
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(actionWidth, ImGui::GetFrameHeight()));
                ImGui::TableNextColumn();
                DrawCenteredTableText(taskName.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TableNextColumn();
                if (ImGui::Button("详情", ImVec2(actionWidth, 0.0f)))
                    openDetail = true;
                ImGui::EndTable();
            }

            const ImVec4 statusColor = StatusColor(snapshot.overallStatus, isDark);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
            const float bannerWidth = ImGui::GetContentRegionAvail().x;
            constexpr float bannerHeight = 72.0f;
            ImGui::InvisibleButton("##task_status", ImVec2(bannerWidth, bannerHeight));
            const ImVec2 bannerMax(bannerMin.x + bannerWidth, bannerMin.y + bannerHeight);
            drawList->AddRectFilled(bannerMin, bannerMax,
                ImGui::ColorConvertFloat4ToU32(statusColor), 6.0f);

            const char* statusText = StatusText(snapshot.overallStatus);
            const float statusFontSize = ImGui::GetFontSize() * 2.0f;
            const ImVec2 statusSize = ImGui::GetFont()->CalcTextSizeA(
                statusFontSize, FLT_MAX, 0.0f, statusText);
            drawList->AddText(ImGui::GetFont(), statusFontSize,
                ImVec2(bannerMin.x + (bannerWidth - statusSize.x) * 0.5f,
                    bannerMin.y + 8.0f),
                IM_COL32(255, 255, 255, 255), statusText);
            const char* description = StatusDescription(snapshot.overallStatus);
            const ImVec2 descriptionSize = ImGui::CalcTextSize(description);
            drawList->AddText(
                ImVec2(bannerMin.x + (bannerWidth - descriptionSize.x) * 0.5f,
                    bannerMin.y + 48.0f),
                IM_COL32(255, 255, 255, 225), description);

            char totalText[16]{};
            char passText[16]{};
            char issueText[16]{};
            char timeText[24]{};
            const int issueCount = snapshot.failCount + snapshot.errorCount + snapshot.pendingCount;
            std::snprintf(totalText, sizeof(totalText), "%zu", snapshot.rows.size());
            std::snprintf(passText, sizeof(passText), "%d", snapshot.passCount);
            std::snprintf(issueText, sizeof(issueText), "%d", issueCount);
            std::snprintf(timeText, sizeof(timeText), "%.1fms", snapshot.totalTimeMs);
            if (ImGui::BeginTable("##task_metrics", 4, ImGuiTableFlags_SizingStretchSame))
            {
                DrawCompactMetric("工具", totalText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                DrawCompactMetric("通过", passText, StatusTextColor(ToolResultStatus::Pass, isDark));
                DrawCompactMetric("异常", issueText, issueCount > 0
                    ? StatusTextColor(ToolResultStatus::Fail, isDark)
                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                DrawCompactMetric("耗时", timeText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::EndTable();
            }

            ImGui::Separator();
            // 总览卡始终显示自己的运行图像，不复用详情页的图像/结果切换状态。
            const float previewHeight = ImGui::GetContentRegionAvail().y;
            if (previewHeight > 1.0f)
                DrawResultImageThumbnail(previewHeight, snapshot);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        return openDetail;
    }

    bool DrawWindowHeader(const char* title, RunResultViewState& view,
        bool allowGroupWindows, bool& requestOpenGroups)
    {
        bool toggleMaximized = false;
        constexpr float buttonWidth = 72.0f;
        constexpr float actionGap = 6.0f;
        const float controlWidth = allowGroupWindows
            ? buttonWidth * 2.0f + actionGap : buttonWidth;
        const float headerHeight = ImGui::GetFrameHeight();

        if (ImGui::BeginTable("##run_result_header", 3, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("##header_left", ImGuiTableColumnFlags_WidthFixed, controlWidth);
            ImGui::TableSetupColumn("##header_title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##header_action", ImGuiTableColumnFlags_WidthFixed, controlWidth);

            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, headerHeight));

            ImGui::TableNextColumn();
            const ImVec2 titleMin = ImGui::GetCursorScreenPos();
            const float titleWidth = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##run_result_title", ImVec2(titleWidth, headerHeight));
            const ImVec2 titleSize = ImGui::CalcTextSize(title);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(titleMin.x + (titleWidth - titleSize.x) * 0.5f,
                    titleMin.y + (headerHeight - titleSize.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), title);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("双击最大化/还原");
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    toggleMaximized = true;
            }

            ImGui::TableNextColumn();
            if (allowGroupWindows)
            {
                if (ImGui::Button("任务总览", ImVec2(buttonWidth, 0.0f)))
                    requestOpenGroups = true;
                ImGui::SetItemTooltip("在一个总览窗口中显示所有工具分组（最多 8 个）");
                ImGui::SameLine(0.0f, actionGap);
            }
            if (ImGui::Button(view.maximized ? "还原" : "最大化", ImVec2(buttonWidth, 0.0f)))
                toggleMaximized = true;

            ImGui::EndTable();
        }
        return toggleMaximized;
    }

    ImU32 ResultOverlayColor(const ToolResult& result, std::size_t index)
    {
        if (result.status == ToolResultStatus::Fail)
            return IM_COL32(255, 70, 62, 255);
        if (result.status == ToolResultStatus::Error)
            return IM_COL32(255, 166, 45, 255);
        static constexpr ImU32 colors[] = {
            IM_COL32(35, 230, 105, 255),
            IM_COL32(35, 190, 255, 255),
            IM_COL32(255, 190, 35, 255),
            IM_COL32(210, 90, 255, 255),
            IM_COL32(25, 230, 215, 255),
        };
        return colors[index % IM_ARRAYSIZE(colors)];
    }

    bool OverlayLabelRectsOverlap(const ImVec4& left, const ImVec4& right)
    {
        return left.x < right.z && left.z > right.x &&
            left.y < right.w && left.w > right.y;
    }

    std::string TruncateOverlayLabel(const char* text, std::size_t maxBytes = 52)
    {
        std::string value = text ? text : "";
        if (value.size() <= maxBytes)
            return value;
        std::size_t cut = maxBytes;
        while (cut > 0 &&
            (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        return value.substr(0, cut) + "...";
    }

    void DrawOverlayLabel(ImDrawList* drawList, const ImVec2& anchor,
        const char* text, ImU32 color, const ImVec2& imageMin, const ImVec2& imageMax,
        std::vector<ImVec4>& occupiedLabels)
    {
        if (!text || !*text)
            return;

        const std::string displayText = TruncateOverlayLabel(text);
        const ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
        constexpr float padX = 4.0f;
        constexpr float padY = 2.0f;
        const float rowHeight = textSize.y + padY * 2.0f + 4.0f;
        ImVec2 position{};
        ImVec4 labelRect{};
        bool placed = false;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            if (attempt == 0)
                position = ImVec2(anchor.x + 2.0f,
                    anchor.y - textSize.y - padY * 2.0f - 2.0f);
            else if (attempt == 1)
                position = ImVec2(anchor.x + 2.0f, anchor.y + 3.0f);
            else
                position = ImVec2(anchor.x + 2.0f,
                    imageMin.y + 4.0f + (attempt - 2) * rowHeight);

            position.x = (std::clamp)(position.x, imageMin.x + padX + 2.0f,
                (std::max)(imageMin.x + padX + 2.0f,
                    imageMax.x - textSize.x - padX - 2.0f));
            position.y = (std::clamp)(position.y, imageMin.y + padY + 2.0f,
                (std::max)(imageMin.y + padY + 2.0f,
                    imageMax.y - textSize.y - padY - 2.0f));
            labelRect = ImVec4(
                position.x - padX, position.y - padY,
                position.x + textSize.x + padX, position.y + textSize.y + padY);

            const bool overlaps = std::any_of(occupiedLabels.begin(), occupiedLabels.end(),
                [&labelRect](const ImVec4& occupied)
                {
                    return OverlayLabelRectsOverlap(labelRect, occupied);
                });
            if (!overlaps)
            {
                placed = true;
                break;
            }
        }
        if (!placed)
            return;

        occupiedLabels.push_back(labelRect);
        const ImVec2 backgroundMin(labelRect.x, labelRect.y);
        const ImVec2 backgroundMax(labelRect.z, labelRect.w);
        drawList->AddRectFilled(backgroundMin, backgroundMax, IM_COL32(15, 18, 22, 225), 3.0f);
        drawList->AddRect(backgroundMin, backgroundMax, color, 3.0f, 0, 1.0f);
        drawList->AddText(position, IM_COL32(255, 255, 255, 255), displayText.c_str());
    }

    void DrawResultImageOverlays(const RunResultSnapshot& snapshot,
        const ImVec2& imageMin, const ImVec2& imageMax, float scale)
    {
        if (snapshot.overlayResults.empty())
            return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float thickness = (std::clamp)(scale * 2.0f, 1.5f, 4.0f);
        auto ToScreen = [&imageMin, scale](float x, float y)
        {
            return ImVec2(imageMin.x + x * scale, imageMin.y + y * scale);
        };

        int labelCount = 0;
        std::vector<ImVec4> occupiedLabels;
        drawList->PushClipRect(imageMin, imageMax, true);
        for (std::size_t resultIndex = 0;
            resultIndex < snapshot.overlayResults.size(); ++resultIndex)
        {
            const ToolResult& result = snapshot.overlayResults[resultIndex];
            if (result.skipped)
                continue;
            const ImU32 color = ResultOverlayColor(result, resultIndex);
            const bool showDetectionLabels = result.texts.empty();
            const bool showRegionLabels = result.texts.empty() && result.detections.empty();

            for (const ToolResult::Detection& detection : result.detections)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(detection.box.x), static_cast<float>(detection.box.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(detection.box.x + detection.box.width),
                    static_cast<float>(detection.box.y + detection.box.height));
                drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (showDetectionLabels && labelCount++ < 24)
                {
                    char label[192]{};
                    const char* name = detection.label.empty()
                        ? result.toolName.c_str() : detection.label.c_str();
                    if (detection.score > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f", name, detection.score);
                    else
                        std::snprintf(label, sizeof(label), "%s", name);
                    DrawOverlayLabel(drawList, boxMin, label, color,
                        imageMin, imageMax, occupiedLabels);
                }
            }

            for (const ToolResult::Region& region : result.regions)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(region.bbox.x), static_cast<float>(region.bbox.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(region.bbox.x + region.bbox.width),
                    static_cast<float>(region.bbox.y + region.bbox.height));
                if (region.bbox.width > 0 && region.bbox.height > 0)
                    drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (region.contour.size() >= 2)
                {
                    std::vector<ImVec2> contour;
                    contour.reserve(region.contour.size());
                    for (const cv::Point& point : region.contour)
                        contour.push_back(ToScreen(static_cast<float>(point.x), static_cast<float>(point.y)));
                    drawList->AddPolyline(contour.data(), static_cast<int>(contour.size()),
                        color, ImDrawFlags_Closed, thickness);
                }
                // Keep the geometry from every result type, while assigning labels
                // to the most informative type only: text, detection, then region.
                if (showRegionLabels && labelCount++ < 24)
                {
                    char label[192]{};
                    const char* name = region.label.empty()
                        ? result.toolName.c_str() : region.label.c_str();
                    if (region.score > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f", name, region.score);
                    else
                        std::snprintf(label, sizeof(label), "%s", name);
                    DrawOverlayLabel(drawList, boxMin, label, color,
                        imageMin, imageMax, occupiedLabels);
                }
            }

            for (const ToolResult::TextItem& text : result.texts)
            {
                const ImVec2 boxMin = ToScreen(
                    static_cast<float>(text.box.x), static_cast<float>(text.box.y));
                const ImVec2 boxMax = ToScreen(
                    static_cast<float>(text.box.x + text.box.width),
                    static_cast<float>(text.box.y + text.box.height));
                drawList->AddRect(boxMin, boxMax, color, 2.0f, 0, thickness);
                if (labelCount++ < 24)
                {
                    char label[256]{};
                    const char* recognized = text.text.empty()
                        ? result.toolName.c_str() : text.text.c_str();
                    if (text.confidence > 0.0f)
                        std::snprintf(label, sizeof(label), "%s  %.2f", recognized, text.confidence);
                    else
                        std::snprintf(label, sizeof(label), "%s", recognized);
                    DrawOverlayLabel(drawList, boxMin, label, color,
                        imageMin, imageMax, occupiedLabels);
                }
            }

            bool lineLabelDrawn = !result.detections.empty() ||
                !result.regions.empty() || !result.texts.empty();
            for (const ToolResult::Line& line : result.lines)
            {
                const ImVec2 p1 = ToScreen(static_cast<float>(line.p1.x), static_cast<float>(line.p1.y));
                const ImVec2 p2 = ToScreen(static_cast<float>(line.p2.x), static_cast<float>(line.p2.y));
                drawList->AddLine(p1, p2, color, thickness);
                if (!lineLabelDrawn && labelCount++ < 24)
                {
                    DrawOverlayLabel(drawList, p1, result.toolName.c_str(),
                        color, imageMin, imageMax, occupiedLabels);
                    lineLabelDrawn = true;
                }
            }
        }
        drawList->PopClipRect();
    }

    void DrawResultImageThumbnail(float height, const RunResultSnapshot& snapshot)
    {
        const PreviewTextureView texture = GetSnapshotTexture(snapshot, 1024);
        const bool imageReady = texture.ready && texture.gpuHandle.ptr != 0 &&
            !snapshot.resultImage.empty();
        const ImVec4 previewBackground = g_CurrentTheme == 0
            ? ImVec4(0.035f, 0.043f, 0.050f, 1.0f)
            : ImVec4(0.88f, 0.91f, 0.93f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, previewBackground);
        ImGui::BeginChild("##task_result_thumbnail", ImVec2(0.0f, height),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##task_result_thumbnail_canvas", canvasSize);
        const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (imageReady && canvasSize.x > 1.0f && canvasSize.y > 1.0f)
        {
            const float imageWidth = static_cast<float>(snapshot.resultImage.cols);
            const float imageHeight = static_cast<float>(snapshot.resultImage.rows);
            const float scale = (std::min)(canvasSize.x / imageWidth,
                canvasSize.y / imageHeight);
            const ImVec2 imageSize(imageWidth * scale, imageHeight * scale);
            const ImVec2 imageMin(
                canvasMin.x + (canvasSize.x - imageSize.x) * 0.5f,
                canvasMin.y + (canvasSize.y - imageSize.y) * 0.5f);
            const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

            drawList->PushClipRect(canvasMin, canvasMax, true);
            drawList->AddImage((ImTextureID)texture.gpuHandle.ptr, imageMin, imageMax);
            drawList->AddRect(imageMin, imageMax,
                ImGui::ColorConvertFloat4ToU32(
                    g_CurrentTheme == 0 ? ImVec4(0.30f, 0.34f, 0.38f, 1.0f)
                                        : ImVec4(0.58f, 0.64f, 0.68f, 1.0f)),
                3.0f, 0, 1.0f);
            DrawResultImageOverlays(snapshot, imageMin, imageMax, scale);
            drawList->PopClipRect();
        }
        else
        {
            const char* emptyText = "暂无可显示的运行图像";
            const ImVec2 textSize = ImGui::CalcTextSize(emptyText);
            drawList->AddText(
                ImVec2(canvasMin.x + (canvasSize.x - textSize.x) * 0.5f,
                    canvasMin.y + (canvasSize.y - textSize.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)),
                emptyText);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawResultImagePreview(float height, const RunResultSnapshot& snapshot,
        RunResultViewState& view)
    {
        const PreviewTextureView texture = GetSnapshotTexture(snapshot, 2048);
        const int sourceWidth = snapshot.resultImage.cols;
        const int sourceHeight = snapshot.resultImage.rows;
        const bool imageReady = texture.ready && texture.gpuHandle.ptr != 0 &&
            sourceWidth > 0 && sourceHeight > 0;
        const ImVec4 previewBackground = g_CurrentTheme == 0
            ? ImVec4(0.055f, 0.065f, 0.075f, 1.0f)
            : ImVec4(0.88f, 0.91f, 0.93f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, previewBackground);
        ImGui::BeginChild("##run_result_image_preview", ImVec2(0.0f, height),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (imageReady && (view.imageWidth != sourceWidth ||
            view.imageHeight != sourceHeight))
        {
            view.imageWidth = sourceWidth;
            view.imageHeight = sourceHeight;
            view.imageZoom = 1.0f;
            view.imagePan = ImVec2(0.0f, 0.0f);
        }

        if (imageReady)
        {
            const float oldZoom = view.imageZoom;
            if (ImGui::SmallButton("-##result_image_zoom_out"))
                view.imageZoom = (std::max)(0.25f, view.imageZoom / 1.25f);
            ImGui::SameLine();
            if (ImGui::SmallButton("适应##result_image_fit"))
            {
                view.imageZoom = 1.0f;
                view.imagePan = ImVec2(0.0f, 0.0f);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+##result_image_zoom_in"))
                view.imageZoom = (std::min)(8.0f, view.imageZoom * 1.25f);
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f%%  ·  滚轮缩放 / 左键拖动 / 双击适应",
                view.imageZoom * 100.0f);

            // Button zooming keeps the current visual center stable.
            if (oldZoom != view.imageZoom && view.imageZoom == 1.0f)
                view.imagePan = ImVec2(0.0f, 0.0f);
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize(
            (std::max)(1.0f, available.x),
            (std::max)(1.0f, available.y));
        const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
        ImGui::InvisibleButton("##run_result_image_canvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft);
        const bool canvasHovered = ImGui::IsItemHovered();
        const bool canvasActive = ImGui::IsItemActive();

        if (imageReady)
        {
            constexpr float innerPadding = 12.0f;
            const float maxWidth = (std::max)(1.0f, canvasSize.x - innerPadding * 2.0f);
            const float maxHeight = (std::max)(1.0f, canvasSize.y - innerPadding * 2.0f);
            const float fitScale = (std::min)(
                maxWidth / static_cast<float>(sourceWidth),
                maxHeight / static_cast<float>(sourceHeight));
            float scale = fitScale * view.imageZoom;
            const ImVec2 imageSize(
                sourceWidth * scale,
                sourceHeight * scale);

            ImVec2 imageMin(
                canvasMin.x + (canvasSize.x - imageSize.x) * 0.5f + view.imagePan.x,
                canvasMin.y + (canvasSize.y - imageSize.y) * 0.5f + view.imagePan.y);

            if (canvasHovered)
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const ImVec2 imagePoint(
                        (mouse.x - imageMin.x) / scale,
                        (mouse.y - imageMin.y) / scale);
                    const float nextZoom = (std::clamp)(
                        view.imageZoom * std::pow(1.15f, wheel), 0.25f, 8.0f);
                    if (nextZoom != view.imageZoom)
                    {
                        view.imageZoom = nextZoom;
                        scale = fitScale * view.imageZoom;
                        const ImVec2 nextImageSize(
                            sourceWidth * scale,
                            sourceHeight * scale);
                        const ImVec2 centeredMin(
                            canvasMin.x + (canvasSize.x - nextImageSize.x) * 0.5f,
                            canvasMin.y + (canvasSize.y - nextImageSize.y) * 0.5f);
                        view.imagePan = ImVec2(
                            mouse.x - centeredMin.x - imagePoint.x * scale,
                            mouse.y - centeredMin.y - imagePoint.y * scale);
                    }
                }

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    view.imageZoom = 1.0f;
                    view.imagePan = ImVec2(0.0f, 0.0f);
                }
            }
            if (canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                view.imagePan.x += delta.x;
                view.imagePan.y += delta.y;
            }
            if (canvasHovered || canvasActive)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            scale = fitScale * view.imageZoom;
            const ImVec2 finalImageSize(
                sourceWidth * scale,
                sourceHeight * scale);
            // Keep a grab handle visible even after an aggressive drag.
            constexpr float minimumVisible = 36.0f;
            const float maxPanX = (std::max)(0.0f,
                (canvasSize.x + finalImageSize.x) * 0.5f - minimumVisible);
            const float maxPanY = (std::max)(0.0f,
                (canvasSize.y + finalImageSize.y) * 0.5f - minimumVisible);
            view.imagePan.x = (std::clamp)(view.imagePan.x, -maxPanX, maxPanX);
            view.imagePan.y = (std::clamp)(view.imagePan.y, -maxPanY, maxPanY);
            imageMin = ImVec2(
                canvasMin.x + (canvasSize.x - finalImageSize.x) * 0.5f + view.imagePan.x,
                canvasMin.y + (canvasSize.y - finalImageSize.y) * 0.5f + view.imagePan.y);
            const ImVec2 imageMax(
                imageMin.x + finalImageSize.x,
                imageMin.y + finalImageSize.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(canvasMin, canvasMax, true);
            drawList->AddImage((ImTextureID)texture.gpuHandle.ptr, imageMin, imageMax);
            drawList->AddRect(imageMin, imageMax,
                ImGui::ColorConvertFloat4ToU32(
                    g_CurrentTheme == 0 ? ImVec4(0.30f, 0.34f, 0.38f, 1.0f)
                                        : ImVec4(0.58f, 0.64f, 0.68f, 1.0f)),
                4.0f, 0, 1.0f);
            DrawResultImageOverlays(snapshot, imageMin, imageMax, scale);
            drawList->PopClipRect();
        }
        else
        {
            const char* emptyText = "暂无可显示的运行图像";
            const ImVec2 textSize = ImGui::CalcTextSize(emptyText);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(canvasMin.x + (canvasSize.x - textSize.x) * 0.5f,
                    canvasMin.y + (canvasSize.y - textSize.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)),
                emptyText);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    bool DrawExpandedGroupContent(GroupResultWindow& groupWindow)
    {
        RunResultSnapshot& snapshot = groupWindow.snapshot;
        RunResultViewState& view = groupWindow.view;
        const bool isDark = g_CurrentTheme == 0;
        ImGui::PushID(static_cast<int>(groupWindow.id));

        const ImVec4 statusColor = StatusColor(snapshot.overallStatus, isDark);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
        const float bannerWidth = ImGui::GetContentRegionAvail().x;
        constexpr float bannerHeight = 104.0f;
        ImGui::InvisibleButton("##expanded_status", ImVec2(bannerWidth, bannerHeight));
        const ImVec2 bannerMax(bannerMin.x + bannerWidth, bannerMin.y + bannerHeight);
        drawList->AddRectFilled(bannerMin, bannerMax,
            ImGui::ColorConvertFloat4ToU32(statusColor), 7.0f);

        const char* statusText = StatusText(snapshot.overallStatus);
        const float statusFontSize = ImGui::GetFontSize() * 2.6f;
        const ImVec2 statusSize = ImGui::GetFont()->CalcTextSizeA(
            statusFontSize, FLT_MAX, 0.0f, statusText);
        drawList->AddText(ImGui::GetFont(), statusFontSize,
            ImVec2(bannerMin.x + (bannerWidth - statusSize.x) * 0.5f,
                bannerMin.y + 13.0f),
            IM_COL32(255, 255, 255, 255), statusText);
        const char* description = StatusDescription(snapshot.overallStatus);
        const ImVec2 descriptionSize = ImGui::CalcTextSize(description);
        drawList->AddText(
            ImVec2(bannerMin.x + (bannerWidth - descriptionSize.x) * 0.5f,
                bannerMin.y + 72.0f),
            IM_COL32(255, 255, 255, 225), description);

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        std::string recipeText = "配方  " +
            (snapshot.recipeName.empty() ? std::string("未命名配方") : snapshot.recipeName);
        if (snapshot.loopRound)
        {
            char loopText[48]{};
            std::snprintf(loopText, sizeof(loopText), "  ·  循环第 %llu 轮",
                static_cast<unsigned long long>(snapshot.loopIteration));
            recipeText += loopText;
        }
        DrawCenteredText(recipeText.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));

        char totalText[24]{};
        char passText[24]{};
        char issueText[24]{};
        char timeText[32]{};
        const int issueCount = snapshot.failCount + snapshot.errorCount + snapshot.pendingCount;
        std::snprintf(totalText, sizeof(totalText), "%zu", snapshot.rows.size());
        std::snprintf(passText, sizeof(passText), "%d", snapshot.passCount);
        std::snprintf(issueText, sizeof(issueText), "%d", issueCount);
        std::snprintf(timeText, sizeof(timeText), "%.1f ms", snapshot.totalTimeMs);
        if (ImGui::BeginTable("##expanded_metrics", 4, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            DrawMetricCard("##expanded_total", "执行工具", totalText,
                ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TableNextColumn();
            DrawMetricCard("##expanded_pass", "通过", passText,
                StatusTextColor(ToolResultStatus::Pass, isDark));
            ImGui::TableNextColumn();
            DrawMetricCard("##expanded_issue", "不合格/异常", issueText,
                issueCount > 0 ? StatusTextColor(ToolResultStatus::Fail, isDark)
                               : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TableNextColumn();
            DrawMetricCard("##expanded_time", "总耗时", timeText,
                ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::EndTable();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::SeparatorText(view.showImagePreview ? "结果图像" : "工具结果");
        ImGui::PopStyleVar();
        const float buttonAreaHeight = ImGui::GetFrameHeight() +
            ImGui::GetStyle().ItemSpacing.y * 2.0f;
        const float contentHeight = (std::max)(150.0f,
            ImGui::GetContentRegionAvail().y - buttonAreaHeight);
        if (view.showImagePreview)
        {
            DrawResultImagePreview(contentHeight, snapshot, view);
        }
        else if (ImGui::BeginTable("##expanded_rows", 5,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, contentHeight)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("序号", ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableSetupColumn("工具", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("结果", ImGuiTableColumnFlags_WidthFixed, 82.0f);
            ImGui::TableSetupColumn("耗时", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("摘要", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            const char* headers[] = { "序号", "工具", "结果", "耗时", "摘要" };
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ImGui::GetFrameHeight());
            for (int column = 0; column < IM_ARRAYSIZE(headers); ++column)
            {
                ImGui::TableSetColumnIndex(column);
                DrawCenteredTableText(headers[column], ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            for (const RunResultRow& row : snapshot.rows)
            {
                ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                ImGui::TableNextColumn();
                char indexText[16]{};
                std::snprintf(indexText, sizeof(indexText), "%d", row.index);
                DrawCenteredTableText(indexText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TableNextColumn();
                DrawCenteredTableText(row.name.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TableNextColumn();
                const char* resultText = !row.executed ? "未执行"
                    : (row.skipped ? "跳过" : StatusText(row.status));
                const ImVec4 resultColor = (!row.executed || row.skipped)
                    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                    : StatusTextColor(row.status, isDark);
                DrawCenteredTableText(resultText, resultColor);
                ImGui::TableNextColumn();
                char rowTimeText[32]{};
                std::snprintf(rowTimeText, sizeof(rowTimeText), "%.2f ms", row.timeMs);
                DrawCenteredTableText(rowTimeText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ImGui::TableNextColumn();
                DrawCenteredTableText(row.summary.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            ImGui::EndTable();
        }

        constexpr float gap = 8.0f;
        const float actionWidth = (std::max)(0.0f,
            (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f);
        const bool loopEnabled = ToolController::IsLoopEnabled();
        const bool runInProgress = !loopEnabled &&
            ToolController::GetMode() != ToolController::Mode::Idle;
        const bool canRunAgain = !ToolChainState::Empty() && ImageState::HasImage();
        const char* runText = loopEnabled ? "停止循环"
            : (runInProgress ? "正在运行..." : "再次运行当前任务");
        ImGui::BeginDisabled(!loopEnabled && (!canRunAgain || runInProgress));
        if (ImGui::Button(runText, ImVec2(actionWidth, 0.0f)))
        {
            if (loopEnabled)
                ToolController::SetLoopEnabled(false);
            else
                ToolController::RequestRunTaskGroup(groupWindow.groupName, false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, gap);
        if (ImGui::Button(view.showImagePreview ? "查看结果" : "查看图像",
            ImVec2(actionWidth, 0.0f)))
        {
            view.showImagePreview = !view.showImagePreview;
            if (view.showImagePreview)
            {
                view.imageZoom = 1.0f;
                view.imagePan = ImVec2(0.0f, 0.0f);
            }
        }
        ImGui::SameLine(0.0f, gap);
        const bool returnToOverview = ImGui::Button("返回总览",
            ImVec2(actionWidth, 0.0f));
        ImGui::PopID();
        return returnToOverview;
    }

    bool DrawGroupDashboard()
    {
        if (!g_groupDashboardView.visible)
            return false;

        const std::vector<std::string> currentGroups = CollectTaskGroups();
        GroupResultWindow* expandedGroup = nullptr;
        if (g_expandedGroupId != 0)
        {
            auto expandedIt = std::find_if(g_groupWindows.begin(), g_groupWindows.end(),
                [](const GroupResultWindow& window)
                {
                    return window.id == g_expandedGroupId;
                });
            if (expandedIt != g_groupWindows.end() && expandedIt->snapshot.valid &&
                std::find(currentGroups.begin(), currentGroups.end(), expandedIt->groupName) !=
                    currentGroups.end())
            {
                expandedGroup = &*expandedIt;
            }
            else
            {
                g_expandedGroupId = 0;
            }
        }
        const std::string dashboardTitle = expandedGroup
            ? "任务详情 · " + (expandedGroup->groupName.empty()
                ? std::string("未分组") : expandedGroup->groupName)
            : std::string("任务结果总览");

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 normalSize(
            (std::clamp)(viewport->WorkSize.x * 0.96f, 900.0f, 1760.0f),
            (std::clamp)(viewport->WorkSize.y * 0.92f, 620.0f, 1040.0f));
        if (g_groupDashboardView.maximized)
        {
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        }
        else if (g_groupDashboardView.restorePlacement)
        {
            ImGui::SetNextWindowPos(g_groupDashboardView.restorePosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(g_groupDashboardView.restoreSize, ImGuiCond_Always);
            g_groupDashboardView.restorePlacement = false;
        }
        else
        {
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                    viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(normalSize, ImGuiCond_Appearing);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
            g_groupDashboardView.maximized ? 0.0f : 7.0f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        if (g_groupDashboardView.maximized)
            flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        if (!ImGui::Begin("任务结果总览###run_result_group_dashboard", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return false;
        }

        bool unusedOpenGroups = false;
        if (DrawWindowHeader(dashboardTitle.c_str(), g_groupDashboardView,
            false, unusedOpenGroups))
        {
            if (g_groupDashboardView.maximized)
            {
                g_groupDashboardView.maximized = false;
                g_groupDashboardView.restorePlacement = true;
            }
            else
            {
                g_groupDashboardView.restorePosition = ImGui::GetWindowPos();
                g_groupDashboardView.restoreSize = ImGui::GetWindowSize();
                g_groupDashboardView.maximized = true;
            }
        }
        ImGui::Separator();

        if (expandedGroup)
        {
            if (DrawExpandedGroupContent(*expandedGroup))
                g_expandedGroupId = 0;
            ImGui::End();
            ImGui::PopStyleVar(2);
            return false;
        }

        std::vector<GroupResultWindow*> visibleGroups;
        visibleGroups.reserve(currentGroups.size());
        for (const std::string& groupName : currentGroups)
        {
            auto groupWindow = std::find_if(g_groupWindows.begin(), g_groupWindows.end(),
                [&groupName](const GroupResultWindow& window)
                {
                    return window.groupName == groupName;
                });
            if (groupWindow != g_groupWindows.end() && groupWindow->snapshot.valid)
                visibleGroups.push_back(&*groupWindow);
        }

        const float footerHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        const float gridHeight = (std::max)(220.0f,
            ImGui::GetContentRegionAvail().y - footerHeight);
        if (ImGui::BeginChild("##task_dashboard_grid", ImVec2(0.0f, gridHeight),
            ImGuiChildFlags_None))
        {
            if (visibleGroups.empty())
            {
                DrawCenteredText("当前配方没有可显示的任务分组",
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            else
            {
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const int columns = visibleGroups.size() == 1 ? 1
                    : (availableWidth >= 1080.0f ? 4 : 2);
                // 卡片保持足够高度留给图像；任务较多时由外层区域纵向滚动。
                // 不再为了把所有任务塞进一屏而压缩卡片内容。
                constexpr float cardHeight = 360.0f;
                if (ImGui::BeginTable("##task_dashboard_table", columns,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
                {
                    for (GroupResultWindow* groupWindow : visibleGroups)
                    {
                        ImGui::TableNextColumn();
                        if (DrawGroupSummaryCard(*groupWindow, cardHeight - 4.0f))
                            g_expandedGroupId = groupWindow->id;
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::EndChild();

        const float gap = 8.0f;
        const float actionWidth = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const bool loopEnabled = ToolController::IsLoopEnabled();
        const bool runInProgress = !loopEnabled &&
            ToolController::GetMode() != ToolController::Mode::Idle;
        const bool canRunAgain = !ToolChainState::Empty() && ImageState::HasImage();
        const char* runText = loopEnabled ? "停止循环"
            : (runInProgress ? "正在运行..." : "再次运行全部");
        ImGui::BeginDisabled(!loopEnabled && (!canRunAgain || runInProgress));
        if (ImGui::Button(runText, ImVec2(actionWidth, 0.0f)))
        {
            if (loopEnabled)
                ToolController::SetLoopEnabled(false);
            else
                ToolController::RequestRunAll(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, gap);
        bool returnToEditor = false;
        if (ImGui::Button("返回编辑", ImVec2(actionWidth, 0.0f)))
        {
            g_groupDashboardView.visible = false;
            ToolController::SetRuntimeMode(false);
            returnToEditor = true;
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        return returnToEditor;
    }
}

bool HasRunResultSnapshot()
{
    return g_snapshot.valid;
}

bool IsRunResultWindowVisible()
{
    return g_mainView.visible || g_groupDashboardView.visible ||
        std::any_of(g_groupWindows.begin(), g_groupWindows.end(),
            [](const GroupResultWindow& window) { return window.view.visible; });
}

void SetRunResultWindowVisible(bool visible)
{
    if (visible && !g_snapshot.valid)
        return;
    g_mainView.visible = visible;
    if (visible)
    {
        g_expandedGroupId = 0;
        g_groupDashboardView.visible = false;
        for (GroupResultWindow& groupWindow : g_groupWindows)
            groupWindow.view.visible = false;
    }
    else
    {
        g_expandedGroupId = 0;
        g_groupDashboardView.visible = false;
        for (GroupResultWindow& groupWindow : g_groupWindows)
            groupWindow.view.visible = false;
    }
}

bool IsRunResultAutoShowEnabled()
{
    LoadAutoShowPreference();
    return g_autoShow;
}

void SetRunResultAutoShowEnabled(bool enabled)
{
    LoadAutoShowPreference();
    g_autoShow = enabled;
    SaveAutoShowPreference();
    if (!g_autoShow)
    {
        g_expandedGroupId = 0;
        g_mainView.visible = false;
        g_groupDashboardView.visible = false;
        for (GroupResultWindow& groupWindow : g_groupWindows)
            groupWindow.view.visible = false;
    }
}

void ShowRunResultWindow()
{
    LoadAutoShowPreference();
    const std::uint64_t completedSerial = ToolController::GetCompletedBatchSerial();
    if (completedSerial != g_seenBatchSerial)
    {
        g_seenBatchSerial = completedSerial;
        CaptureSnapshots();
        const bool groupModeVisible = g_groupDashboardView.visible ||
            std::any_of(g_groupWindows.begin(), g_groupWindows.end(),
                [](const GroupResultWindow& window) { return window.view.visible; });
        if (g_autoShow)
        {
            const bool completedAllTasks = !ToolController::WasLastRunTaskGroup();
            if (completedAllTasks && !CollectTaskGroups().empty())
            {
                // 分组配方执行全部后直接进入任务总览，省去中间汇总窗口。
                OpenGroupResultWindows();
            }
            else if (!groupModeVisible)
            {
                g_mainView.visible = true;
            }
        }
    }

    bool requestOpenGroups = false;
    auto drawWindow = [&requestOpenGroups](RunResultSnapshot& snapshot,
        RunResultViewState& view, const char* windowId, const char* title,
        bool mainWindow, int cascadeIndex)
    {
        if (!view.visible || !snapshot.valid)
            return false;

        const bool isDark = g_CurrentTheme == 0;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float width = mainWindow
            ? (std::clamp)(viewport->WorkSize.x * 0.64f, 680.0f, 920.0f)
            : (std::clamp)(viewport->WorkSize.x * 0.46f, 500.0f, 760.0f);
        const float height = mainWindow
            ? (std::clamp)(viewport->WorkSize.y * 0.76f, 520.0f, 700.0f)
            : (std::clamp)(viewport->WorkSize.y * 0.64f, 440.0f, 620.0f);
        if (view.maximized)
        {
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        }
        else
        {
            if (view.restorePlacement)
            {
                ImGui::SetNextWindowPos(view.restorePosition, ImGuiCond_Always);
                ImGui::SetNextWindowSize(view.restoreSize, ImGuiCond_Always);
                view.restorePlacement = false;
            }
            else
            {
                if (mainWindow)
                {
                    ImGui::SetNextWindowPos(
                        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                }
                else
                {
                    constexpr float margin = 16.0f;
                    const int taskIndex = (std::max)(0, cascadeIndex - 1);
                    const int columns = (std::max)(1, static_cast<int>(
                        (viewport->WorkSize.x - margin) / (width + margin)));
                    const int column = taskIndex % columns;
                    const int row = taskIndex / columns;
                    ImGui::SetNextWindowPos(
                        ImVec2(viewport->WorkPos.x + margin + column * (width + margin),
                            viewport->WorkPos.y + margin + row * 34.0f),
                        ImGuiCond_Appearing);
                }
                ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, view.maximized ? 0.0f : 7.0f);
        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        if (view.maximized)
            windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        if (!ImGui::Begin(windowId, nullptr, windowFlags))
        {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return false;
        }

        if (DrawWindowHeader(title, view, mainWindow, requestOpenGroups))
        {
            if (view.maximized)
            {
                view.maximized = false;
                view.restorePlacement = true;
            }
            else
            {
                view.restorePosition = ImGui::GetWindowPos();
                view.restoreSize = ImGui::GetWindowSize();
                view.maximized = true;
            }
        }
        ImGui::Separator();

    const ImVec4 statusColor = StatusColor(snapshot.overallStatus, isDark);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
    const float bannerWidth = ImGui::GetContentRegionAvail().x;
    const float bannerHeight = 112.0f;
    ImGui::InvisibleButton("##run_result_banner", ImVec2(bannerWidth, bannerHeight));
    const ImVec2 bannerMax(bannerMin.x + bannerWidth, bannerMin.y + bannerHeight);
    drawList->AddRectFilled(bannerMin, bannerMax, ImGui::ColorConvertFloat4ToU32(statusColor), 7.0f);

    const char* statusText = StatusText(snapshot.overallStatus);
    const float statusFontSize = ImGui::GetFontSize() * 2.8f;
    const ImVec2 statusSize = ImGui::GetFont()->CalcTextSizeA(statusFontSize, FLT_MAX, 0.0f, statusText);
    drawList->AddText(ImGui::GetFont(), statusFontSize,
        ImVec2(bannerMin.x + (bannerWidth - statusSize.x) * 0.5f, bannerMin.y + 15.0f),
        IM_COL32(255, 255, 255, 255), statusText);

    const char* statusDescription = StatusDescription(snapshot.overallStatus);
    const ImVec2 descriptionSize = ImGui::CalcTextSize(statusDescription);
    drawList->AddText(
        ImVec2(bannerMin.x + (bannerWidth - descriptionSize.x) * 0.5f, bannerMin.y + 78.0f),
        IM_COL32(255, 255, 255, 225), statusDescription);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    std::string recipeText = "配方  " +
        (snapshot.recipeName.empty() ? std::string("未命名配方") : snapshot.recipeName);
    if (snapshot.loopRound)
    {
        char loopText[48]{};
        std::snprintf(loopText, sizeof(loopText), "  ·  循环第 %llu 轮",
            static_cast<unsigned long long>(snapshot.loopIteration));
        recipeText += loopText;
    }
    DrawCenteredText(recipeText.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));

    char passText[32]{};
    char failText[32]{};
    char timeText[32]{};
    char totalText[32]{};
    std::snprintf(passText, sizeof(passText), "%d", snapshot.passCount);
    const int issueCount = snapshot.failCount + snapshot.errorCount + snapshot.pendingCount;
    std::snprintf(failText, sizeof(failText), "%d", issueCount);
    std::snprintf(timeText, sizeof(timeText), "%.1f ms", snapshot.totalTimeMs);
    std::snprintf(totalText, sizeof(totalText), "%zu", snapshot.rows.size());

    if (ImGui::BeginTable("##run_result_metrics", 4, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        DrawMetricCard("##metric_total", "执行工具", totalText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::TableNextColumn();
        DrawMetricCard("##metric_pass", "通过", passText, StatusTextColor(ToolResultStatus::Pass, isDark));
        ImGui::TableNextColumn();
        DrawMetricCard("##metric_fail", "不合格/异常", failText,
            issueCount > 0
                ? StatusTextColor(ToolResultStatus::Fail, isDark)
                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TableNextColumn();
        DrawMetricCard("##metric_time", "总耗时", timeText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::EndTable();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f, 0.5f));
    ImGui::SeparatorText(view.showImagePreview ? "结果图像" : "工具结果");
    ImGui::PopStyleVar();
    const float buttonAreaHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float contentHeight = (std::max)(150.0f, ImGui::GetContentRegionAvail().y - buttonAreaHeight);
    if (view.showImagePreview)
    {
        DrawResultImagePreview(contentHeight, snapshot, view);
    }
    else if (ImGui::BeginTable("##run_result_rows", 5,
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, contentHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("序号", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("工具", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("结果", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("耗时", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("摘要", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        const char* headers[] = { "序号", "工具", "结果", "耗时", "摘要" };
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ImGui::GetFrameHeight());
        for (int column = 0; column < IM_ARRAYSIZE(headers); ++column)
        {
            ImGui::TableSetColumnIndex(column);
            DrawCenteredTableText(headers[column], ImGui::GetStyleColorVec4(ImGuiCol_Text));
        }

        for (const RunResultRow& row : snapshot.rows)
        {
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            char indexText[16]{};
            std::snprintf(indexText, sizeof(indexText), "%d", row.index);
            DrawCenteredTableText(indexText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TableNextColumn();
            DrawCenteredTableText(row.name.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TableNextColumn();
            if (!row.executed)
                DrawCenteredTableText("未执行", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            else if (row.skipped)
                DrawCenteredTableText("跳过", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            else
                DrawCenteredTableText(StatusText(row.status), StatusTextColor(row.status, isDark));
            ImGui::TableNextColumn();
            char rowTimeText[32]{};
            std::snprintf(rowTimeText, sizeof(rowTimeText), "%.2f ms", row.timeMs);
            DrawCenteredTableText(rowTimeText, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TableNextColumn();
            DrawCenteredTableText(row.summary.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        }
        ImGui::EndTable();
    }

    const float gap = 8.0f;
    const float actionWidth = (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f);
    const bool loopEnabled = ToolController::IsLoopEnabled();
    const bool runInProgress = !loopEnabled &&
        ToolController::GetMode() != ToolController::Mode::Idle;
    const bool canRunAgain = !ToolChainState::Empty() && ImageState::HasImage();
    const char* primaryActionText = loopEnabled
        ? "停止循环"
        : (runInProgress ? "正在运行..."
            : (ToolController::WasLastRunTaskGroup()
                ? "再次运行当前任务" : "再次运行全部"));
    ImGui::BeginDisabled(!loopEnabled && (!canRunAgain || runInProgress));
    if (ImGui::Button(primaryActionText, ImVec2(actionWidth, 0.0f)))
    {
        if (loopEnabled)
            ToolController::SetLoopEnabled(false);
        else
            ToolController::RequestRepeatLastRun(false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button(view.showImagePreview ? "查看结果" : "查看图像",
        ImVec2(actionWidth, 0.0f)))
    {
        view.showImagePreview = !view.showImagePreview;
        if (view.showImagePreview)
        {
            view.imageZoom = 1.0f;
            view.imagePan = ImVec2(0.0f, 0.0f);
        }
    }
    ImGui::SameLine(0.0f, gap);
    bool returnToEditor = false;
    if (ImGui::Button(mainWindow ? "返回编辑" : "返回总览",
        ImVec2(actionWidth, 0.0f)))
    {
        view.visible = false;
        if (mainWindow)
        {
            ToolController::SetRuntimeMode(false);
            returnToEditor = true;
        }
        else
        {
            g_groupDashboardView.visible = true;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    return returnToEditor;
    };

    const bool returnToEditor = drawWindow(g_snapshot, g_mainView,
        "运行结果###run_result_main", "运行结果", true, 0);

    if (requestOpenGroups)
        OpenGroupResultWindows();

    const bool dashboardReturnToEditor = DrawGroupDashboard();

    if (returnToEditor || dashboardReturnToEditor)
    {
        g_expandedGroupId = 0;
        g_groupDashboardView.visible = false;
        for (GroupResultWindow& groupWindow : g_groupWindows)
            groupWindow.view.visible = false;
    }
}
}
