#include "ThemeManager.h"
#include <fstream>

extern HWND g_hWnd; // 主窗口句柄（Windows_imgui.cpp）

int g_CurrentTheme = 0;
const char *g_ThemeNames[] = {"夜间", "白天"};

namespace
{
    constexpr DWORD kDwmWindowCornerPreference = 33;
    constexpr DWORD kDwmBorderColor = 34;
    constexpr DWORD kDwmCaptionColor = 35;
    constexpr DWORD kDwmTextColor = 36;

    void ApplyNativeTitleBarTheme(int theme)
    {
        if (!g_hWnd)
            return;

        const BOOL dark = (theme == 0);
        DwmSetWindowAttribute(g_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const DWORD cornerPreferenceRound = 2; // DWMWCP_ROUND on Windows 11.
        DwmSetWindowAttribute(g_hWnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmWindowCornerPreference),
                              &cornerPreferenceRound, sizeof(cornerPreferenceRound));

        const COLORREF caption = dark ? RGB(35, 38, 43) : RGB(173, 184, 199);
        const COLORREF text = dark ? RGB(226, 230, 236) : RGB(32, 40, 52);
        const COLORREF border = dark ? RGB(53, 58, 66) : RGB(145, 157, 174);

        DwmSetWindowAttribute(g_hWnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmCaptionColor), &caption, sizeof(caption));
        DwmSetWindowAttribute(g_hWnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmTextColor), &text, sizeof(text));
        DwmSetWindowAttribute(g_hWnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmBorderColor), &border, sizeof(border));
    }
}

void ApplyTheme(int theme)
{
    // 切换 ImGui 主题色
    if (theme == 0)
    {
        ImGui::StyleColorsDark();
        ImGuiStyle &dark = ImGui::GetStyle();
        ImVec4 *c = dark.Colors;
        c[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.25f, 0.27f, 0.31f, 1.00f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    }
    else
    {
        ImGui::StyleColorsLight();
        ImGuiStyle &light = ImGui::GetStyle();
        ImVec4 *c = light.Colors;

        c[ImGuiCol_Text] = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.34f, 0.39f, 0.46f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.80f, 0.83f, 0.87f, 1.00f);
        c[ImGuiCol_ChildBg] = ImVec4(0.86f, 0.89f, 0.93f, 1.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
        c[ImGuiCol_Border] = ImVec4(0.46f, 0.52f, 0.60f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.69f, 0.75f, 0.82f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.61f, 0.70f, 0.80f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.53f, 0.64f, 0.77f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.66f, 0.70f, 0.76f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.56f, 0.64f, 0.74f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.66f, 0.70f, 0.76f, 1.00f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.68f, 0.72f, 0.78f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.75f, 0.78f, 0.82f, 1.00f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.47f, 0.55f, 0.65f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.48f, 0.56f, 0.67f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.48f, 0.62f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.16f, 0.43f, 0.76f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.55f, 0.86f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.18f, 0.45f, 0.78f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.52f, 0.67f, 0.84f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.43f, 0.59f, 0.78f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.49f, 0.70f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.58f, 0.70f, 0.85f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.47f, 0.62f, 0.80f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.52f, 0.74f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.43f, 0.48f, 0.56f, 1.00f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(0.38f, 0.55f, 0.76f, 1.00f);
        c[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.48f, 0.72f, 1.00f);
        c[ImGuiCol_Tab] = ImVec4(0.69f, 0.77f, 0.87f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.50f, 0.66f, 0.84f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.58f, 0.71f, 0.87f, 1.00f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.72f, 0.77f, 0.83f, 1.00f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.62f, 0.70f, 0.80f, 1.00f);
        c[ImGuiCol_DockingPreview] = ImVec4(0.24f, 0.50f, 0.82f, 0.35f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.80f, 0.83f, 0.87f, 1.00f);
        c[ImGuiCol_ResizeGrip] = ImVec4(0.45f, 0.55f, 0.68f, 0.35f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(0.36f, 0.52f, 0.72f, 0.65f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.45f, 0.70f, 0.95f);
    }
    g_CurrentTheme = theme;

    // Viewports 模式下强制背景不透明
    ImGuiStyle &style = ImGui::GetStyle();
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 3.0f;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ApplyNativeTitleBarTheme(theme);

    // 持久化：写入 theme.cfg
    std::ofstream f("theme.cfg", std::ios::trunc);
    if (f)
        f << theme;
}

// 启动时加载主题配置
void LoadTheme()
{
    std::ifstream f("theme.cfg");
    int t = 0;
    if (f >> t)
    {
        t = (t == 1) ? 1 : 0;
    }
    ApplyTheme(t);
}
