#include "AppTitleBar.h"
#include "../Core/AppRuntimeState.h"
#include "../include/imgui/imgui.h"

namespace UI
{
namespace
{
    constexpr float kTitleBarHeight = 30.0f;
    constexpr float kTitleButtonWidth = 46.0f;
    constexpr float kAppMarkSize = 16.0f;
    constexpr float kMenuStartX = 34.0f;

    void DrawAppMark(ImDrawList* drawList, ImVec2 pos, float size)
    {
        ImU32 bg = IM_COL32(0, 122, 204, 255);
        ImU32 bg2 = IM_COL32(25, 118, 210, 255);
        ImU32 fg = IM_COL32(245, 250, 255, 255);
        ImU32 dark = IM_COL32(16, 48, 86, 255);

        ImVec2 p1(pos.x + size, pos.y + size);
        drawList->AddRectFilledMultiColor(pos, p1, bg, bg2, bg2, bg);
        drawList->AddRect(pos, p1, IM_COL32(255, 255, 255, 70), 4.0f, 0, 1.0f);

        ImVec2 c(pos.x + size * 0.50f, pos.y + size * 0.50f);
        drawList->AddCircleFilled(c, size * 0.22f, fg, 20);
        drawList->AddCircleFilled(c, size * 0.09f, dark, 16);
        drawList->AddLine(ImVec2(pos.x + size * 0.22f, pos.y + size * 0.30f), ImVec2(pos.x + size * 0.38f, pos.y + size * 0.30f), fg, 1.5f);
        drawList->AddLine(ImVec2(pos.x + size * 0.22f, pos.y + size * 0.30f), ImVec2(pos.x + size * 0.22f, pos.y + size * 0.46f), fg, 1.5f);
        drawList->AddLine(ImVec2(pos.x + size * 0.78f, pos.y + size * 0.70f), ImVec2(pos.x + size * 0.62f, pos.y + size * 0.70f), fg, 1.5f);
        drawList->AddLine(ImVec2(pos.x + size * 0.78f, pos.y + size * 0.70f), ImVec2(pos.x + size * 0.78f, pos.y + size * 0.54f), fg, 1.5f);
    }

    enum class TitleButtonIcon
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    bool TitleIconButton(const char* id, TitleButtonIcon icon, ImVec2 size, ImU32 hoverColor)
    {
        ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        if (hovered || held)
            drawList->AddRectFilled(min, max, hoverColor);

        const ImU32 iconColor = hovered && icon == TitleButtonIcon::Close
                                    ? IM_COL32(255, 255, 255, 245)
                                    : ImGui::GetColorU32(ImGuiCol_Text);
        const float cx = (min.x + max.x) * 0.5f;
        const float cy = (min.y + max.y) * 0.5f;
        const float stroke = 1.25f;

        switch (icon)
        {
        case TitleButtonIcon::Minimize:
            drawList->AddLine(ImVec2(cx - 5.5f, cy + 4.0f), ImVec2(cx + 5.5f, cy + 4.0f), iconColor, stroke);
            break;
        case TitleButtonIcon::Maximize:
            drawList->AddRect(ImVec2(cx - 5.5f, cy - 5.5f), ImVec2(cx + 5.5f, cy + 5.5f), iconColor, 0.0f, 0, stroke);
            break;
        case TitleButtonIcon::Restore:
            drawList->AddRect(ImVec2(cx - 2.0f, cy - 6.0f), ImVec2(cx + 6.0f, cy + 2.0f), iconColor, 0.0f, 0, stroke);
            drawList->AddRect(ImVec2(cx - 6.0f, cy - 2.0f), ImVec2(cx + 2.0f, cy + 6.0f), iconColor, 0.0f, 0, stroke);
            break;
        case TitleButtonIcon::Close:
            drawList->AddLine(ImVec2(cx - 5.0f, cy - 5.0f), ImVec2(cx + 5.0f, cy + 5.0f), iconColor, 1.35f);
            drawList->AddLine(ImVec2(cx + 5.0f, cy - 5.0f), ImVec2(cx - 5.0f, cy + 5.0f), iconColor, 1.35f);
            break;
        }
        return pressed;
    }
}

float GetAppTitleBarHeight()
{
    return kTitleBarHeight;
}

void ShowAppTitleBar()
{
    HWND window = AppRuntimeState::WindowHandle();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = GetAppTitleBarHeight();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::Begin("AppTitleBar", nullptr, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetWindowPos();
        ImVec2 max(min.x + ImGui::GetWindowWidth(), min.y + height);
        ImU32 line = ImGui::GetColorU32(ImGuiCol_Separator);
        drawList->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), line, 1.0f);

        DrawAppMark(drawList, ImVec2(min.x + 8.0f, min.y + 7.0f), kAppMarkSize);

        const char* title = "IMgui_Opencv Vision";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        const float rightLimit = ImGui::GetWindowWidth() - kTitleButtonWidth * 3.0f - 16.0f;
        float titleX = (ImGui::GetWindowWidth() - titleSize.x) * 0.5f;
        if (titleX < 390.0f)
            titleX = 390.0f;
        if (titleX + titleSize.x > rightLimit)
            titleX = rightLimit - titleSize.x;
        if (titleX > kMenuStartX + 250.0f)
        {
            ImVec2 titlePos(min.x + titleX, min.y + (height - titleSize.y) * 0.5f);
            drawList->AddText(titlePos, ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::SetCursorPos(ImVec2(kMenuStartX, 0.0f));
        DrawAppMainMenuItems();
        ImGui::PopStyleVar(2);

        const float buttonW = kTitleButtonWidth;
        const float buttonH = height;
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - buttonW * 3.0f, 0.0f));
        if (TitleIconButton("##title_minimize", TitleButtonIcon::Minimize, ImVec2(buttonW, buttonH), ImGui::GetColorU32(ImGuiCol_HeaderHovered)))
            ::ShowWindow(window, SW_MINIMIZE);
        ImGui::SameLine(0.0f, 0.0f);
        const TitleButtonIcon maxIcon = ::IsZoomed(window) ? TitleButtonIcon::Restore : TitleButtonIcon::Maximize;
        if (TitleIconButton("##title_maximize", maxIcon, ImVec2(buttonW, buttonH), ImGui::GetColorU32(ImGuiCol_HeaderHovered)))
            ::ShowWindow(window, ::IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
        ImGui::SameLine(0.0f, 0.0f);
        if (TitleIconButton("##title_close", TitleButtonIcon::Close, ImVec2(buttonW, buttonH), IM_COL32(210, 50, 55, 210)))
            ::PostMessageW(window, WM_CLOSE, 0, 0);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
}
