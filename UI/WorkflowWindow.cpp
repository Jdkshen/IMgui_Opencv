#include "WorkflowWindow.h"

#include "../Core/ToolChainState.h"
#include "../Core/ToolChainValidator.h"
#include "../Core/ToolTypes.h"
#include "../include/imgui/imgui.h"
#include "../include/imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace UI::WorkflowWindow
{
namespace
{
bool s_open = false;
bool s_requestFocus = false;
bool s_requestDock = true;

ImGuiID ImagePreviewDockId()
{
    if (ImGuiWindow* window = ImGui::FindWindowByName("图像预览"))
        return window->DockId;
    return 0;
}

void DrawGraph(const std::vector<int>& visibleToolIndices)
{
    constexpr float nodeWidth = 170.0f;
    constexpr float nodeHeight = 78.0f;
    constexpr float horizontalGap = 18.0f;
    constexpr float verticalGap = 44.0f;
    constexpr float sidePadding = 14.0f;
    constexpr float topPadding = 50.0f;
    constexpr float bottomPadding = 12.0f;

    const float availableWidth = (std::max)(1.0f,
        ImGui::GetContentRegionAvail().x);
    const float usableWidth = (std::max)(nodeWidth,
        availableWidth - sidePadding * 2.0f);
    const int columns = (std::max)(1, static_cast<int>(
        (usableWidth + horizontalGap) / (nodeWidth + horizontalGap)));
    const int rows = (static_cast<int>(visibleToolIndices.size()) +
        columns - 1) / columns;
    const float gridWidth = columns * nodeWidth +
        (columns - 1) * horizontalGap;
    const float canvasWidth = (std::max)(availableWidth,
        sidePadding * 2.0f + gridWidth);
    const float canvasHeight = topPadding + rows * nodeHeight +
        (std::max)(0, rows - 1) * verticalGap + bottomPadding;

    ImGui::BeginChild("##standalone_tool_workflow_graph", ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const auto nodePosition = [&](int ordinal)
    {
        const int row = ordinal / columns;
        const int column = ordinal % columns;
        return ImVec2(
            origin.x + sidePadding + column * (nodeWidth + horizontalGap),
            origin.y + topPadding + row * (nodeHeight + verticalGap));
    };
    const auto visibleOrdinal = [&](int toolIndex)
    {
        const auto found = std::find(visibleToolIndices.begin(),
            visibleToolIndices.end(), toolIndex);
        return found == visibleToolIndices.end() ? -1 :
            static_cast<int>(found - visibleToolIndices.begin());
    };

    for (int ordinal = 1;
         ordinal < static_cast<int>(visibleToolIndices.size()); ++ordinal)
    {
        const ImVec2 previous = nodePosition(ordinal - 1);
        const ImVec2 current = nodePosition(ordinal);
        if (ordinal / columns == (ordinal - 1) / columns)
        {
            const ImVec2 from(previous.x + nodeWidth,
                previous.y + nodeHeight * 0.5f);
            const ImVec2 to(current.x, current.y + nodeHeight * 0.5f);
            drawList->AddLine(from, to, IM_COL32(105, 125, 140, 180), 2.0f);
            drawList->AddTriangleFilled(to,
                ImVec2(to.x - 7.0f, to.y - 4.0f),
                ImVec2(to.x - 7.0f, to.y + 4.0f),
                IM_COL32(105, 125, 140, 220));
        }
        else
        {
            const ImVec2 from(previous.x + nodeWidth * 0.5f,
                previous.y + nodeHeight);
            const ImVec2 to(current.x + nodeWidth * 0.5f, current.y);
            const float middleY = (from.y + to.y) * 0.5f;
            drawList->AddLine(from, ImVec2(from.x, middleY),
                IM_COL32(105, 125, 140, 180), 2.0f);
            drawList->AddLine(ImVec2(from.x, middleY), ImVec2(to.x, middleY),
                IM_COL32(105, 125, 140, 180), 2.0f);
            drawList->AddLine(ImVec2(to.x, middleY), to,
                IM_COL32(105, 125, 140, 180), 2.0f);
            drawList->AddTriangleFilled(to,
                ImVec2(to.x - 4.0f, to.y - 7.0f),
                ImVec2(to.x + 4.0f, to.y - 7.0f),
                IM_COL32(105, 125, 140, 220));
        }
    }

    const std::vector<ToolChainDependency> dependencies =
        ToolChainValidator::DescribeDependencies(ToolChainState::ReadOnlyTools());
    for (const ToolChainDependency& dependency : dependencies)
    {
        if (!dependency.valid)
            continue;
        const int sourceOrdinal = visibleOrdinal(dependency.sourceIndex);
        const int consumerOrdinal = visibleOrdinal(dependency.consumerIndex);
        if (sourceOrdinal < 0 || consumerOrdinal < 0)
            continue;
        const ImVec2 source = nodePosition(sourceOrdinal);
        const ImVec2 consumer = nodePosition(consumerOrdinal);
        const ImU32 color = dependency.kind == ToolDependencyKind::Fixture
            ? IM_COL32(255, 170, 55, 230) : IM_COL32(170, 100, 255, 230);
        if (sourceOrdinal / columns == consumerOrdinal / columns)
        {
            const ImVec2 from(source.x + nodeWidth * 0.5f, source.y);
            const ImVec2 to(consumer.x + nodeWidth * 0.5f, consumer.y);
            const float arch = 22.0f +
                std::abs(consumerOrdinal - sourceOrdinal) * 7.0f;
            drawList->AddBezierCubic(from,
                ImVec2(from.x, from.y - arch),
                ImVec2(to.x, to.y - arch), to, color, 2.0f);
        }
        else
        {
            const ImVec2 from(source.x + nodeWidth * 0.5f,
                source.y + nodeHeight);
            const ImVec2 to(consumer.x + nodeWidth * 0.5f, consumer.y);
            const float offset = (std::max)(26.0f,
                (to.y - from.y) * 0.42f);
            drawList->AddBezierCubic(from,
                ImVec2(from.x, from.y + offset),
                ImVec2(to.x, to.y - offset), to, color, 2.0f);
        }
    }

    for (int ordinal = 0;
         ordinal < static_cast<int>(visibleToolIndices.size()); ++ordinal)
    {
        const int toolIndex = visibleToolIndices[ordinal];
        const ToolInstance* tool = ToolChainState::AtReadOnly(toolIndex);
        if (!tool)
            continue;
        const ImVec2 position = nodePosition(ordinal);
        ImU32 fill = tool->enabled ? IM_COL32(35, 68, 78, 245) :
            IM_COL32(65, 65, 65, 220);
        if (tool->hasLastResult)
        {
            if (tool->lastResult.status == ToolResultStatus::Pass)
                fill = IM_COL32(35, 105, 70, 245);
            else if (tool->lastResult.status == ToolResultStatus::Fail)
                fill = IM_COL32(125, 55, 45, 245);
            else if (tool->lastResult.status == ToolResultStatus::Error)
                fill = IM_COL32(130, 75, 30, 245);
        }
        const ImVec2 nodeMax(position.x + nodeWidth, position.y + nodeHeight);
        drawList->AddRectFilled(position, nodeMax, fill, 7.0f);
        drawList->AddRect(position, nodeMax,
            toolIndex == ToolChainState::ActiveIndex()
                ? IM_COL32(90, 220, 245, 255)
                : IM_COL32(125, 155, 165, 230),
            7.0f, 0, toolIndex == ToolChainState::ActiveIndex() ? 3.0f : 1.0f);

        const char* registryName = tool->type == 12 ? "Original" :
            ToolRegistry::GetName(tool->type);
        const std::string title = std::to_string(toolIndex + 1) + ". " +
            ToolInstanceTitle(registryName, tool->label);
        drawList->PushClipRect(ImVec2(position.x + 8.0f, position.y + 5.0f),
            ImVec2(nodeMax.x - 8.0f, nodeMax.y - 5.0f), true);
        drawList->AddText(ImVec2(position.x + 8.0f, position.y + 8.0f),
            IM_COL32_WHITE, title.c_str());
        const char* inputName = tool->inputSourceMode == 0 ? "上一原图" :
            (tool->inputSourceMode == 1 ? "上一处理图" : "原图工具");
        drawList->AddText(ImVec2(position.x + 8.0f, position.y + 32.0f),
            IM_COL32(190, 215, 220, 255), inputName);
        if (tool->fixture.enabled)
            drawList->AddText(ImVec2(position.x + 8.0f, position.y + 53.0f),
                IM_COL32(255, 190, 80, 255), "Fixture");
        else if (tool->resultRoiMode != 0)
            drawList->AddText(ImVec2(position.x + 8.0f, position.y + 53.0f),
                IM_COL32(190, 125, 255, 255), "Result ROI");
        drawList->PopClipRect();

        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(toolIndex);
        if (ImGui::InvisibleButton("##workflow_node", ImVec2(nodeWidth, nodeHeight)))
            ToolChainState::SetActiveIndex(toolIndex);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n点击后定位到工具卡片", title.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}
}

bool IsOpen()
{
    return s_open;
}

void Open()
{
    s_open = true;
    s_requestFocus = true;
}

void Draw(const std::vector<int>& visibleToolIndices,
    const std::string& chainTitle)
{
    if (!s_open)
        return;
    if (s_requestDock)
    {
        const ImGuiID dockId = ImagePreviewDockId();
        if (dockId != 0)
        {
            ImGui::SetNextWindowDockID(dockId, ImGuiCond_Always);
            s_requestDock = false;
        }
    }
    if (s_requestFocus)
    {
        ImGui::SetNextWindowFocus();
        s_requestFocus = false;
    }
    ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("工具流程图", &s_open))
    {
        ImGui::Text("%s · 工具链", chainTitle.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%zu 个", visibleToolIndices.size());
        ImGui::SameLine();
        ImGui::TextDisabled("· 自动换行 · 紫色=结果 ROI · 橙色=Fixture · 点击节点定位");
        ImGui::Separator();
        if (visibleToolIndices.empty())
            ImGui::TextDisabled("当前筛选没有工具");
        else
            DrawGraph(visibleToolIndices);
    }
    ImGui::End();
}
}
