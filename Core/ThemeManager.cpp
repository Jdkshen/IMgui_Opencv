#include "ThemeManager.h"
#include "AppRuntimeState.h"
#include <fstream>

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
        HWND window = AppRuntimeState::WindowHandle();
        if (!window)
            return;

        const BOOL dark = (theme == 0);
        DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const DWORD cornerPreferenceRound = 2; // DWMWCP_ROUND on Windows 11.
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(kDwmWindowCornerPreference),
                              &cornerPreferenceRound, sizeof(cornerPreferenceRound));

        const COLORREF caption = dark ? RGB(35, 38, 43) : RGB(173, 184, 199);
        const COLORREF text = dark ? RGB(226, 230, 236) : RGB(32, 40, 52);
        const COLORREF border = dark ? RGB(53, 58, 66) : RGB(145, 157, 174);

        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(kDwmCaptionColor), &caption, sizeof(caption));
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(kDwmTextColor), &text, sizeof(text));
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(kDwmBorderColor), &border, sizeof(border));
    }
}

void ApplyTheme(int theme)
{
    ImGuiStyle &style = ImGui::GetStyle();

    // Quiet industrial palette: neutral surfaces, teal interaction, explicit status colors.
    if (theme == 0)
    {
        ImGui::StyleColorsDark();
        ImVec4 *c = style.Colors;
        c[ImGuiCol_Text]                  = ImVec4(0.86f, 0.89f, 0.92f, 1.00f);
        c[ImGuiCol_TextDisabled]          = ImVec4(0.56f, 0.61f, 0.67f, 1.00f);
        c[ImGuiCol_WindowBg]              = ImVec4(0.075f, 0.086f, 0.100f, 1.00f);
        c[ImGuiCol_ChildBg]               = ImVec4(0.090f, 0.105f, 0.120f, 1.00f);
        c[ImGuiCol_PopupBg]               = ImVec4(0.105f, 0.122f, 0.140f, 0.98f);
        c[ImGuiCol_Border]                = ImVec4(0.16f, 0.19f, 0.22f, 1.00f);
        c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg]               = ImVec4(0.125f, 0.148f, 0.175f, 1.00f);
        c[ImGuiCol_FrameBgHovered]        = ImVec4(0.17f, 0.22f, 0.25f, 1.00f);
        c[ImGuiCol_FrameBgActive]         = ImVec4(0.14f, 0.30f, 0.35f, 1.00f);
        c[ImGuiCol_TitleBg]               = ImVec4(0.085f, 0.098f, 0.113f, 1.00f);
        c[ImGuiCol_TitleBgActive]         = ImVec4(0.105f, 0.125f, 0.145f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.075f, 0.086f, 0.100f, 1.00f);
        c[ImGuiCol_MenuBarBg]             = ImVec4(0.080f, 0.094f, 0.108f, 1.00f);
        c[ImGuiCol_ScrollbarBg]           = ImVec4(0.065f, 0.075f, 0.086f, 1.00f);
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.26f, 0.30f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.30f, 0.36f, 0.40f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.25f, 0.52f, 0.59f, 1.00f);
        c[ImGuiCol_CheckMark]             = ImVec4(0.28f, 0.75f, 0.84f, 1.00f);
        c[ImGuiCol_SliderGrab]            = ImVec4(0.25f, 0.62f, 0.72f, 1.00f);
        c[ImGuiCol_SliderGrabActive]      = ImVec4(0.32f, 0.78f, 0.86f, 1.00f);
        c[ImGuiCol_Button]                = ImVec4(0.14f, 0.17f, 0.20f, 1.00f);
        c[ImGuiCol_ButtonHovered]         = ImVec4(0.18f, 0.30f, 0.35f, 1.00f);
        c[ImGuiCol_ButtonActive]          = ImVec4(0.15f, 0.43f, 0.52f, 1.00f);
        c[ImGuiCol_Header]                = ImVec4(0.14f, 0.21f, 0.24f, 1.00f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(0.18f, 0.31f, 0.36f, 1.00f);
        c[ImGuiCol_HeaderActive]          = ImVec4(0.17f, 0.39f, 0.46f, 1.00f);
        c[ImGuiCol_Separator]             = ImVec4(0.16f, 0.19f, 0.22f, 1.00f);
        c[ImGuiCol_SeparatorHovered]      = ImVec4(0.25f, 0.58f, 0.66f, 1.00f);
        c[ImGuiCol_SeparatorActive]       = ImVec4(0.30f, 0.72f, 0.80f, 1.00f);
        c[ImGuiCol_Tab]                   = ImVec4(0.095f, 0.112f, 0.130f, 1.00f);
        c[ImGuiCol_TabHovered]            = ImVec4(0.17f, 0.29f, 0.34f, 1.00f);
        c[ImGuiCol_TabSelected]           = ImVec4(0.14f, 0.24f, 0.28f, 1.00f);
        c[ImGuiCol_TabSelectedOverline]   = ImVec4(0.29f, 0.73f, 0.82f, 1.00f);
        c[ImGuiCol_TabDimmed]             = ImVec4(0.075f, 0.086f, 0.100f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]     = ImVec4(0.11f, 0.16f, 0.18f, 1.00f);
        c[ImGuiCol_DockingPreview]        = ImVec4(0.22f, 0.65f, 0.76f, 0.45f);
        c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.060f, 0.070f, 0.080f, 1.00f);
        c[ImGuiCol_PlotLines]             = ImVec4(0.28f, 0.72f, 0.80f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.98f, 0.72f, 0.28f, 1.00f);
        c[ImGuiCol_TableHeaderBg]         = ImVec4(0.12f, 0.15f, 0.18f, 1.00f);
        c[ImGuiCol_TableBorderStrong]     = ImVec4(0.24f, 0.28f, 0.32f, 1.00f);
        c[ImGuiCol_TableBorderLight]      = ImVec4(0.17f, 0.20f, 0.23f, 1.00f);
        c[ImGuiCol_TableRowBgAlt]         = ImVec4(0.12f, 0.14f, 0.16f, 0.55f);
        c[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.53f, 0.62f, 0.45f);
        c[ImGuiCol_NavCursor]             = ImVec4(0.32f, 0.78f, 0.86f, 1.00f);
    }
    else
    {
        ImGui::StyleColorsLight();
        ImVec4 *c = style.Colors;
        c[ImGuiCol_Text]                  = ImVec4(0.10f, 0.13f, 0.16f, 1.00f);
        c[ImGuiCol_TextDisabled]          = ImVec4(0.39f, 0.44f, 0.49f, 1.00f);
        c[ImGuiCol_WindowBg]              = ImVec4(0.925f, 0.940f, 0.950f, 1.00f);
        c[ImGuiCol_ChildBg]               = ImVec4(0.965f, 0.975f, 0.980f, 1.00f);
        c[ImGuiCol_PopupBg]               = ImVec4(0.985f, 0.990f, 0.995f, 0.99f);
        c[ImGuiCol_Border]                = ImVec4(0.72f, 0.76f, 0.79f, 1.00f);
        c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg]               = ImVec4(0.865f, 0.890f, 0.910f, 1.00f);
        c[ImGuiCol_FrameBgHovered]        = ImVec4(0.80f, 0.865f, 0.885f, 1.00f);
        c[ImGuiCol_FrameBgActive]         = ImVec4(0.70f, 0.825f, 0.855f, 1.00f);
        c[ImGuiCol_TitleBg]               = ImVec4(0.86f, 0.885f, 0.900f, 1.00f);
        c[ImGuiCol_TitleBgActive]         = ImVec4(0.79f, 0.835f, 0.855f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.89f, 0.905f, 0.915f, 1.00f);
        c[ImGuiCol_MenuBarBg]             = ImVec4(0.85f, 0.875f, 0.890f, 1.00f);
        c[ImGuiCol_ScrollbarBg]           = ImVec4(0.89f, 0.905f, 0.915f, 1.00f);
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.62f, 0.68f, 0.72f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.49f, 0.59f, 0.64f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.24f, 0.55f, 0.63f, 1.00f);
        c[ImGuiCol_CheckMark]             = ImVec4(0.05f, 0.48f, 0.58f, 1.00f);
        c[ImGuiCol_SliderGrab]            = ImVec4(0.12f, 0.55f, 0.64f, 1.00f);
        c[ImGuiCol_SliderGrabActive]      = ImVec4(0.05f, 0.43f, 0.52f, 1.00f);
        c[ImGuiCol_Button]                = ImVec4(0.84f, 0.87f, 0.89f, 1.00f);
        c[ImGuiCol_ButtonHovered]         = ImVec4(0.76f, 0.84f, 0.86f, 1.00f);
        c[ImGuiCol_ButtonActive]          = ImVec4(0.65f, 0.78f, 0.81f, 1.00f);
        c[ImGuiCol_Header]                = ImVec4(0.80f, 0.86f, 0.87f, 1.00f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(0.70f, 0.82f, 0.85f, 1.00f);
        c[ImGuiCol_HeaderActive]          = ImVec4(0.60f, 0.76f, 0.80f, 1.00f);
        c[ImGuiCol_Separator]             = ImVec4(0.70f, 0.74f, 0.77f, 1.00f);
        c[ImGuiCol_SeparatorHovered]      = ImVec4(0.28f, 0.62f, 0.69f, 1.00f);
        c[ImGuiCol_SeparatorActive]       = ImVec4(0.12f, 0.50f, 0.59f, 1.00f);
        c[ImGuiCol_Tab]                   = ImVec4(0.84f, 0.87f, 0.89f, 1.00f);
        c[ImGuiCol_TabHovered]            = ImVec4(0.72f, 0.82f, 0.84f, 1.00f);
        c[ImGuiCol_TabSelected]           = ImVec4(0.75f, 0.84f, 0.86f, 1.00f);
        c[ImGuiCol_TabSelectedOverline]   = ImVec4(0.08f, 0.48f, 0.57f, 1.00f);
        c[ImGuiCol_TabDimmed]             = ImVec4(0.88f, 0.90f, 0.91f, 1.00f);
        c[ImGuiCol_TabDimmedSelected]     = ImVec4(0.81f, 0.85f, 0.86f, 1.00f);
        c[ImGuiCol_DockingPreview]        = ImVec4(0.10f, 0.54f, 0.64f, 0.35f);
        c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.91f, 0.925f, 0.935f, 1.00f);
        c[ImGuiCol_PlotLines]             = ImVec4(0.08f, 0.47f, 0.57f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.82f, 0.46f, 0.08f, 1.00f);
        c[ImGuiCol_TableHeaderBg]         = ImVec4(0.82f, 0.86f, 0.88f, 1.00f);
        c[ImGuiCol_TableBorderStrong]     = ImVec4(0.66f, 0.70f, 0.73f, 1.00f);
        c[ImGuiCol_TableBorderLight]      = ImVec4(0.78f, 0.81f, 0.83f, 1.00f);
        c[ImGuiCol_TableRowBgAlt]         = ImVec4(0.86f, 0.89f, 0.90f, 0.55f);
        c[ImGuiCol_TextSelectedBg]        = ImVec4(0.18f, 0.55f, 0.64f, 0.32f);
        c[ImGuiCol_NavCursor]             = ImVec4(0.05f, 0.45f, 0.54f, 1.00f);
    }
    g_CurrentTheme = theme;

    style.DisabledAlpha = 0.65f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(10.0f, 5.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 5.0f;
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
