#pragma once
#include <windows.h>
#include <dwmapi.h>
#include "../imgui/imgui.h"

// =========================
// 主题切换
// =========================
extern int   g_CurrentTheme;
extern const char* g_ThemeNames[];

void ApplyTheme(int theme);
void LoadTheme();
