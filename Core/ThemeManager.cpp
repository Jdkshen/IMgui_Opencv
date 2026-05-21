#include "ThemeManager.h"
#include <fstream>

extern HWND g_hWnd;  // 主窗口句柄（Windows_imgui.cpp）

int   g_CurrentTheme = 0;
const char* g_ThemeNames[] = { "夜间", "白天" };

void ApplyTheme(int theme)
{
    if (theme == 0)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();
    g_CurrentTheme = theme;

    ImGuiStyle& style = ImGui::GetStyle();
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (g_hWnd)
    {
        BOOL dark = (theme == 0);
        DwmSetWindowAttribute(g_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }

    // 写入文件持久化
    std::ofstream f("theme.cfg", std::ios::trunc);
    if (f) f << theme;
}

void LoadTheme()
{
    std::ifstream f("theme.cfg");
    int t = 0;
    if (f >> t) ApplyTheme(t);
}
