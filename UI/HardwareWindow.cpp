#include "HardwareWindow.h"

#include "DockSpaceHost.h"
#include "../Core/ThemeManager.h"
#include "../include/imgui/imgui.h"

#include <algorithm>

namespace UI
{
void RequestHardwareWindowFocus()
{
    g_ShowOpenCV = true;
    g_ShowHardware = true;
}

void ShowHardwareWindow()
{
    if (!g_ShowHardware)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // DockSpaceHost 的菜单栏使用 2px 的垂直 FramePadding。
    const float menuBarHeight = ImGui::GetFontSize() + 4.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x,
        (std::max)(1.0f, viewport->Size.y - menuBarHeight)));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::Begin("##HardwareFullscreenWorkspace", nullptr, flags);

    const bool isDark = g_CurrentTheme == 0;
    const ImVec4 titleColor = isDark
        ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f)
        : ImVec4(0.05f, 0.39f, 0.46f, 1.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(titleColor, "设备连接与 PLC 配置");

    const char* backLabel = "返回图像";
    const float backWidth = ImGui::CalcTextSize(backLabel).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine((std::max)(ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - backWidth));
    if (ImGui::Button(backLabel))
        g_ShowHardware = false;

    ImGui::Separator();
    static int selectedPage = 0;
    // 使用等宽导航按钮，让通讯页面在不同 DPI 下仍容易辨认。
    const char* pageLabels[] = {
        "1  工业相机", "2  实时保存", "3  PLC / TCP 连接", "4  PLC 握手"};
    const ImVec4 selectedPageColor = isDark
        ? ImVec4(0.08f, 0.40f, 0.46f, 1.0f)
        : ImVec4(0.04f, 0.48f, 0.56f, 1.0f);
    const ImVec4 selectedPageHovered = isDark
        ? ImVec4(0.10f, 0.50f, 0.57f, 1.0f)
        : ImVec4(0.05f, 0.56f, 0.64f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    if (ImGui::BeginTable("##HardwarePageNavigation", 4,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
    {
        for (int pageIndex = 0; pageIndex < 4; ++pageIndex)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(pageIndex);
            const bool selected = selectedPage == pageIndex;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, selectedPageColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selectedPageHovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedPageHovered);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            if (ImGui::Button(pageLabels[pageIndex], ImVec2(-1.0f, 36.0f)))
                selectedPage = pageIndex;
            if (selected)
                ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);

    ImGui::PushID(selectedPage);
    ImGui::BeginChild("##HardwarePageContent", ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    DrawHardwarePanel(selectedPage);
    ImGui::EndChild();
    ImGui::PopID();

    ImGui::End();
    ImGui::PopStyleVar(3);
}
}
